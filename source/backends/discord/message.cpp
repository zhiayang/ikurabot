// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "cmd.h"
#include "defs.h"
#include "timer.h"
#include "discord.h"

#include "picojson.h"

namespace ikura::discord
{
	/*
		this is a bit fishy, but IMO it's totally fine thread-wise. no matter how many dispatches
		come in from discord, they are all serially processed by the recv_worker. so, we can ensure
		that all of these functions are not called re-entrantly, and we don't need to hold a big
		database lock -- we can just extract a reference and release the lock.
	*/

	static DiscordGuild& get_guild(Snowflake id);
	static DiscordChannel& get_channel(Snowflake id, DiscordGuild& guild);
	static DiscordUser& get_user(Snowflake id, DiscordGuild& guild, pj::object json_author);

	static std::pair<std::string, std::vector<ikura::relative_str>> preprocess_discord_message(ikura::str_view msg, DiscordGuild& guild);


	void DiscordState::processMessage(pj::object json)
	{
		auto time = timer();

		auto& guild  = get_guild(Snowflake(json["guild_id"].as_str()));
		auto& author = get_user(Snowflake(json["author"].as_obj()["id"].as_str()), guild,
			json["author"].as_obj());

		auto& chan   = get_channel(Snowflake(json["channel_id"].as_str()), guild);

		// check for self
		if(author.id == config::discord::getUserId())
			return;

		// check for ignored
		if(config::discord::isUserIgnored(author.id))
			return;

		auto msg = json["content"].as_str();
		auto [ sanitised_msg, emote_idxs ] = preprocess_discord_message(msg, guild);

		// only process commands if we're not lurking
		bool ran_cmd = false;
		if(!this->channels[chan.id].lurk)
			ran_cmd = cmd::processMessage(author.id.str(), author.name, &this->channels[chan.id], msg, /* enablePings: */ true);

		lg::log("msg", "(%.2f ms) discord/%s/#%s: <%s> %s", time.measure(), guild.name, chan.name, author.name, msg);
	}





	void update_guild(DiscordState* st, pj::object json)
	{
		auto id = Snowflake(json["id"].as_str());
		zpr::println("guild %d", id.value);

		database().perform_write([&](auto& db) {
			auto& guild = db.discordData.guilds[id];

			guild.id = id;
			guild.name = json["name"].as_str();

			config::discord::Guild cfg_guild;
			for(const auto& g : config::discord::getJoinGuilds())
			{
				if(g.id == id.str())
				{
					cfg_guild = g;
					break;
				}
			}

			for(auto& r : json["roles"].as_arr())
			{
				auto j = r.as_obj();
				auto id = Snowflake(j["id"].as_str());

				auto& role = guild.roles[id];
				role.id = id;
				role.name = j["name"].as_str();
				role.discordPerms = (uint64_t) j["permissions"].as_int();
			}

			for(auto& c : json["channels"].as_arr())
			{
				auto j = c.as_obj();

				// get only text channels -- type 0.
				if(auto type = j["type"].as_int(); type != 0)
					continue;

				auto id = Snowflake(j["id"].as_str());

				auto& chan = guild.channels[id];
				chan.id = id;
				chan.name = j["name"].as_str();

				st->channels[id] = Channel(st, &guild, id, cfg_guild.lurk, cfg_guild.respondToPings,
					cfg_guild.silentInterpErrors, cfg_guild.commandPrefix);
			}

			for(auto& e : json["emojis"].as_arr())
			{
				auto j = e.as_obj();

				if(!j["available"].as_bool())
					continue;

				guild.emotes[j["name"].as_str()] = { Snowflake(j["id"].as_str()), j["animated"].as_bool() };
			}
		});
	}




	static std::pair<std::string, std::vector<ikura::relative_str>> preprocess_discord_message(ikura::str_view msg, DiscordGuild& guild)
	{
		std::string output;
		output.reserve(msg.size());

		auto is_digit = [](char c) -> bool { return '0' <= c && c <= '9'; };
		auto is_alnum = [](char c) -> bool {
			return c == '_' || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9');
		};

		auto loop_fn = [](const ikura::str_view& msg, size_t& i, auto pred, char end) -> bool {
			while(i < msg.size())
			{
				if(!pred(msg[i]))
					return (msg[i] == end);

				i++;
			}

			return false;
		};

		// replace discord-formatted emotes (eg. <:KEKW:1234567>) with just their name,
		// then create a corresponding entry in emote_idxs. also strip mentions to just
		// the name.

		bool was_space = true;
		std::vector<ikura::relative_str> emote_idxs;
		while(msg.size() > 0)
		{
			if(msg.find('<') == 0 && msg.size() > 4)
			{
				/*
					valid things:
					<@ID>       -- user
					<@!ID>      -- user (but nickname)
					<@&ID>      -- role
					<#ID>       -- channel
					<:name:ID>  -- emote
					<a:name:ID> -- animated emote
				*/

				if(msg.find("<:") == 0 || msg.find("<a:") == 0)
				{
					size_t k = 2;
					if(msg.find("<a:") == 0)
						k = 3;

					auto i = k;
					if(!loop_fn(msg, i, is_alnum, ':'))
						goto normal;

					auto name = msg.substr(k, i - k);

					k = ++i;
					if(!loop_fn(msg, i, is_digit, '>'))
						goto normal;

					// we don't really need the id.
					// auto id = Snowflake(msg.substr(k, i - k));

					msg.remove_prefix(i + 1);

					if(!was_space)
						output += ' ';

					emote_idxs.emplace_back(output.size(), name.size());
					output += name;

					if(msg.size() > 0 && msg[0] != ' ' && msg[0] != '\t')
						output += ' ';
				}
				else
				{
					size_t k = 1;
					if(msg.find("<@!") == 0 || msg.find("<@&") == 0)
						k = 3;

					else if(msg.find("<@") == 0 || msg.find("<#") == 0)
						k = 2;

					else
						goto normal;

					auto i = k;
					if(!loop_fn(msg, i, is_digit, '>'))
						goto normal;

					auto id = Snowflake(msg.substr(k, i - k));

					if(!was_space)
						output += ' ';

					if(msg.find("<@&") == 0)        output += "@" + guild.roles[id].name;
					else if(msg.find("<#") == 0)    output += "#" + guild.channels[id].name;
					else if(msg.find("<@!") == 0)   output += guild.knownUsers[id].name;
					else if(msg.find("<@") == 0)    output += guild.knownUsers[id].name;

					msg.remove_prefix(i + 1);

					if(msg.size() > 0 && msg[0] != ' ' && msg[0] != '\t')
						output += ' ';
				}
			}
			else
			{
			normal:

				was_space = (msg[0] == ' ' || msg[0] == '\t');

				output += msg[0];
				msg.remove_prefix(1);
			}
		}

		// zpr::println("output = %s", output);
		// zpr::println("emotes:");
		// for(auto& rs : emote_idxs)
		// 	zpr::println("  %s", rs.get(output));

		return { output, emote_idxs };
	}

	static DiscordGuild& get_guild(Snowflake id)
	{
		return database().wlock()->discordData.guilds[id];
	}

	static DiscordUser& get_user(Snowflake id, DiscordGuild& guild, pj::object json_author)
	{
		auto& user = guild.knownUsers[id];

		user.id = id;
		user.name = json_author["username"].as_str();

		return user;
	}

	static DiscordChannel& get_channel(Snowflake id, DiscordGuild& guild)
	{
		return guild.channels[id];
	}
}

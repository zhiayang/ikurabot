// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <regex>

#include "db.h"
#include "zfu.h"
#include "cmd.h"
#include "defs.h"
#include "timer.h"
#include "config.h"
#include "markov.h"
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
	static DiscordUser& update_user(DiscordGuild& guild, pj::object json);

	uint64_t parse_timestamp(ikura::str_view s);
	static std::pair<std::string, std::vector<ikura::relative_str>> sanitise_discord_message(ikura::str_view msg, DiscordGuild& guild);

	void DiscordState::processMessage(pj::object json, bool wasEdit)
	{
		auto time = timer();

		// if there's no author, no content, or if it's a webhook, ignore it.
		if(json["author"].is_null() || json["content"].is_null() || !json["webhook_id"].is_null())
			return;

		auto& guild  = get_guild(Snowflake(json["guild_id"].as_str()));
		auto& chan   = guild.channels[Snowflake(json["channel_id"].as_str())];
		auto& author = update_user(guild, json);

		// check for self
		if(author.id == config::discord::getUserId())
			return;

		// check for ignored
		if(config::discord::isUserIgnored(author.id))
			return;

		// check for bots
		if(!json["author"].as_obj()["bot"].is_null() && json["author"].as_obj()["bot"].as_bool())
			return;

		auto msg = json["content"].as_str();

		auto [ sanitised, emote_idxs ] = sanitise_discord_message(msg, guild);

		// only process commands if we're not lurking
		bool ran_cmd = false;
		if(!this->channels[chan.id].lurk)
			ran_cmd = cmd::processMessage(author.id.str(), author.nickname, &this->channels[chan.id], sanitised, /* enablePings: */ true);

		/*
			auto timestamp = parse_timestamp((wasEdit
				? (json["edited_timestamp"].is_null()
					? json["timestamp"]
					: json["edited_timestamp"]
				)
				: json["timestamp"]
			).as_str());
		*/

		if(!ran_cmd && chan.name != "bot-shrine")
			markov::process(sanitised, emote_idxs);

		// zpr::println("the raw message:\n{}", json["content"].as_str());
		// zpr::println("the sanitised message:\n{}", sanitised);

		auto ts = util::getMillisecondTimestamp();
		this->logMessage(ts, author, chan, guild, Snowflake(json["id"].as_str()), sanitised, emote_idxs, ran_cmd, wasEdit);

		console::logMessage(Backend::Discord, guild.name, chan.name, time.measure(), author.nickname,
			(wasEdit ? "(edit) " : "") + sanitised);

		// lg::log("msg", "discord/{}/#{}: {}({.2f} ms) <{}> {}", guild.name, chan.name, wasEdit ? "(edit) " : "",
		// 	time.measure(), author.nickname, sanitised);
	}



	void update_guild_emotes(DiscordGuild& guild, pj::object json)
	{
		for(auto& e : json["emojis"].as_arr())
		{
			auto j = e.as_obj();

			if(!j["available"].as_bool())
				continue;

			// zpr::println("updating emotes: {} -> {}", j["name"].as_str(), j["id"].as_str());

			uint64_t flags = 0;
			if(j["animated"].as_bool())         flags |= EmoteFlags::IS_ANIMATED;
			if(j["require_colons"].as_bool())   flags |= EmoteFlags::NEEDS_COLONS;

			guild.emotes[j["name"].as_str()] = { Snowflake(j["id"].as_str()), flags };
		}
	}

	void update_guild(DiscordState* st, pj::object json)
	{
		auto id = Snowflake(json["id"].as_str());

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
					cfg_guild.silentInterpErrors, cfg_guild.runMessageHandlers, cfg_guild.commandPrefixes);
			}

			update_guild_emotes(guild, json);

			lg::log("discord", "updated guild {}", guild.name);
		});
	}



	std::optional<Snowflake> parseMention(ikura::str_view str, size_t* consumed)
	{
		/*
			valid things:
			<@ID>       -- user
			<@!ID>      -- user (but nickname)
			<@&ID>      -- role
			<#ID>       -- channel
		*/

		size_t k = 1;
		if(str.find("<@!") == 0 || str.find("<@&") == 0)
			k = 3;

		else if(str.find("<@") == 0 || str.find("<#") == 0)
			k = 2;

		else
			return { };

		auto i = k;
		while(i < str.size())
		{
			if(!('0' <= str[i] && str[i] <= '9'))
			{
				if(str[i] == '>')   break;
				else                return { };
			}

			i++;
		}

		if(consumed) *consumed = i + 1;
		return Snowflake(str.substr(k, i - k));
	}


	static std::pair<std::string, std::vector<ikura::relative_str>> sanitise_discord_message(ikura::str_view msg, DiscordGuild& guild)
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
					size_t cons = 0;
					auto id_ = parseMention(msg, &cons);

					if(!id_.has_value())
						goto normal;

					auto id = id_.value();

					if(!was_space)
						output += ' ';

					if(msg.find("<@&") == 0)        output += "@" + guild.roles[id].name;
					else if(msg.find("<#") == 0)    output += "#" + guild.channels[id].name;
					else if(msg.find("<@!") == 0)   output += guild.knownUsers[id].nickname;
					else if(msg.find("<@") == 0)    output += guild.knownUsers[id].username;

					msg.remove_prefix(cons);

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

		// zpr::println("output = {}", output);
		// zpr::println("emotes:");
		// for(auto& rs : emote_idxs)
		// 	zpr::println("  {}", rs.get(output));

		return { output, emote_idxs };
	}

	static DiscordGuild& get_guild(Snowflake id)
	{
		return database().wlock()->discordData.guilds[id];
	}

	static DiscordUser& update_user(DiscordGuild& guild, pj::object json)
	{
		auto& j_author = json["author"].as_obj();
		auto& j_member = json["member"].as_obj();

		auto id = Snowflake(j_author["id"].as_str());
		auto& user = guild.knownUsers[id];

		auto old_username = user.username;
		auto old_nickname = user.nickname;

		user.username = j_author["username"].as_str();
		user.nickname = (j_member["nick"].is_null()
			? user.username
			: j_member["nick"].as_str()
		);

		user.permissions |= permissions::EVERYONE;

		if(user.id.empty())
		{
			user.id = id;

			lg::log("discord", "adding (nick: {}, user: {}, id: {}) to guild '{}'",
				user.nickname, user.username, id.str(), guild.name);
		}
		else if(!old_username.empty() && old_username != user.username)
		{
			guild.usernameMap.erase(old_username);
			lg::log("discord", "username changed; old: {}, new: {}", old_username, user.username);
		}
		else if(!old_nickname.empty() && old_nickname != user.nickname)
		{
			guild.nicknameMap.erase(old_nickname);
			lg::log("discord", "nickname changed; old: {}, new: {}", old_nickname, user.nickname);
		}
		else if(user.id != id)
		{
			lg::warn("discord", "user id got changed?! old: {}, new: {}",
				user.id.str(), id.str());
		}

		// just re-do the roles every time, i guess. there's no good way to do deltas anyway.
		user.discordRoles = zfu::map(j_member["roles"].as_arr(), [](auto& o) -> auto {
			return Snowflake(o.as_str());
		});

		guild.usernameMap[user.username] = user.id;
		guild.nicknameMap[user.nickname] = user.id;

		return user;
	}

	static auto timestamp_regex = std::regex("(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d+)(\\+|-)(\\d{2}):(\\d{2})");
	uint64_t parse_timestamp(ikura::str_view s)
	{
		// eg: 2017-07-11T17:27:07.299000+00:00

		{
			auto tmp = s.str();

			std::smatch sm;
			std::regex_match(tmp, sm, timestamp_regex);

			if(sm.size() != 1 + 10)
			{
				lg::error("discord", "malformed timestamp '{}'", s);
				return 0;
			}

			auto year   = util::stoi(sm[1].str()).value_or(0);
			auto month  = util::stoi(sm[2].str()).value_or(0);
			auto day    = util::stoi(sm[3].str()).value_or(0);
			auto hour   = util::stoi(sm[4].str()).value_or(0);
			auto minute = util::stoi(sm[5].str()).value_or(0);
			auto second = util::stoi(sm[6].str()).value_or(0);
			auto sfrac  = util::stoi(sm[7].str()).value_or(0);
			auto tz_neg = (sm[8] == "-");
			auto tz_hr  = util::stoi(sm[9].str()).value_or(0);
			auto tz_min = util::stoi(sm[10].str()).value_or(0);

			// std::chrono is useless until c++20 (even so debatable), so just use good old .
			// just deal with the base date+time here; we'll use std::chrono to add the timezone
			// and fractional seconds.
			auto tm = std::tm();
			tm.tm_year = year;
			tm.tm_mon  = month;
			tm.tm_mday = day;
			tm.tm_hour = hour;
			tm.tm_min  = minute;
			tm.tm_sec  = second;

			// zpr::println("{}", s);
			// zpr::println("{04}-{02}-{02}T{02}:{02}:{02}.{06}{}{02}:{02}", year, month, day, hour, minute, second, sfrac,
			// 	tz_neg ? '-' : '+', tz_hr, tz_min);

			auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
			tp += std::chrono::hours((tz_neg ? -1 : 1) * tz_hr);
			tp += std::chrono::minutes((tz_neg ? -1 : 1) * tz_min);
			tp += std::chrono::milliseconds(1000 / sfrac);

			return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
		}
	}
}

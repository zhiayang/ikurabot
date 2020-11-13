// dispatch.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "zpr.h"
#include "defs.h"
#include "discord.h"

#include "picojson.h"

namespace ikura::discord
{
	void update_guild(DiscordState* st, pj::object json);
	void process_message(DiscordState* st, pj::object json);
	void update_guild_emotes(DiscordGuild& guild, pj::object json);


	template <typename... Args>
	void error(const char* fmt, Args&& ... args)
	{
		lg::error("discord", fmt, args...);
	}

	void DiscordState::processEvent(std::map<std::string, pj::value> msg)
	{
		if(msg["op"].as_int() != opcode::DISPATCH)
			return error("trying to process non-dispatch message");

		auto s = msg["s"];
		auto t = msg["t"];

		if(!s.is_int())
			return error("sequence was not an integer (got '{}')", s.serialise());

		if(!t.is_str())
			return error("expected string for 't'");

		auto seq = s.as_int();
		if(seq < this->sequence)
			lg::warn("discord", "outdated sequence (current {}, received {})", this->sequence, seq);

		this->sequence = std::max(seq, this->sequence);

		auto type = t.as_str();
		if(type == "GUILD_CREATE")
		{
			update_guild(this, msg["d"].as_obj());
		}
		else if(type == "MESSAGE_CREATE")
		{
			this->processMessage(msg["d"].as_obj(), /* wasEdit: */ false);
		}
		else if(type == "MESSAGE_UPDATE")
		{
			// for now, we just treat a message edit as a new message.
			this->processMessage(msg["d"].as_obj(), /* wasEdit: */ true);
		}
		else if(type == "GUILD_EMOJIS_UPDATE")
		{
			auto id = Snowflake(msg["d"].as_obj()["guild_id"].as_str());
			database().perform_write([&](auto& db) {
				auto& dd = db.discordData;
				if(auto it = dd.guilds.find(id); it != dd.guilds.end())
				{
					update_guild_emotes(it.value(), msg["d"].as_obj());
					lg::log("discord", "updated emotes for guild '{}'", it->second.name);
				}
				else
				{
					lg::error("discord", "received emote update for unknown guild '{}'", id.str());
				}
			});
		}
		else if(type == "RESUMED")
		{
			lg::log("discord", "resume replay finished");
		}
		else if(type == "READY")
		{
			auto sess = msg["d"].as_obj()["session_id"].as_str();
			if(sess != this->session_id)
			{
				lg::log("discord", "session id: {}", sess);
				this->session_id = sess;
			}
		}
		else if(type == "MESSAGE_REACTION_ADD" || type == "MESSAGE_REACTION_REMOVE")
		{
			// do nothing for now
		}
		else
		{
			lg::warn("discord", "ignoring message type '{}'", type);
			zpr::println("{}", pj::value(msg).serialise(true));
		}
	}
}

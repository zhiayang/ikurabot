// dispatch.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zpr.h"
#include "defs.h"
#include "discord.h"

#include "picojson.h"

namespace ikura::discord
{
	void update_guild(DiscordState* st, pj::object json);
	void process_message(DiscordState* st, pj::object json);


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
			return error("sequence was not an integer (got '%s')", s.serialise());

		if(!t.is_str())
			return error("expected string for 't'");

		auto seq = s.as_int();
		if(seq < this->sequence)
			lg::warn("discord", "outdated sequence (current %ld, received %ld)", this->sequence, seq);

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
		else if(type == "READY")
		{
			auto sess = msg["d"].as_obj()["session_id"].as_str();
			if(sess != this->session_id)
			{
				lg::log("discord", "session id: %s", sess);
				this->session_id = sess;
			}
		}
		else if(type == "MESSAGE_REACTION_ADD" || type == "MESSAGE_REACTION_REMOVE")
		{
			// do nothing for now
		}
		else
		{
			lg::warn("discord", "ignoring message type '%s'", type);
			zpr::println("%s", pj::value(msg).serialise(true));
		}
	}
}

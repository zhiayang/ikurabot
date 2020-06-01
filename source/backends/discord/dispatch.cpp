// dispatch.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zpr.h"
#include "defs.h"
#include "discord.h"

#include "picojson.h"

namespace ikura::discord
{
	template <typename... Args>
	void error(const char* fmt, Args&& ... args)
	{
		lg::error("discord", fmt, args...);
	}

	void DiscordState::processMessage(std::map<std::string, pj::value> msg)
	{
		if(msg["op"].get<int64_t>() != opcode::DISPATCH)
			return error("trying to process non-dispatch message");

		auto s = msg["s"];
		auto t = msg["t"];

		if(!s.is<int64_t>())
			return error("sequence was not an integer (got '%s')", s.serialise());

		if(!t.is<std::string>())
			return error("expected string for 't'");

		zpr::println("%s", pj::value(msg).serialise(true));

		auto seq = s.get<int64_t>();
		if(seq != this->sequence + 1)
			return error("out-of-order sequence (expected %ld, got %ld)", this->sequence + 1, seq);

		this->sequence += 1;


		auto type = t.get<std::string>();
		if(type == "GUILD_CREATE")
		{

		}
		else if(type == "")
		{
		}
		else if(type == "READY")
		{
			// do nothing
		}
		else
		{
			lg::log("ignoring message type '%s'", type);
		}
	}
}

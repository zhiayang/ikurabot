// command.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"

namespace ikura::cmd
{
	static void processCommand(str_view user, const Channel* chan, str_view cmd);
	static Message generateResponse(str_view user, const Channel* chan, str_view msg);

	void processMessage(str_view user, const Channel* chan, str_view message)
	{
		auto pref = chan->getCommandPrefix();
		if(message.find(pref) == 0)
		{
			processCommand(user, chan, message.drop(pref.size()));
		}
		else if(message.find(chan->getUsername()) != std::string::npos)
		{
			if(chan->shouldReplyMentions())
				chan->sendMessage(generateResponse(user, chan, message));
		}
	}


	static void processCommand(str_view user, const Channel* chan, str_view cmd)
	{
		CmdContext cs;
		cs.caller = user;
		cs.channel = chan;

		// split by words
		auto split = util::split(cmd, ' ');

		auto command = interpreter().rlock()->findCommand(split[0]);

		if(command)
		{
			auto msg = interpreter().map_write([&](auto& fs) {
				return command->run(&fs, cs);
			});

			if(msg) chan->sendMessage(msg.value());
		}
		else if(split[0] == "macro")
		{
			if(split.size() < 3)
			{
				chan->sendMessage(Message().add("not enough arguments to macro"));
				return;
			}

			auto name = split[1];
			if(interpreter().rlock()->findCommand(name) != nullptr)
			{
				chan->sendMessage(Message().add(zpr::sprint("macro or alias '%s' already exists", name)));
				return;
			}

			// drop the first two
			auto code = util::join(ikura::span(split).drop(2), " ");
			interpreter().wlock()->commands.emplace(name, new Macro(name.str(), code));

			chan->sendMessage(Message().add(zpr::sprint("added command '%s'", name)));
		}
		else
		{
			lg::warn("cmd", "user '%s' tried non-existent command '%s'", user, split[0]);
		}
	}

	static Message generateResponse(str_view user, const Channel* chan, str_view msg)
	{
		return Message().add(zpr::sprint("%s AYAYA /", user));
	}












	Command::Command(std::string name) : name(std::move(name)) { }

	std::optional<Command*> Command::deserialise(Span& buf)
	{
		auto tag = buf.peek();
		switch(tag)
		{
			case serialise::TAG_MACRO: {
				auto ret = Macro::deserialise(buf);
				if(ret) return ret.value();     // force unwrap to cast Macro* -> Command*
				else    return { };
			}

			case serialise::TAG_FUNCTION:
			default:
				lg::error("db", "type tag mismatch (unexpected '%02x')", tag);
				return { };
		}
	}
}

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
				return interpretCommandCode(&fs, &cs, command->getCode());
			});

			if(msg) chan->sendMessage(msg.value());
		}
		else if(split[0] == "addcmd")
		{
			if(split.size() < 3)
			{
				chan->sendMessage(Message().add("not enough arguments to addcmd"));
				return;
			}

			auto name = split[1];
			if(interpreter().rlock()->findCommand(name) != nullptr)
			{
				chan->sendMessage(Message().add(zpr::sprint("command or alias '%s' already exists", name)));
				return;
			}

			// drop the first two
			auto code = util::join(ikura::span(split).drop(2), " ");
			interpreter().wlock()->commands.emplace(name, Command(name.str(), code));

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












	Command::Command(std::string name, std::string code) : name(name), code(code) { }

	std::optional<Message> Command::run(InterpState* fs, CmdContext* cs) const
	{
		return interpretCommandCode(fs, cs, this->code);
	}


	void Command::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// just write the name and the source code.
		wr.write(this->name);
		wr.write(this->code);
	}

	std::optional<Command> Command::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		std::string name;
		std::string code;

		if(!rd.read(&name))
			return { };

		if(!rd.read(&code))
			return { };

		return Command(name, code);
	}



}

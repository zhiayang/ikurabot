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
			processCommand(user, chan, message.drop(pref.size()));

		else if(message.find(chan->getUsername()) != std::string::npos)
		{
			if(chan->shouldReplyMentions())
				chan->sendMessage(generateResponse(user, chan, message));
		}
	}




	static void processCommand(str_view user, const Channel* chan, str_view cmd)
	{
	}

	static Message generateResponse(str_view user, const Channel* chan, str_view msg)
	{
		return Message().add(zpr::sprint("%s AYAYA /", user));
	}
}

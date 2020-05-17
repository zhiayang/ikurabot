// irc.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "irc.h"
#include "types.h"

namespace ikura::irc
{
	struct IRCMessage
	{
		// all of these reference the original message to keep this lightweight.
		ikura::str_view user;
		ikura::str_view nick;
		ikura::str_view host;
		ikura::str_view command;
		std::vector<ikura::str_view> params;
		ikura::string_map<std::string> tags;

		bool isCTCP = false;
		ikura::str_view ctcpCommand;
	};

	std::optional<IRCMessage> parseMessage(ikura::str_view);
}

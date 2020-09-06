// irc.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

#include <list>

#include "irc.h"
#include "config.h"
#include "synchro.h"

using namespace std::chrono_literals;

namespace ikura::irc
{
	static std::list<Synchronised<IRCServer>> servers;











	void init()
	{
		if(!config::haveIRC())
			return;

		for(const auto& srv : config::irc::getJoinServers())
			servers.emplace_back(srv, 5000ms);
	}
}

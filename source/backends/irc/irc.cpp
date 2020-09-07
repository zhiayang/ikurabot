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


	const Channel* getChannelFromServer(ikura::str_view server, ikura::str_view channel)
	{
		for(const auto& s : servers)
		{
			auto ret = s.map_read([&](auto& srv) -> const Channel* {
				if(srv.name != server)
					return nullptr;

				if(auto it = srv.channels.find(channel); it != srv.channels.end())
					return &it->second;

				return nullptr;
			});

			if(ret)
				return ret;
		}

		return nullptr;
	}


	void init()
	{
		if(!config::haveIRC())
			return;

		for(const auto& srv : config::irc::getJoinServers())
			servers.emplace_back(srv, 5000ms);

		for(auto& srv : servers)
			srv.wlock()->connect();
	}


	void shutdown()
	{
		if(!config::haveIRC())
			return;

		for(auto& srv : servers)
			srv.wlock()->disconnect();
	}
}

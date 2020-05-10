// twitch.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <deque>
#include <thread>
#include <string>
#include <unordered_map>

#include "defs.h"
#include "network.h"

namespace ikura::twitch
{
	struct TwitchChannel
	{
		std::string name;
		bool lurk;
		bool mod;
	};

	struct TwitchState
	{
		TwitchState(URL url, std::chrono::nanoseconds timeout) : ws(url, timeout) { }

		WebSocket ws;

		bool connected = false;
		std::thread sender;
		condvar<bool> haveQueued;
		Synchronised<std::vector<std::string>, std::shared_mutex> sendQueue;

		std::string username;
		std::unordered_map<std::string, TwitchChannel> channels;
	};

	void init();
	void shutdown();

	void processRawMessage(TwitchState* st, std::string_view msg);
	void processMessage(TwitchState* st, std::string_view msg);

	void sendRawMessage(TwitchState* st, std::string_view msg);
	void sendMessage(TwitchState* st, std::string_view channel, std::string_view msg);
}

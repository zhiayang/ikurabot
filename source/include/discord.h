// discord.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "network.h"

namespace ikura::discord
{
	struct DiscordState
	{
		DiscordState(URL url, std::chrono::nanoseconds timeout);

		bool connected = false;

	private:
		WebSocket ws;
		std::chrono::milliseconds heartbeat_interval;

		std::thread tx_thread;
		std::thread rx_thread;
		std::thread hb_thread;

		void connect();
		void disconnect();

		friend void send_worker();
		friend void recv_worker();

		friend void heartbeat_worker();

		friend void init();
		friend void shutdown();
	};

	void init();
	void shutdown();
}

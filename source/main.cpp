// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <thread>
#include <chrono>

#include "defs.h"
#include "network.h"

using namespace std::chrono_literals;

int main(int argc, char** argv)
{
	if(argc != 2)
	{
		zpr::println("usage: ./ikurabot <config.json>");
		exit(1);
	}

	if(!ikura::config::load(argv[1]))
	{
		ikura::lg::error("invalid config file '%s'", argv[1]);
		exit(1);
	}


	zpr::println("hello, world!");

	// auto ws = ikura::WebSocket(ikura::URL("wss://echo.websocket.org"), 500ms);
	auto ws = ikura::WebSocket(ikura::URL("wss://irc-ws.chat.twitch.tv"), 500ms);

	ikura::condvar<bool> cv;
	ws.onReceiveText([&](bool, std::string_view sv) {
		zpr::println("%s", sv);
		// cv.set(true);
	});

	ws.connect();

	// cv.wait(true, 3s);
	std::this_thread::sleep_for(5s);
	ws.disconnect();
}


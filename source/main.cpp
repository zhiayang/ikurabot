// main.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"

#include <thread>
#include <chrono>
#include "kissnet.h"

#include "network.h"

namespace kn = kissnet;
using namespace std::chrono_literals;

constexpr const char* token = "oauth:as_if_i'd_leave_my_token_here";

int main()
{
	zpr::println("hello, world!");

	auto ws = ikura::WebSocket("echo.websocket.org", 443, /* ssl: */ true);

	ws.onReceiveText([](bool, std::string_view sv) {
		zpr::println("%s", sv);
	});

	ws.connect();

	ws.send("KEKW");

	std::this_thread::sleep_for(0.5s);
	ws.send("AYAYA");

	std::this_thread::sleep_for(0.5s);
	ws.send("I thought not. It's not a story the Jedi would tell you. It's a Sith legend. Darth Plagueis was a Dark Lord of the Sith, so powerful and so wise he could use the Force to influence the midichlorians to create life... He had such a knowledge of the dark side that he could even keep the ones he cared about from dying. The dark side of the Force is a pathway to many abilities some consider to be unnatural. He became so powerful... the only thing he was afraid of was losing his power, which eventually, of course, he did. Unfortunately, he taught his apprentice everything he knew, then his apprentice killed him in his sleep. It's ironic he could save others from death, but not himself.");

	while(true)
		;
}


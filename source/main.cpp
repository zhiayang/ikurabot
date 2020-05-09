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

	auto ws = ikura::WebSocket("echo.websocket.org", 80, /* ssl: */ false, 250'000);

	ikura::condvar<bool> cv;
	ws.onReceiveText([&](bool, std::string_view sv) {
		zpr::println("%s", sv);
		cv.set(true);
	});

	ws.connect();

	std::this_thread::sleep_for(2s);
	ws.send("Sets the timeout value that specifies the maximum amount of time an input function waits until it completes. It accepts a timeval structure with the number of seconds and microseconds specifying the limit on how long to wait for an input operation to complete. If a receive operation has blocked for this much time without receiving additional data, it shall return with a partial count or errno set to [EAGAIN] or [EWOULDBLOCK] if no data is received. The default for this option is zero, which indicates that a receive operation shall not time out. This option takes a timeval structure. Note that not all implementations allow this option to be set.");

	cv.wait(true);
	ws.disconnect();
}


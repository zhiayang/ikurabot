// console.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <unordered_map>

#include "defs.h"
#include "synchro.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::console
{
	static bool is_connected = false;
	static Synchronised<std::unordered_map<Socket*, Buffer>> socketBuffers;
	static ikura::wait_queue<Socket*> danglingSockets;

	static const ikura::str_view PROMPT = "λ ikura$ ";

	static void process_command(Socket* sock, ikura::str_view cmd)
	{
		if(cmd == "exit" || cmd == "q")
		{
			// remove the fella
			socketBuffers.wlock()->erase(sock);

			// we can't kill the socket from here (since function will be called from
			// socket's own thread. so, pass it on to someone else.
			danglingSockets.push(sock);
		}
		else if(cmd == "stop")
		{
			// kill the entire bot, so disconnect the entire server.
			is_connected = false;
		}
		else
		{
			zpr::println("command: %s", cmd);
			sock->send(Span::fromString(PROMPT));
		}
	}











	static void setup_receiver(Socket* sock)
	{
		sock->onReceive([sock](Span input) {
			auto& buf = socketBuffers.wlock()->at(sock);
			buf.write(input);

			// similar thing as the websocket; if we have more than one message in the buffer
			// possibly incomplete, we can't discard existing data.
			auto sv = ikura::str_view((const char*) buf.data(), buf.size());

			while(sv.size() > 0)
			{
				auto tmp = sv.find('\n');
				if(tmp == std::string::npos)
					return;

				auto cmd = sv.take(tmp);
				sv.remove_prefix(tmp + 1);

				process_command(sock, cmd);
			}

			buf.clear();
		});
	}

	void init()
	{
		auto port = config::global::getConsolePort();
		if(port == 0 || !(0 < port && port < 65536))
		{
			lg::warn("console", "invalid port for console, ignoring.");
			return;
		}

		auto srv = new Socket("localhost", port, /* ssl: */ false);
		is_connected = true;
		srv->listen();

		lg::log("console", "starting console on port %d", port);

		auto reaper = std::thread([]() {
			while(true)
			{
				auto sock = danglingSockets.pop();
				if(sock == nullptr)
					break;

				sock->disconnect();
				delete sock;
			}
		});

		while(true)
		{
			if(!is_connected)
				break;

			if(auto sock = srv->accept(200ms); sock != nullptr)
			{
				lg::log("console", "started session");
				socketBuffers.wlock()->emplace(sock, Buffer(512));
				setup_receiver(sock);

				sock->send(Span::fromString(PROMPT));
			}
		}

		// kill everything.
		socketBuffers.perform_write([](auto& sb) {
			for(auto& [ s, b ] : sb)
				s->disconnect();
		});

		danglingSockets.push(nullptr);
		reaper.join();
	}
}

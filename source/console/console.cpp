// console.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <unordered_map>

#include "db.h"
#include "defs.h"
#include "markov.h"
#include "synchro.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::console
{
	static bool is_connected = false;
	static Synchronised<std::unordered_map<Socket*, Buffer>> socketBuffers;
	static ikura::wait_queue<Socket*> danglingSockets;

	static const char* PROMPT = "λ ikura$ ";

	static void echo_message(Socket* sock, ikura::str_view sv)
	{
		if(sock) sock->send(Span::fromString(sv));
		else     fprintf(stderr, "%s", sv.str().c_str());
	}

	// returns false if we should quit.
	static bool process_command(Socket* sock, ikura::str_view cmd)
	{
		if(cmd == "exit" || cmd == "q")
		{
			// only try to do socket stuff if we're a local connection.
			if(sock != nullptr)
			{
				// remove the fella
				socketBuffers.wlock()->erase(sock);

				// we can't kill the socket from here (since function will be called from
				// socket's own thread). so, pass it on to someone else.
				danglingSockets.push(sock);
			}
			else
			{
				// if you exit the local session, you kill the bot.
				is_connected = false;
			}

			echo_message(sock, "\n");
			return false;
		}
		else if(cmd == "stop" || cmd == "s")
		{
			// kill the entire bot, so disconnect the entire server.
			is_connected = false;

			echo_message(sock, "\n");
			return false;
		}
		else
		{
			if(cmd == "sync")
			{
				database().rlock()->sync();
			}
			else if(cmd == "retrain")
			{
				markov::retrain();
				auto thr = std::thread([]() {
					while(true)
					{
						std::this_thread::sleep_for(250ms);

						auto p = ikura::markov::retrainingProgress();
						if(p == 1.0)
							break;

						ikura::lg::log("markov", "retraining progress: %.2f", 100 * p);
					}
				});

				thr.detach();
			}
			else if(!cmd.empty())
			{
				echo_message(sock, zpr::sprint("unknown command '%s'\n", cmd));
			}

			echo_message(sock, PROMPT);
			return true;
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

		auto local_con = std::thread([]() {
			while(true)
			{
				if(!is_connected)
					break;

				// messy as FUCK
				{
					auto fds = pollfd { .fd = STDIN_FILENO, .events = POLLIN };
					auto ret = poll(&fds, 1, (200ms).count());
					if(ret == 0) continue;
				}

				char buf[512] = { }; fgets(buf, 512, stdin);

				auto len = strlen(buf);
				if(len == 0)
					continue;

				if(buf[len - 1] == '\n')
					len -= 1;

				auto res = process_command(nullptr, ikura::str_view(buf, len));
				if(!res) break;
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

				echo_message(sock, PROMPT);
			}
		}

		// kill everything.
		socketBuffers.perform_write([](auto& sb) {
			for(auto& [ s, b ] : sb)
				s->disconnect();
		});

		danglingSockets.push(nullptr);

		local_con.join();
		reaper.join();
	}
}

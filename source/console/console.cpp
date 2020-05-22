// console.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <unordered_map>

#include "db.h"
#include "cmd.h"
#include "defs.h"
#include "interp.h"
#include "twitch.h"
#include "markov.h"
#include "synchro.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::console
{
	static struct {

		bool is_connected = false;
		ikura::wait_queue<Socket*> danglingSockets;
		Synchronised<std::unordered_map<Socket*, Buffer>> socketBuffers;

		// todo: not thread safe
		const Channel* currentChannel = nullptr;

	} State;

	static void echo_message(Socket* sock, ikura::str_view sv)
	{
		if(sock) sock->send(Span::fromString(sv));
		else     fprintf(stderr, "%s", sv.str().c_str());
	}

	static void print_prompt(Socket* sock)
	{
		auto ch = (State.currentChannel ? zpr::sprint(" (#%s) ", State.currentChannel->getName()) : "");
		echo_message(sock, zpr::sprint("λ ikura%s$ ", ch));
	}

	// returns false if we should quit.
	static bool process_command(Socket* sock, ikura::str_view cmd_str)
	{
		if(cmd_str == "exit" || cmd_str == "q")
		{
			// only try to do socket stuff if we're a local connection.
			if(sock != nullptr)
			{
				// remove the fella
				State.socketBuffers.wlock()->erase(sock);

				// we can't kill the socket from here (since function will be called from
				// socket's own thread). so, pass it on to someone else.
				State.danglingSockets.push(sock);
			}
			else
			{
				// if you exit the local session, you kill the bot.
				State.is_connected = false;
			}

			echo_message(sock, "\n");
			return false;
		}
		else if(cmd_str == "stop" || cmd_str == "s")
		{
			// kill the entire bot, so disconnect the entire server.
			State.is_connected = false;

			echo_message(sock, "\n");
			return false;
		}
		else
		{
			auto cmd  = cmd_str.take(cmd_str.find(' '));
			auto args = util::split(cmd_str.drop(cmd.size() + 1), ' ');

			if(cmd == "sync")
			{
				if(!args.empty())
				{
					echo_message(sock, "'sync' takes 0 arguments\n");
					return true;
				}

				database().rlock()->sync();
			}
			else if(cmd == "retrain")
			{
				if(!args.empty())
				{
					echo_message(sock, "'retrain' takes 0 arguments\n");
					return true;
				}

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
			else if(cmd == "join")
			{
				if(args.size() != 2)
				{
					echo_message(sock, "'join' takes 2 arguments: join <backend> <name>\n");
					return true;
				}

				auto backend = args[0];
				auto channel = args[1];

				if(backend == "twitch")
				{
					if(channel.find('#') == 0)
						channel.remove_prefix(1);

					auto chan = twitch::getChannel(channel);
					if(chan != nullptr)
					{
						State.currentChannel = chan;
						echo_message(sock, zpr::sprint("joined #%s\n", channel));
					}
					else
					{
						echo_message(sock, zpr::sprint("channel '#%s' does not exist\n", channel));
					}
				}
				else
				{
					echo_message(sock, zpr::sprint("unknown backend '%s'\n", backend));
				}
			}
			else if(cmd == "\\")    // as if you typed the following commands in chat.
			{
				auto chan = State.currentChannel;
				if(chan == nullptr)
				{
					echo_message(sock, "not in a channel\n");
					return true;
				}

				auto arg_str = cmd_str.drop(cmd.size() + 1);
				if(arg_str.empty())
				{
					echo_message(sock, "expected arguments\n");
				}
				else
				{
					// massive hack but idgaf
					auto cmded = cmd::processMessage(twitch::MAGIC_OWNER_USERID, config::twitch::getOwner(),
						chan, arg_str, /* enablePings: */ false);

					if(!cmded)
					{
						chan->sendMessage(Message(arg_str));
					}
				}
			}
			else if(!cmd.empty())
			{
				echo_message(sock, zpr::sprint("unknown command '%s'\n", cmd));
			}

			print_prompt(sock);
			return true;
		}
	}











	static void setup_receiver(Socket* sock)
	{
		sock->onReceive([sock](Span input) {
			auto& buf = State.socketBuffers.wlock()->at(sock);
			buf.write(input);

			// similar thing as the websocket; if we have more than one message in the buffer
			// possibly incomplete, we can't discard existing data.
			auto sv = buf.sv();

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
		State.is_connected = true;
		srv->listen();

		lg::log("console", "starting console on port %d", port);

		auto reaper = std::thread([]() {
			while(true)
			{
				auto sock = State.danglingSockets.pop();
				if(sock == nullptr)
					break;

				sock->disconnect();
				delete sock;
			}
		});

		auto local_con = std::thread([]() {
			while(true)
			{
				if(!State.is_connected)
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
			if(!State.is_connected)
				break;

			if(auto sock = srv->accept(200ms); sock != nullptr)
			{
				lg::log("console", "started session");
				State.socketBuffers.wlock()->emplace(sock, Buffer(512));
				setup_receiver(sock);

				print_prompt(sock);
			}
		}

		// kill everything.
		State.socketBuffers.perform_write([](auto& sb) {
			for(auto& [ s, b ] : sb)
				s->disconnect();
		});

		State.danglingSockets.push(nullptr);

		local_con.join();
		reaper.join();
	}
}

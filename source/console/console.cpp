// console.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <unordered_map>

#include "db.h"
#include "cmd.h"
#include "defs.h"
#include "config.h"
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
		auto ch = (State.currentChannel ? zpr::sprint(" (%s%s) ", State.currentChannel->getBackend() == Backend::Twitch ? "#" : "",
			State.currentChannel->getName()) : "");
		echo_message(sock, zpr::sprint("λ ikura%s$ ", ch));
	}

	static void kill_socket(Socket* sock)
	{
		// remove the fella
		State.socketBuffers.wlock()->erase(sock);

		// we can't kill the socket from here (since function will be called from
		// socket's own thread). so, pass it on to someone else.
		State.danglingSockets.push(sock);

		sock->onReceive([](auto) { });
	}

	// returns false if we should quit.
	static bool process_command(Socket* sock, ikura::str_view cmd_str)
	{
		if(!cmd_str.empty() && sock != nullptr)
			lg::log("console", "console command: %s", cmd_str);

		auto say = [&](ikura::str_view msg) {

			auto chan = State.currentChannel;
			if(chan == nullptr)
			{
				echo_message(sock, "not in a channel\n");
				return;
			}

			std::string userid;
			std::string username;

			auto b = chan->getBackend();
			if(b == Backend::Twitch)
			{
				userid   = twitch::MAGIC_OWNER_USERID;
				username = config::twitch::getUsername();
			}
			else if(b == Backend::Discord)
			{
				userid   = config::discord::getUserId().str();
				username = config::discord::getUsername();
			}
			else if(b == Backend::IRC)
			{
				// uwu...
				userid   = irc::MAGIC_OWNER_USERID;
				username = irc::MAGIC_OWNER_USERID;
			}
			else
			{
				lg::fatal("console", "unsupported backend");
			}

			// massive hack but idgaf
			auto cmded = cmd::processMessage(userid, username, chan, cmd_str, /* enablePings: */ false);

			if(!cmded)
				chan->sendMessage(Message(msg));
		};


		if(cmd_str.find('/') == 0)
		{
			cmd_str.remove_prefix(1);
			if(cmd_str == "exit" || cmd_str == "q")
			{
				// only try to do socket stuff if we're a local connection.
				if(sock != nullptr)
				{
					kill_socket(sock);
				}
				else
				{
					// if you exit the local session, you kill the bot.
					State.is_connected = false;
				}

				echo_message(sock, "exiting...\n");
				return false;
			}
			else if(cmd_str == "stop" || cmd_str == "s")
			{
				// kill the entire bot, so disconnect the entire server.
				State.is_connected = false;

				echo_message(sock, "\n");
				echo_message(sock, "stopping...\n");
				return false;
			}
			else
			{
				auto cmd   = cmd_str.take(cmd_str.find(' '));
				auto _args = util::split(cmd_str.drop(cmd.size() + 1), ' ');
				auto args  = ikura::span(_args);

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
							util::sleep_for(250ms);

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
					if(args.size() < 2)
					{
						echo_message(sock, "'join' takes at least 2 arguments\n");
						return true;
					}

					auto backend = args[0];

					if(backend == "twitch")
					{
						auto channel = args[1];

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
					else if(backend == "irc")
					{
						if(args.size() < 3)
						{
							echo_message(sock, zpr::sprint("need server and channel\n"));
							goto end;
						}

						auto server = args[1];
						auto channel = args[2];

						auto chan = irc::getChannelFromServer(server, channel);
						if(chan != nullptr)
						{
							State.currentChannel = chan;
							echo_message(sock, zpr::sprint("joined %s\n", channel));
						}
						else
						{
							echo_message(sock, zpr::sprint("channel '%s' does not exist\n", channel));
						}
					}
					else if(backend == "discord")
					{
						if(args.size() < 3)
						{
							echo_message(sock, zpr::sprint("need guild and channel\n"));
							goto end;
						}

						size_t i = 0;
						std::string guild_name = args[1].str();
						for(i = 2; i < args.size(); i++)
						{
							if(args[i - 1].back() == '\\')
							{
								guild_name.pop_back();
								guild_name += " " + args[i].str();
							}
							else
							{
								break;
							}
						}

						if(i == args.size())
						{
							echo_message(sock, zpr::sprint("missing channel\n"));
							goto end;
						}

						std::string channel_name = util::join(args.drop(i), " ");

						auto guild = database().map_read([&guild_name](auto& db) -> const discord::DiscordGuild* {
							auto& dd = db.discordData;
							for(const auto& [ s, g ] : dd.guilds)
								if(g.name == guild_name)
									return &g;

							return nullptr;
						});

						if(guild == nullptr)
						{
							echo_message(sock, zpr::sprint("guild '%s' does not exist\n", guild_name));
							goto end;
						}

						discord::Snowflake chan_id;
						for(const auto& [ s, c ] : guild->channels)
							if(c.name == channel_name)
								chan_id = c.id;

						if(chan_id.empty())
						{
							echo_message(sock, zpr::sprint("channel '#%s' does not exist\n", channel_name));
							goto end;
						}

						auto chan = discord::getChannel(chan_id);
						assert(chan);

						State.currentChannel = chan;
						echo_message(sock, zpr::sprint("joined #%s\n", channel_name));
					}
					else
					{
						echo_message(sock, zpr::sprint("unknown backend '%s'\n", backend));
					}
				}
				else if(cmd == "say")
				{
					auto stuff = cmd_str.drop(cmd_str.find(' ')).trim();
					say(stuff);
				}
				else if(!cmd.empty())
				{
					echo_message(sock, zpr::sprint("unknown command '%s'\n", cmd));
				}

			end:
				print_prompt(sock);
				return true;
			}
		}
		else
		{
			if(!cmd_str.empty() /* && sock == nullptr*/ )
				say(cmd_str);

			print_prompt(sock);
			return true;
		}
	}








	static constexpr auto AUTH_TIMEOUT = 10000ms;

	// inspired by tsoding's kgbotka (https://github.com/tsoding/kgbotka)
	static bool authenticate_conn(Socket* sock)
	{
		auto echo = [&sock](const auto& fmt, auto&&... xs) {
			echo_message(sock, zpr::sprint(fmt, xs...));
		};

		std::string csrf;
		{
			static constexpr size_t CSRF_BYTES = 24;

			uint8_t bytes[CSRF_BYTES];
			for(size_t i = 0; i < CSRF_BYTES; i++)
				bytes[i] = random::get<uint8_t>();

			csrf = base64::encode(bytes, CSRF_BYTES);
		}

		echo("csrf: %s\n", csrf);
		echo("csrf? ");

		condvar<bool> cv;
		bool success = false;

		auto buf = Buffer(256);
		sock->onReceive([&buf, &csrf, &success, &cv](Span input) {

			buf.write(input);
			if(buf.sv().find("\n") == std::string::npos)
				return;

			auto user_csrf = buf.sv().take(buf.sv().find("\n")).trim(/* newlines: */ true);
			success = (user_csrf == csrf);

			cv.set(true);
		});

		if(!cv.wait(true, AUTH_TIMEOUT) || !success)
			return false;

		// now for the password.
		echo("\n");
		echo("pass? ");

		auto cfg = config::console::getConfig();
		assert(cfg.password.algo == "sha256");

		sock->onReceive([&buf, &cfg, &success, &cv](Span input) {

			buf.write(input);
			if(buf.sv().find("\n") == std::string::npos)
				return;

			auto pass = buf.sv().take(buf.sv().find("\n")).trim(/* newlines: */ true);
			auto foo = zpr::sprint("%s+%s", pass, cfg.password.salt);

			uint8_t hash[32];
			hash::sha256(hash, foo.data(), foo.size());

			success = (memcmp(hash, cfg.password.hash.data(), 32) == 0);
			cv.set(true);
		});

		cv.set_quiet(false);
		success = false;
		buf.clear();

		if(!cv.wait(true, AUTH_TIMEOUT) || !success)
			return false;

		echo("ok\n");
		return true;
	}

	static void setup_receiver(Socket* sock)
	{
		// this blocks but it's fine.
		if(!authenticate_conn(sock))
		{
			lg::warn("console", "authentication failed!");
			sock->disconnect();
			kill_socket(sock);
			return;
		}

		lg::log("console", "session authenticated (ip: %s)", sock->getAddress());

		print_prompt(sock);

		sock->onReceive([sock](Span input) {
			auto& buf = State.socketBuffers.wlock()->at(sock);
			buf.write(input);

			// similar thing as the websocket; if we have more than one message in the buffer
			// possibly incomplete, we can't discard existing data.
			auto sv = buf.sv();

			// zpr::println("%s", sv);

			while(sv.size() > 0)
			{
				auto tmp = sv.find('\n');
				if(tmp == std::string::npos)
					return;

				auto cmd = sv.take(tmp);
				sv.remove_prefix(tmp + 1);

				if(!process_command(sock, cmd.trim(/* newlines: */ true)))
					return;
			}

			buf.clear();
		});
	}

	void init()
	{
		signal(SIGPIPE, [](int) { });

		State.is_connected = true;
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

				auto res = process_command(nullptr, ikura::str_view(buf, len).trim(/* newlines: */ true));
				if(!res) break;
			}
		});

		auto consoleConfig = config::console::getConfig();
		auto port = consoleConfig.port;


		if(consoleConfig.enabled && port > 0 && !consoleConfig.host.empty())
		{
			auto srv = new Socket(consoleConfig.host, port, /* ssl: */ false);
			State.is_connected = true;

			if(srv->listen())
			{
				lg::log("console", "starting console on port %d (bind: %s)", port, srv->getAddress());

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

				while(true)
				{
					if(!State.is_connected)
						break;

					if(auto sock = srv->accept(200ms); sock != nullptr)
					{
						lg::log("console", "authenticating session (ip: %s)", sock->getAddress());
						State.socketBuffers.wlock()->emplace(sock, Buffer(512));
						setup_receiver(sock);
					}
				}

				// kill everything.
				State.socketBuffers.perform_write([](auto& sb) {
					for(auto& [ s, b ] : sb)
						s->disconnect();
				});

				State.danglingSockets.push(nullptr);
				reaper.join();
			}
			else
			{
				lg::warn("console", "could not bind console port %d", port);
			}
		}

		local_con.join();
	}
}

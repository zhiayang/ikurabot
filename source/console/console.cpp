// console.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <map>

#include "db.h"
#include "cmd.h"
#include "defs.h"
#include "types.h"
#include "config.h"
#include "interp.h"
#include "twitch.h"
#include "markov.h"
#include "synchro.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::console
{
	constexpr uint8_t FF_NONE        = 0x0;
	constexpr uint8_t FF_IRC_ALL     = 0x1;
	constexpr uint8_t FF_TWITCH_ALL  = 0x2;
	constexpr uint8_t FF_DISCORD_ALL = 0x4;

	struct LogFilter
	{
		ikura::string_set ircServers;
		ikura::string_set twitchChannels;
		ikura::string_set discordServers;

		ikura::string_map<ikura::string_set> ircChannels;
		ikura::string_map<ikura::string_set> discordChannels;

		uint8_t flags = FF_NONE;
	};

	static struct {

		bool is_connected = false;
		ikura::wait_queue<Socket*> danglingSockets;
		std::map<Socket*, LogFilter> filterSettings;
		Synchronised<std::map<Socket*, Buffer*>> socketBuffers;

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
		auto ch = (State.currentChannel ? zpr::sprint(" ({}{}) ", State.currentChannel->getBackend() == Backend::Twitch ? "#" : "",
			State.currentChannel->getName()) : "");
		echo_message(sock, zpr::sprint("λ ikura{}$ ", ch));
	}

	static void kill_socket(Socket* sock)
	{
		// remove the fella
		State.socketBuffers.perform_write([&](auto& sb) {
			delete sb[sock];
			sb.erase(sock);
		});

		// we can't kill the socket from here (since function will be called from
		// socket's own thread). so, pass it on to someone else.
		State.danglingSockets.push(sock);

		sock->onReceive([](auto) { });

		lg::log("console", "disconnected session (ip: {})", sock->getAddress());
	}

	// returns false if we should quit.
	static bool process_command(Socket* sock, ikura::str_view cmd_str)
	{
		if(!cmd_str.empty() && sock != nullptr)
			lg::log("console", "console command: {}", cmd_str);

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

		auto parse_stuff = [](ikura::str_view argstr) -> std::tuple<Backend, std::string, std::string> {

			std::tuple<Backend, std::string, std::string> ret;
			std::get<0>(ret) = Backend::Invalid;

			auto _args = util::split(argstr, ' ');
			auto args = ikura::span(_args);
			auto backend = args[0];

			if(backend == "twitch")
			{
				std::get<0>(ret) = Backend::Twitch;

				if(args.size() > 1)
				{
					auto channel = args[1];
					if(channel.find('#') == 0)
						channel.remove_prefix(1);

					std::get<1>(ret) = "";
					std::get<2>(ret) = channel.str();
				}

				return ret;
			}
			else if(backend == "irc")
			{
				std::get<0>(ret) = Backend::IRC;

				if(args.size() > 1)
					std::get<1>(ret) = args[1];

				if(args.size() > 2)
					std::get<2>(ret) = args[2];

				return ret;
			}
			else if(backend == "discord")
			{
				std::get<0>(ret) = Backend::Discord;

				if(args.size() > 1)
				{
					size_t i = 0;
					auto guild_name = args[1].str();
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

					std::get<1>(ret) = guild_name;

					if(i < args.size())
					{
						auto channel_name = util::join(args.drop(i), " ");
						std::get<2>(ret) = channel_name;
					}
				}

				return ret;
			}

			return { };
		};

		auto parse_join_args = [parse_stuff, sock](ikura::str_view argstr)
			-> std::optional<std::tuple<Backend, std::string, std::string>> {

			auto ret = parse_stuff(argstr);
			auto backend = std::get<0>(ret);

			if(backend == Backend::Twitch)
			{
				if(std::get<2>(ret).empty())
				{
					echo_message(sock, "missing channel\n");
					return { };
				}

				return ret;
			}
			else if(backend == Backend::IRC)
			{
				if(std::get<1>(ret).empty() || std::get<2>(ret).empty())
				{
					echo_message(sock, "need server and channel\n");
					return { };
				}

				return ret;
			}
			else if(backend == Backend::Discord)
			{
				if(std::get<1>(ret).empty() || std::get<2>(ret).empty())
				{
					echo_message(sock, zpr::sprint("need guild and channel\n"));
					return { };
				}

				return ret;
			}
			else
			{
				echo_message(sock, zpr::sprint("unknown backend\n"));
				return { };
			}
		};

		auto backend_to_flags = [](Backend b) -> uint8_t {
			if(b == Backend::IRC)       return FF_IRC_ALL;
			if(b == Backend::Twitch)    return FF_TWITCH_ALL;
			if(b == Backend::Discord)   return FF_DISCORD_ALL;

			return 0;
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
				auto cmd    = cmd_str.take(cmd_str.find(' '));
				auto argstr = cmd_str.drop(cmd.size() + 1);
				auto _args  = util::split(argstr, ' ');
				auto args   = ikura::span(_args);

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

							ikura::lg::log("markov", "retraining progress: {.2f}", 100 * p);
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

					Backend backend;
					std::string server;
					std::string channel;

					if(auto res = parse_join_args(argstr); res.has_value())
						std::tie(backend, server, channel) = res.value();

					else
						goto end;

					if(backend == Backend::Twitch)
					{
						auto chan = twitch::getChannel(channel);
						if(chan != nullptr)
						{
							State.currentChannel = chan;
							echo_message(sock, zpr::sprint("joined #{}\n", channel));
						}
						else
						{
							echo_message(sock, zpr::sprint("channel '#{}' does not exist\n", channel));
						}
					}
					else if(backend == Backend::IRC)
					{
						auto chan = irc::getChannelFromServer(server, channel);
						if(chan != nullptr)
						{
							State.currentChannel = chan;
							echo_message(sock, zpr::sprint("joined {}\n", channel));
						}
						else
						{
							echo_message(sock, zpr::sprint("channel '{}' does not exist\n", channel));
						}
					}
					else if(backend == Backend::Discord)
					{
						if(args.size() < 3)
						{
							echo_message(sock, zpr::sprint("need guild and channel\n"));
							goto end;
						}

						auto& guild_name = server;
						auto& channel_name = channel;

						auto guild = database().map_read([&guild_name](auto& db) -> const discord::DiscordGuild* {
							auto& dd = db.discordData;
							for(const auto& [ s, g ] : dd.guilds)
								if(g.name == guild_name)
									return &g;

							return nullptr;
						});

						if(guild == nullptr)
						{
							echo_message(sock, zpr::sprint("guild '{}' does not exist\n", guild_name));
							goto end;
						}

						discord::Snowflake chan_id;
						for(const auto& [ s, c ] : guild->channels)
							if(c.name == channel_name)
								chan_id = c.id;

						if(chan_id.empty())
						{
							echo_message(sock, zpr::sprint("channel '#{}' does not exist\n", channel_name));
							goto end;
						}

						auto chan = discord::getChannel(chan_id);
						assert(chan);

						State.currentChannel = chan;
						echo_message(sock, zpr::sprint("joined #{}\n", channel_name));
					}
				}
				else if(cmd == "say")
				{
					auto stuff = cmd_str.drop(cmd_str.find(' ')).trim();
					say(stuff);
				}
				else if(cmd == "show")
				{
					auto& filt = State.filterSettings[sock];
					if(argstr == "all")
					{
						filt.flags |= (FF_IRC_ALL | FF_DISCORD_ALL | FF_TWITCH_ALL);
					}
					else
					{
						auto [ backend, server, channel ] = parse_stuff(argstr);
						if(server.empty() && channel.empty())
						{
							filt.flags |= backend_to_flags(backend);
						}
						else if(backend == Backend::Twitch)
						{
							filt.twitchChannels.insert(channel);
						}
						else if(backend == Backend::IRC)
						{
							if(channel.empty()) filt.ircServers.insert(server);
							else                filt.ircChannels[server].insert(channel);
						}
						else if(backend == Backend::Discord)
						{
							if(channel.empty()) filt.discordServers.insert(server);
							else                filt.discordChannels[server].insert(channel);
						}
					}
				}
				else if(cmd == "hide")
				{
					auto& filt = State.filterSettings[sock];
					if(argstr == "all")
					{
						filt.flags = FF_NONE;
					}
					else
					{
						auto [ backend, server, channel ] = parse_stuff(argstr);
						if(backend == Backend::Twitch)
						{
							filt.twitchChannels.erase(channel);

							if(channel.empty())
								filt.twitchChannels.clear();
						}
						else if(backend == Backend::IRC)
						{
							if(server.empty())          filt.ircServers.clear();
							else if(channel.empty())    filt.ircServers.erase(server);
							else                        filt.ircChannels[server].erase(channel);
						}
						else if(backend == Backend::Discord)
						{
							if(server.empty())          filt.discordServers.clear();
							else if(channel.empty())    filt.discordServers.erase(server);
							else                        filt.discordChannels[server].erase(channel);
						}
					}
				}
				else if(!cmd.empty())
				{
					echo_message(sock, zpr::sprint("unknown command '{}'\n", cmd));
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

		echo("csrf: {}\n", csrf);
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
			auto foo = zpr::sprint("{}+{}", pass, cfg.password.salt);

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

		lg::log("console", "session authenticated (ip: {})", sock->getAddress());

		print_prompt(sock);

		sock->onReceive([sock](Span input) {
			Buffer* buf = State.socketBuffers.map_read([sock](auto& sb) {
				if(auto it = sb.find(sock); it != sb.end())
					return it->second;

				return (Buffer*) nullptr;
			});

			if(!buf)
				return;

			buf->write(input);

			// similar thing as the websocket; if we have more than one message in the buffer
			// possibly incomplete, we can't discard existing data.
			auto sv = buf->sv();

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

			buf->clear();
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
				lg::log("console", "starting console on port {} (bind: {})", port, srv->getAddress());

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
						lg::log("console", "authenticating session (ip: {})", sock->getAddress());
						State.socketBuffers.wlock()->emplace(sock, new Buffer(512));
						setup_receiver(sock);
					}
				}

				// kill everything.
				State.socketBuffers.perform_write([](auto& sb) {
					for(auto& [ s, b ] : sb)
					{
						s->disconnect();
						delete b;
					}
				});

				State.danglingSockets.push(nullptr);
				reaper.join();
			}
			else
			{
				lg::warn("console", "could not bind console port {}", port);
			}
		}

		local_con.join();
		lg::log("console", "quitting");
	}


	void logMessage(Backend backend, ikura::str_view server, ikura::str_view channel,
		double time, ikura::str_view user, ikura::str_view message)
	{
		std::string origin;
		if(backend == Backend::Twitch)
			origin = zpr::sprint("twitch/#{}", channel);

		else if(backend == Backend::IRC)
			origin = zpr::sprint("irc/{}/{}", server, channel);

		else if(backend == Backend::Discord)
			origin = zpr::sprint("discord/{}/#{}", server, channel);

		auto out = zpr::sprint("{}: ({.2f} ms) <{}> {}", origin, time, user, message);
		lg::log("msg", "{}", out);

		// broadcast to all sockets.
		State.socketBuffers.perform_read([&](auto& sb) {

			for(auto& [ sock, _ ] : sb)
			{
				auto& filts = State.filterSettings[sock];

				bool pass = false;
				if(backend == Backend::IRC)
				{
					auto tmp = filts.ircChannels.find(server);

					pass = (filts.flags & FF_IRC_ALL)
						|| (filts.ircServers.contains(server))
						|| (tmp != filts.ircChannels.end() && tmp->second.contains(channel));
				}
				else if(backend == Backend::Discord)
				{
					auto tmp = filts.discordChannels.find(server);

					pass = (filts.flags & FF_DISCORD_ALL)
						|| (filts.discordServers.contains(server))
						|| (tmp != filts.discordChannels.end() && tmp->second.contains(channel));
				}
				else if(backend == Backend::Twitch)
				{
					pass = (filts.flags & FF_TWITCH_ALL)
						|| (filts.twitchChannels.contains(channel));
				}

				if(pass)
				{
					echo_message(sock, zpr::sprint("\n{} {}|{} {}msg{}: {}",
						util::getCurrentTimeString(), colours::WHITE_BOLD, colours::COLOUR_RESET,
						colours::BLUE_BOLD, colours::COLOUR_RESET, out));
				}
			}
		});
	}
}

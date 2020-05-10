// twitch.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "twitch.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::twitch
{
	constexpr int CONNECT_RETRIES           = 5;
	constexpr const char* TWITCH_WSS_URL    = "wss://irc-ws.chat.twitch.tv";

	struct RateLimit
	{
		using duration = std::chrono::steady_clock::duration;
		using time_point = std::chrono::time_point<std::chrono::steady_clock>;

		RateLimit(uint64_t limit, duration interval) : limit(limit), interval(interval) { }

		uint64_t tokens;
		time_point lastRefilled;

		uint64_t limit;
		duration interval;

		bool attempt()
		{
			if(now() >= lastRefilled + interval)
			{
				tokens = std::max(limit, tokens + limit);
				lastRefilled = now();
			}

			if(tokens > 0)
			{
				tokens -= 1;
				return true;
			}

			return false;
		}

		time_point next()       { return now() + (lastRefilled + interval - now()); }
		static time_point now() { return std::chrono::steady_clock::now(); }
	};


	// this does not need to be synchronised, because we will only touch this from one thread.
	static TwitchState* state = nullptr;



	static void send_worker()
	{
		// twitch says 20 messages every 30 seconds
		auto rate = new RateLimit(20, 30s);

		while(true)
		{
			state->haveQueued.wait(true);
			if(!state->connected)
				break;

			state->sendQueue.perform_write([&](auto& queue) {
				for(const auto& m : queue)
				{
					if(!rate->attempt())
						std::this_thread::sleep_until(rate->next());

					state->ws.send(m);
					lg::log("twitch", ">>  %s", m.substr(0, m.size() - 2));
				}

				queue.clear();
			});

			state->haveQueued.set_quiet(false);
		}

		lg::log("twitch", "sender thread exited");
	}


	void init()
	{
		if(!config::haveTwitch())
			return;

		state = new TwitchState(URL(TWITCH_WSS_URL), 2000ms);

		auto backoff = 500ms;

		// try to connect.
		lg::log("twitch", "connecting...");
		for(int i = 0; i < CONNECT_RETRIES; i++)
		{
			if(state->ws.connect())
				break;

			lg::warn("twitch", "connection failed, retrying... (%d/%d)", i + 1, CONNECT_RETRIES);
			std::this_thread::sleep_for(backoff);
			backoff *= 2;
		}

		condvar<bool> didcon;

		state->ws.onReceiveText([&didcon](bool, std::string_view sv) {
			if(sv.find(":tmi.twitch.tv 001") == 0)
				didcon.set(true);

			processRawMessage(state, sv);
		});

		state->username = config::twitch::getUsername();

		// startup
		state->ws.send(zpr::sprint("PASS oauth:%s\r\n", config::twitch::getOAuthToken()));
		state->ws.send(zpr::sprint("NICK %s\r\n", config::twitch::getUsername()));

		didcon.wait(true);
		lg::log("twitch", "connected");


		for(const auto& chan : config::twitch::getJoinChannels())
		{
			state->channels[chan.name] = chan;
			state->ws.send(zpr::sprint("JOIN #%s\r\n", chan.name));
		}

		state->connected = true;
		state->sender = std::thread(&send_worker);
	}

	void shutdown()
	{
		if(!config::haveTwitch() || !state->connected)
			return;

		lg::log("twitch", "leaving channels...");

		// part from channels
		for(auto& [ name, chan ] : state->channels)
			state->ws.send(zpr::sprint("PART #%s\r\n", chan.name));

		std::this_thread::sleep_for(1s);
		state->ws.disconnect();

		state->connected = false;
		state->haveQueued.set(true);
		state->sender.join();

		database().rlock()->sync();
	}
}

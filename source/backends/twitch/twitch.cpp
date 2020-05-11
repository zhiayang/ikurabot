// twitch.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "rate.h"
#include "twitch.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::twitch
{
	constexpr int CONNECT_RETRIES           = 5;
	constexpr const char* TWITCH_WSS_URL    = "wss://irc-ws.chat.twitch.tv";

	// this does not need to be synchronised, because we will only touch this from one thread.
	static TwitchState* state = nullptr;

	TwitchState::TwitchState(URL url, std::chrono::nanoseconds timeout,
		std::string&& user, std::vector<config::twitch::Chan>&& chans) : ws(url, timeout)
	{
		auto backoff = 500ms;

		// try to connect.
		lg::log("twitch", "connecting...");
		for(int i = 0; i < CONNECT_RETRIES; i++)
		{
			if(this->ws.connect())
				break;

			lg::warn("twitch", "connection failed, retrying... (%d/%d)", i + 1, CONNECT_RETRIES);
			std::this_thread::sleep_for(backoff);
			backoff *= 2;
		}

		this->username = std::move(user);
		for(const auto& cfg : config::twitch::getJoinChannels())
			this->channels.emplace(cfg.name, TwitchChannel(this, cfg.name, cfg.lurk, cfg.mod, cfg.respondToPings));
	}

	void TwitchState::connect()
	{
		condvar<bool> didcon;
		this->ws.onReceiveText([&didcon, this](bool, ikura::str_view msg) {
			if(msg.find(":tmi.twitch.tv 001") == 0)
				didcon.set(true);

			while(msg.size() > 0)
			{
				auto i = msg.find("\r\n");
				this->processMessage(msg.substr(0, i));
				msg.remove_prefix(i + 2);
			}
		});

		// startup
		lg::log("twitch", "authenticating...");
		this->ws.send(zpr::sprint("PASS oauth:%s\r\n", config::twitch::getOAuthToken()));
		this->ws.send(zpr::sprint("NICK %s\r\n", config::twitch::getUsername()));

		didcon.wait(true);
		lg::log("twitch", "connected");

		this->connected = true;
		this->sender = std::thread([]() {
			// twitch says 20 messages every 30 seconds, so set it a little lower.
			// for channels that it moderates, the bot gets 100 every 30s.
			auto pleb_rate = new RateLimit(18, 30s);
			auto mod_rate  = new RateLimit(90, 30s);

			while(true)
			{
				state->haveQueued.wait(true);
				if(!state->connected)
					break;

				state->sendQueue.perform_write([&](auto& queue) {
					for(const auto& [ msg, mod ] : queue)
					{
						auto rate = (mod ? mod_rate : pleb_rate);

						if(!rate->attempt())
						{
							lg::warn("twitch", "exceeded rate limit");
							std::this_thread::sleep_until(rate->next());
						}

						state->ws.send(msg);
						lg::log("twitch", ">> %s", msg.substr(0, msg.size() - 2));
					}

					queue.clear();
				});

				state->haveQueued.set_quiet(false);
			}

			lg::log("twitch", "sender thread exited");
		});

		// join channels
		for(auto [ _, chan ] : this->channels)
		{
			this->ws.send(zpr::sprint("JOIN #%s\r\n", chan.name));
			lg::log("twitch", "joined #%s", chan.name);
		}
	}

	void TwitchState::disconnect()
	{
		lg::log("twitch", "leaving channels...");

		// part from channels
		for(auto& [ name, chan ] : state->channels)
			this->ws.send(zpr::sprint("PART #%s\r\n", chan.name));

		std::this_thread::sleep_for(1s);
		this->ws.disconnect();

		this->connected = false;
		this->haveQueued.set(true);
		this->sender.join();
	}

	void init()
	{
		if(!config::haveTwitch())
			return;

		state = new TwitchState(URL(TWITCH_WSS_URL), 2000ms, config::twitch::getUsername(), config::twitch::getJoinChannels());
		state->connect();
	}

	void shutdown()
	{
		if(!config::haveTwitch() || !state->connected)
			return;

		state->disconnect();
	}
}

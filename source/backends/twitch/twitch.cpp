// twitch.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "rate.h"
#include "timer.h"
#include "twitch.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura::twitch
{
	constexpr int CONNECT_RETRIES           = 5;
	constexpr const char* TWITCH_WSS_URL    = "wss://irc-ws.chat.twitch.tv";

	static Synchronised<TwitchState>* _state = nullptr;
	static Synchronised<TwitchState>& state() { return *_state; }

	static MessageQueue<twitch::QueuedMsg> msg_queue;
	MessageQueue<twitch::QueuedMsg>& message_queue() { return msg_queue; }


	void send_worker()
	{
		// twitch says 20 messages every 30 seconds, so set it a little lower.
		// for channels that it moderates, the bot gets 100 every 30s.
		auto pleb_rate = new RateLimit(18, 30s);
		auto mod_rate  = new RateLimit(90, 30s);

		while(true)
		{
			auto msg = message_queue().pop_send();
			if(msg.disconnected)
				break;

			auto rate = (msg.is_moderator ? mod_rate : pleb_rate);
			if(!rate->attempt())
			{
				lg::warn("twitch", "exceeded rate limit");
				std::this_thread::sleep_until(rate->next());
			}

			state().wlock()->ws.send(msg.msg);
		}

		lg::log("twitch", "send worker exited");
	}

	void recv_worker()
	{
		while(true)
		{
			auto msg = message_queue().pop_receive();
			if(msg.disconnected)
				break;

			auto t = timer();
			state().wlock()->processMessage(msg.msg);

			lg::log("twitch", "processed message in %.3f ms", t.measure());
		}

		lg::log("twitch", "receive worker exited");
	}






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

		if(!this->ws.connected())
			lg::error("twitch", "connection failed");

		this->username = std::move(user);
		for(const auto& cfg : config::twitch::getJoinChannels())
		{
			this->channels.emplace(cfg.name, TwitchChannel(this, cfg.name, cfg.lurk,
				cfg.mod, cfg.respondToPings, cfg.silentInterpErrors, cfg.commandPrefix));
		}
	}

	void TwitchState::connect()
	{
		if(!this->ws.connected())
			return;

		condvar<bool> didcon;
		this->ws.onReceiveText([&didcon](bool, ikura::str_view msg) {
			if(msg.find(":tmi.twitch.tv 001") == 0)
				didcon.set(true);
		});

		this->tx_thread = std::thread(send_worker);
		this->rx_thread = std::thread(recv_worker);

		// startup
		lg::log("twitch", "authenticating...");
		this->ws.send(zpr::sprint("PASS oauth:%s\r\n", config::twitch::getOAuthToken()));
		this->ws.send(zpr::sprint("NICK %s\r\n", config::twitch::getUsername()));

		if(!didcon.wait(true, 3000ms))
		{
			lg::error("twitch", "connection failed (did not authenticate)");
			this->ws.onReceiveText([](bool, ikura::str_view) { });
			this->ws.disconnect();
			return;
		}

		lg::log("twitch", "connected");
		this->connected = true;

		// install the real handler now.
		this->ws.onReceiveText([](bool, ikura::str_view msg) {
			while(msg.size() > 0)
			{
				auto x = msg.take(msg.find("\r\n")).str();
				msg.remove_prefix(x.size() + 2);

				message_queue().emplace_receive_quiet(std::move(x));
			}

			message_queue().notify_pending_receives();
		});

		// request tags
		this->ws.send("CAP REQ :twitch.tv/tags");

		// join channels
		for(auto [ _, chan ] : this->channels)
			this->ws.send(zpr::sprint("JOIN #%s\r\n", chan.name));
	}

	void TwitchState::disconnect()
	{
		lg::log("twitch", "leaving channels...");

		// we must kill the threads now, because we have the writelock on the state.
		// if they (the receiver thread) attempts to process an incoming message now,
		// it will deadlock because it also wants the writelock for the state.
		message_queue().push_send(QueuedMsg::disconnect());
		message_queue().push_receive(QueuedMsg::disconnect());

		// part from channels. we don't particularly care about the response anyway.
		for(auto& [ name, chan ] : this->channels)
			this->ws.send(zpr::sprint("PART #%s\r\n", chan.name));

		std::this_thread::sleep_for(350ms);
		this->ws.disconnect();

		this->connected = false;

		// wait for the workers to finish.
		this->tx_thread.join();
		this->rx_thread.join();

		lg::log("twitch", "disconnected");
	}

	void init()
	{
		if(!config::haveTwitch())
			return;

		_state = new Synchronised<TwitchState>(URL(TWITCH_WSS_URL), 5000ms,
			config::twitch::getUsername(), config::twitch::getJoinChannels()
		);

		state().wlock()->connect();
	}

	void shutdown()
	{
		if(!config::haveTwitch() || !state().rlock()->connected)
			return;

		state().wlock()->disconnect();
	}
}

// twitch.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <shared_mutex>

#include "defs.h"
#include "synchro.h"
#include "network.h"

namespace ikura::twitch
{
	struct TwitchState;
	struct TwitchChannel : Channel
	{
		std::string name;
		bool lurk;
		bool mod;
		bool respondToPings;

		TwitchChannel() : name(""), lurk(false), mod(false), respondToPings(false) { }
		TwitchChannel(TwitchState* st, std::string n, bool l, bool m, bool p)
			: name(std::move(n)), lurk(l), mod(m), respondToPings(p), state(st) { }

		virtual std::string getUsername() const override;
		virtual std::string getCommandPrefix() const override;
		virtual bool shouldReplyMentions() const override { return this->respondToPings; };

		virtual void sendMessage(const Message& msg) const override;

	private:
		TwitchState* state = nullptr;
	};

	struct TwitchState
	{
		TwitchState(URL url, std::chrono::nanoseconds timeout, std::string&& user, std::vector<config::twitch::Chan>&& chans);

		bool connected = false;
		std::string username;
		ikura::string_map<TwitchChannel> channels;

		void processMessage(ikura::str_view msg);
		void sendMessage(ikura::str_view channel, ikura::str_view msg);
		void sendRawMessage(ikura::str_view msg, ikura::str_view associated_channel = "");

	private:
		WebSocket ws;
		std::thread sender;
		condvar<bool> haveQueued;
		Synchronised<std::vector<std::pair<std::string, bool>>, std::shared_mutex> sendQueue;

		void connect();
		void disconnect();

		friend void init();
		friend void shutdown();

		friend struct TwitchChannel;
	};

	void init();
	void shutdown();
}

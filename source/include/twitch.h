// twitch.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "synchro.h"
#include "network.h"
#include "msgqueue.h"

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

		virtual std::string getName() const override;
		virtual std::string getUsername() const override;
		virtual std::string getCommandPrefix() const override;
		virtual bool shouldReplyMentions() const override;
		virtual uint32_t getUserPermissions(ikura::str_view user) const override;

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

		std::thread tx_thread;
		std::thread rx_thread;

		void connect();
		void disconnect();

		friend void send_worker();
		friend void recv_worker();

		friend void init();
		friend void shutdown();

		friend struct TwitchChannel;
	};

	struct QueuedMsg
	{
		QueuedMsg(std::string msg) : msg(std::move(msg)), is_moderator(false), disconnected(false) { }
		QueuedMsg(std::string msg, bool mod) : msg(std::move(msg)), is_moderator(mod), disconnected(false) { }
		QueuedMsg(std::string msg, bool mod, bool discon) : msg(std::move(msg)), is_moderator(mod), disconnected(discon) { }

		static QueuedMsg disconnect() { return QueuedMsg("__disconnect__", false, true); }

		std::string msg;
		bool is_moderator;
		bool disconnected;
	};

	void init();
	void shutdown();

	MessageQueue<twitch::QueuedMsg>& message_queue();
}

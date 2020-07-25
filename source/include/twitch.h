// twitch.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "emotes.h"
#include "network.h"
#include "msgqueue.h"

namespace ikura::config::twitch { struct Chan; }

namespace ikura::twitch
{
	constexpr const char* MAGIC_OWNER_USERID = "__owner__";

	struct TwitchState;
	struct Channel : ikura::Channel
	{
		Channel() : name(""), lurk(false), mod(false), respondToPings(false) { }
		Channel(TwitchState* st, std::string n, bool l, bool m, bool p, bool si, bool mh, std::string cp)
			: name(std::move(n)), lurk(l), mod(m), respondToPings(p), silentInterpErrors(si), runMessageHandlers(mh),
			  commandPrefix(std::move(cp)), state(st) { }

		virtual std::string getName() const override;
		virtual std::string getUsername() const override;
		virtual std::string getCommandPrefix() const override;
		virtual bool shouldReplyMentions() const override;
		virtual bool shouldPrintInterpErrors() const override;
		virtual bool shouldRunMessageHandlers() const override;
		virtual Backend getBackend() const override { return Backend::Twitch; }
		virtual bool checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const override;

		virtual void sendMessage(const Message& msg) const override;

	private:
		std::string name;
		bool lurk;
		bool mod;
		bool respondToPings;
		bool silentInterpErrors;
		bool runMessageHandlers;
		std::string commandPrefix;

		mutable std::string lastSentMessage;

		TwitchState* state = nullptr;

		friend struct TwitchState;
	};

	struct TwitchState
	{
		TwitchState(URL url, std::chrono::nanoseconds timeout, std::string&& user, std::vector<config::twitch::Chan>&& chans);

		bool connected = false;
		std::string username;
		ikura::string_map<Channel> channels;

		void processMessage(ikura::str_view msg);
		void sendMessage(ikura::str_view channel, ikura::str_view msg);
		void sendRawMessage(ikura::str_view msg, ikura::str_view associated_channel = "");

		void logMessage(uint64_t timestamp, ikura::str_view userid, Channel* chan, ikura::str_view message,
			const std::vector<ikura::relative_str>& emote_idxs, bool isCmd);

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

		friend struct Channel;
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

	const Channel* getChannel(ikura::str_view name);

	MessageQueue<twitch::QueuedMsg>& mqueue();




	// db stuff
	struct TwitchMessage : Serialisable
	{
		// in milliseconds, as usual.
		uint64_t timestamp = 0;

		std::string userid;
		std::string username;
		std::string displayname;

		std::string channel;

		ikura::relative_str message;
		std::vector<ikura::relative_str> emotePositions;

		bool isCommand = false;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<TwitchMessage> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_LOG_MSG;
	};

	struct TwitchMessageLog : Serialisable
	{
		std::vector<TwitchMessage> messages;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<TwitchMessageLog> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_LOG;
	};

	struct TwitchUser : Serialisable
	{
		std::string id;
		std::string username;
		std::string displayname;

		uint64_t permissions;           // see defs.h/ikura::permissions
		uint64_t subscribedMonths;

		std::vector<uint64_t> groups;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<TwitchUser> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_USER;
	};

	struct TwitchChannel : Serialisable
	{
		std::string id;
		std::string name;

		// map from userid to user.
		ikura::string_map<TwitchUser> knownUsers;

		// map from username to userid
		ikura::string_map<std::string> usernameMapping;

		EmoteCacheDB ffzEmotes;
		EmoteCacheDB bttvEmotes;

		TwitchUser* getUser(ikura::str_view userid);
		const TwitchUser* getUser(ikura::str_view userid) const;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<TwitchChannel> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_CHANNEL;
	};

	struct TwitchDB : Serialisable
	{
		ikura::string_map<TwitchChannel> channels;

		TwitchMessageLog messageLog;
		EmoteCacheDB globalBttvEmotes;

		const TwitchChannel* getChannel(ikura::str_view name) const;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<TwitchDB> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_DB;
	};
}

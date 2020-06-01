// discord.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <map>
#include <unordered_map>

#include "network.h"
#include "msgqueue.h"

namespace picojson { class value; }

// ugh
namespace ikura::discord
{
	struct Snowflake : Serialisable
	{
		uint64_t value;

		bool operator == (Snowflake s) const { return this->value == s.value; }
		bool operator != (Snowflake s) const { return this->value != s.value; }

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Snowflake> deserialise(Span& buf);
	};
}

namespace std
{
	template <>
	struct hash<ikura::discord::Snowflake>
	{
		size_t operator () (ikura::discord::Snowflake s) const
		{
			return std::hash<uint64_t>()(s.value);
		}
	};
}

namespace ikura::discord
{
	namespace opcode
	{
		constexpr int64_t DISPATCH      = 0;
		constexpr int64_t HEARTBEAT     = 1;
		constexpr int64_t IDENTIFY      = 2;
		constexpr int64_t RESUME        = 6;
		constexpr int64_t RECONNECT     = 7;
		constexpr int64_t INVALID_SESS  = 9;
		constexpr int64_t HELLO         = 10;
		constexpr int64_t HEARTBEAT_ACK = 11;
	}

	namespace intent
	{
		constexpr int64_t GUILDS                    = (1 << 0);
		constexpr int64_t GUILD_MEMBERS             = (1 << 1);
		constexpr int64_t GUILD_BANS                = (1 << 2);
		constexpr int64_t GUILD_EMOJIS              = (1 << 3);
		constexpr int64_t GUILD_INTEGRATIONS        = (1 << 4);
		constexpr int64_t GUILD_WEBHOOKS            = (1 << 5);
		constexpr int64_t GUILD_INVITES             = (1 << 6);
		constexpr int64_t GUILD_VOICE_STATES        = (1 << 7);
		constexpr int64_t GUILD_PRESENCES           = (1 << 8);
		constexpr int64_t GUILD_MESSAGES            = (1 << 9);
		constexpr int64_t GUILD_MESSAGE_REACTIONS   = (1 << 10);
		constexpr int64_t GUILD_MESSAGE_TYPING      = (1 << 11);
		constexpr int64_t DIRECT_MESSAGES           = (1 << 12);
		constexpr int64_t DIRECT_MESSAGE_REACTIONS  = (1 << 13);
		constexpr int64_t DIRECT_MESSAGE_TYPING     = (1 << 14);
	}

	struct DiscordState
	{
		DiscordState(URL url, std::chrono::nanoseconds timeout);

		bool connected = false;

	private:
		WebSocket ws;
		std::chrono::milliseconds heartbeat_interval;

		std::thread tx_thread;
		std::thread rx_thread;
		std::thread hb_thread;

		int64_t sequence = -1;
		bool didAckHeartbeat = false;
		std::chrono::system_clock::time_point last_heartbeat_ack;

		void connect();
		void disconnect();

		void processMessage(std::map<std::string, picojson::value> m);

		friend void send_worker();
		friend void recv_worker();

		friend void heartbeat_worker();

		friend void init();
		friend void shutdown();
	};

	void init();
	void shutdown();

	struct QueuedMsg;
	MessageQueue<discord::QueuedMsg>& mqueue();

	struct DiscordUser : Serialisable
	{
		Snowflake id;
		std::string name;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordUser> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_USER;
	};

	struct DiscordRole : Serialisable
	{
		Snowflake id;
		std::string name;

		// discord permissions bitflag
		uint64_t discordPerms;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordRole> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_ROLE;
	};

	// similar to twitch, we tie the credentials of a user to a guild, instead of to the user itself.
	struct DiscordUserCredentials : Serialisable
	{
		// see defs.h/ikura::permissions
		uint64_t permissions;

		// these are internal groups, shared with the twitch db.
		std::vector<uint64_t> groups;

		std::vector<Snowflake> discordRoles;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordUserCredentials> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_USER_CREDS;
	};

	struct DiscordChannel : Serialisable
	{
		Snowflake id;
		std::string name;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordChannel> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_CHANNEL;
	};

	struct DiscordGuild : Serialisable
	{
		Snowflake id;
		std::string name;

		std::unordered_map<Snowflake, DiscordChannel> channels;
		std::unordered_map<Snowflake, DiscordRole> roles;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordGuild> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_GUILD;
	};

	struct DiscordDB : Serialisable
	{
		std::unordered_map<Snowflake, DiscordGuild> guilds;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordDB> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_DB;
	};
}

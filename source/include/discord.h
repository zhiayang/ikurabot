// discord.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <map>
#include <unordered_map>

#include "network.h"
#include "msgqueue.h"

namespace picojson { class value; }

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

	struct DiscordGuild;
	struct DiscordState;

	struct Channel : ikura::Channel
	{
		Channel() : lurk(false), respondToPings(false) { }
		Channel(DiscordState* st, DiscordGuild* g, Snowflake id, bool l, bool p, bool si, std::string cp)
			: guild(g), channelId(id), lurk(l), respondToPings(p), silentInterpErrors(si),
			  commandPrefix(std::move(cp)), state(st) { }

		virtual std::string getName() const override;
		virtual std::string getUsername() const override;
		virtual std::string getCommandPrefix() const override;
		virtual bool shouldReplyMentions() const override;
		virtual bool shouldPrintInterpErrors() const override;
		virtual Backend getBackend() const override { return Backend::Discord; }
		virtual bool checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const override;

		virtual void sendMessage(const Message& msg) const override;

		DiscordGuild* getGuild() { return this->guild; }
		const DiscordGuild* getGuild() const { return this->guild; }

	private:
		DiscordGuild* guild = nullptr;
		Snowflake channelId;
		bool lurk;
		bool respondToPings;
		bool silentInterpErrors = false;
		std::string commandPrefix;

		DiscordState* state = nullptr;

		friend struct DiscordState;
	};

	struct DiscordState
	{
		DiscordState(URL url, std::chrono::nanoseconds timeout);

		bool connect();
		void disconnect();



		tsl::robin_map<Snowflake, Channel> channels;

		static constexpr int API_VERSION     = 6;
		static constexpr const char* API_URL = "https://discord.com/api";

	private:
		WebSocket ws;
		std::chrono::milliseconds heartbeat_interval;

		std::thread tx_thread;
		std::thread rx_thread;
		std::thread hb_thread;

		int64_t sequence = -1;
		std::string session_id;
		bool didAckHeartbeat = false;
		std::chrono::system_clock::time_point last_heartbeat_ack;

		void send_resume();
		void send_identify();

		bool init(bool resume);
		bool internal_connect(bool resume);

		bool resume();

		void processEvent(std::map<std::string, picojson::value> m);
		void processMessage(std::map<std::string, picojson::value> m);

		friend void send_worker();
		friend void recv_worker();

		friend void heartbeat_worker();
	};

	void init();
	void shutdown();

	struct QueuedMsg;
	MessageQueue<discord::QueuedMsg>& mqueue();

	std::optional<Snowflake> parseMention(ikura::str_view str, size_t* consumed);

	struct DiscordUser : Serialisable
	{
		Snowflake id;
		std::string username;
		std::string nickname;

		// see defs.h/ikura::permissions
		uint64_t permissions;

		// these are internal groups, shared with the twitch db.
		std::vector<uint64_t> groups;

		std::vector<Snowflake> discordRoles;

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

		tsl::robin_map<Snowflake, DiscordRole> roles;
		tsl::robin_map<Snowflake, DiscordChannel> channels;

		tsl::robin_map<Snowflake, DiscordUser> knownUsers;

		// { id, is_animated }
		ikura::string_map<std::pair<Snowflake, bool>> emotes;

		ikura::string_map<Snowflake> roleNames;
		ikura::string_map<Snowflake> usernameMap;
		ikura::string_map<Snowflake> nicknameMap;

		DiscordRole* getRole(ikura::str_view name);
		const DiscordRole* getRole(ikura::str_view name) const;

		DiscordUser* getUser(Snowflake id);
		const DiscordUser* getUser(Snowflake id) const;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordGuild> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_GUILD;
	};

	struct DiscordDB : Serialisable
	{
		tsl::robin_map<Snowflake, DiscordGuild> guilds;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<DiscordDB> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_DISCORD_DB;
	};
}

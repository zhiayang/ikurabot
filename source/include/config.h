// config.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "types.h"

namespace ikura::config
{
	bool load(ikura::str_view path);

	bool haveIRC();
	bool haveTwitch();
	bool haveDiscord();

	namespace twitch
	{
		struct Chan
		{
			std::string name;
			bool lurk;
			bool mod;
			bool respondToPings;
			bool silentInterpErrors;
			bool runMessageHandlers;
			std::vector<std::string> commandPrefixes;

			bool haveBTTVEmotes;
			bool haveFFZEmotes;
		};

		std::string getOwner();
		std::string getUsername();
		std::string getOAuthToken();
		std::vector<Chan> getJoinChannels();
		std::vector<std::string> getIgnoredUsers();
		bool isUserIgnored(ikura::str_view id);
		uint64_t getEmoteAutoUpdateInterval();
	}

	namespace discord
	{
		struct Guild
		{
			std::string id;
			bool lurk;
			bool respondToPings;
			bool silentInterpErrors;
			bool runMessageHandlers;
			std::vector<std::string> commandPrefixes;
		};

		std::string getUsername();
		std::string getOAuthToken();
		std::vector<Guild> getJoinGuilds();
		ikura::discord::Snowflake getOwner();
		ikura::discord::Snowflake getUserId();
		std::vector<ikura::discord::Snowflake> getIgnoredUserIds();
		bool isUserIgnored(ikura::discord::Snowflake userid);
	}

	namespace irc
	{
		struct Channel
		{
			// includes the #, because aodhneine wanted it
			// also because it's a good idea
			std::string name;

			bool lurk;
			bool respondToPings;
			bool silentInterpErrors;
			bool runMessageHandlers;
			std::vector<std::string> commandPrefixes;
		};

		struct Server
		{
			std::string name;
			std::string hostname;
			uint16_t port;
			bool useSSL;
			bool useSASL;

			std::string nickname;
			std::string username;
			std::string password;
			std::string owner;

			std::vector<std::string> ignoredUsers;
			std::vector<Channel> channels;

			bool isUserIgnored(ikura::str_view id);
		};

		std::vector<Server> getJoinServers();
	}

	namespace console
	{
		struct Password
		{
			std::string salt;
			std::string algo;
			std::vector<uint8_t> hash;
		};

		struct ConsoleConfig
		{
			bool enabled;
			uint16_t port;
			std::string host;

			Password password;
		};

		ConsoleConfig getConfig();
	}

	namespace markov
	{
		struct MarkovConfig
		{
			bool stripPings;
			int minLength;
			int maxRetries;
		};

		MarkovConfig getConfig();
	}
}

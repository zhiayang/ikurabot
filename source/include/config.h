// config.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "types.h"

namespace ikura::config
{
	bool load(ikura::str_view path);

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
			std::string commandPrefix;
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
			std::string commandPrefix;
		};

		std::string getUsername();
		std::string getOAuthToken();
		std::vector<Guild> getJoinGuilds();
		ikura::discord::Snowflake getOwner();
		ikura::discord::Snowflake getUserId();
		std::vector<ikura::discord::Snowflake> getIgnoredUserIds();
		bool isUserIgnored(ikura::discord::Snowflake userid);
	}

	namespace global
	{
		int getConsolePort();
		bool stripMentionsFromMarkov();

		size_t getMinMarkovLength();
		size_t getMaxMarkovRetries();
	}
}

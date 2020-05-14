// db.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <string>
#include <optional>

#include "defs.h"
#include "buffer.h"
#include "synchro.h"

namespace ikura
{
	namespace db
	{
		struct TwitchUserCredentials : Serialisable
		{
			uint32_t permissions;       // see defs.h/ikura::permissions
			uint32_t subscriptionMonths;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<TwitchUserCredentials> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_USER_CREDS;
		};

		struct TwitchUser : Serialisable
		{
			std::string id;
			std::string username;
			std::string displayname;

			TwitchUserCredentials credentials;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<TwitchUser> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_USER;
		};

		struct TwitchDB : Serialisable
		{
			// map from userid to user.
			ikura::string_map<TwitchUser> knownTwitchUsers;

			// cache of known username -> userids
			ikura::string_map<std::string> knownTwitchIdMappings;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<TwitchDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_DB;
		};

		struct DbInterpState : Serialisable
		{
			virtual void serialise(Buffer& buf) const override;
			static std::optional<DbInterpState> deserialise(Span& buf);
		};

		struct Database : Serialisable
		{
			TwitchDB twitchData;
			DbInterpState interpState;

			void sync() const;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Database> deserialise(Span& buf);

			static Database create();

		private:
			char magic[8];      // 'ikura_db'
			uint32_t version;   // currently, 1
			uint32_t flags;     // there are none defined
			uint64_t timestamp; // modified timestamp (millis since 1970)
		};

		bool load(ikura::str_view path, bool create);
	}

	Synchronised<db::Database>& database();
}

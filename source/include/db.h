// db.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <string>
#include <optional>
#include <shared_mutex>

#include "cmd.h"
#include "defs.h"
#include "synchro.h"
#include "serialise.h"

namespace ikura
{
	namespace db
	{
		struct TwitchUser : serialise::Serialisable
		{
			std::string id;
			std::string username;
			std::string displayname;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<TwitchUser> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_USER;
		};

		struct TwitchDB : serialise::Serialisable
		{
			// map from userid to user.
			ikura::string_map<TwitchUser> knownTwitchUsers;

			// cache of known username -> userids
			ikura::string_map<std::string> knownTwitchIdMappings;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<TwitchDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_TWITCH_DB;
		};

		struct CommandDB : serialise::Serialisable
		{
			// map of name -> Command
			ikura::string_map<cmd::Command> commands;

			// map of alias -> name
			ikura::string_map<std::string> aliases;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<CommandDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_COMMAND_DB;
		};

		struct Database : serialise::Serialisable
		{
			TwitchDB twitchData;
			CommandDB commands;

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

	Synchronised<db::Database, std::shared_mutex>& database();
}

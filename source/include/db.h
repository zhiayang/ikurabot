// db.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "defs.h"
#include "buffer.h"
#include "markov.h"
#include "twitch.h"
#include "discord.h"
#include "synchro.h"

namespace ikura
{
	namespace db
	{
		struct DbInterpState : Serialisable
		{
			virtual void serialise(Buffer& buf) const override;
			static std::optional<DbInterpState> deserialise(Span& buf);
		};

		struct MessageDB : Serialisable
		{
			const std::string& data() const { return this->rawData; }
			ikura::relative_str logMessageContents(ikura::str_view contents);

			virtual void serialise(Buffer& buf) const override;
			static std::optional<MessageDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_MESSAGE_DB;

		private:
			std::string rawData;
		};

		struct GenericUser : Serialisable
		{
			std::string id;
			Backend backend;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<GenericUser> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_GENERIC_USER;
		};

		struct Group : Serialisable
		{
			uint64_t id;
			std::string name;
			std::vector<GenericUser> members;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Group> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_GROUP;
		};

		struct SharedDB : Serialisable
		{
			ikura::string_map<Group> groups;

			Group* getGroup(ikura::str_view name);
			const Group* getGroup(ikura::str_view name) const;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<SharedDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_SHARED_DB;
		};

		struct Database : Serialisable
		{
			DbInterpState interpState;
			twitch::TwitchDB twitchData;
			markov::MarkovDB markovData;
			discord::DiscordDB discordData;
			SharedDB sharedData;
			MessageDB messageData;

			void sync() const;

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Database> deserialise(Span& buf);

			static Database create();

			uint32_t version() const { return this->_version; }

		private:
			char _magic[8];
			uint32_t _version;
			uint32_t _flags;
			uint64_t _timestamp;
		};

		uint32_t getVersion();
		bool load(ikura::str_view path, bool create);
	}

	Synchronised<db::Database>& database();
}

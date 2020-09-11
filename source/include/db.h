// db.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "irc.h"
#include "markov.h"
#include "twitch.h"
#include "discord.h"

namespace ikura
{
	namespace twitch  { struct TwitchDB; }
	namespace markov  { struct MarkovDB; }
	namespace discord { struct DiscordDB; }
	namespace irc::db { struct IrcDB; }

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
			GenericUser() { }
			GenericUser(std::string id, Backend b) : id(std::move(id)), backend(b) { }

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

			void addUser(const std::string& userid, Backend backend);
			void removeUser(const std::string& userid, Backend backend);

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Group> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_GROUP;
		};

		struct SharedDB : Serialisable
		{
			Group* getGroup(ikura::str_view name);
			const Group* getGroup(ikura::str_view name) const;

			Group* getGroup(uint64_t id);
			const Group* getGroup(uint64_t id) const;

			const ikura::string_map<Group>& getGroups() const;

			// returns true on success
			bool addGroup(ikura::str_view name);
			bool removeGroup(ikura::str_view name);

			virtual void serialise(Buffer& buf) const override;
			static std::optional<SharedDB> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_SHARED_DB;

		private:
			ikura::string_map<Group> groups;
			tsl::robin_map<uint64_t, std::string> groupIds;
		};

		struct Database : Serialisable
		{
			DbInterpState interpState;
			twitch::TwitchDB twitchData;
			markov::MarkovDB markovData;
			discord::DiscordDB discordData;
			irc::db::IrcDB ircData;
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
		bool load(ikura::str_view path, bool create, bool readonly);
	}

	Synchronised<db::Database>& database();
}

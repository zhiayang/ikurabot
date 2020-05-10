// database.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


#include <filesystem>

#include "db.h"
#include "defs.h"
#include "serialise.h"

namespace std { namespace fs = filesystem; }

namespace ikura::db
{
	struct Superblock
	{
		char magic[8];      // "ikura_db"
		uint32_t version;   // currently, 1
		uint32_t flags;     // there are none defined
		uint64_t timestamp; // the timestamp, in milliseconds when the database was last modified
	};

	static_assert(sizeof(Superblock) == 24);

	constexpr const char* DB_MAGIC  = "ikura_db";
	constexpr uint64_t DB_VERSION   = 1;

	static Database TheDatabase;
	static std::fs::path databasePath;

	// just a simple wrapper
	template <typename... Args>
	std::nullopt_t error(const std::string& fmt, Args&&... args)
	{
		lg::error("db", fmt, args...);
		return std::nullopt;
	}

	Database Database::create()
	{
		Database db;

		memcpy(db.magic, DB_MAGIC, 8);
		db.flags = 0;
		db.version = DB_VERSION;
		db.timestamp = util::getMillisecondTimestamp();

		return db;
	}

	static void createNewDatabase(const std::fs::path& path)
	{
		lg::log("db", "creating new database '%s'", path.string());

		auto db = Database::create();

		// mode 664
		int fd = open(path.string().c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if(fd < 0)
		{
			lg::error("db", "failed to create! error: %s", strerror(errno));
			exit(1);
		}

		auto buf = Buffer(1024);
		db.serialise(buf);

		write(fd, buf.data(), buf.size());
		close(fd);
	}

	bool load(std::string_view p, bool create)
	{
		std::fs::path path = p;
		if(!std::fs::exists(path))
		{
			if(create)  createNewDatabase(path);
			else        return lg::error("db", "file does not exist");
		}
		else if(create)
		{
			lg::warn("db", "database '%s' exists, ignoring '--create' flag", path.string());
		}

		// ok, for sure now there's something.
		auto [ buf, len ] = util::mmapEntireFile(path.string());
		if(buf == nullptr || len == 0)
			return false;

		databasePath = path;

		bool succ = false;
		auto span = Span(buf, len);

		if(auto db = Database::deserialise(span); db.has_value())
			succ = true, TheDatabase = std::move(db.value());

		util::munmapEntireFile(buf, len);
		return succ;
	}

	void Database::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);

		Superblock sb;
		memcpy(sb.magic, this->magic, 8);
		sb.flags = this->flags;
		sb.version = this->version;
		sb.timestamp = util::getMillisecondTimestamp();

		buf.write(&sb, sizeof(Superblock));

		wr.write(this->twitchData);
		wr.write(this->commands);
	}

	std::optional<Database> Database::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		auto sb = buf.as<Superblock>();

		if(buf.size() < sizeof(Superblock))
			return error("database truncated (not enough bytes!)");

		if(strncmp(sb->magic, DB_MAGIC, 8) != 0)
			return error("invalid database identifier (expected '%s', got '%.*s')", DB_MAGIC, 8, sb->magic);

		if(sb->version != DB_VERSION)
			return error("invalid version %lu (expected %lu)", sb->version, DB_VERSION);

		Database db;
		memcpy(db.magic, sb->magic, 8);
		db.flags = sb->flags;
		db.version = sb->version;
		db.timestamp = sb->timestamp;

		buf.remove_prefix(sizeof(Superblock));

		if(!rd.read(&db.twitchData))
			return error("failed to read twitch data");

		if(!rd.read(&db.commands))
			return error("failed to read commands");

		return db;
	}


	void Database::sync() const
	{
		auto buf = Buffer(512);
		this->serialise(buf);

		int fd = open(databasePath.string().c_str(), O_WRONLY | O_TRUNC);
		if(fd < 0)
		{
			lg::error("failed to open for writing! error: %s", strerror(errno));
			return;
		}

		size_t todo = buf.size();
		while(todo > 0)
			todo -= write(fd, buf.data(), todo);

		close(fd);
	}

	void TwitchDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->knownTwitchUsers);
		wr.write(this->knownTwitchIdMappings);
	}

	std::optional<TwitchDB> TwitchDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return error("type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		TwitchDB ret;

		if(!rd.read(&ret.knownTwitchUsers))
			return error("failed to read twitch users");

		if(!rd.read(&ret.knownTwitchIdMappings))
			return error("failed to read twitch ids");

		return ret;
	}








	void TwitchUser::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->username);
		wr.write(this->displayname);
	}

	std::optional<TwitchUser> TwitchUser::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return error("type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		TwitchUser ret;
		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.username))
			return { };

		if(!rd.read(&ret.displayname))
			return { };

		return ret;
	}


	std::optional<CommandDB> CommandDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return error("type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		CommandDB ret;

		if(!rd.read(&ret.commands))
			return { };

		if(!rd.read(&ret.aliases))
			return { };

		return ret;
	}

	void CommandDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->commands);
		wr.write(this->aliases);
	}
}

namespace ikura::cmd
{
	void Command::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
		wr.write(this->action);
		wr.write(this->timeoutType);
		wr.write(this->allowedUsers);
		wr.write(this->timeoutMilliseconds);
	}

	std::optional<Command> Command::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return db::error("type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		Command ret;
		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.action))
			return { };

		if(!rd.read(&ret.timeoutType))
			return { };

		if(!rd.read(&ret.allowedUsers))
			return { };

		if(!rd.read(&ret.timeoutMilliseconds))
			return { };

		return ret;
	}
}


namespace ikura
{
	db::Database& database()
	{
		return db::TheDatabase;
	}
}

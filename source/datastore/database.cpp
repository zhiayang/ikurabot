// database.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


#include <filesystem>

#include "db.h"
#include "timer.h"
#include "twitch.h"
#include "markov.h"
#include "discord.h"
#include "synchro.h"
#include "serialise.h"

using namespace std::chrono_literals;
namespace std { namespace fs = filesystem; }

namespace ikura::db
{
	struct Superblock
	{
		char magic[8];      // "ikura_db"
		uint32_t version;   // see DB_VERSION
		uint32_t flags;     // there are none defined
		uint64_t timestamp; // the timestamp, in milliseconds when the database was last modified
	};

	static_assert(sizeof(Superblock) == 24);

	constexpr uint32_t DB_VERSION   = 30;
	constexpr const char* DB_MAGIC  = "ikura_db";

	// the database will only sync to disk if it was modified
	// or rather, if anyone took a write lock on it. so we can afford
	// to set the interval a little shorter.
	constexpr auto SYNC_INTERVAL    = 30s;

	static std::atomic<bool> databaseDirty = false;
	static Synchronised<Database> TheDatabase;
	static std::fs::path databasePath;
	static bool readOnly = false;

	// this is kind of a dirty hack, but whatever. it's not like we'll have more than 1 database instance
	// per program. what this stores is the version of the database that we read from disk; this lets
	// us selectively read fields when we add/remove stuff. this is only set in Database::deserialise,
	// immediately after it reads the superblock.
	static uint32_t currentDatabaseVersion = 0;
	uint32_t getVersion() { return currentDatabaseVersion; }


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

		memcpy(db._magic, DB_MAGIC, 8);
		db._flags = 0;
		db._version = DB_VERSION;
		db._timestamp = util::getMillisecondTimestamp();

		return db;
	}

	static void createNewDatabase(const std::fs::path& path)
	{
		lg::log("db", "creating new database '{}'", path.string());

		*TheDatabase.wlock().get() = Database::create();
		TheDatabase.rlock()->sync();
	}

	bool load(ikura::str_view p, bool create, bool readonly)
	{
		std::fs::path path = p.str();
		databasePath = path;
		readOnly = readonly;

		if(!std::fs::exists(path))
		{
			if(create)  createNewDatabase(path);
			else        return lg::error_b("db", "file does not exist");
		}
		else if(create)
		{
			lg::warn("db", "database '{}' exists, ignoring '--create' flag", path.string());
		}

		if(!(std::fs::is_regular_file(path) || std::fs::is_symlink(path)))
		{
			return lg::error_b("db", "given path '{}' was not a regular file (or symlink)",
				path.string());
		}

		auto t = timer();

		// ok, for sure now there's something.
		auto [ fd, buf, len ] = util::mmapEntireFile(path.string());
		if(buf == nullptr || len == 0)
			return false;

		bool succ = false;
		auto span = Span(buf, len);

		lg::log("db", "loading database...");
		if(auto db = Database::deserialise(span); db.has_value())
			succ = true, *TheDatabase.wlock().get() = std::move(db.value());

		util::munmapEntireFile(fd, buf, len);

		if(succ)
		{
			if(!readOnly)
			{
				// make a backup if we wrote.
				if(currentDatabaseVersion < DB_VERSION)
				{
					auto backup = path;
					backup.replace_filename(zpr::sprint("db-backup-v{}.db", currentDatabaseVersion));

					lg::log("db", "making a backup: '{}' -> '{}'", path.string(), backup.string());

					std::error_code ec;
					std::fs::copy_file(path, backup, ec);

					if(ec)
						return lg::error_b("db", "failed to create backup: {}", ec.message());
				}

				TheDatabase.on_write_lock([]() { databaseDirty = true; });

				// setup an idiot to periodically synchronise the database to disk.
				auto thr = std::thread([]() {
					while(true)
					{
						util::sleep_for(SYNC_INTERVAL);
						if(databaseDirty)
						{
							databaseDirty = false;
							ikura::database().rlock()->sync();
						}
					}
				});

				thr.detach();
			}

			lg::log("db", "{}database (version {}) loaded in {.2f} ms",
				readOnly ? "READONLY " : "",
				currentDatabaseVersion, t.measure());
		}

		return succ;
	}

	void Database::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);

		Superblock sb;
		memcpy(sb.magic, this->_magic, 8);
		sb.flags = this->_flags;
		sb.version = this->_version;
		sb.timestamp = util::getMillisecondTimestamp();

		currentDatabaseVersion = this->_version;

		buf.write(&sb, sizeof(Superblock));

		wr.write(this->twitchData);
		wr.write(this->interpState);
		wr.write(this->markovData);
		wr.write(this->sharedData);
		wr.write(this->discordData);
		wr.write(this->ircData);
		wr.write(this->messageData);
	}

	std::optional<Database> Database::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		auto sb = buf.as<Superblock>();

		if(buf.size() < sizeof(Superblock))
			return error("database truncated (not enough bytes!)");

		if(strncmp(sb->magic, DB_MAGIC, 8) != 0)
			return error("invalid database identifier (expected '{}', got '{}')", DB_MAGIC, zpr::p(8)(sb->magic));

		if(sb->version > DB_VERSION)
			return error("invalid version {} (expected <= {})", sb->version, DB_VERSION);

		auto t = timer();
		double times[7] = { };

		Database db;
		memcpy(db._magic, sb->magic, 8);
		db._flags = sb->flags;
		db._version = sb->version;
		db._timestamp = sb->timestamp;

		buf.remove_prefix(sizeof(Superblock));

		currentDatabaseVersion = db._version;
		if(currentDatabaseVersion < DB_VERSION)
			lg::log("db", "upgrading database from version {} to {}", currentDatabaseVersion, DB_VERSION);

		if(!rd.read(&db.twitchData))
			return error("failed to read twitch data");

		times[0] = t.reset();
		if(!rd.read(&db.interpState))
			return error("failed to read command interpreter state");

		times[1] = t.reset();
		if(!rd.read(&db.markovData))
			return error("failed to read markov data");

		times[2] = t.reset();
		if(!rd.read(&db.sharedData))
			return error("failed to read shared data");

		times[3] = t.reset();
		if(!rd.read(&db.discordData))
			return error("failed to read discord data");

		times[4] = t.reset();
		if(currentDatabaseVersion >= 25 && !rd.read(&db.ircData))
			return error("failed to read irc data");

		times[5] = t.reset();
		if(!rd.read(&db.messageData))
			return error("failed to read message logs");

		times[6] = t.reset();

		// once we are done reading the database from disk, the in-memory state is considered gospel.
		// thus, we can "upgrade" the version.
		db._version = DB_VERSION;

		lg::log("db", "db loads (ms): [ {.2f}, {.2f}, {.2f}, {.2f}, {.2f}, {.2f}, {.2f} ]",
			times[0], times[1], times[2], times[3], times[4], times[5], times[6]);

		return db;
	}


	void Database::sync() const
	{
		if(readOnly)
			return;

		auto t = timer();

		auto buf = Buffer(512);
		this->serialise(buf);

		// make the new one
		std::fs::path newdb = databasePath;
		newdb.concat(".new");

		int fd = open(newdb.string().c_str(), O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if(fd < 0)
		{
			error("failed to open for writing! error: {}", strerror(errno));
			return;
		}

		size_t todo = buf.size();
		while(todo > 0)
		{
			auto ret = write(fd, buf.data(), todo);
			if(ret <= 0)
			{
				error("failed to sync! write error: {}", strerror(errno));
				close(fd);
				return;
			}

			todo -= ret;
		}

		close(fd);

		std::error_code ec;
		std::fs::rename(newdb, databasePath, ec);
		if(ec)
		{
			error("failed to sync! error: {}", ec.message());
			return;
		}

		lg::log("db", "sync in {.2f} ms", t.measure());
	}
}

namespace ikura
{
	Synchronised<db::Database>& database()
	{
		return db::TheDatabase;
	}
}

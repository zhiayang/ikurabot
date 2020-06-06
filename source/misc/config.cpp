// config.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <filesystem>

#include "defs.h"
#include "config.h"
#include "twitch.h"
#include "discord.h"

#include "picojson.h"

namespace std { namespace fs = filesystem; }

namespace ikura::config
{
	using ikura::discord::Snowflake;

	static struct {

		bool present = false;

		std::string owner;
		std::string username;
		std::string oauthToken;
		std::vector<twitch::Chan> channels;
		std::vector<std::string> ignoredUsers;
		uint64_t emoteAutoUpdateInterval_millis;

	} TwitchConfig;

	static struct {

		bool present = false;

		Snowflake owner;
		std::string username;
		Snowflake userid;
		std::string oauthToken;
		std::vector<discord::Guild> guilds;
		std::vector<Snowflake> ignoredUsers;

	} DiscordConfig;

	static struct {
		int consolePort = 0;
		bool stripMentionsFromMarkov = false;
		size_t minMarkovLength = 0;
		size_t maxMarkovRetries = 0;
	} GlobalConfig;

	namespace twitch
	{
		std::string getOwner()          { return TwitchConfig.owner; }
		std::string getUsername()       { return TwitchConfig.username; }
		std::string getOAuthToken()     { return TwitchConfig.oauthToken; }

		std::vector<Chan> getJoinChannels()         { return TwitchConfig.channels; }
		std::vector<std::string> getIgnoredUsers()  { return TwitchConfig.ignoredUsers; }

		bool isUserIgnored(ikura::str_view username)
		{
			return std::find(TwitchConfig.ignoredUsers.begin(), TwitchConfig.ignoredUsers.end(),
				username.str()) != TwitchConfig.ignoredUsers.end();
		}

		uint64_t getEmoteAutoUpdateInterval()
		{
			return TwitchConfig.emoteAutoUpdateInterval_millis;
		}
	}

	namespace discord
	{
		Snowflake getOwner()                { return DiscordConfig.owner; }
		std::string getUsername()           { return DiscordConfig.username; }
		std::string getOAuthToken()         { return DiscordConfig.oauthToken; }
		std::vector<Guild> getJoinGuilds()  { return DiscordConfig.guilds; }

		std::vector<Snowflake> getIgnoredUsers() { return DiscordConfig.ignoredUsers; }
		Snowflake getUserId() { return DiscordConfig.userid; }

		bool isUserIgnored(Snowflake id)
		{
			return std::find(DiscordConfig.ignoredUsers.begin(), DiscordConfig.ignoredUsers.end(),
				id) != DiscordConfig.ignoredUsers.end();
		}
	}

	namespace global
	{
		int getConsolePort() { return GlobalConfig.consolePort; }
		bool stripMentionsFromMarkov() { return GlobalConfig.stripMentionsFromMarkov; }

		size_t getMinMarkovLength() { return GlobalConfig.minMarkovLength; }
		size_t getMaxMarkovRetries() { return GlobalConfig.maxMarkovRetries; }
	}








	static std::string get_string(const pj::object& opts, const std::string& key, const std::string& def)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is_str())
				return it->second.as_str();

			else
				lg::error("cfg", "expected string value for '%s'", key);
		}

		return def;
	}

	static std::vector<pj::value> get_array(const pj::object& opts, const std::string& key)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is_arr())
				return it->second.as_arr();

			else
				lg::error("cfg", "expected array value for '%s'", key);
		}

		return { };
	}

	static int64_t get_integer(const pj::object& opts, const std::string& key, int64_t def)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is_int())
				return it->second.as_int();

			else
				lg::error("cfg", "expected integer value for '%s'", key);
		}

		return def;
	}

	static bool get_bool(const pj::object& opts, const std::string& key, bool def)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is_bool())
				return it->second.as_bool();

			else
				lg::error("cfg", "expected boolean value for '%s'", key);
		}

		return def;
	}


	static bool loadGlobalConfig(const pj::object& global)
	{
		GlobalConfig.consolePort = get_integer(global, "console_port", 0);
		GlobalConfig.stripMentionsFromMarkov = get_bool(global, "strip_markov_pings", false);

		// defaults to 1 and 0 -- min length of 1, 0 retries.
		auto minLen = get_integer(global, "min_markov_length", 1);
		auto maxRetry = get_integer(global, "max_markov_retries", 0);

		if(minLen < 1)
		{
			lg::warn("cfg/global", "invalid value '%ld' for min_markov_length", minLen);
			minLen = 1;
		}
		if(maxRetry < 1)
		{
			lg::warn("cfg/global", "invalid value '%ld' for max_markov_retries", maxRetry);
			maxRetry = 0;
		}

		GlobalConfig.minMarkovLength = minLen;
		GlobalConfig.maxMarkovRetries = maxRetry;

		return true;
	}

	static bool loadDiscordConfig(const pj::object& discord)
	{
		auto username = get_string(discord, "username", "");
		if(username.empty())
			return lg::error_b("cfg/discord", "username cannot be empty");

		auto oauthToken = get_string(discord, "oauth_token", "");
		if(oauthToken.empty())
			return lg::error_b("cfg/discord", "oauth_token cannot be empty");

		auto userid = get_string(discord, "id", "");
		if(userid.empty())
			return lg::error_b("cfg/discord", "id cannot be empty");

		auto owner = get_string(discord, "owner", "");
		if(owner.empty())
			return lg::error_b("cfg/discord", "owner cannot be empty");

		DiscordConfig.owner = Snowflake(owner);
		DiscordConfig.userid = Snowflake(userid);
		DiscordConfig.username = username;
		DiscordConfig.oauthToken = oauthToken;

		auto guilds = get_array(discord, "guilds");
		if(!guilds.empty())
		{
			for(const auto& ch : guilds)
			{
				if(!ch.is_obj())
				{
					lg::error("cfg/discord", "channel should be a json object");
					continue;
				}

				auto obj = ch.as_obj();

				discord::Guild guild;
				guild.id = get_string(obj, "id", "");
				if(guild.id.empty())
				{
					lg::error("cfg/discord", "guild id cannot be empty");
				}
				else
				{
					guild.silentInterpErrors = get_bool(obj, "silent_interp_errors", false);
					guild.respondToPings = get_bool(obj, "respond_to_pings", false);
					guild.lurk = get_bool(obj, "lurk", false);
					guild.commandPrefix = get_string(obj, "command_prefix", "");

					DiscordConfig.guilds.push_back(std::move(guild));
				}
			}
		}

		auto ignored = get_array(discord, "ignored_users");
		if(!ignored.empty())
		{
			for(const auto& ign : ignored)
			{
				if(!ign.is_str())
				{
					lg::error("cfg/discord", "ignored_users should contain strings");
					continue;
				}

				auto str = ign.as_str();
				DiscordConfig.ignoredUsers.emplace_back(std::move(str));
			}
		}

		DiscordConfig.present = true;
		return true;
	}








	static bool loadTwitchConfig(const pj::object& twitch)
	{
		auto username = get_string(twitch, "username", "");
		if(username.empty())
			return lg::error_b("cfg/twitch", "username cannot be empty");

		auto owner = get_string(twitch, "owner", "");
		if(owner.empty())
			return lg::error_b("cfg/twitch", "owner cannot be empty");

		auto oauthToken = get_string(twitch, "oauth_token", "");
		if(oauthToken.empty())
			return lg::error_b("cfg/twitch", "oauth_token cannot be empty");

		TwitchConfig.owner = owner;
		TwitchConfig.username = username;
		TwitchConfig.oauthToken = oauthToken;

		// config file in seconds, but we want milliseconds for internal consistency
		TwitchConfig.emoteAutoUpdateInterval_millis = 1000 * get_integer(twitch, "bttv_ffz_autorefresh_interval", 0);

		auto ignored = get_array(twitch, "ignored_users");
		if(!ignored.empty())
		{
			for(const auto& ign : ignored)
			{
				if(!ign.is_str())
				{
					lg::error("cfg/twitch", "ignored_users should contain strings");
					continue;
				}

				auto str = ign.as_str();
				TwitchConfig.ignoredUsers.push_back(std::move(str));
			}
		}

		auto channels = get_array(twitch, "channels");
		if(!channels.empty())
		{
			for(const auto& ch : channels)
			{
				if(!ch.is_obj())
				{
					lg::error("cfg/twitch", "channel should be a json object");
					continue;
				}

				auto obj = ch.as_obj();

				twitch::Chan chan;
				chan.name = get_string(obj, "name", "");
				if(chan.name.empty())
				{
					lg::error("cfg/twitch", "channel name cannot be empty");
				}
				else
				{
					chan.silentInterpErrors = get_bool(obj, "silent_interp_errors", false);
					chan.respondToPings = get_bool(obj, "respond_to_pings", false);
					chan.lurk = get_bool(obj, "lurk", false);
					chan.mod = get_bool(obj, "mod", false);
					chan.commandPrefix = get_string(obj, "command_prefix", "");

					TwitchConfig.channels.push_back(std::move(chan));
				}
			}
		}

		TwitchConfig.present = true;
		return true;
	}

	bool haveTwitch()
	{
		return TwitchConfig.present;
	}

	bool haveDiscord()
	{
		return DiscordConfig.present;
	}

	bool load(ikura::str_view path)
	{
		std::fs::path configPath = path.sv();
		if(!std::fs::exists(configPath))
			return lg::error_b("cfg", "file does not exist", path);

		auto [ buf, sz ] = util::readEntireFile(configPath.string());
		if(!buf || sz == 0)
			return lg::error_b("cfg", "failed to read file");

		pj::value config;

		auto begin = buf;
		auto end = buf + sz;
		std::string err;
		pj::parse(config, begin, end, &err);

		if(!err.empty())
			return lg::error_b("cfg", "json error: %s", err);

		if(auto global = config.get("global"); !global.is_null() && global.is_obj())
		{
			loadGlobalConfig(global.as_obj());
		}

		if(auto twitch = config.get("twitch"); !twitch.is_null() && twitch.is_obj())
		{
			loadTwitchConfig(twitch.as_obj());
		}

		if(auto discord = config.get("discord"); !discord.is_null() && discord.is_obj())
		{
			loadDiscordConfig(discord.as_obj());
		}

		delete[] buf;
		return true;
	}
}

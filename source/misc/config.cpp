// config.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <filesystem>

#include "defs.h"
#include "twitch.h"

#define PICOJSON_USE_INT64 1
#include "picojson.h"

namespace pj = picojson;
namespace std { namespace fs = filesystem; }

namespace ikura::config
{
	static struct {

		bool present = false;

		std::string owner;
		std::string username;
		std::string oauthToken;
		std::vector<twitch::Chan> channels;

	} TwitchConfig;

	static struct {
		int consolePort = 0;
	} GlobalConfig;

	namespace twitch
	{
		std::string getOwner()          { return TwitchConfig.owner; }
		std::string getUsername()       { return TwitchConfig.username; }
		std::string getOAuthToken()     { return TwitchConfig.oauthToken; }

		std::vector<Chan> getJoinChannels()
		{
			return TwitchConfig.channels;
		}
	}

	namespace global
	{
		int getConsolePort() { return GlobalConfig.consolePort; }
	}








	static std::string get_string(const pj::object& opts, const std::string& key, const std::string& def)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is<std::string>())
				return it->second.get<std::string>();

			else
				lg::error("cfg", "expected string value for '%s'", key);
		}

		return def;
	}

	static std::vector<pj::value> get_array(const pj::object& opts, const std::string& key)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is<pj::array>())
				return it->second.get<pj::array>();

			else
				lg::error("cfg", "expected array value for '%s'", key);
		}

		return { };
	}

	static int64_t get_integer(const pj::object& opts, const std::string& key, int64_t def)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is<int64_t>())
				return it->second.get<int64_t>();

			else
				lg::error("cfg", "expected integer value for '%s'", key);
		}

		return def;
	}

	static bool get_bool(const pj::object& opts, const std::string& key, bool def)
	{
		if(auto it = opts.find(key); it != opts.end())
		{
			if(it->second.is<bool>())
				return it->second.get<bool>();

			else
				lg::error("cfg", "expected boolean value for '%s'", key);
		}

		return def;
	}


	static bool loadGlobalConfig(const pj::object& global)
	{
		GlobalConfig.consolePort = get_integer(global, "console_port", 0);
		return true;
	}

	static bool loadTwitchConfig(const pj::object& twitch)
	{
		auto username = get_string(twitch, "username", "");
		if(username.empty())
			return lg::error("cfg/twitch", "username cannot be empty");

		auto owner = get_string(twitch, "owner", "");
		if(owner.empty())
			return lg::error("cfg/twitch", "owner cannot be empty");

		auto oauthToken = get_string(twitch, "oauth_token", "");
		if(oauthToken.empty())
			return lg::error("cfg/twitch", "oauth_token cannot be empty");

		TwitchConfig.owner = owner;
		TwitchConfig.username = username;
		TwitchConfig.oauthToken = oauthToken;

		auto channels = get_array(twitch, "channels");
		if(!channels.empty())
		{
			for(const auto& ch : channels)
			{
				if(!ch.is<pj::object>())
					lg::error("cfg/twitch", "channel should be a json object");

				auto obj = ch.get<pj::object>();

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
		return false;
	}

	bool load(ikura::str_view path)
	{
		std::fs::path configPath = path.sv();
		if(!std::fs::exists(configPath))
			return lg::error("cfg", "file does not exist", path);

		auto [ buf, sz ] = util::readEntireFile(configPath.string());
		if(!buf || sz == 0)
			return lg::error("cfg", "failed to read file");

		pj::value config;

		auto begin = buf;
		auto end = buf + sz;
		std::string err;
		pj::parse(config, begin, end, &err);

		if(!err.empty())
			return lg::error("cfg", "json error: %s", err);

		if(auto global = config.get("global"); !global.is<pj::null>() && global.is<pj::object>())
		{
			loadGlobalConfig(global.get<pj::object>());
		}

		if(auto twitch = config.get("twitch"); !twitch.is<pj::null>() && twitch.is<pj::object>())
		{
			loadTwitchConfig(twitch.get<pj::object>());
		}

		if(auto discord = config.get("discord"); !discord.is<pj::null>() && discord.is<pj::object>())
		{
		}


		delete[] buf;
		return true;
	}
}

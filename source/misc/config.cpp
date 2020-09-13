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

	static std::vector<irc::Server> IrcServerConfigs;

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

	namespace irc
	{
		bool Server::isUserIgnored(ikura::str_view name)
		{
			return std::find(this->ignoredUsers.begin(), this->ignoredUsers.end(), name) != this->ignoredUsers.end();
		}

		std::vector<Server> getJoinServers()
		{
			return IrcServerConfigs;
		}
	}

	namespace markov
	{
		static markov::MarkovConfig config;
		MarkovConfig getConfig()
		{
			return config;
		}
	}

	namespace console
	{
		static console::ConsoleConfig config;
		ConsoleConfig getConfig()
		{
			return config;
		}
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

	// file:<path> or env:<var>
	static std::string get_secret_string(const pj::object& opts, const std::string& key, const std::string& def)
	{
		auto raw = get_string(opts, key, def);
		if(raw.find("file:") == 0)
		{
			auto path = raw.substr(5);
			auto [ buf, sz ] = util::readEntireFile(path);

			if(!buf || sz == 0)
			{
				lg::error("cfg", "could not read file '%s' for key '%s'", path, key);
				return "";
			}

			//* for now we just take the first line.
			// TODO: allow specifying the line? eg. file:<path>:line
			auto ret = util::split(std::string((const char*) buf, sz), '\n')[0].str();
			delete[] buf;

			return ret;
		}
		else if(raw.find("env:") == 0)
		{
			auto name = raw.substr(4);
			return util::getEnvironmentVar(name);
		}
		else
		{
			return raw;
		}
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

	static void loadConsoleConfig(const pj::object& obj)
	{
		console::config.port    = get_integer(obj, "port", 0);
		console::config.host    = get_string(obj, "hostname", "");
		console::config.enabled = get_bool(obj, "enabled", false);

		if(auto pwobj = obj.find("password"); pwobj->second.is_obj())
		{
			auto& obj = pwobj->second.as_obj();

			console::config.password.salt = get_string(obj, "salt", "");
			console::config.password.algo = get_string(obj, "algo", "");

			if(console::config.password.algo != "sha256")
			{
				lg::error("cfg/console", "unsupported hash algo '%s', password disabled",
					console::config.password.algo);

				goto fail;
			}

			auto& algo = console::config.password.algo;
			auto hash = get_string(obj, "hash", "");
			if(hash.empty())
			{
				lg::error("cfg/console", "hash cannot be empty");
				goto fail;
			}

			if(algo == "sha256" && hash.size() != 32)
			{
				lg::error("cfg/console", "password hash has invalid length for '%s'", hash.size(), algo);
				goto fail;
			}


			console::config.password.hash.reserve(hash.size() / 2);
			for(size_t i = 0; i < hash.size() / 2; i++)
			{
				char a = hash[i * 2 + 0];
				char b = hash[i * 2 + 1];

				auto is_valid_char = [](char x) -> bool {
					return ('0' <= x && x <= '9')
						|| ('a' <= x && x <= 'f')
						|| ('A' <= x && x <= 'F');
				};

				auto get_value = [](char x) -> uint8_t {
					if('0' <= x && x <= '9') return x - '0';
					if('a' <= x && x <= 'f') return 10 + x - 'a';
					if('A' <= x && x <= 'F') return 10 + x - 'A';

					return 0;
				};

				if(!is_valid_char(a) || !is_valid_char(b))
				{
					lg::error("cfg/console", "invalid char '%c' or '%c' in hash", a, b);
					goto fail;
				}

				uint8_t x = (get_value(a) << 4) | get_value(b);
				console::config.password.hash.push_back(x);
			}
		}
		else
		{
			lg::warn("cfg/console", "no password set, remote console will be disabled");

		fail:
			console::config.enabled = false;
		}
	}


	static void loadMarkovConfig(const pj::object& obj)
	{
		markov::config.stripPings = get_bool(obj, "strip_pings", false);

		// defaults to 1 and 0 -- min length of 1, 0 retries.
		auto minLen = get_integer(obj, "min_length", 1);
		auto maxRetry = get_integer(obj, "max_retries", 0);

		if(minLen < 1)
		{
			lg::warn("cfg/markov", "invalid value '%ld' for min_length", minLen);
			minLen = 1;
		}

		if(maxRetry < 1)
		{
			lg::warn("cfg/markov", "invalid value '%ld' for max_retries", maxRetry);
			maxRetry = 0;
		}

		markov::config.minLength = minLen;
		markov::config.maxRetries = maxRetry;
	}



	static bool loadDiscordConfig(const pj::object& discord)
	{
		auto username = get_string(discord, "username", "");
		if(username.empty())
			return lg::error_b("cfg/discord", "username cannot be empty");

		auto oauthToken = get_secret_string(discord, "oauth_token", "");
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
					guild.lurk                  = get_bool(obj, "lurk", false);
					guild.silentInterpErrors    = get_bool(obj, "silent_interp_errors", false);
					guild.respondToPings        = get_bool(obj, "respond_to_pings", false);
					guild.runMessageHandlers    = get_bool(obj, "run_message_handlers", false);
					guild.commandPrefix         = get_string(obj, "command_prefix", "");

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

		auto oauthToken = get_secret_string(twitch, "oauth_token", "");
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
					chan.respondToPings     = get_bool(obj, "respond_to_pings", false);
					chan.lurk               = get_bool(obj, "lurk", false);
					chan.mod                = get_bool(obj, "mod", false);
					chan.runMessageHandlers = get_bool(obj, "run_message_handlers", false);
					chan.haveFFZEmotes      = get_bool(obj, "ffz_emotes", false);
					chan.haveBTTVEmotes     = get_bool(obj, "bttv_emotes", false);
					chan.commandPrefix      = get_string(obj, "command_prefix", "");

					TwitchConfig.channels.push_back(std::move(chan));
				}
			}
		}

		TwitchConfig.present = true;
		return true;
	}


	static bool loadIRCConfig(const pj::object& irc)
	{
		auto servers = get_array(irc, "servers");
		if(!servers.empty())
		{
			for(const auto& srv : servers)
			{
				if(!srv.is_obj())
				{
					lg::error("cfg/irc", "server should be a json object");
					continue;
				}

				auto obj = srv.as_obj();

				irc::Server server;
				server.hostname = get_string(obj, "hostname", "");
				if(server.hostname.empty())
				{
					lg::error("cfg/irc", "server hostname cannot be empty");
					continue;
				}

				server.name     = get_string(obj, "name", "");
				server.useSSL   = get_bool(obj, "ssl", true);
				server.useSASL  = get_bool(obj, "sasl", false);
				server.port     = get_integer(obj, "port", server.useSSL ? 6697 : 6667);

				server.username = get_string(obj, "username", "");
				server.nickname = get_string(obj, "nickname", server.username);

				if(server.username.empty() && server.nickname.empty())
				{
					lg::error("cfg/irc", "username cannot be empty");
					continue;
				}
				else if(server.nickname.empty())
				{
					server.nickname = server.username;
				}
				else if(server.username.empty())
				{
					server.username = server.nickname;
				}

				// i guess password can be empty if you didn't bother to identify with services...
				server.password = get_secret_string(obj, "password", "");

				server.owner = get_string(obj, "owner", "");
				auto ignored = get_array(obj, "ignored_users");
				if(!ignored.empty())
				{
					for(const auto& ign : ignored)
					{
						if(!ign.is_str())
						{
							lg::error("cfg/irc", "ignored_users should contain strings");
							continue;
						}

						auto str = ign.as_str();
						server.ignoredUsers.push_back(std::move(str));
					}
				}

				auto channels = get_array(obj, "channels");
				if(!channels.empty())
				{
					for(const auto& ch : channels)
					{
						if(!ch.is_obj())
						{
							lg::error("cfg/irc", "channel should be a json object");
							continue;
						}

						auto obj = ch.as_obj();

						irc::Channel chan;
						chan.name = get_string(obj, "name", "");
						if(chan.name.empty())
						{
							lg::error("cfg/irc", "channel name cannot be empty");
						}
						else
						{
							chan.silentInterpErrors = get_bool(obj, "silent_interp_errors", false);
							chan.respondToPings = get_bool(obj, "respond_to_pings", false);
							chan.lurk = get_bool(obj, "lurk", false);
							chan.commandPrefix = get_string(obj, "command_prefix", "");
							chan.runMessageHandlers = get_bool(obj, "run_message_handlers", false);

							server.channels.push_back(std::move(chan));
						}
					}
				}

				IrcServerConfigs.push_back(std::move(server));
			}
		}

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

	bool haveIRC()
	{
		return !IrcServerConfigs.empty();
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

		if(auto markov = config.get("markov"); markov.is_obj())
			loadMarkovConfig(markov.as_obj());

		if(auto console = config.get("console"); console.is_obj())
			loadConsoleConfig(console.as_obj());

		if(auto twitch = config.get("twitch"); twitch.is_obj())
			loadTwitchConfig(twitch.as_obj());

		if(auto discord = config.get("discord"); discord.is_obj())
			loadDiscordConfig(discord.as_obj());

		if(auto irc = config.get("irc"); irc.is_obj())
			loadIRCConfig(irc.as_obj());

		delete[] buf;
		return true;
	}
}

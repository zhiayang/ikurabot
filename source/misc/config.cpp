// config.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <filesystem>

#include "defs.h"

#define PICOJSON_USE_INT64 1
#include "picojson.h"


namespace pj = picojson;
namespace std { namespace fs = filesystem; }

namespace ikura::config
{
	static struct {

		struct TwitchChannel
		{
			std::string name;
			bool lurk;
		};

		bool present = false;

		std::string username;
		std::string oauthToken;
		std::vector<TwitchChannel> channels;

	} TwitchConfig;

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


	static bool loadTwitchConfig(const pj::object& twitch)
	{
		auto username = get_string(twitch, "username", "");
		if(username.empty())
			return lg::error("cfg/twitch", "username cannot be empty");

		auto oauthToken = get_string(twitch, "oauth_token", "");
		if(oauthToken.empty())
			return lg::error("cfg/twitch", "oauth_token cannot be empty");

		TwitchConfig.username = username;
		TwitchConfig.oauthToken = oauthToken;

		auto channels = get_array(twitch, "channels");
		if(!channels.empty())
		{
			for(const auto& ch : channels)
			{
				if(!ch.is<pj::object>())
					lg::error("cfg/twitch", "channel should be a json object");

				auto chan = ch.get<pj::object>();

				decltype(TwitchConfig)::TwitchChannel tmp;
				tmp.name = get_string(chan, "name", "");
				if(tmp.name.empty())
				{
					lg::error("cfg/twitch", "channel name cannot be empty");
				}
				else
				{
					tmp.lurk = get_bool(chan, "lurk", false);
					TwitchConfig.channels.push_back(std::move(tmp));
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

	bool load(std::string_view path)
	{
		std::fs::path configPath = path;
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

		// read twitch
		if(auto twitch = config.get("twitch"); !twitch.is<pj::null>() && twitch.is<pj::object>())
		{
			loadTwitchConfig(twitch.get<pj::object>());
		}

		// read discord
		if(auto discord = config.get("discord"); !discord.is<pj::null>() && discord.is<pj::object>())
		{
		}






		delete[] buf;
		return true;
	}

#if 0

	void readConfig()
	{
		// if there's a manual one, use that.
		std::fs::path path;
		if(auto cp = getConfigPath(); !cp.empty())
		{
			path = cp;
			if(!std::fs::exists(path))
			{
				// ...
				util::error("specified configuration file '%s' does not exist", cp);
				return;
			}
		}

		if(auto cp = getDefaultConfigPath(); !cp.empty())
		{
			path = cp;

			// read it.
			uint8_t* buf = 0; size_t sz = 0;
			std::tie(buf, sz) = util::readEntireFile(path.string());
			if(!buf || sz == 0)
			{
				error("failed to read file");
				return;
			}

			util::log("reading config file '%s'", path.string());


			pj::value config;

			auto begin = buf;
			auto end = buf + sz;
			std::string err;
			pj::parse(config, begin, end, &err);
			if(!err.empty())
			{
				error("%s", err);
				return;
			}

			// the top-level object should be "options".
			if(auto options = config.get("options"); !options.is<pj::null>())
			{

				auto opts = options.get<pj::object>();

				auto get_string = [&opts](const std::string& key, const std::string& def) -> std::string {
					if(auto it = opts.find(key); it != opts.end())
					{
						if(it->second.is<std::string>())
							return it->second.get<std::string>();

						else
							error("expected string value for '%s'", key);
					}

					return def;
				};

				auto get_array = [&opts](const std::string& key) -> std::vector<pj::value> {
					if(auto it = opts.find(key); it != opts.end())
					{
						if(it->second.is<pj::array>())
							return it->second.get<pj::array>();

						else
							error("expected array value for '%s'", key);
					}

					return { };
				};

				auto get_bool = [&opts](const std::string& key, bool def) -> bool {
					if(auto it = opts.find(key); it != opts.end())
					{
						if(it->second.is<bool>())
							return it->second.get<bool>();

						else
							error("expected boolean value for '%s'", key);
					}

					return def;
				};

				if(auto x = get_string("tvdb-api-key", ""); !x.empty())
					setTVDBApiKey(x);

				if(auto x = get_string("moviedb-api-key", ""); !x.empty())
					setMovieDBApiKey(x);

				if(auto x = get_string("output-folder", ""); !x.empty())
					setOutputFolder(x);


				auto get_langs = [](const std::vector<pj::value>& xs, const std::string& foo) -> std::vector<std::string> {

					return util::mapFilter(xs, [&foo](const pj::value& v) -> std::string {
						if(v.is<std::string>())
							return v.get<std::string>();

						else
							util::error("expected string value in '%s'", foo);

						return "";
					}, [](const std::string& s) -> bool {
						if(s.empty()) return false;
						if(s.size() != 3)
						{
							util::error("invalid language code '%s'. see https://w.wiki/EXG for a list.");
							return false;
						}

						return true;
					});
				};

				if(auto x = get_array("preferred-audio-languages"); !x.empty())
					setAudioLangs(get_langs(x, "preferred-audio-languages"));

				else
					setAudioLangs({ "eng" });

				if(auto x = get_array("preferred-subtitle-languages"); !x.empty())
					setSubtitleLangs(get_langs(x, "preferred-subtitle-languages"));

				else
					setSubtitleLangs({ "eng" });



				// these are simply the default values without a config file.
				// some are true and some are false, because of the way the boolean is
				// named -- eg. in code we have disableAutoCoverSearch (negative), but in
				// the config it's "automatic-cover-search" (positive).

				setDisableProgress(!get_bool("show-progress", true));
				setShouldStopOnError(get_bool("stop-on-first-error", false));
				setIsPreferEnglishTitle(get_bool("prefer-english-title", false));
				setIsPreferEnglishTitle(!get_bool("prefer-original-title", true));
				setDisableAutoCoverSearch(!get_bool("automatic-cover-search", true));
				setShouldDeleteExistingOutput(get_bool("delete-existing-output", false));
				setDisableSmartReplaceCoverArt(!get_bool("smart-cover-art-replacement", true));

				setShouldRenameFiles(get_bool("automatic-rename-files", false));
				setIsOverridingMovieName(get_bool("override-movie-name", false));
				setIsOverridingSeriesName(get_bool("override-series-name", false));
				setIsOverridingEpisodeName(get_bool("override-episode-name", false));
				setShouldRenameWithoutEpisodeTitle(!get_bool("rename-with-episode-title", true));

				setPreferOneStream(get_bool("prefer-one-stream", true));
				setPreferSDHSubs(get_bool("prefer-sdh-subtitles", false));
				setPreferTextSubs(get_bool("prefer-text-subtitles", true));
				setPreferSignSongSubs(get_bool("prefer-signs-and-songs-subs", false));
				setSkipNCOPNCED(get_bool("skip-ncop-nced", false));
			}
			else
			{
				error("no top-level 'options' object");
			}


			delete[] buf;
		}

		// it's ok not to have one.
	}
#endif
}

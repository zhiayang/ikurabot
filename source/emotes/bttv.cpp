// bttv.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "async.h"
#include "twitch.h"
#include "config.h"
#include "network.h"

#include "picojson.h"

constexpr const char* BTTV_API_URL = "https://api.betterttv.net/3";

namespace ikura::twitch::bttv
{
	static CachedEmote construct_cached_emote(const std::string& id, const std::string& code)
	{
		CachedEmote ce;

		ce.source = CachedEmote::Source::BTTV;
		ce.id = id;
		ce.name = code;
		ce.url = zpr::sprint("https://cdn.betterttv.net/emote/%s/3x", ce.id);

		return ce;
	}

	future<void> updateGlobalEmotes(bool force)
	{
		return dispatcher().run([](bool force) {
			auto now = util::getMillisecondTimestamp();
			auto last = database().rlock()->twitchData.globalBttvEmotes.lastUpdatedTimestamp;
			auto interval = config::twitch::getEmoteAutoUpdateInterval();

			if(!force && (interval == 0 || (now - last < interval)))
				return;

			database().wlock()->twitchData.globalBttvEmotes.lastUpdatedTimestamp = now;

			auto [ hdr, body ] = request::get(URL(zpr::sprint("%s/cached/emotes/global", BTTV_API_URL)));
			if(auto st = hdr.statusCode(); st != 200 || body.empty())
				return lg::error("bttv", "failed to fetch emotes (error %d):\n%s", st, body);

			auto res = util::parseJson(body);
			if(!res)
				return lg::error("bttv", "json response error: %s", res.error());

			ikura::string_map<CachedEmote> list;

			auto& json = res.unwrap();
			auto& ems = json.as_arr();
			for(const auto& e : ems)
			{
				auto x = e.as_obj();
				auto n = x["code"].as_str();
				list.emplace(n, construct_cached_emote(x["id"].as_str(), n));
			}

			lg::log("bttv", "fetched %zu global emotes", list.size());
			database().wlock()->twitchData.globalBttvEmotes.emotes = std::move(list);

		}, force);
	}

	future<void> updateChannelEmotes(const std::string& channelId, const std::string& channelName, bool force)
	{
		return dispatcher().run([](bool force, std::string chan_id, std::string chan_name) {
			auto now = util::getMillisecondTimestamp();
			auto last = database().rlock()->twitchData.channels.at(chan_name).bttvEmotes.lastUpdatedTimestamp;
			auto interval = config::twitch::getEmoteAutoUpdateInterval();

			// zpr::println("chan: %d - %d", now, last);
			if(!force && (interval == 0 || (now - last < interval)))
				return;

			auto [ hdr, body ] = request::get(URL(zpr::sprint("%s/cached/users/twitch/%s", BTTV_API_URL, chan_id)));
			if(auto st = hdr.statusCode(); st != 200 || body.empty())
				return lg::error("bttv", "failed to fetch emotes for channel '%s' (error %d):\n%s", chan_name, st, body);

			auto res = util::parseJson(body);
			if(!res)
				return lg::error("bttv", "json response error: %s", res.error());

			ikura::string_map<CachedEmote> list;

			auto& json = res.unwrap().as_obj();
			for(const auto& e : json["channelEmotes"].as_arr())
			{
				auto x = e.as_obj();
				auto n = x["code"].as_str();
				list.emplace(n, construct_cached_emote(x["id"].as_str(), n));
			}

			for(const auto& e : json["sharedEmotes"].as_arr())
			{
				auto x = e.as_obj();
				auto n = x["code"].as_str();
				list.emplace(n, construct_cached_emote(x["id"].as_str(), n));
			}

			lg::log("bttv", "fetched %zu emotes for #%s", list.size(), chan_name);
			database().wlock()->twitchData.channels[chan_name].bttvEmotes.update(std::move(list));

		}, force, channelId, channelName);
	}
}

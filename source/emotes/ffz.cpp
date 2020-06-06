// ffz.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "async.h"
#include "twitch.h"
#include "config.h"
#include "network.h"

#include "picojson.h"

constexpr const char* FFZ_API_URL = "https://api.frankerfacez.com/v1";

namespace ikura::twitch::ffz
{
	future<void> updateChannelEmotes(const std::string& channelId, const std::string& channelName, bool force)
	{
		return dispatcher().run([](bool force, std::string chan_id, std::string chan_name) {
			auto now = util::getMillisecondTimestamp();
			auto last = database().rlock()->twitchData.channels.at(chan_name).ffzEmotes.lastUpdatedTimestamp;
			auto interval = config::twitch::getEmoteAutoUpdateInterval();

			if(!force && (interval == 0 || (now - last < interval)))
				return;

			auto [ hdr, body ] = request::get(URL(zpr::sprint("%s/room/id/%s", FFZ_API_URL, chan_id)));
			if(auto st = hdr.statusCode(); st != 200 || body.empty())
				return lg::error("ffz", "failed to fetch emotes for channel '%s' (error %d):\n%s", chan_name, st, body);

			auto res = util::parseJson(body);
			if(!res)
				return lg::error("ffz", "json response error: %s", res.error());

			ikura::string_map<CachedEmote> list;

			auto& sets = res.unwrap().as_obj()["sets"].as_obj();
			for(auto& [ s, j ] : sets)
			{
				for(auto& e : j.as_obj()["emoticons"].as_arr())
				{
					auto x = e.as_obj();
					auto n = x["name"].as_str();

					CachedEmote ce;
					ce.source = CachedEmote::Source::FFZ;
					ce.id = std::to_string(x["id"].as_int());
					ce.name = n;

					auto& urls = x["urls"].as_obj();
					if(auto u4 = urls["4"]; !u4.is_null())
						ce.url = u4.as_str();

					else if(auto u2 = urls["2"]; !u2.is_null())
						ce.url = u2.as_str();

					else if(auto u1 = urls["1"]; !u1.is_null())
						ce.url = u1.as_str();

					list.emplace(n, std::move(ce));
				}
			}

			lg::log("ffz", "fetched %zu emotes for #%s", list.size(), chan_name);
			database().wlock()->twitchData.channels[chan_name].ffzEmotes.update(std::move(list));

		}, force, channelId, channelName);
	}
}

// emotes.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "async.h"
#include "timer.h"
#include "config.h"
#include "emotes.h"
#include "serialise.h"

namespace ikura::twitch
{
	void initEmotes()
	{
		auto worker = std::thread([]() {
			while(true)
			{
				auto t = timer();

				std::vector<std::pair<std::string, std::string>> channels;
				database().perform_read([&](auto& db) {
					for(const auto& [ n, ch ] : db.twitchData.channels)
						if(!ch.id.empty())
							channels.emplace_back(ch.id, ch.name);
				});

				std::vector<future<void>> futs;

				// first, update bttv
				futs.push_back(bttv::updateGlobalEmotes(/* force: */ false));

				for(const auto& [ id, name ] : channels)
				{
					futs.push_back(ffz::updateChannelEmotes(id, name, /* force: */ false));
					futs.push_back(bttv::updateChannelEmotes(id, name, /* force: */ false));
				}

				futures::wait(futs);

				lg::log("twitch", "updated bttv+ffz emotes in %.2f ms", t.measure());


				std::this_thread::sleep_for(std::chrono::milliseconds(
					config::twitch::getEmoteAutoUpdateInterval()
				));
			}
		});

		worker.detach();
	}











	void CachedEmote::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
		wr.write(this->url);
		wr.write(this->id);
		wr.write((uint64_t) this->source);
	}

	std::optional<CachedEmote> CachedEmote::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		CachedEmote ret;

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.url))
			return { };

		if(!rd.read(&ret.id))
			return { };

		uint64_t tmp = 0;
		if(!rd.read(&tmp))
			return { };

		ret.source = (Source) tmp;

		return ret;
	}


	void EmoteCacheDB::update(ikura::string_map<CachedEmote>&& new_list)
	{
		this->emotes = std::move(new_list);
		this->lastUpdatedTimestamp = util::getMillisecondTimestamp();
	}


	void EmoteCacheDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->emotes);
		wr.write(this->lastUpdatedTimestamp);
	}

	std::optional<EmoteCacheDB> EmoteCacheDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		EmoteCacheDB ret;

		if(!rd.read(&ret.emotes))
			return { };

		if(!rd.read(&ret.lastUpdatedTimestamp))
			return { };

		return ret;
	}
}

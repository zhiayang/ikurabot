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

				util::sleep_for(std::chrono::milliseconds(
					config::twitch::getEmoteAutoUpdateInterval()
				));
			}
		});

		worker.detach();
	}


	std::vector<ikura::str_view> getExternalEmotePositions(ikura::str_view msg, ikura::str_view channel)
	{
		std::vector<ikura::str_view> ret;

		ikura::str_view x;
		ikura::str_view xs = msg;

		while(xs.size() > 0)
		{
			// this is very crude, but it's because
			// (a) the emote lists are hashtables
			// (b) we're splitting by words, not looking for character sequences
			std::tie(x, xs) = util::bisect(xs, ' ');

			bool found = database().map_read([&x, &channel](auto& db) -> bool {
				if(db.twitchData.globalBttvEmotes.contains(x))
					return true;

				auto chan = db.twitchData.getChannel(channel);
				assert(chan);

				if(chan->bttvEmotes.contains(x) || chan->ffzEmotes.contains(x))
					return true;

				return false;
			});

			if(found)
				ret.push_back(x);
		}

		return ret;
	}


/*


		auto& data = db.messageData.data();

		auto& logs = db.twitchData.messageLog;
		for(auto& msg : logs.messages)
		{
			auto txt = msg.message.get(data);
			// auto emotes = get_emotes(msg.channel, txt);
			// emotes.insert(emotes.end(), msg.emotePositions.begin(), msg.emotePositions.end());

			// std::sort(emotes.begin(), emotes.end(), [](auto& a, auto& b) -> bool {
			// 	return a.start() < b.start();
			// });

			// emotes.erase(std::unique(emotes.begin(), emotes.end(), [](auto& a, auto& b) -> bool {
			// 	return a.start() == b.start() && a.size() == b.size();
			// }), emotes.end());

			auto emotes = msg.emotePositions;
			zpr::println("%s\n%s", txt, zfu::listToString(emotes, [&](auto e) { return e.get(txt); }));

			// msg.emotePositions = emotes;
		}
		*/








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

	bool EmoteCacheDB::contains(ikura::str_view emote) const
	{
		return this->emotes.find(emote) != this->emotes.end();
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

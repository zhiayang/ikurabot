// emotes.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "emotes.h"
#include "serialise.h"

namespace ikura::twitch
{
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

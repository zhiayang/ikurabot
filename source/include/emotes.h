// emotes.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "types.h"

namespace ikura::twitch
{
	struct CachedEmote : Serialisable
	{
		enum class Source { Invalid, BTTV, FFZ, };

		std::string name;
		std::string url;
		std::string id;
		Source source;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<CachedEmote> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_CACHED_EMOTE;
	};

	struct EmoteCacheDB : Serialisable
	{
		ikura::string_map<CachedEmote> emotes;
		uint64_t lastUpdatedTimestamp = 0;

		void update(ikura::string_map<CachedEmote>&& new_list);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<EmoteCacheDB> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_CACHED_EMOTE_DB;
	};

	namespace bttv
	{
		void updateGlobalEmotes(bool force, bool sync);
		void updateChannelEmotes(ikura::str_view channelId, bool force, bool sync);
	}

	namespace ffz
	{
		void updateGlobalEmotes(bool force, bool sync);
	}
}

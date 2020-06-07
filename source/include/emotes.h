// emotes.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "defs.h"

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
		bool contains(ikura::str_view emote) const;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<EmoteCacheDB> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_CACHED_EMOTE_DB;
	};


	void initEmotes();

	// get emote positions for bttv and ffz emotes *only*
	std::vector<ikura::str_view> getExternalEmotePositions(ikura::str_view msg, ikura::str_view channel);

	namespace bttv
	{
		future<void> updateGlobalEmotes(bool force);
		future<void> updateChannelEmotes(const std::string& channelId, const std::string& channelName, bool force);
	}

	namespace ffz
	{
		future<void> updateChannelEmotes(const std::string& channelId, const std::string& channelName, bool force);
	}
}

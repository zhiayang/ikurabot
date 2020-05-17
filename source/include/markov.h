// markov.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include "defs.h"
#include "buffer.h"

namespace ikura::markov
{
	struct MarkovDB : Serialisable
	{
		virtual void serialise(Buffer& buf) const override;
		static std::optional<MarkovDB> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MARKOV_DB;
	};

	void init();
	void shutdown();

	void process(ikura::str_view input, const std::vector<ikura::relative_str>& emote_idxs);
	std::string generate(ikura::str_view seed);
}

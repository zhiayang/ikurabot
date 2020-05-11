// cmd.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include <optional>

#include "defs.h"
#include "serialise.h"

namespace ikura::cmd
{
	namespace properties
	{
		static constexpr uint8_t TIMEOUT_NONE           = 0;
		static constexpr uint8_t TIMEOUT_PER_USER       = 1;
		static constexpr uint8_t TIMEOUT_PER_CHANNEL    = 2;
		static constexpr uint8_t TIMEOUT_GLOBAL         = 3;

		static constexpr uint8_t ALLOWED_ALL            = 0;
		static constexpr uint8_t ALLOWED_TRUSTED        = 1;
		static constexpr uint8_t ALLOWED_MODERATOR      = 2;
		static constexpr uint8_t ALLOWED_BROADCASTER    = 3;
	};

	struct Command : serialise::Serialisable
	{
		std::string name;
		std::string action;

		uint8_t timeoutType = properties::TIMEOUT_NONE;
		uint8_t allowedUsers = properties::ALLOWED_ALL;

		uint32_t timeoutMilliseconds = 0;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Command> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_COMMAND;
	};

	void processMessage(ikura::str_view user, const Channel* channel, ikura::str_view message);
}

// cmd.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include <optional>

#include "defs.h"
#include "synchro.h"
#include "serialise.h"

namespace ikura::cmd
{
	struct InterpState;

	struct CmdContext
	{
		ikura::str_view caller;
		const Channel* channel = nullptr;

		std::vector<ikura::str_view> args;
	};

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
		Command(std::string name, std::string code);

		std::string getCode() const { return this->code; }
		std::string getName() const { return this->name; }

		std::optional<Message> run(InterpState* fs, CmdContext* cs) const;


		virtual void serialise(Buffer& buf) const override;
		static std::optional<Command> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_COMMAND;


	private:
		std::string name;
		std::string code;
	};

	void init();
	void processMessage(ikura::str_view user, const Channel* channel, ikura::str_view message);

	std::optional<Message> interpretCommandCode(InterpState* fs, CmdContext* cs, ikura::str_view code);


	// ugh, too many layers. the idea behind this is that we want the command interpreter
	// to be available under its own lock without needing to lock the database.
	struct InterpState : serialise::Serialisable
	{
		ikura::string_map<Command> commands;
		ikura::string_map<std::string> aliases;

		const Command* findCommand(ikura::str_view name) const;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<InterpState> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_STATE;
	};

	struct DbInterpState : serialise::Serialisable
	{
		virtual void serialise(Buffer& buf) const override;
		static std::optional<DbInterpState> deserialise(Span& buf);
	};


	Synchronised<InterpState, std::shared_mutex>& interp();
}

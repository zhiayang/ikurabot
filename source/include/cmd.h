// cmd.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include <optional>

#include "defs.h"
#include "interp.h"
#include "synchro.h"
#include "serialise.h"

namespace ikura::cmd
{
	struct InterpState;

	struct CmdContext
	{
		ikura::str_view caller;
		const Channel* channel = nullptr;

		ikura::span<ikura::str_view> macro_args;
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
		virtual ~Command() { }

		std::string getName() const { return this->name; }
		virtual std::optional<Message> run(InterpState* fs, CmdContext& cs) const = 0;

		// because this is static, it needs to exist in the abstract base class too
		static std::optional<Command*> deserialise(Span& buf);

	protected:
		Command(std::string name);

		std::string name;
		std::string code;
	};

	struct Macro : Command
	{
		Macro(std::string name, std::string raw_code);
		Macro(std::string name, std::vector<std::string> codewords);

		virtual std::optional<Message> run(InterpState* fs, CmdContext& cs) const override;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Macro*> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MACRO;

	private:
		std::string name;
		std::vector<std::string> code;
	};

	// struct Function : Command
	// {
	// 	Function(std::string name, std::string code);

	// 	std::string getCode() const { return this->code; }
	// 	std::string getName() const { return this->name; }

	// 	std::optional<Message> run(InterpState* fs, CmdContext* cs) const;

	// 	virtual void serialise(Buffer& buf) const override;
	// 	static std::optional<Function> deserialise(Span& buf);

	// 	static constexpr uint8_t TYPE_TAG = serialise::TAG_FUNCTION;


	// private:
	// 	std::string name;
	// 	std::string code;
	// };


	void init();
	void processMessage(ikura::str_view user, const Channel* channel, ikura::str_view message);

	// ugh, too many layers. the idea behind this is that we want the command interpreter
	// to be available under its own lock without needing to lock the database.
	struct InterpState : serialise::Serialisable
	{
		ikura::string_map<Command*> commands;
		ikura::string_map<std::string> aliases;

		const Command* findCommand(ikura::str_view name) const;
		std::optional<interp::Value> resolveVariable(ikura::str_view name, CmdContext& cs) const;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<InterpState> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_STATE;

	private:
		ikura::string_map<interp::Value> globals;
	};

	struct DbInterpState : serialise::Serialisable
	{
		virtual void serialise(Buffer& buf) const override;
		static std::optional<DbInterpState> deserialise(Span& buf);
	};


	Synchronised<InterpState, std::shared_mutex>& interpreter();
}

// cmd.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include "defs.h"
#include "interp.h"

namespace ikura::interp
{
	struct InterpState;

	namespace properties
	{
		constexpr uint8_t TIMEOUT_NONE          = 0;
		constexpr uint8_t TIMEOUT_PER_USER      = 1;
		constexpr uint8_t TIMEOUT_PER_CHANNEL   = 2;
		constexpr uint8_t TIMEOUT_GLOBAL        = 3;
	};

	struct Command : Serialisable
	{
		virtual ~Command() { }

		std::string getName() const { return this->name; }
		uint64_t getPermissions() const { return this->permissions; }
		void setPermissions(uint64_t p) { this->permissions = p; }

		Type::Ptr getSignature() const { return this->signature; }

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const = 0;

		// because this is static, it needs to exist in the abstract base class too
		static std::optional<Command*> deserialise(Span& buf);

	protected:
		Command(std::string name, Type::Ptr signature);

		std::string name;
		Type::Ptr signature;
		uint64_t permissions;       // see defs.h/ikura::permissions
	};

	struct Macro : Command
	{
		Macro(std::string name, ikura::str_view raw_code);

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Macro*> deserialise(Span& buf);

		const std::vector<std::string>& getCode() const;

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MACRO;

	private:
		// only used when deserialising.
		Macro(std::string name, std::vector<std::string> codewords);

		std::vector<std::string> code;
	};

	struct BuiltinFunction : Command
	{
		BuiltinFunction(std::string name, Type::Ptr type, Result<interp::Value> (*action)(InterpState*, CmdContext&));

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;

		virtual void serialise(Buffer& buf) const override;
		static void deserialise(Span& buf);

	private:
		Result<interp::Value> (*action)(InterpState*, CmdContext&);
	};

	struct FunctionOverloadSet : Command
	{
		FunctionOverloadSet(std::string name, std::vector<Command*> fns);

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;

		virtual void serialise(Buffer& buf) const override;
		static void deserialise(Span& buf);

	private:
		std::vector<Command*> functions;
	};

	std::vector<std::string> performExpansion(ikura::str_view str);
	std::vector<interp::Value> evaluateMacro(InterpState* fs, CmdContext& cs, const std::vector<std::string>& code);

	Command* getBuiltinFunction(ikura::str_view name);
}

namespace ikura::cmd
{
	ikura::string_map<uint64_t> getDefaultBuiltinPermissions();

	bool verifyPermissions(uint64_t required, uint64_t given);

	// returns true if a command was run.
	bool processMessage(ikura::str_view userid, ikura::str_view username, const Channel* channel, ikura::str_view message);
}

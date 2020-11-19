// cmd.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

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

	namespace ast
	{
		struct FunctionDefn;
	}

	struct Command : Serialisable
	{
		virtual ~Command() { }

		std::string getName() const { return this->name; }

		PermissionSet& perms() { return this->permissions; }
		const PermissionSet& perms() const { return this->permissions; }

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const = 0;
		virtual Type::Ptr getSignature() const = 0;

		// because this is static, it needs to exist in the abstract base class too
		static std::optional<Command*> deserialise(Span& buf);

	protected:
		Command(std::string name);

		std::string name;
		PermissionSet permissions;
	};

	struct Macro : Command
	{
		Macro(std::string name, ikura::str_view raw_code);

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;

		virtual Type::Ptr getSignature() const override;
		const std::vector<std::string>& getCode() const;
		void setCode(ikura::str_view raw_code);


		virtual void serialise(Buffer& buf) const override;
		static std::optional<Macro*> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_MACRO;

	private:
		// only used when deserialising.
		Macro(std::string name, std::vector<std::string> codewords);

		std::vector<std::string> code;
	};

	struct Function : Command
	{
		~Function();
		Function(ast::FunctionDefn* defn);

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;
		virtual Type::Ptr getSignature() const override;
		const ast::FunctionDefn* getDefinition() const { return this->defn; }

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Function*> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_FUNCTION;

	private:
		ast::FunctionDefn* defn;
	};

	struct BuiltinFunction : Command
	{
		BuiltinFunction(std::string name, Type::Ptr type, std::function<Result<interp::Value>(InterpState*, CmdContext&)> action);

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;
		virtual Type::Ptr getSignature() const override;

		virtual void serialise(Buffer& buf) const override;
		static void deserialise(Span& buf);

	private:
		Type::Ptr signature;
		std::function<Result<interp::Value>(InterpState*, CmdContext&)> action;
	};

	struct FunctionOverloadSet : Command
	{
		~FunctionOverloadSet();
		FunctionOverloadSet(std::string name, Type::Ptr sig, std::vector<BuiltinFunction*> fns);

		virtual Result<interp::Value> run(InterpState* fs, CmdContext& cs) const override;
		virtual Type::Ptr getSignature() const override;

		virtual void serialise(Buffer& buf) const override;
		static void deserialise(Span& buf);

	private:
		Type::Ptr signature;
		std::vector<BuiltinFunction*> functions;
	};

	std::vector<ikura::str_view> performExpansion(ikura::str_view str);
	std::vector<interp::Value> evaluateMacro(InterpState* fs, CmdContext& cs, const std::vector<std::string>& code);

	Command* getBuiltinFunction(ikura::str_view name);
}

namespace ikura::cmd
{
	ikura::string_map<PermissionSet> getDefaultBuiltinPermissions();

	// returns true if a command was run.
	bool processMessage(ikura::str_view userid, ikura::str_view username, const Channel* channel,
		ikura::str_view message, bool enablePings, ikura::str_view triggeringMessageId);

	bool processMessage(ikura::str_view userid, ikura::str_view username, const Channel* channel,
		ikura::str_view message, bool enablePings);
}

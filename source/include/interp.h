// interp.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <string>

#include "buffer.h"
#include "synchro.h"
#include "serialise.h"

namespace ikura::cmd
{
	namespace interp
	{
		struct Value : serialise::Serialisable
		{
			static constexpr uint8_t TYPE_VOID      = 0;
			static constexpr uint8_t TYPE_INTEGER   = 1;
			static constexpr uint8_t TYPE_FLOATING  = 2;
			static constexpr uint8_t TYPE_BOOLEAN   = 3;
			static constexpr uint8_t TYPE_STRING    = 4;
			static constexpr uint8_t TYPE_LVALUE    = 5;

			uint8_t type() const { return this->v_type; }
			std::string type_str() const;

			bool isVoid() const     { return this->v_type == TYPE_VOID; }
			bool isInteger() const  { return this->v_type == TYPE_INTEGER; }
			bool isFloating() const { return this->v_type == TYPE_FLOATING; }
			bool isBoolean() const  { return this->v_type == TYPE_BOOLEAN; }
			bool isString() const   { return this->v_type == TYPE_STRING; }
			bool isLValue() const   { return this->v_type == TYPE_LVALUE; }

			int64_t getInteger() const      { return this->v_integer; }
			double getFloating() const      { return this->v_floating; }
			bool getBool() const            { return this->v_bool; }
			std::string getString() const   { return this->v_string; }
			Value* getLValue() const        { return this->v_lvalue; }

			std::string str() const;

			static Value of_void();
			static Value of_bool(bool b);
			static Value of_double(double d);
			static Value of_string(const std::string& s);
			static Value of_integer(int64_t i);
			static Value of_lvalue(Value* v);

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Value> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_VALUE;

		private:
			uint8_t v_type = TYPE_VOID;
			struct {
				int64_t     v_integer;
				double      v_floating;
				bool        v_bool;
				std::string v_string;
				Value*      v_lvalue;
			};
		};
	}

	struct Command;

	struct CmdContext
	{
		ikura::str_view caller;
		const Channel* channel = nullptr;

		ikura::span<ikura::str_view> macro_args;
	};

	struct InterpState : serialise::Serialisable
	{
		ikura::string_map<Command*> commands;
		ikura::string_map<std::string> aliases;

		const Command* findCommand(ikura::str_view name) const;
		interp::Value* resolveAddressOf(ikura::str_view name, CmdContext& cs);
		std::optional<interp::Value> resolveVariable(ikura::str_view name, CmdContext& cs);

		void addGlobal(ikura::str_view name, interp::Value val);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<InterpState> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_STATE;

	private:
		ikura::string_map<interp::Value> globals;
	};

	Synchronised<InterpState, std::shared_mutex>& interpreter();
}

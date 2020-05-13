// interp.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <string>
#include <optional>

#include "buffer.h"
#include "synchro.h"

namespace ikura::cmd
{
	namespace interp
	{
		struct Value : Serialisable
		{
			static constexpr uint8_t TYPE_VOID      = 0;
			static constexpr uint8_t TYPE_INTEGER   = 1;
			static constexpr uint8_t TYPE_DOUBLE    = 2;
			static constexpr uint8_t TYPE_BOOLEAN   = 3;
			static constexpr uint8_t TYPE_STRING    = 4;
			static constexpr uint8_t TYPE_LVALUE    = 5;
			static constexpr uint8_t TYPE_LIST      = 6;

			uint8_t type() const { return this->v_type; }
			std::string type_str() const;

			bool is_void() const;
			bool is_integer() const;
			bool is_double() const;
			bool is_bool() const;
			bool is_string() const;
			bool is_lvalue() const;
			bool is_list() const;

			int64_t     get_integer() const;
			double      get_double() const;
			bool        get_bool() const;
			std::string get_string() const;
			Value*      get_lvalue() const;

			std::vector<Value>& get_list();
			const std::vector<Value>& get_list() const;

			std::string str() const;


			static Value of_void();
			static Value of_bool(bool b);
			static Value of_double(double d);
			static Value of_string(std::string s);
			static Value of_integer(int64_t i);
			static Value of_lvalue(Value* v);
			static Value of_list(std::vector<Value> l);

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Value> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_VALUE;

		private:
			uint8_t v_type = TYPE_VOID;
			struct {
				int64_t             v_integer;
				double              v_double;
				bool                v_bool;
				std::string         v_string;
				Value*              v_lvalue;
				std::vector<Value>  v_list;
			};
		};
	}

	struct Command;

	struct CmdContext
	{
		ikura::str_view caller;
		const Channel* channel = nullptr;

		ikura::span<interp::Value> macro_args;
	};

	struct InterpState : Serialisable
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

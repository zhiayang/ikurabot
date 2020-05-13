// interp.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <map>
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
			static constexpr uint8_t TYPE_MAP       = 7;

			uint8_t type() const { return this->_type; }
			uint8_t key_type() const { return this->_key_type; }
			uint8_t elm_type() const { return this->_elm_type; }

			std::string type_str() const;

			bool is_void() const;
			bool is_integer() const;
			bool is_double() const;
			bool is_bool() const;
			bool is_string() const;
			bool is_lvalue() const;
			bool is_list() const;
			bool is_map() const;

			int64_t     get_integer() const;
			double      get_double() const;
			bool        get_bool() const;
			std::string get_string() const;
			Value*      get_lvalue() const;

			std::vector<Value>& get_list();
			const std::vector<Value>& get_list() const;

			std::map<Value, Value>& get_map();
			const std::map<Value, Value>& get_map() const;

			std::string str() const;


			static Value of_void();
			static Value of_bool(bool b);
			static Value of_double(double d);
			static Value of_string(std::string s);
			static Value of_integer(int64_t i);
			static Value of_lvalue(Value* v);
			static Value of_list(std::vector<Value> l);
			static Value of_list(uint8_t type, std::vector<Value> l);
			static Value of_map(std::map<Value, Value> l);
			static Value of_map(uint8_t key_type, uint8_t value_type, std::map<Value, Value> m);

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Value> deserialise(Span& buf);

			static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_VALUE;


			bool operator == (const Value& other) const
			{
				if(other._type != this->_type || other._elm_type != this->_elm_type || other._key_type != this->_key_type)
					return false;

				switch(this->_type)
				{
					case TYPE_VOID:     return true;
					case TYPE_INTEGER:  return this->v_integer == other.v_integer;
					case TYPE_DOUBLE:   return this->v_double == other.v_double;
					case TYPE_BOOLEAN:  return this->v_bool == other.v_bool;
					case TYPE_STRING:   return this->v_string == other.v_string;
					case TYPE_LVALUE:   return this->v_lvalue == other.v_lvalue;
					case TYPE_LIST:     return this->v_list == other.v_list;
					case TYPE_MAP:      return this->v_map == other.v_map;
					default:
						return false;
				}
			}

			bool operator < (const Value& rhs) const
			{
				if(this->_type != rhs._type)
					return this->_type < rhs._type;

				else if(this->_elm_type != rhs._elm_type)
					return this->_elm_type < rhs._elm_type;

				else if(this->_key_type != rhs._key_type)
					return this->_key_type < rhs._key_type;


				switch(this->_type)
				{
					case TYPE_INTEGER:  return this->v_integer < rhs.v_integer;
					case TYPE_DOUBLE:   return this->v_double < rhs.v_double;
					case TYPE_BOOLEAN:  return !this->v_bool && rhs.v_bool;
					case TYPE_STRING:   return this->v_string < rhs.v_string;
					case TYPE_LVALUE:   return this->v_lvalue < rhs.v_lvalue;
					case TYPE_LIST:     return this->v_list < rhs.v_list;
					case TYPE_MAP:      return this->v_map < rhs.v_map;

					case TYPE_VOID:
					default:
						return false;
				}
			}

		private:
			friend struct Hasher;

			uint8_t _key_type = TYPE_VOID;  // key type in maps.
			uint8_t _elm_type = TYPE_VOID;  // elm type in list, and also value type in map

			uint8_t _type = TYPE_VOID;
			struct {
				int64_t v_integer   = 0;
				double  v_double    = 0;
				bool    v_bool      = false;
				Value*  v_lvalue    = 0;

				std::string v_string;
				std::vector<Value> v_list;
				std::map<Value, Value> v_map;
			};
		};
	}

	struct Command;

	struct CmdContext
	{
		ikura::str_view caller;
		const Channel* channel = nullptr;

		// the arguments, split by spaces and Value::of_string-ed
		ikura::span<interp::Value> macro_args;
	};

	struct InterpState : Serialisable
	{
		ikura::string_map<Command*> commands;
		ikura::string_map<std::string> aliases;

		const Command* findCommand(ikura::str_view name) const;
		bool removeCommandOrAlias(ikura::str_view name);

		interp::Value* resolveAddressOf(ikura::str_view name, CmdContext& cs);
		std::optional<interp::Value> resolveVariable(ikura::str_view name, CmdContext& cs);

		std::optional<interp::Value> evaluateExpr(ikura::str_view expr, CmdContext& cs);

		void addGlobal(ikura::str_view name, interp::Value val);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<InterpState> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_STATE;

	private:
		ikura::string_map<interp::Value*> globals;
	};

	Synchronised<InterpState, std::shared_mutex>& interpreter();
}

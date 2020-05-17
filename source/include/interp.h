// interp.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <map>
#include <string>
#include <memory>
#include <complex>

#include "buffer.h"
#include "synchro.h"

namespace ikura
{
	using complex = std::complex<double>;

	// static inline complex operator + (const complex& a, int64_t b) { return a + complex(b, 0); }
	// static inline complex operator + (int64_t a, const complex& b) { return b + complex(a, 0); }
	// static inline complex operator - (const complex& a, int64_t b) { return a - complex(b, 0); }
	// static inline complex operator - (int64_t a, const complex& b) { return b + complex(a, 0); }

	// static inline complex operator + (const complex& a, double b) { return a + complex(b, 0); }
	// static inline complex operator + (double a, const complex& b) { return b + complex(a, 0); }
	// static inline complex operator - (const complex& a, double b) { return a - complex(b, 0); }
	// static inline complex operator - (double a, const complex& b) { return b + complex(a, 0); }
}

namespace ikura::interp
{
	struct Command;

	struct Type : Serialisable
	{
		using Ptr = std::shared_ptr<const Type>;

		static constexpr uint8_t T_VOID     = 0;
		static constexpr uint8_t T_INTEGER  = 1;
		static constexpr uint8_t T_DOUBLE   = 2;
		static constexpr uint8_t T_BOOLEAN  = 3;
		static constexpr uint8_t T_LIST     = 4;
		static constexpr uint8_t T_MAP      = 5;
		static constexpr uint8_t T_CHAR     = 6;
		static constexpr uint8_t T_FUNCTION = 7;
		static constexpr uint8_t T_COMPLEX  = 8;

		uint8_t type_id() const { return this->_type; }
		Ptr key_type() const { return this->_key_type; }
		Ptr elm_type() const { return this->_elm_type; }
		Ptr ret_type() const { return this->_elm_type; }    // not a typo -- use elm_type to store ret_type also.
		std::vector<Ptr> arg_types() const { return this->_arg_types; }

		bool is_map() const;
		bool is_void() const;
		bool is_bool() const;
		bool is_list() const;
		bool is_char() const;
		bool is_string() const; // is_list && list->elm_type->is_char
		bool is_double() const;
		bool is_integer() const;
		bool is_function() const;
		bool is_complex() const;

		bool is_same(Ptr other) const;
		int get_cast_dist(Ptr to) const;

		std::string str() const;

		static Ptr get_void();
		static Ptr get_bool();
		static Ptr get_char();
		static Ptr get_string();
		static Ptr get_double();
		static Ptr get_integer();
		static Ptr get_complex();
		static Ptr get_list(Ptr elm_type);
		static Ptr get_map(Ptr key_type, Ptr elm_type);
		static Ptr get_macro_function();
		static Ptr get_function(Ptr return_type, std::vector<Ptr> arg_types);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<std::shared_ptr<const Type>> deserialise(Span& buf);

		Type(uint8_t t) : _type(t) { };
		Type(uint8_t t, Ptr elm) : _type(t), _elm_type(elm) { };
		Type(uint8_t t, Ptr key, Ptr elm) : _type(t), _key_type(key), _elm_type(elm) { };
		Type(uint8_t t, std::vector<Ptr> args, Ptr ret) : _type(t), _elm_type(ret), _arg_types(args) { };

	private:
		uint8_t _type = T_VOID;
		Ptr _key_type;
		Ptr _elm_type;  // elm type for lists, value type for map, return type for functions
		std::vector<Ptr> _arg_types;
	};

	struct Value : Serialisable
	{
		Value(Type::Ptr t) : _type(t) { }

		Type::Ptr type() const { return this->_type; }

		bool is_map() const;
		bool is_void() const;
		bool is_bool() const;
		bool is_list() const;
		bool is_char() const;
		bool is_string() const; // is_list && list->elm_type->is_char
		bool is_double() const;
		bool is_lvalue() const;
		bool is_integer() const;
		bool is_function() const;
		bool is_complex() const;
		bool is_same_type(const Value& other) const { return this->_type->is_same(other._type); }

		bool     get_bool() const;
		uint32_t get_char() const;
		double   get_double() const;
		Value*   get_lvalue() const;
		int64_t  get_integer() const;
		Command* get_function() const;
		ikura::complex get_complex() const;

		std::vector<Value>& get_list();
		const std::vector<Value>& get_list() const;

		std::map<Value, Value>& get_map();
		const std::map<Value, Value>& get_map() const;

		std::string str() const;
		std::string raw_str() const;

		std::optional<Value> cast_to(Type::Ptr type) const;

		static Value default_of(Type::Ptr type);

		static Value of_void();
		static Value of_bool(bool b);
		static Value of_char(uint32_t c);
		static Value of_double(double d);
		static Value of_complex(const ikura::complex& c);
		static Value of_complex(double re, double im);
		static Value of_string(ikura::str_view s);
		static Value of_string(const std::string& s);
		static Value of_integer(int64_t i);
		static Value of_lvalue(Value* v);
		static Value of_list(Type::Ptr, std::vector<Value> l);
		static Value of_function(Command* function);
		static Value of_map(Type::Ptr key_type, Type::Ptr value_type, std::map<Value, Value> m);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Value> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_VALUE;

		bool operator == (const Value& other) const
		{
			if(!this->is_same_type(other) || this->is_lvalue() != other.is_lvalue())
				return false;

			if(this->_type->is_void())          return true;
			else if(this->_type->is_map())      return this->v_map == other.v_map;
			else if(this->_type->is_bool())     return this->v_bool == other.v_bool;
			else if(this->_type->is_list())     return this->v_list == other.v_list;
			else if(this->_type->is_char())     return this->v_char == other.v_char;
			else if(this->_type->is_double())   return this->v_double == other.v_double;
			else if(this->_type->is_integer())  return this->v_integer == other.v_integer;
			else if(this->_type->is_complex())  return this->v_complex == other.v_complex;
			else if(this->is_lvalue())          return this->v_lvalue == other.v_lvalue;
			else                                return false;
		}

		bool operator < (const Value& rhs) const
		{
			if(!this->is_same_type(rhs) || this->is_lvalue() != rhs.is_lvalue())
				return this->type()->type_id() < rhs.type()->type_id();

			if(this->_type->is_void())          return false;
			else if(this->_type->is_map())      return this->v_map < rhs.v_map;
			else if(this->_type->is_bool())     return this->v_bool < rhs.v_bool;
			else if(this->_type->is_list())     return this->v_list < rhs.v_list;
			else if(this->_type->is_char())     return this->v_char < rhs.v_char;
			else if(this->_type->is_double())   return this->v_double < rhs.v_double;
			else if(this->_type->is_integer())  return this->v_integer < rhs.v_integer;
			else if(this->_type->is_complex())  return std::norm(this->v_complex) < std::norm(rhs.v_complex);
			else if(this->is_lvalue())
			{
				if(this->v_lvalue && rhs.v_lvalue)
					return *this->v_lvalue < *rhs.v_lvalue;

				return this->v_lvalue;
			}
			else                                return false;
		}

	private:
		friend struct Hasher;
		Type::Ptr _type;

		bool v_is_lvalue = false;
		struct {
			int64_t  v_integer   = 0;
			double   v_double    = 0;
			bool     v_bool      = false;
			Value*   v_lvalue    = 0;
			uint32_t v_char      = 0;
			Command* v_function  = 0;

			ikura::complex v_complex = 0;
			std::vector<Value> v_list;
			std::map<Value, Value> v_map;
		};
	};

	struct Command;
	struct CmdContext
	{
		ikura::str_view callerid;
		ikura::str_view callername;

		const Channel* channel = nullptr;

		// the arguments, split by spaces and Value::of_string-ed
		std::vector<interp::Value> macro_args;
	};

	struct InterpState : Serialisable
	{
		// we need this to setup some global stuff.
		InterpState();

		ikura::string_map<Command*> commands;
		ikura::string_map<std::string> aliases;

		ikura::string_map<uint64_t> builtinCommandPermissions;

		Command* findCommand(ikura::str_view name) const;
		bool removeCommandOrAlias(ikura::str_view name);

		std::pair<std::optional<interp::Value>, interp::Value*> resolveVariable(ikura::str_view name, CmdContext& cs);

		Result<interp::Value> evaluateExpr(ikura::str_view expr, CmdContext& cs);

		void addGlobal(ikura::str_view name, interp::Value val);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<InterpState> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_STATE;

	private:
		ikura::string_map<interp::Value*> globals;
	};
}

namespace ikura
{
	Synchronised<interp::InterpState>& interpreter();
}

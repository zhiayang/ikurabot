// interp.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <map>
#include <string>
#include <memory>
#include <complex>

#include "defs.h"
#include "perms.h"

namespace ikura
{
	class complex : public std::complex<double>
	{
	public:
		complex(const std::complex<double>& c) : std::complex<double>(c) { }

		using std::complex<double>::complex;

		bool is_integral() const
		{
			return !this->is_complex()
				&& (this->real() == static_cast<double>(static_cast<int64_t>(this->real())));
		}
		bool is_complex() const { return this->imag() != 0; }
		int64_t integer() const { return static_cast<int64_t>(this->real()); }
	};
}

namespace ikura::interp
{
	struct Command;

	struct Type : Serialisable
	{
		using Ptr = std::shared_ptr<const Type>;

		static constexpr uint8_t T_VOID     = 0;
		static constexpr uint8_t T_BOOLEAN  = 3;
		static constexpr uint8_t T_LIST     = 4;
		static constexpr uint8_t T_MAP      = 5;
		static constexpr uint8_t T_CHAR     = 6;
		static constexpr uint8_t T_FUNCTION = 7;
		static constexpr uint8_t T_NUMBER   = 8;
		static constexpr uint8_t T_VAR_LIST = 9;
		static constexpr uint8_t T_GENERIC  = 10;

		uint8_t type_id() const { return this->_type; }
		Ptr key_type() const { return this->_key_type; }
		Ptr elm_type() const { return this->_elm_type; }
		Ptr ret_type() const { return this->_elm_type; }    // not a typo -- use elm_type to store ret_type also.
		std::vector<Ptr> arg_types() const { return this->_arg_types; }
		std::string generic_name() const { return this->_gen_name; }

		bool is_map() const;
		bool is_void() const;
		bool is_bool() const;
		bool is_list() const;
		bool is_char() const;
		bool is_string() const; // is_list && list->elm_type->is_char
		bool is_function() const;
		bool is_number() const;
		bool is_variadic_list() const;
		bool is_generic() const;        // whether the type itself is a generic type
		bool has_generics() const;      // whether the type is generic in some way, eg. array of T.

		bool is_same(Ptr other) const;
		int get_cast_dist(Ptr to) const;

		std::string str() const;

		static Ptr get_void();
		static Ptr get_bool();
		static Ptr get_char();
		static Ptr get_string();
		static Ptr get_number();
		static Ptr get_list(Ptr elm_type);
		static Ptr get_variadic_list(Ptr elm_type);
		static Ptr get_map(Ptr key_type, Ptr elm_type);
		static Ptr get_macro_function();
		static Ptr get_function(Ptr return_type, std::vector<Ptr> arg_types);
		static Ptr get_generic(std::string name, int group);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<std::shared_ptr<const Type>> deserialise(Span& buf);


		Type(uint8_t t) : _type(t) { }
		Type(uint8_t t, Ptr elm) : _type(t), _elm_type(elm) { }
		Type(uint8_t t, Ptr key, Ptr elm) : _type(t), _key_type(key), _elm_type(elm) { }
		Type(uint8_t t, std::vector<Ptr> args, Ptr ret) : _type(t), _elm_type(ret), _arg_types(args) { }
		Type(uint8_t t, std::string g, uint64_t s) : _type(t), _gen_name(std::move(g)), _gen_group(s) { }


	private:
		uint8_t _type = T_VOID;
		Ptr _key_type;
		Ptr _elm_type;  // elm type for lists, value type for map, return type for functions
		std::vector<Ptr> _arg_types;
		std::string _gen_name;
		uint64_t _gen_group = 0;
	};

	struct Value : Serialisable
	{
		Value(Type::Ptr t) : _type(t) { }
		~Value() { }

		Type::Ptr type() const { return this->_type; }

		bool is_map() const;
		bool is_void() const;
		bool is_bool() const;
		bool is_list() const;
		bool is_char() const;
		bool is_string() const; // is_list && list->elm_type->is_char
		bool is_real() const;
		bool is_lvalue() const;
		bool is_function() const;
		bool is_number() const;
		bool is_same_type(const Value& other) const { return this->_type->is_same(other._type); }

		bool     get_bool() const;
		uint32_t get_char() const;
		Value*   get_lvalue() const;
		ikura::complex get_number() const;
		std::shared_ptr<Command> get_function() const;

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
		static Value of_number(const ikura::complex& c);

		static Value of_number(double re, double im);
		static Value of_number(double re);

		static Value of_string(const char* s);
		static Value of_string(ikura::str_view s);
		static Value of_string(const std::string& s);
		static Value of_lvalue(Value* v);
		static Value of_list(Type::Ptr, std::vector<Value> l);
		static Value of_variadic_list(Type::Ptr, std::vector<Value> l);
		static Value of_function(Command* function);
		static Value of_function(std::shared_ptr<Command> function);
		static Value of_map(Type::Ptr key_type, Type::Ptr value_type, std::map<Value, Value> m);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Value> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_VALUE;


		// when converting this value to a message, send one message per list element
		static constexpr uint8_t FLAG_DISMANTLE_LIST = 0x1;

		uint8_t flags() const { return this->_flags; }
		void set_flags(uint8_t f) { this->_flags = f; }

		Value decay() const;

		Value(const Value&) = default;

		Value& operator = (const Value& other)
		{
			if(&other == this)
				return *this;

			auto copy = other;
			return (*this = std::move(copy));
		}

		Value& operator = (Value&& rhs)
		{
			if(&rhs == this)
				return *this;

			if(rhs._type->is_void())            ;
			else if(rhs._type->is_map())        this->v_map = std::move(rhs.v_map);
			else if(rhs._type->is_bool())       this->v_bool = std::move(rhs.v_bool);
			else if(rhs._type->is_list())       this->v_list = std::move(rhs.v_list);
			else if(rhs._type->is_char())       this->v_char = std::move(rhs.v_char);
			else if(rhs._type->is_number())     this->v_number = std::move(rhs.v_number);
			else if(rhs.is_lvalue())            this->v_lvalue = std::move(rhs.v_lvalue);
			else if(rhs.is_function())          this->v_function = rhs.v_function;
			else                                assert(false);

			this->_type = rhs._type;
			this->_flags = rhs._flags;
			this->v_is_lvalue = rhs.v_is_lvalue;
			return *this;
		}

		bool operator == (const Value& other) const
		{
			if(!this->is_same_type(other) || this->is_lvalue() != other.is_lvalue())
				return false;

			if(this->_type->is_void())          return true;
			else if(this->_type->is_map())      return this->v_map == other.v_map;
			else if(this->_type->is_bool())     return this->v_bool == other.v_bool;
			else if(this->_type->is_list())     return this->v_list == other.v_list;
			else if(this->_type->is_char())     return this->v_char == other.v_char;
			else if(this->_type->is_number())   return this->v_number == other.v_number;
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
			else if(this->_type->is_number())   return std::norm(this->v_number) < std::norm(rhs.v_number);
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

		uint8_t _flags = 0;
		bool v_is_lvalue = false;
		struct {
			bool     v_bool      = false;
			Value*   v_lvalue    = 0;
			uint32_t v_char      = 0;

			std::shared_ptr<Command> v_function;

			ikura::complex v_number = 0;
			std::vector<Value> v_list;
			std::map<Value, Value> v_map;
		};

		static Value decay(const Value& v);
		static std::vector<Value> decay(const std::vector<Value>& vs);
		static std::map<Value, Value> decay(const std::map<Value, Value>& vs);
	};

	struct Command;
	struct CmdContext
	{
		ikura::str_view callerid;
		ikura::str_view callername;

		const Channel* channel = nullptr;

		uint64_t executionStart = 0;

		// the arguments, split by spaces and Value::of_string-ed
		std::vector<interp::Value> arguments;
		std::string macro_args;
	};

	struct InterpState : Serialisable
	{
		// we need this to setup some global stuff.
		InterpState();

		ikura::string_map<Command*> commands;
		ikura::string_map<std::string> aliases;

		ikura::string_map<PermissionSet> builtinCommandPermissions;

		std::shared_ptr<Command> findCommand(ikura::str_view name) const;
		bool removeCommandOrAlias(ikura::str_view name);

		std::pair<std::optional<interp::Value>, interp::Value*> resolveVariable(ikura::str_view name, CmdContext& cs);

		Result<interp::Value> evaluateExpr(ikura::str_view expr, CmdContext& cs);

		Result<bool> addGlobal(ikura::str_view name, interp::Value val);
		Result<bool> removeGlobal(ikura::str_view name);

		virtual void serialise(Buffer& buf) const override;
		static std::optional<InterpState> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_STATE;

	private:
		ikura::string_map<interp::Value*> globals;
	};

	int getFunctionOverloadDistance(const std::vector<Type::Ptr>& target, const std::vector<Type::Ptr>& given);
	Result<std::vector<Value>> coerceTypesForFunctionCall(ikura::str_view name, Type::Ptr signature, std::vector<Value> given);
}

namespace ikura
{
	Synchronised<interp::InterpState>& interpreter();
}

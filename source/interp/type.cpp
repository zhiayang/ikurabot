// type.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "interp.h"

namespace ikura::interp
{
	bool Type::is_map() const       { return this->_type == T_MAP; }
	bool Type::is_void() const      { return this->_type == T_VOID; }
	bool Type::is_bool() const      { return this->_type == T_BOOLEAN; }
	bool Type::is_list() const      { return this->_type == T_LIST; }
	bool Type::is_char() const      { return this->_type == T_CHAR; }
	bool Type::is_string() const    { return this->_type == T_LIST && this->_elm_type->is_char(); }
	bool Type::is_double() const    { return this->_type == T_DOUBLE; }
	bool Type::is_integer() const   { return this->_type == T_INTEGER; }

	bool Type::is_same(Type::Ptr other) const
	{
		if(this->is_list() && other->is_list())
			return this->elm_type()->is_same(other->elm_type());

		if(this->is_map() && other->is_map())
			return this->elm_type()->is_same(other->elm_type())
				&& this->key_type()->is_same(other->key_type());

		return (this->_type == other->_type);
	}

	std::string Type::str() const
	{
		if(this->is_void())     return "void";
		if(this->is_char())     return "char";
		if(this->is_bool())     return "bool";
		if(this->is_string())   return "str";
		if(this->is_double())   return "dbl";
		if(this->is_integer())  return "int";
		if(this->is_list())     return zpr::sprint("[%s]", this->elm_type()->str());
		if(this->is_map())      return zpr::sprint("[%s: %s]", this->key_type()->str(), this->elm_type()->str());

		return "??";
	}

	static auto t_void = std::make_shared<const Type>(Type::T_VOID);
	static auto t_bool = std::make_shared<const Type>(Type::T_BOOLEAN);
	static auto t_char = std::make_shared<const Type>(Type::T_CHAR);
	static auto t_double = std::make_shared<const Type>(Type::T_DOUBLE);
	static auto t_integer = std::make_shared<const Type>(Type::T_INTEGER);

	Type::Ptr Type::get_void()    { return t_void; }
	Type::Ptr Type::get_bool()    { return t_bool; }
	Type::Ptr Type::get_char()    { return t_char; }
	Type::Ptr Type::get_double()  { return t_double; }
	Type::Ptr Type::get_integer() { return t_integer; }
	Type::Ptr Type::get_string()  { return Type::get_list(Type::get_char()); }

	Type::Ptr Type::get_list(Type::Ptr elm_type)
	{
		return std::make_shared<const Type>(T_LIST, elm_type);
	}

	Type::Ptr Type::get_map(Type::Ptr key_type, Type::Ptr elm_type)
	{
		return std::make_shared<const Type>(T_MAP, key_type, elm_type);
	}

	void Type::serialise(Buffer& buf) const
	{
		buf.write(&this->_type, 1);
		if(this->is_list()) this->_elm_type->serialise(buf);
		if(this->is_map())  this->_key_type->serialise(buf), this->_elm_type->serialise(buf);
	}

	std::optional<std::shared_ptr<const Type>> Type::deserialise(Span& buf)
	{
		auto t = *buf.as<uint8_t>();
		buf.remove_prefix(1);
		if(!t) return { };


		if(t == T_VOID)     return t_void;
		if(t == T_BOOLEAN)  return t_bool;
		if(t == T_CHAR)     return t_char;
		if(t == T_DOUBLE)   return t_double;
		if(t == T_INTEGER)  return t_integer;

		if(t == T_LIST)
		{
			auto e = deserialise(buf);
			if(!e) return { };

			return Type::get_list(e.value());
		}

		if(t == T_MAP)
		{
			auto k = deserialise(buf);
			if(!k) return { };

			auto v = deserialise(buf);
			if(!v) return { };

			return Type::get_map(k.value(), v.value());
		}

		lg::error("db/interp", "invalid type '%x'", t);
		return { };
	}
}

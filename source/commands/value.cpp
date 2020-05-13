// value.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "cmd.h"
#include "serialise.h"

namespace ikura::cmd::interp
{
	std::string Value::str() const
	{
		switch(this->v_type)
		{
			case TYPE_VOID:     return "()";
			case TYPE_BOOLEAN:  return zpr::sprint("%s", this->v_bool);
			case TYPE_INTEGER:  return zpr::sprint("%d", this->v_integer);
			case TYPE_DOUBLE:   return zpr::sprint("%.3f", this->v_double);
			case TYPE_STRING:   return zpr::sprint("\"%s\"", this->v_string);
			case TYPE_LVALUE:   return zpr::sprint("%s", this->v_lvalue->str());
			case TYPE_LIST:     return zfu::listToString(this->v_list, [](const auto& x) -> auto { return x.str(); });
			default:            return "??";
		}
	}

	Value Value::of_void()
	{
		return Value();
	}

	Value Value::of_bool(bool b)
	{
		Value ret;
		ret.v_type = TYPE_BOOLEAN;
		ret.v_bool = b;

		return ret;
	}

	Value Value::of_double(double d)
	{
		Value ret;
		ret.v_type = TYPE_DOUBLE;
		ret.v_double = d;

		return ret;
	}

	Value Value::of_string(std::string s)
	{
		Value ret;
		ret.v_type = TYPE_STRING;
		ret.v_string = std::move(s);

		return ret;
	}

	Value Value::of_integer(int64_t i)
	{
		Value ret;
		ret.v_type = TYPE_INTEGER;
		ret.v_integer = i;

		return ret;
	}

	Value Value::of_lvalue(Value* v)
	{
		Value ret;
		ret.v_type = TYPE_LVALUE;
		ret.v_lvalue = v;

		return ret;
	}

	Value Value::of_list(std::vector<Value> l)
	{
		Value ret;
		ret.v_type = TYPE_LIST;
		ret.v_list = std::move(l);

		return ret;
	}

	std::string Value::type_str() const
	{
		switch(this->v_type)
		{
			case TYPE_VOID:     return "void";
			case TYPE_BOOLEAN:  return "bool";
			case TYPE_INTEGER:  return "int";
			case TYPE_DOUBLE:   return "dbl";
			case TYPE_STRING:   return "str";
			case TYPE_LVALUE:   return "&" + this->v_lvalue->type_str();
			default:            return "??";
		}
	}


	bool Value::is_lvalue() const   { return this->v_type == TYPE_LVALUE; }
	bool Value::is_list() const     { return this->v_type == TYPE_LIST || (this->is_lvalue() && this->v_lvalue->is_list()); }
	bool Value::is_void() const     { return this->v_type == TYPE_VOID || (this->is_lvalue() && this->v_lvalue->is_void()); }
	bool Value::is_string() const   { return this->v_type == TYPE_STRING || (this->is_lvalue() && this->v_lvalue->is_string()); }
	bool Value::is_integer() const  { return this->v_type == TYPE_INTEGER || (this->is_lvalue() && this->v_lvalue->is_integer()); }
	bool Value::is_bool() const     { return this->v_type == TYPE_BOOLEAN || (this->is_lvalue() && this->v_lvalue->is_bool()); }
	bool Value::is_double() const   { return this->v_type == TYPE_DOUBLE || (this->is_lvalue() && this->v_lvalue->is_double()); }


	int64_t Value::get_integer() const      { return this->is_lvalue() ? this->v_lvalue->get_integer() : this->v_integer; }
	double Value::get_double() const        { return this->is_lvalue() ? this->v_lvalue->get_double() : this->v_double; }
	bool Value::get_bool() const            { return this->is_lvalue() ? this->v_lvalue->get_bool() : this->v_bool; }
	std::string Value::get_string() const   { return this->is_lvalue() ? this->v_lvalue->get_string() : this->v_string; }
	Value* Value::get_lvalue() const        { return this->v_lvalue; }

	const std::vector<Value>& Value::get_list() const   { return this->is_lvalue() ? this->v_lvalue->get_list() : this->v_list; }
	std::vector<Value>& Value::get_list()               { return this->is_lvalue() ? this->v_lvalue->get_list() : this->v_list; }



	void Value::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// manually write the value type, so we don't use 2 bytes to write 1 byte
		buf.write(&this->v_type, sizeof(uint8_t));

		switch(this->v_type)
		{
			case TYPE_BOOLEAN:  wr.write(this->v_bool); break;
			case TYPE_STRING:   wr.write(this->v_string); break;
			case TYPE_DOUBLE:   wr.write(this->v_double); break;
			case TYPE_INTEGER:  wr.write(this->v_integer); break;

			case TYPE_VOID:     break;
			case TYPE_LVALUE:   lg::error("db/interp", "cannot serialise lvalues"); break;
			default:            lg::error("db/interp", "invalid value type"); break;
		}
	}

	std::optional<Value> Value::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		// manually read the type.
		auto type = buf.peek();
		buf.remove_prefix(1);

		switch(type)
		{
			case TYPE_BOOLEAN:  { auto x = rd.read<bool>(); if(!x) return { }; return Value::of_bool(x.value()); }
			case TYPE_DOUBLE:   { auto x = rd.read<double>(); if(!x) return { }; return Value::of_double(x.value()); }
			case TYPE_INTEGER:  { auto x = rd.read<int64_t>(); if(!x) return { }; return Value::of_integer(x.value()); }
			case TYPE_STRING:   { auto x = rd.read<std::string>(); if(!x) return { }; return Value::of_string(x.value()); }
			case TYPE_VOID:     return Value::of_void();

			case TYPE_LVALUE:
				lg::error("db/interp", "cannot deserialise lvalues");
				return { };

			default:
				lg::error("db/interp", "invalid value type");
				return { };
		}
	}
}

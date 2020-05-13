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
		switch(this->_type)
		{
			case TYPE_VOID:     return "()";
			case TYPE_BOOLEAN:  return zpr::sprint("%s", this->v_bool);
			case TYPE_INTEGER:  return zpr::sprint("%d", this->v_integer);
			case TYPE_DOUBLE:   return zpr::sprint("%.3f", this->v_double);
			case TYPE_STRING:   return zpr::sprint("\"%s\"", this->v_string);
			case TYPE_LVALUE:   return zpr::sprint("%s", this->v_lvalue->str());
			case TYPE_LIST:     return zfu::listToString(this->v_list, [](const auto& x) -> auto { return x.str(); });
			case TYPE_MAP: {
				std::string ret = "[ ";
				size_t i = 0;
				for(const auto& [ k, v ] : this->v_map)
				{
					ret += zpr::sprint("%s: %s", k.str(), v.str());
					if(i + 1 != this->v_map.size())
						ret += ", ";

					i++;
				}

				return ret + " ]";
			}
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
		ret._type = TYPE_BOOLEAN;
		ret.v_bool = b;

		return ret;
	}

	Value Value::of_double(double d)
	{
		Value ret;
		ret._type = TYPE_DOUBLE;
		ret.v_double = d;

		return ret;
	}

	Value Value::of_string(std::string s)
	{
		Value ret;
		ret._type = TYPE_STRING;
		ret.v_string = std::move(s);

		return ret;
	}

	Value Value::of_integer(int64_t i)
	{
		Value ret;
		ret._type = TYPE_INTEGER;
		ret.v_integer = i;

		return ret;
	}

	Value Value::of_lvalue(Value* v)
	{
		Value ret;
		ret._type = TYPE_LVALUE;
		ret.v_lvalue = v;

		return ret;
	}

	Value Value::of_list(std::vector<Value> l)
	{
		uint8_t inner = TYPE_VOID;
		if(l.empty())   lg::warn("interp", "of_list on empty vector results in [void]");
		else            inner = l[0].type();

		return Value::of_list(inner, std::move(l));
	}

	Value Value::of_list(uint8_t type, std::vector<Value> l)
	{
		Value ret;
		ret._type = TYPE_LIST;
		ret._elm_type = type;
		ret.v_list = std::move(l);

		return ret;
	}


	Value Value::of_map(std::map<Value, Value> m)
	{
		uint8_t key = TYPE_VOID;
		uint8_t elm = TYPE_VOID;

		if(m.empty())
		{
			lg::warn("interp", "of_map on empty map results in [void: void]");
		}
		else
		{
			key = m.begin()->first.type();
			elm = m.begin()->second.type();
		}

		return Value::of_map(key, elm, std::move(m));
	}

	Value Value::of_map(uint8_t key_type, uint8_t elm_type, std::map<Value, Value> m)
	{
		Value ret;
		ret._type = TYPE_MAP;
		ret._key_type = key_type;
		ret._elm_type = elm_type;
		ret.v_map = std::move(m);

		return ret;
	}





	static const char* type_to_str(uint8_t t)
	{
		switch(t)
		{
			case Value::TYPE_VOID:      return "void";
			case Value::TYPE_BOOLEAN:   return "bool";
			case Value::TYPE_INTEGER:   return "int";
			case Value::TYPE_DOUBLE:    return "dbl";
			case Value::TYPE_STRING:    return "str";
			default:                    return "??";
		}
	}

	std::string Value::type_str() const
	{
		if(this->_type == TYPE_LVALUE)
			return "&" + this->v_lvalue->type_str();

		if(this->_type == TYPE_LIST)
			return zpr::sprint("[%s]", type_to_str(this->_elm_type));

		return type_to_str(this->_type);
	}


	bool Value::is_lvalue() const   { return this->_type == TYPE_LVALUE; }
	bool Value::is_list() const     { return this->_type == TYPE_LIST || (this->is_lvalue() && this->v_lvalue->is_list()); }
	bool Value::is_void() const     { return this->_type == TYPE_VOID || (this->is_lvalue() && this->v_lvalue->is_void()); }
	bool Value::is_string() const   { return this->_type == TYPE_STRING || (this->is_lvalue() && this->v_lvalue->is_string()); }
	bool Value::is_integer() const  { return this->_type == TYPE_INTEGER || (this->is_lvalue() && this->v_lvalue->is_integer()); }
	bool Value::is_bool() const     { return this->_type == TYPE_BOOLEAN || (this->is_lvalue() && this->v_lvalue->is_bool()); }
	bool Value::is_double() const   { return this->_type == TYPE_DOUBLE || (this->is_lvalue() && this->v_lvalue->is_double()); }
	bool Value::is_map() const      { return this->_type == TYPE_MAP || (this->is_lvalue() && this->v_lvalue->is_map()); }


	int64_t Value::get_integer() const      { return this->is_lvalue() ? this->v_lvalue->get_integer() : this->v_integer; }
	double Value::get_double() const        { return this->is_lvalue() ? this->v_lvalue->get_double() : this->v_double; }
	bool Value::get_bool() const            { return this->is_lvalue() ? this->v_lvalue->get_bool() : this->v_bool; }
	std::string Value::get_string() const   { return this->is_lvalue() ? this->v_lvalue->get_string() : this->v_string; }
	Value* Value::get_lvalue() const        { return this->v_lvalue; }

	const std::vector<Value>& Value::get_list() const   { return this->is_lvalue() ? this->v_lvalue->get_list() : this->v_list; }
	std::vector<Value>& Value::get_list()               { return this->is_lvalue() ? this->v_lvalue->get_list() : this->v_list; }

	const std::map<Value, Value>& Value::get_map() const{ return this->is_lvalue() ? this->v_lvalue->get_map() : this->v_map; }
	std::map<Value, Value>& Value::get_map()            { return this->is_lvalue() ? this->v_lvalue->get_map() : this->v_map; }



	void Value::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// manually write the value type, so we don't use 2 bytes to write 1 byte
		buf.write(&this->_type, sizeof(uint8_t));

		switch(this->_type)
		{
			case TYPE_BOOLEAN:  wr.write(this->v_bool); break;
			case TYPE_STRING:   wr.write(this->v_string); break;
			case TYPE_DOUBLE:   wr.write(this->v_double); break;
			case TYPE_INTEGER:  wr.write(this->v_integer); break;

			case TYPE_LIST:
				buf.write(&this->_elm_type, sizeof(uint8_t));
				wr.write(this->v_list);
				break;

			case TYPE_MAP:
				buf.write(&this->_elm_type, sizeof(uint8_t));
				buf.write(&this->_key_type, sizeof(uint8_t));
				wr.write(this->v_map);
				break;

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
			case TYPE_LIST: {
				auto elm_ty = buf.peek(); buf.remove_prefix(1);
				auto x = rd.read<std::vector<Value>>();
				if(!x) return { };

				return Value::of_list(elm_ty, x.value());
			}

			case TYPE_MAP: {
				auto elm_ty = buf.peek(); buf.remove_prefix(1);
				auto key_ty = buf.peek(); buf.remove_prefix(1);

				auto x = rd.read<std::map<Value, Value>>();
				if(!x) return { };

				return Value::of_map(key_ty, elm_ty, x.value());
			}

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

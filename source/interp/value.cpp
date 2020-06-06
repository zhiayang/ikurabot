// value.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "cmd.h"
#include "synchro.h"
#include "serialise.h"

namespace ikura::interp
{
	std::string Value::raw_str() const
	{
		if(this->is_lvalue())               return this->v_lvalue->raw_str();
		else if(this->_type->is_void())     return "";
		else if(this->_type->is_bool())     return zpr::sprint("%s", this->v_bool);
		else if(this->_type->is_char())     return zpr::sprint("%c", (char) this->v_char);
		else if(this->_type->is_double())   return zpr::sprint("%.3f", this->v_double);
		else if(this->_type->is_integer())  return zpr::sprint("%d", this->v_integer);
		else if(this->_type->is_complex())
		{
			auto imag = this->v_complex.imag();
			if(std::abs(imag) < 0.0001)
				imag = 0;

			if(imag != 0)   return zpr::sprint("%.3f%+.3fi", this->v_complex.real(), imag);
			else            return zpr::sprint("%.3f", this->v_complex.real());
		}
		else if(this->_type->is_map())
		{
			std::string ret;
			size_t i = 0;
			for(const auto& [ k, v ] : this->v_map)
			{
				ret += zpr::sprint("%s: %s", k.raw_str(), v.raw_str());
				if(i + 1 != this->v_map.size())
					ret += " ";

				i++;
			}
			return ret;
		}
		else if(this->_type->is_list())
		{
			if(this->_type->elm_type()->is_char())
			{
				std::string ret;
				for(const auto& c : this->v_list)
					ret += (char) c.get_char();

				return ret;
			}
			else
			{
				return zfu::listToString(this->v_list, [](const auto& x) -> auto { return x.str(); },
					/* braces: */ false, /* sep: */ " ");
			}
		}
		else
		{
			return "";
		}
	}

	std::string Value::str() const
	{
		if(this->is_lvalue())               return this->v_lvalue->str();
		else if(this->_type->is_void())     return "()";
		else if(this->_type->is_bool())     return zpr::sprint("%s", this->v_bool);
		else if(this->_type->is_char())     return zpr::sprint("'%c'", (char) this->v_char);
		else if(this->_type->is_double())   return zpr::sprint("%.3f", this->v_double);
		else if(this->_type->is_integer())  return zpr::sprint("%d", this->v_integer);
		else if(this->_type->is_complex())
		{
			auto imag = this->v_complex.imag();
			if(std::abs(imag) < 0.0001)
				imag = 0;

			if(imag != 0)   return zpr::sprint("%.3f%+.3fi", this->v_complex.real(), imag);
			else            return zpr::sprint("%.3f", this->v_complex.real());
		}
		else if(this->_type->is_map())
		{
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
		else if(this->_type->is_list())
		{
			if(this->_type->elm_type()->is_char())
			{
				std::string ret = "\"";
				for(const auto& c : this->v_list)
					ret += (char) c.get_char();

				return ret + "\"";
			}
			else
			{
				return zfu::listToString(this->v_list, [](const auto& x) -> auto { return x.str(); });
			}
		}
		else
		{
			return "??";
		}
	}

	Value Value::default_of(Type::Ptr type)
	{
		return Value(type);
	}

	Value Value::of_void()
	{
		return Value(Type::get_void());
	}

	Value Value::of_bool(bool b)
	{
		auto ret = Value(Type::get_bool());
		ret.v_bool = b;

		return ret;
	}

	Value Value::of_char(uint32_t c)
	{
		auto ret = Value(Type::get_char());
		ret.v_char = c;

		return ret;
	}

	Value Value::of_double(double d)
	{
		auto ret = Value(Type::get_double());
		ret.v_double = d;

		return ret;
	}

	Value Value::of_complex(const ikura::complex& c)
	{
		auto ret = Value(Type::get_complex());
		ret.v_complex = c;

		return ret;
	}

	Value Value::of_complex(double re, double im)
	{
		auto ret = Value(Type::get_complex());
		ret.v_complex = ikura::complex(re, im);

		return ret;
	}

	Value Value::of_string(const char* s)
	{
		return of_string(ikura::str_view(s, strlen(s)));
	}

	Value Value::of_string(const std::string& s)
	{
		return of_string(ikura::str_view(s));
	}

	Value Value::of_string(ikura::str_view s)
	{
		auto ret = Value(Type::get_string());
		for(char c : s)
			ret.v_list.push_back(Value::of_char(c));

		return ret;
	}

	Value Value::of_integer(int64_t i)
	{
		auto ret = Value(Type::get_integer());
		ret.v_integer = i;

		return ret;
	}

	Value Value::of_lvalue(Value* v)
	{
		auto ret = Value(v->type());
		ret.v_is_lvalue = true;
		ret.v_lvalue = v;

		return ret;
	}

	Value Value::of_variadic_list(Type::Ptr type, std::vector<Value> l)
	{
		auto ret = Value(Type::get_variadic_list(type));
		ret.v_list = std::move(l);

		return ret;
	}

	Value Value::of_list(Type::Ptr type, std::vector<Value> l)
	{
		auto ret = Value(Type::get_list(type));
		ret.v_list = std::move(l);

		return ret;
	}

	Value Value::of_map(Type::Ptr key_type, Type::Ptr elm_type, std::map<Value, Value> m)
	{
		auto ret = Value(Type::get_map(key_type, elm_type));
		ret.v_map = std::move(m);

		return ret;
	}

	Value Value::of_function(Command* function)
	{
		auto sig = function->getSignature();
		auto ret = Value(Type::get_function(sig->ret_type(), sig->arg_types()));
		ret.v_function = function;

		return ret;
	}

	std::optional<Value> Value::cast_to(Type::Ptr type) const
	{
		auto castable = (this->type()->get_cast_dist(type) != -1);
		if(!castable) return { };

		if(this->_type->is_same(type))
			return *this;

		if(this->is_integer() && type->is_double())
			return Value::of_double((double) this->get_integer());

		if(this->is_integer() && type->is_complex())
			return Value::of_complex(this->get_integer(), 0);

		if(this->is_double() && type->is_complex())
			return Value::of_complex(this->get_double(), 0);

		if((this->is_list() && type->is_list()) || (this->is_map() && type->is_map()))
			return *this;

		else
			return { };
	}


	bool Value::is_lvalue() const   { return this->v_is_lvalue; }
	bool Value::is_list() const     { return this->_type->is_list(); }
	bool Value::is_void() const     { return this->_type->is_void(); }
	bool Value::is_integer() const  { return this->_type->is_integer(); }
	bool Value::is_bool() const     { return this->_type->is_bool(); }
	bool Value::is_double() const   { return this->_type->is_double(); }
	bool Value::is_string() const   { return this->_type->is_string(); }
	bool Value::is_map() const      { return this->_type->is_map(); }
	bool Value::is_char() const     { return this->_type->is_char(); }
	bool Value::is_function() const { return this->_type->is_function(); }
	bool Value::is_complex() const  { return this->_type->is_complex(); }

	Command* Value::get_function() const    { return this->is_lvalue() ? this->v_lvalue->get_function() : this->v_function; }
	int64_t Value::get_integer() const      { return this->is_lvalue() ? this->v_lvalue->get_integer() : this->v_integer; }
	double Value::get_double() const        { return this->is_lvalue() ? this->v_lvalue->get_double() : this->v_double; }
	bool Value::get_bool() const            { return this->is_lvalue() ? this->v_lvalue->get_bool() : this->v_bool; }
	uint32_t Value::get_char() const        { return this->is_lvalue() ? this->v_lvalue->get_char() : this->v_char; }
	ikura::complex Value::get_complex()const{ return this->is_lvalue() ? this->v_lvalue->get_complex() : this->v_complex; }
	Value* Value::get_lvalue() const        { return this->v_lvalue; }

	const std::vector<Value>& Value::get_list() const   { return this->is_lvalue() ? this->v_lvalue->get_list() : this->v_list; }
	std::vector<Value>& Value::get_list()               { return this->is_lvalue() ? this->v_lvalue->get_list() : this->v_list; }

	const std::map<Value, Value>& Value::get_map() const{ return this->is_lvalue() ? this->v_lvalue->get_map() : this->v_map; }
	std::map<Value, Value>& Value::get_map()            { return this->is_lvalue() ? this->v_lvalue->get_map() : this->v_map; }

	void Value::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);

		wr.tag(TYPE_TAG);
		this->type()->serialise(buf);

		//* we write the integer as uint64_t even though they're signed; this is to take
		//* advantage of the SMALL_U64/TINY_U64 serialisation space-saving optimisations.
		//* when deserialising, we must convert it.

		if(this->_type->is_void())          ;
		else if(this->_type->is_bool())     wr.write(this->v_bool);
		else if(this->_type->is_char())     wr.write(this->v_char);
		else if(this->_type->is_double())   wr.write(this->v_double);
		else if(this->_type->is_complex())  wr.write(this->v_complex.real()), wr.write(this->v_complex.imag());
		else if(this->_type->is_integer())  wr.write((uint64_t) this->v_integer);
		else if(this->_type->is_map())      wr.write(this->v_map);
		else if(this->_type->is_list())     wr.write(this->v_list);
		else if(this->_type->is_function()) wr.write(this->v_function->getName());
		else                                lg::error("db", "invalid value type");
	}

	std::optional<Value> Value::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		auto tmp = Type::deserialise(buf);
		if(!tmp)
			return lg::error_o("db", "failed to deser type");

		auto type = tmp.value();

		if(type->is_void())
		{
			return Value::of_void();
		}
		else if(type->is_bool())
		{
			auto x = rd.read<bool>();
			if(!x) return { };

			return Value::of_bool(x.value());
		}
		else if(type->is_char())
		{
			auto x = rd.read<uint32_t>();
			if(!x) return { };

			return Value::of_char(x.value());
		}
		else if(type->is_double())
		{
			auto x = rd.read<double>();
			if(!x) return { };

			return Value::of_double(x.value());
		}
		else if(type->is_complex())
		{
			auto re = rd.read<double>(); if(!re) return { };
			auto im = rd.read<double>(); if(!im) return { };

			return Value::of_complex(re.value(), im.value());
		}
		else if(type->is_integer())
		{
			//* read as u64, then cast to i64 for the value.
			auto x = rd.read<uint64_t>();
			if(!x) return { };

			return Value::of_integer((int64_t) x.value());
		}
		else if(type->is_list())
		{
			auto x = rd.read<std::vector<Value>>();
			if(!x) return { };

			return (type->is_variadic_list() ? Value::of_variadic_list : Value::of_list)(type->elm_type(), x.value());
		}
		else if(type->is_map())
		{
			auto x = rd.read<std::map<Value, Value>>();
			if(!x) return { };

			return Value::of_map(type->key_type(), type->elm_type(), x.value());
		}
		else if(type->is_function())
		{
			auto name = rd.read<std::string>();
			if(!name) return { };

			return Value::of_function(interpreter().rlock()->findCommand(name.value()));
		}
		else
		{
			return lg::error_o("db", "invalid value type");
		}
	}
}

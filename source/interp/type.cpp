// type.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "defs.h"
#include "interp.h"
#include "buffer.h"
#include "serialise.h"

namespace ikura::interp
{
	bool Type::is_map() const           { return this->_type == T_MAP; }
	bool Type::is_void() const          { return this->_type == T_VOID; }
	bool Type::is_bool() const          { return this->_type == T_BOOLEAN; }
	bool Type::is_char() const          { return this->_type == T_CHAR; }
	bool Type::is_string() const        { return this->_type == T_LIST && this->_elm_type->is_char(); }
	bool Type::is_double() const        { return this->_type == T_DOUBLE; }
	bool Type::is_integer() const       { return this->_type == T_INTEGER; }
	bool Type::is_function() const      { return this->_type == T_FUNCTION; }
	bool Type::is_complex() const       { return this->_type == T_COMPLEX; }
	bool Type::is_variadic_list() const { return this->_type == T_VAR_LIST; }
	bool Type::is_list() const          { return this->_type == T_LIST || this->_type == T_VAR_LIST; }
	bool Type::is_generic() const       { return this->_type == T_GENERIC; }
	bool Type::has_generics() const
	{
		if(this->_type == T_GENERIC)
		{
			return true;
		}
		else if(this->is_list())
		{
			return this->_elm_type->is_generic();
		}
		else if(this->is_map())
		{
			return this->_key_type->is_generic() || this->_elm_type->is_generic();
		}
		else if(this->is_function())
		{
			if(this->_elm_type->is_generic())
				return true;

			for(const auto& p : this->_arg_types)
				if(p->is_generic())
					return true;
		}

		return false;
	}

	int Type::get_cast_dist(Ptr other) const
	{
		if(this->is_same(other))
		{
			return 0;
		}
		else if(this->is_integer() && other->is_double())
		{
			return 1;
		}
		else if((this->is_integer() || this->is_double()) && other->is_complex())
		{
			return 2;
		}
		else if(this->is_list() && other->is_list())
		{
			// for now, we use list[void] and map[void, void] as "generic" any placeholder types.
			// and also for empty lists.
			if(this->elm_type()->is_void() || other->elm_type()->is_void())
				return 2;

			else if(other->elm_type()->is_generic())
				return 10;
		}
		else if(this->is_map() && other->is_map())
		{
			// same thing here, but we must make sure that, for [K: V], we can go to only one of:
			// [K: void], [void: V], or [void: void].

			if(this->key_type()->is_same(other->key_type()) && other->elm_type()->is_void())
				return 2;

			else if(this->elm_type()->is_same(other->elm_type()) && other->key_type()->is_void())
				return 2;

			else if(other->key_type()->is_void() && other->elm_type()->is_void())
				return 3;

			else if(other->key_type()->is_generic() && other->elm_type()->is_generic())
				return 20;

			else if(other->key_type()->is_generic())
				return 10;

			else if(other->elm_type()->is_generic())
				return 10;
		}
		else if(this->is_function() && other->is_function())
		{
			if(this->arg_types().size() == other->arg_types().size())
			{
				int sum = 0;
				for(size_t i = 0; i < this->arg_types().size(); i++)
				{
					int k = this->arg_types()[i]->get_cast_dist(other->arg_types()[i]);
					if(k == -1) return -1;

					sum += k;
				}

				return sum;
			}
		}
		else if(other->is_generic())
		{
			return 10;
		}

		return -1;
	}

	bool Type::is_same(Ptr other) const
	{
		if(this->is_list() && other->is_list())
			return this->elm_type()->is_same(other->elm_type());

		if(this->is_map() && other->is_map())
			return this->elm_type()->is_same(other->elm_type())
				&& this->key_type()->is_same(other->key_type());

		if(this->is_function() && other->is_function())
		{
			if(!this->ret_type()->is_same(other->ret_type()))
				return false;

			if(this->arg_types().size() != other->arg_types().size())
				return false;

			for(size_t i = 0; i < this->arg_types().size(); i++)
				if(!this->arg_types()[i]->is_same(other->arg_types()[i]))
					return false;

			return true;
		}

		if(this->is_generic() && other->is_generic())
		{
			return this->generic_name() == other->generic_name()
				&& this->_gen_group == other->_gen_group;
		}

		return (this->_type == other->_type);
	}

	std::string Type::str() const
	{
		if(this->is_void())         return "void";
		if(this->is_char())         return "char";
		if(this->is_bool())         return "bool";
		if(this->is_string())       return "str";
		if(this->is_double())       return "double";
		if(this->is_integer())      return "int";
		if(this->is_variadic_list())return zpr::sprint("[{}...]", this->elm_type()->str());
		if(this->is_list())         return zpr::sprint("[{}]", this->elm_type()->str());
		if(this->is_map())          return zpr::sprint("[{}: {}]", this->key_type()->str(), this->elm_type()->str());
		if(this->is_complex())      return "complex";
		if(this->is_generic())      return this->_gen_name;
		if(this->is_function())
		{
			return zpr::sprint("({}) -> {}",
				zfu::listToString(this->arg_types(), [](const auto& t) -> auto { return t->str(); }, /* braces: */ false),
				this->ret_type()->str()
			);
		}

		return "??";
	}

	static Type::Ptr t_void;
	static Type::Ptr t_bool;
	static Type::Ptr t_char;
	static Type::Ptr t_double;
	static Type::Ptr t_integer;
	static Type::Ptr t_complex;

	Type::Ptr Type::get_void()    { if(!t_void) t_void = std::make_shared<const Type>(Type::T_VOID); return t_void; }
	Type::Ptr Type::get_bool()    { if(!t_bool) t_bool = std::make_shared<const Type>(Type::T_BOOLEAN); return t_bool; }
	Type::Ptr Type::get_char()    { if(!t_char) t_char = std::make_shared<const Type>(Type::T_CHAR); return t_char; }
	Type::Ptr Type::get_double()  { if(!t_double) t_double = std::make_shared<const Type>(Type::T_DOUBLE); return t_double; }
	Type::Ptr Type::get_integer() { if(!t_integer) t_integer = std::make_shared<const Type>(Type::T_INTEGER); return t_integer; }
	Type::Ptr Type::get_complex() { if(!t_complex) t_complex = std::make_shared<const Type>(Type::T_COMPLEX); return t_complex; }
	Type::Ptr Type::get_string()  { return Type::get_list(Type::get_char()); }

	Type::Ptr Type::get_list(Ptr elm_type)
	{
		return std::make_shared<const Type>(T_LIST, std::move(elm_type));
	}

	Type::Ptr Type::get_variadic_list(Ptr elm_type)
	{
		return std::make_shared<const Type>(T_VAR_LIST, std::move(elm_type));
	}

	Type::Ptr Type::get_map(Ptr key_type, Ptr elm_type)
	{
		return std::make_shared<const Type>(T_MAP, std::move(key_type), std::move(elm_type));
	}

	Type::Ptr Type::get_macro_function()
	{
		std::vector<Ptr> arg_list = { get_list(get_string()) };
		return std::make_shared<const Type>(T_FUNCTION,
			std::move(arg_list),    // argument type for macros is also a list of strings.
			get_list(get_string())  // return type for macros is always a list of strings
		);
	}

	Type::Ptr Type::get_function(Ptr return_type, std::vector<Ptr> arg_types)
	{
		return std::make_shared<const Type>(T_FUNCTION,
			std::move(arg_types),
			std::move(return_type)
		);
	}

	Type::Ptr Type::get_generic(std::string name, int group)
	{
		return std::make_shared<const Type>(T_GENERIC,
			std::move(name), group
		);
	}

	void Type::serialise(Buffer& buf) const
	{
		buf.write(&this->_type, 1);
		if(this->is_list())
		{
			this->_elm_type->serialise(buf);
		}
		else if(this->is_map())
		{
			this->_key_type->serialise(buf), this->_elm_type->serialise(buf);
		}
		else if(this->is_function())
		{
			this->ret_type()->serialise(buf);
			serialise::Writer(buf).write((uint64_t) this->_arg_types.size());

			for(auto t : this->_arg_types)
				t->serialise(buf);
		}
		else if(this->is_generic())
		{
			auto wr = serialise::Writer(buf);
			wr.write(this->_gen_name);
			wr.write(this->_gen_group);
		}
	}

	std::optional<std::shared_ptr<const Type>> Type::deserialise(Span& buf)
	{
		auto t = *buf.as<uint8_t>();
		buf.remove_prefix(1);

		if(t == T_VOID)     return t_void;
		if(t == T_BOOLEAN)  return t_bool;
		if(t == T_CHAR)     return t_char;
		if(t == T_DOUBLE)   return t_double;
		if(t == T_INTEGER)  return t_integer;
		if(t == T_COMPLEX)  return t_complex;

		if(t == T_LIST || t == T_VAR_LIST)
		{
			auto e = Type::deserialise(buf);
			if(!e) return { };

			return (t == T_VAR_LIST ? Type::get_variadic_list : Type::get_list)(e.value());
		}
		else if(t == T_FUNCTION)
		{
			auto ret = Type::deserialise(buf);
			if(!ret) return { };

			auto arg_cnt = serialise::Reader(buf).read<uint64_t>().value();

			std::vector<Type::Ptr> args;
			for(size_t i = 0; i < arg_cnt; i++)
			{
				auto a = Type::deserialise(buf);
				if(!a) return { };
				args.push_back(a.value());
			}

			return Type::get_function(ret.value(), args);
		}
		else if(t == T_MAP)
		{
			auto k = Type::deserialise(buf);
			if(!k) return { };

			auto v = Type::deserialise(buf);
			if(!v) return { };

			return Type::get_map(k.value(), v.value());
		}
		else if(t == T_GENERIC)
		{
			std::string name;
			uint64_t group = 0;

			auto rd = serialise::Reader(buf);
			if(!rd.read(&name))  return { };
			if(!rd.read(&group)) return { };

			return Type::get_generic(std::move(name), group);
		}

		return lg::error_o("db", "invalid type '{02x}'", t);
	}
}

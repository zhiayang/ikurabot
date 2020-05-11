// expr.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "utils.h"

namespace ikura::cmd::ast
{
	using TT = lexer::TokenType;
	using interp::Value;

	auto make_int = Value::of_integer;
	auto make_flt = Value::of_double;
	auto make_str = Value::of_string;
	auto make_bool = Value::of_bool;
	auto make_void = Value::of_void;

	std::optional<Value> LitString::evaluate(InterpState* fs, const CmdContext& cs) const
	{
		return { };
	}

	std::optional<Value> LitInteger::evaluate(InterpState* fs, const CmdContext& cs) const
	{
		return Value::of_integer(this->value);
	}

	std::optional<Value> LitDouble::evaluate(InterpState* fs, const CmdContext& cs) const
	{
		return Value::of_double(this->value);
	}

	std::optional<Value> LitBoolean::evaluate(InterpState* fs, const CmdContext& cs) const
	{
		return Value::of_bool(this->value);
	}

	std::optional<Value> UnaryOp::evaluate(InterpState* fs, const CmdContext& cs) const
	{
		return { };
	}

	std::optional<Value> BinaryOp::evaluate(InterpState* fs, const CmdContext& cs) const
	{
		auto _lhs = this->lhs->evaluate(fs, cs);
		auto _rhs = this->rhs->evaluate(fs, cs);

		if(!_lhs || !_rhs)
			return { };

		auto lhs = _lhs.value();
		auto rhs = _rhs.value();

		auto rep_str = [](int64_t n, const std::string& s) -> std::string {
			std::string ret; ret.reserve(n * s.size());
			while(n-- > 0) ret += s;
			return ret;
		};

		zpr::println("eval %s %s %s", lhs.str(), op_str, rhs.str());

		if(this->op == TT::Plus)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() + rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() + rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() + rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() + rhs.getFloating());
			else if(lhs.isString() && rhs.isString())       return make_str(lhs.getString() + rhs.getString());
		}
		else if(this->op == TT::Minus)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() - rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() - rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() - rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() - rhs.getFloating());
		}
		else if(this->op == TT::Asterisk)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() * rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() * rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() * rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() * rhs.getFloating());
			else if(lhs.isString() && rhs.isInteger())      return make_str(rep_str(rhs.getInteger(), lhs.getString()));
			else if(lhs.isInteger() && rhs.isString())      return make_str(rep_str(lhs.getInteger(), rhs.getString()));
		}
		else if(this->op == TT::Slash)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() / rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() / rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() / rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() / rhs.getFloating());
		}
		else if(this->op == TT::Percent)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(std::modulus()(lhs.getInteger(), rhs.getInteger()));
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(fmodl(lhs.getInteger(), rhs.getFloating()));
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(fmodl(lhs.getFloating(), rhs.getInteger()));
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(fmodl(lhs.getFloating(), rhs.getFloating()));
		}

		lg::error("interp", "unknown operator '%s'", this->op_str);
		return { };
	}
}





namespace ikura::cmd::interp
{
	std::string Value::str() const
	{
		switch(this->v_type)
		{
			case TYPE_VOID:     return "()";
			case TYPE_BOOLEAN:  return zpr::sprint("%s", this->v_bool);
			case TYPE_INTEGER:  return zpr::sprint("%d", this->v_integer);
			case TYPE_FLOATING: return zpr::sprint("%.3f", this->v_floating);
			case TYPE_STRING:   return zpr::sprint("%s", this->v_string);
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
		ret.v_type = TYPE_FLOATING;
		ret.v_floating = d;

		return ret;
	}

	Value Value::of_string(const std::string& s)
	{
		Value ret;
		ret.v_type = TYPE_STRING;
		ret.v_string = s;

		return ret;
	}

	Value Value::of_integer(int64_t i)
	{
		Value ret;
		ret.v_type = TYPE_INTEGER;
		ret.v_integer = i;

		return ret;
	}

}

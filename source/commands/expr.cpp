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

	std::optional<Value> LitString::evaluate(InterpState* fs, CmdContext& cs) const
	{
		return make_str(this->value);
	}

	std::optional<Value> LitInteger::evaluate(InterpState* fs, CmdContext& cs) const
	{
		return make_int(this->value);
	}

	std::optional<Value> LitDouble::evaluate(InterpState* fs, CmdContext& cs) const
	{
		return make_flt(this->value);
	}

	std::optional<Value> LitBoolean::evaluate(InterpState* fs, CmdContext& cs) const
	{
		return make_bool(this->value);
	}

	std::optional<Value> UnaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto _e = this->expr->evaluate(fs, cs);
		if(!_e) return { };

		auto e = _e.value();

		if(this->op == TT::Plus)
		{
			if(e.isInteger() || e.isFloating())
				return e;
		}
		else if(this->op == TT::Minus)
		{
			if(e.isInteger())       return make_int(-e.getInteger());
			else if(e.isFloating()) return make_flt(-e.getFloating());
		}
		else if(this->op == TT::Exclamation)
		{
			if(e.isBoolean())
				return make_bool(!e.getBool());
		}
		else if(this->op == TT::Tilde)
		{
			if(e.isInteger())
				return make_int(~e.getInteger());
		}

		lg::error("interp", "invalid unary '%s' on type '%s'  --  (in expr %s%s)",
			this->op_str, e.type_str(), this->op_str, e.str());
		return { };
	}

	std::optional<Value> BinaryOp::evaluate(InterpState* fs, CmdContext& cs) const
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
		else if(this->op == TT::Caret)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_flt(powl(lhs.getInteger(), rhs.getInteger()));
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(powl(lhs.getInteger(), rhs.getFloating()));
			else if(lhs.isFloating() && rhs.isInteger())    return make_flt(powl(lhs.getFloating(), rhs.getInteger()));
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(powl(lhs.getFloating(), rhs.getFloating()));
		}
		else if(this->op == TT::ShiftLeft && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() << rhs.getInteger());
		}
		else if(this->op == TT::ShiftRight && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() >> rhs.getInteger());
		}
		else if(this->op == TT::Ampersand && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() & rhs.getInteger());
		}
		else if(this->op == TT::Pipe && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() | rhs.getInteger());
		}

		lg::error("interp", "invalid binary '%s' between types '%s' and '%s' -- in expr (%s %s %s)",
			this->op_str, lhs.type_str(), rhs.type_str(), lhs.str(), this->op_str, rhs.str());
		return { };
	}
}

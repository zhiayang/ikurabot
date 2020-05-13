// expr.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "utils.h"

namespace ikura::cmd::ast
{
	using TT = lexer::TokenType;
	using interp::Value;

	template <typename... Args>
	std::nullopt_t error(const std::string& fmt, Args&&... args)
	{
		lg::error("interp", fmt, args...);
		return std::nullopt;
	}

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

		return error("invalid unary '%s' on type '%s'  --  (in expr %s%s)",
			this->op_str, e.type_str(), this->op_str, e.str());
	}

	static std::optional<Value> perform_binop(InterpState* fs, TT op, ikura::str_view op_str, const Value& lhs, const Value& rhs)
	{
		auto rep_str = [](int64_t n, const std::string& s) -> std::string {
			std::string ret; ret.reserve(n * s.size());
			while(n-- > 0) ret += s;
			return ret;
		};

		if(op == TT::Plus || op == TT::PlusEquals)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() + rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() + rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() + rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() + rhs.getFloating());
			else if(lhs.isString() && rhs.isString())       return make_str(lhs.getString() + rhs.getString());
		}
		else if(op == TT::Minus || op == TT::MinusEquals)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() - rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() - rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() - rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() - rhs.getFloating());
		}
		else if(op == TT::Asterisk || op == TT::TimesEquals)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() * rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() * rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() * rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() * rhs.getFloating());
			else if(lhs.isString() && rhs.isInteger())      return make_str(rep_str(rhs.getInteger(), lhs.getString()));
			else if(lhs.isInteger() && rhs.isString())      return make_str(rep_str(lhs.getInteger(), rhs.getString()));
		}
		else if(op == TT::Slash || op == TT::DivideEquals)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(lhs.getInteger() / rhs.getInteger());
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(lhs.getInteger() / rhs.getFloating());
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(lhs.getFloating() / rhs.getInteger());
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(lhs.getFloating() / rhs.getFloating());
		}
		else if(op == TT::Percent || op == TT::RemainderEquals)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int(std::modulus()(lhs.getInteger(), rhs.getInteger()));
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(fmodl(lhs.getInteger(), rhs.getFloating()));
			else if(lhs.isFloating() && rhs.isInteger())    return make_int(fmodl(lhs.getFloating(), rhs.getInteger()));
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(fmodl(lhs.getFloating(), rhs.getFloating()));
		}
		else if(op == TT::Caret || op == TT::ExponentEquals)
		{
			if(lhs.isInteger() && rhs.isInteger())          return make_int((int64_t) powl(lhs.getInteger(), rhs.getInteger()));
			else if(lhs.isInteger() && rhs.isFloating())    return make_flt(powl(lhs.getInteger(), rhs.getFloating()));
			else if(lhs.isFloating() && rhs.isInteger())    return make_flt(powl(lhs.getFloating(), rhs.getInteger()));
			else if(lhs.isFloating() && rhs.isFloating())   return make_flt(powl(lhs.getFloating(), rhs.getFloating()));
		}
		else if((op == TT::ShiftLeft || op == TT::ShiftLeftEquals) && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() << rhs.getInteger());
		}
		else if((op == TT::ShiftRight || op == TT::ShiftRightEquals) && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() >> rhs.getInteger());
		}
		else if((op == TT::Ampersand || op == TT::BitwiseAndEquals) && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() & rhs.getInteger());
		}
		else if((op == TT::Pipe || op == TT::BitwiseOrEquals) && lhs.isInteger() && rhs.isInteger())
		{
			return make_int(lhs.getInteger() | rhs.getInteger());
		}

		return error("invalid binary '%s' between types '%s' and '%s' -- in expr (%s %s %s)",
			op_str, lhs.type_str(), rhs.type_str(), lhs.str(), op_str, rhs.str());
	}

	std::optional<Value> BinaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto _lhs = this->lhs->evaluate(fs, cs);
		auto _rhs = this->rhs->evaluate(fs, cs);

		if(!_lhs || !_rhs)
			return { };

		auto lhs = _lhs.value();
		auto rhs = _rhs.value();

		return perform_binop(fs, this->op, this->op_str, lhs, rhs);
	}


	std::optional<Value> AssignOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto lhs = this->lhs->evaluate(fs, cs);
		if(!lhs) return { };

		auto rhs = this->rhs->evaluate(fs, cs);
		if(!rhs) return { };

		if(!lhs->isLValue())
			return error("cannot assign to rvalue");

		auto lval = lhs->getLValue();
		if(!lval) return { };

		auto ltyp = lval->type();
		auto rtyp = rhs->type();

		// if this is a compound operator, first eval the expression
		if(this->op != TT::Equal)
		{
			rhs = perform_binop(fs, this->op, this->op_str, *lval, rhs.value());
			if(!rhs) return { };

			rtyp = rhs->type();
		}

		// check if they're assignable.
		if(ltyp != rtyp)
		{
			return error("cannot assign value of type '%s' to variable of type '%s'",
				rhs->type_str(), lval->type_str());
		}

		// ok
		*lval = rhs.value();
		return Value::of_void();
	}


	std::optional<Value> VarRef::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto lhs = fs->resolveAddressOf(this->name, cs);
		if(!lhs) return { };

		return Value::of_lvalue(lhs);
	}
}

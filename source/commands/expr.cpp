// expr.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "zfu.h"

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
			if(e.is_integer() || e.is_double())
				return e;
		}
		else if(this->op == TT::Minus)
		{
			if(e.is_integer())      return make_int(-e.get_integer());
			else if(e.is_double())  return make_flt(-e.get_double());
		}
		else if(this->op == TT::Exclamation)
		{
			if(e.is_bool())
				return make_bool(!e.get_bool());
		}
		else if(this->op == TT::Tilde)
		{
			if(e.is_integer())
				return make_int(~e.get_integer());
		}

		return error("invalid unary '%s' on type '%s'  --  (in expr %s%s)",
			this->op_str, e.type_str(), this->op_str, e.str());
	}

	static std::optional<Value> perform_binop(InterpState* fs, TT op, ikura::str_view op_str, Value& lhs, const Value& rhs)
	{
		auto rep_str = [](int64_t n, const std::string& s) -> std::string {
			std::string ret; ret.reserve(n * s.size());
			while(n-- > 0) ret += s;
			return ret;
		};

		if(op == TT::Plus || op == TT::PlusEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() + rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() + rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_int(lhs.get_double() + rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() + rhs.get_double());
			else if(lhs.is_string() && rhs.is_string())     return make_str(lhs.get_string() + rhs.get_string());
			if(lhs.is_list())
			{
				if(rhs.is_list())
				{
					if(lhs.elm_type() != rhs.elm_type())
						return error("cannot concatenate lists of type '%s' and '%s'", lhs.type_str(), rhs.type_str());

					auto rl = rhs.get_list();

					// plus equals will modify, plus will make a new temporary.
					if(op == TT::Plus)
					{
						auto tmp = lhs.get_list(); tmp.insert(tmp.end(), rl.begin(), rl.end());
						return Value::of_list(tmp);
					}
					else
					{
						auto& tmp = lhs.get_list(); tmp.insert(tmp.end(), rl.begin(), rl.end());
						return lhs;
					}
				}
				else
				{
					if(lhs.elm_type() != rhs.type())
						return error("cannot append value of type '%s' to a list of type '%s'", rhs.type_str(), lhs.type_str());

					// plus equals will modify, plus will make a new temporary.
					if(op == TT::Plus)
					{
						auto tmp = lhs.get_list(); tmp.push_back(rhs);
						return Value::of_list(tmp);
					}
					else
					{
						auto& tmp = lhs.get_list(); tmp.push_back(rhs);
						return lhs;
					}
				}
			}
		}
		else if(op == TT::Minus || op == TT::MinusEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() - rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() - rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_int(lhs.get_double() - rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() - rhs.get_double());
		}
		else if(op == TT::Asterisk || op == TT::TimesEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() * rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() * rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_int(lhs.get_double() * rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() * rhs.get_double());
			else if(lhs.is_string() && rhs.is_integer())    return make_str(rep_str(rhs.get_integer(), lhs.get_string()));
			else if(lhs.is_integer() && rhs.is_string())    return make_str(rep_str(lhs.get_integer(), rhs.get_string()));
		}
		else if(op == TT::Slash || op == TT::DivideEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() / rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() / rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_int(lhs.get_double() / rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() / rhs.get_double());
		}
		else if(op == TT::Percent || op == TT::RemainderEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(std::modulus()(lhs.get_integer(), rhs.get_integer()));
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(fmodl(lhs.get_integer(), rhs.get_double()));
			else if(lhs.is_double() && rhs.is_integer())    return make_int(fmodl(lhs.get_double(), rhs.get_integer()));
			else if(lhs.is_double() && rhs.is_double())     return make_flt(fmodl(lhs.get_double(), rhs.get_double()));
		}
		else if(op == TT::Caret || op == TT::ExponentEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int((int64_t) powl(lhs.get_integer(), rhs.get_integer()));
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(powl(lhs.get_integer(), rhs.get_double()));
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(powl(lhs.get_double(), rhs.get_integer()));
			else if(lhs.is_double() && rhs.is_double())     return make_flt(powl(lhs.get_double(), rhs.get_double()));
		}
		else if((op == TT::ShiftLeft || op == TT::ShiftLeftEquals) && lhs.is_integer() && rhs.is_integer())
		{
			return make_int(lhs.get_integer() << rhs.get_integer());
		}
		else if((op == TT::ShiftRight || op == TT::ShiftRightEquals) && lhs.is_integer() && rhs.is_integer())
		{
			return make_int(lhs.get_integer() >> rhs.get_integer());
		}
		else if((op == TT::Ampersand || op == TT::BitwiseAndEquals) && lhs.is_integer() && rhs.is_integer())
		{
			return make_int(lhs.get_integer() & rhs.get_integer());
		}
		else if((op == TT::Pipe || op == TT::BitwiseOrEquals) && lhs.is_integer() && rhs.is_integer())
		{
			return make_int(lhs.get_integer() | rhs.get_integer());
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

		if(!lhs->is_lvalue())
			return error("cannot assign to rvalue");

		auto lval = lhs->get_lvalue();
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
		return lhs;
	}


	std::optional<Value> VarRef::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto [ val, ref ] = fs->resolveVariable(this->name, cs);
		if(ref) return Value::of_lvalue(ref);
		else    return val;
	}
}

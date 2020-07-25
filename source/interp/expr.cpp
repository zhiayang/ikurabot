// expr.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "ast.h"
#include "cmd.h"

namespace ikura::interp::ast
{
	using TT = lexer::TokenType;
	using interp::Value;

	Value make_cmp(double re, double im) { return Value::of_complex(re, im); }
	Value make_cmp(ikura::complex cmp) { return Value::of_complex(std::move(cmp)); }

	auto make_int = Value::of_integer;
	auto make_flt = Value::of_double;
	auto make_bool = Value::of_bool;
	auto make_char = Value::of_char;
	auto make_void = Value::of_void;

	Result<Value> UnaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto _e = this->expr->evaluate(fs, cs);
		if(!_e) return _e;

		auto e = _e.unwrap();

		if(this->op == TT::Plus)
		{
			if(e.is_integer() || e.is_double() || e.is_complex())
				return e;
		}
		else if(this->op == TT::Minus)
		{
			if(e.is_integer())      return make_int(-e.get_integer());
			else if(e.is_double())  return make_flt(-e.get_double());
			else if(e.is_complex()) return make_cmp(-e.get_complex());
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

		return zpr::sprint("invalid unary '%s' on type '%s'  --  (in expr %s%s)",
			this->op_str, e.type()->str(), this->op_str, e.str());
	}

	static Result<Value> perform_binop(InterpState* fs, TT op, ikura::str_view op_str,
		Value& lhs, const Value& rhs, bool* didAppend = nullptr)
	{
		// auto rep_str = [](int64_t n, const std::string& s) -> std::string {
		// 	std::string ret; ret.reserve(n * s.size());
		// 	while(n-- > 0) ret += s;
		// 	return ret;
		// };

		if(op == TT::Plus || op == TT::PlusEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() + rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() + rhs.get_double());
			else if(lhs.is_char() && rhs.is_integer())      return make_char(lhs.get_char() + rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_char())      return make_char(lhs.get_integer() + rhs.get_char());
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(lhs.get_double() + rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() + rhs.get_double());
			else if(lhs.is_integer() && rhs.is_complex())   return make_cmp((double) lhs.get_integer() + rhs.get_complex());
			else if(lhs.is_double() && rhs.is_complex())    return make_cmp(lhs.get_double() + rhs.get_complex());
			else if(lhs.is_complex() && rhs.is_integer())   return make_cmp(lhs.get_complex() + (double) rhs.get_integer());
			else if(lhs.is_complex() && rhs.is_double())    return make_cmp(lhs.get_complex() + rhs.get_double());
			else if(lhs.is_complex() && rhs.is_complex())   return make_cmp(lhs.get_complex() + rhs.get_complex());
			else if(lhs.is_list())
			{
				Value* left = nullptr;
				if(op == TT::PlusEquals)
				{
					if(!lhs.is_lvalue())
						return zpr::sprint("cannot append to rvalue");

					left = lhs.get_lvalue();
					assert(left);
				}
				else
				{
					left = &lhs;
				}

				// lg::log("interp", "%s + %s", left->type()->str(), rhs.type()->str());

				if(rhs.is_list() && (left->type()->elm_type()->is_same(rhs.type()->elm_type())
					|| left->type()->elm_type()->is_void()
					|| rhs.type()->elm_type()->is_void()))
				{
					auto rl = rhs.get_list();

					// plus equals will modify, plus will make a new temporary.
					if(op == TT::Plus)
					{
						auto tmp = left->get_list(); tmp.insert(tmp.end(), rl.begin(), rl.end());
						return Value::of_list(left->type()->elm_type(), tmp);
					}
					else
					{
						if(didAppend) *didAppend = true;

						left->get_list().insert(left->get_list().end(), rl.begin(), rl.end());
						return lhs;
					}
				}
			}
		}
		else if(op == TT::Minus || op == TT::MinusEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() - rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() - rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(lhs.get_double() - rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() - rhs.get_double());
			else if(lhs.is_integer() && rhs.is_complex())   return make_cmp((double) lhs.get_integer() - rhs.get_complex());
			else if(lhs.is_double() && rhs.is_complex())    return make_cmp(lhs.get_double() - rhs.get_complex());
			else if(lhs.is_complex() && rhs.is_integer())   return make_cmp(lhs.get_complex() - (double) rhs.get_integer());
			else if(lhs.is_complex() && rhs.is_double())    return make_cmp(lhs.get_complex() - rhs.get_double());
			else if(lhs.is_complex() && rhs.is_complex())   return make_cmp(lhs.get_complex() - rhs.get_complex());
			else if(lhs.is_char() && rhs.is_integer())      return make_char(lhs.get_char() - rhs.get_integer());
		}
		else if(op == TT::Asterisk || op == TT::TimesEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(lhs.get_integer() * rhs.get_integer());
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(lhs.get_integer() * rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(lhs.get_double() * rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() * rhs.get_double());
			else if(lhs.is_integer() && rhs.is_complex())   return make_cmp((double) lhs.get_integer() * rhs.get_complex());
			else if(lhs.is_double() && rhs.is_complex())    return make_cmp(lhs.get_double() * rhs.get_complex());
			else if(lhs.is_complex() && rhs.is_integer())   return make_cmp(lhs.get_complex() * (double) rhs.get_integer());
			else if(lhs.is_complex() && rhs.is_double())    return make_cmp(lhs.get_complex() * rhs.get_double());
			else if(lhs.is_complex() && rhs.is_complex())   return make_cmp(lhs.get_complex() * rhs.get_complex());
		}
		else if(op == TT::Slash || op == TT::DivideEquals)
		{
			if(lhs.is_integer() && rhs.is_double())         return make_flt(lhs.get_integer() / rhs.get_double());
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(lhs.get_double() / rhs.get_integer());
			else if(lhs.is_double() && rhs.is_double())     return make_flt(lhs.get_double() / rhs.get_double());
			else if(lhs.is_integer() && rhs.is_complex())   return make_cmp((double) lhs.get_integer() / rhs.get_complex());
			else if(lhs.is_double() && rhs.is_complex())    return make_cmp(lhs.get_double() / rhs.get_complex());
			else if(lhs.is_complex() && rhs.is_integer())   return make_cmp(lhs.get_complex() / (double) rhs.get_integer());
			else if(lhs.is_complex() && rhs.is_double())    return make_cmp(lhs.get_complex() / rhs.get_double());
			else if(lhs.is_complex() && rhs.is_complex())   return make_cmp(lhs.get_complex() / rhs.get_complex());
			else if(lhs.is_integer() && rhs.is_integer())
			{
				auto r = rhs.get_integer();
				if(r == 0)  return make_int(INT64_MAX);
				else        return make_int(lhs.get_integer() / r);
			}
		}
		else if(op == TT::Percent || op == TT::RemainderEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int(std::modulus()(lhs.get_integer(), rhs.get_integer()));
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(fmodl(lhs.get_integer(), rhs.get_double()));
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(fmodl(lhs.get_double(), rhs.get_integer()));
			else if(lhs.is_double() && rhs.is_double())     return make_flt(fmodl(lhs.get_double(), rhs.get_double()));
		}
		else if(op == TT::Caret || op == TT::ExponentEquals)
		{
			if(lhs.is_integer() && rhs.is_integer())        return make_int((int64_t) powl(lhs.get_integer(), rhs.get_integer()));
			else if(lhs.is_integer() && rhs.is_double())    return make_flt(powl(lhs.get_integer(), rhs.get_double()));
			else if(lhs.is_double() && rhs.is_integer())    return make_flt(powl(lhs.get_double(), rhs.get_integer()));
			else if(lhs.is_double() && rhs.is_double())     return make_flt(powl(lhs.get_double(), rhs.get_double()));
			else if(lhs.is_integer() && rhs.is_complex())   return make_cmp(std::pow(lhs.get_integer(), rhs.get_complex()));
			else if(lhs.is_double() && rhs.is_complex())    return make_cmp(std::pow(lhs.get_double(), rhs.get_complex()));
			else if(lhs.is_complex() && rhs.is_integer())   return make_cmp(std::pow(lhs.get_complex(), rhs.get_integer()));
			else if(lhs.is_complex() && rhs.is_double())    return make_cmp(std::pow(lhs.get_complex(), rhs.get_double()));
			else if(lhs.is_complex() && rhs.is_complex())   return make_cmp(std::pow(lhs.get_complex(), rhs.get_complex()));
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

		return zpr::sprint("invalid binary '%s' between types '%s' and '%s' -- in expr (%s %s %s)",
			op_str, lhs.type()->str(), rhs.type()->str(), lhs.str(), op_str, rhs.str());
	}

	Result<Value> BinaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto lhs = this->lhs->evaluate(fs, cs);
		auto rhs = this->rhs->evaluate(fs, cs);

		if(!lhs) return lhs;
		if(!rhs) return rhs;

		return perform_binop(fs, this->op, this->op_str, lhs.unwrap(), rhs.unwrap());
	}

	Result<Value> TernaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(this->op != TT::Question)
			return zpr::sprint("unsupported '%s'", this->op_str);

		auto cond = this->op1->evaluate(fs, cs);
		if(!cond) return cond;

		if(!cond->is_bool())
			return zpr::sprint("invalid use of ?: with type '%s' as first operand", cond->type()->str());

		auto tmp = cond->get_bool();
		if(tmp) return this->op2->evaluate(fs, cs);
		else    return this->op3->evaluate(fs, cs);
	}

	Result<Value> AssignOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto lhs = this->lhs->evaluate(fs, cs);
		if(!lhs) return lhs;

		auto rhs = this->rhs->evaluate(fs, cs);
		if(!rhs) return rhs;

		if(!lhs->is_lvalue())
			return zpr::sprint("cannot assign to rvalue");

		auto ltyp = lhs->type();
		auto rtyp = rhs->type();

		if(this->op != TT::Equal)
		{
			bool didAppend = false;
			auto ret = perform_binop(fs, this->op, this->op_str, lhs.unwrap(), rhs.unwrap(), &didAppend);

			if(didAppend) return ret;
			else          rhs = ret;

			if(!rhs)
				return rhs;
		}

		// check if they're assignable.
		if(!ltyp->is_same(rhs->type()))
		{
			return zpr::sprint("cannot assign value of type '%s' to variable of type '%s'",
				rhs->type()->str(), ltyp->str());
		}

		// ok
		*lhs->get_lvalue() = rhs.unwrap();
		return lhs;
	}

	Result<Value> ComparisonOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(this->exprs.size() != this->ops.size() + 1 || this->exprs.size() < 2)
			return zpr::sprint("operand count mismatch");

		/*
			basically, we transform us into a series of chained "&&" binops.
			10 < 20 < 30 > 25 > 15   =>   (10 < 20) && (20 < 30) && (30 > 25) && (25 > 15)
		*/

		auto compare = [](TT op, std::string op_str, const Value& lhs, const Value& rhs) -> Result<bool> {
			if(op == TT::EqualTo || op == TT::NotEqual)
			{
				auto foozle = [](const Value& lhs, const Value& rhs) -> std::optional<bool> {

					if(lhs.is_integer() && rhs.is_integer())    return lhs.get_integer() == rhs.get_integer();
					if(lhs.is_double() && rhs.is_integer())     return lhs.get_double() == (double) rhs.get_integer();
					if(lhs.is_integer() && rhs.is_double())     return (double) lhs.get_integer() == rhs.get_double();
					if(lhs.is_double() && rhs.is_double())      return lhs.get_double() == rhs.get_double();
					if(lhs.is_complex() && rhs.is_integer())    return lhs.get_complex() == (double) rhs.get_integer();
					if(lhs.is_complex() && rhs.is_double())     return lhs.get_complex() == rhs.get_double();
					if(lhs.is_double() && rhs.is_complex())     return lhs.get_double() == rhs.get_complex();
					if(lhs.is_integer() && rhs.is_complex())    return (double) lhs.get_integer() == rhs.get_complex();
					if(lhs.is_complex() && rhs.is_complex())    return lhs.get_complex() == rhs.get_complex();
					if(lhs.is_list() && rhs.is_list())          return lhs.get_list() == rhs.get_list();
					if(lhs.is_char() && rhs.is_char())          return lhs.get_char() == rhs.get_char();
					if(lhs.is_bool() && rhs.is_bool())          return lhs.get_bool() == rhs.get_bool();
					if(lhs.is_map() && rhs.is_map())            return lhs.get_map() == rhs.get_map();
					if(lhs.is_void() && rhs.is_void())          return true;

					return { };
				};

				auto ret = foozle(lhs, rhs);
				if(!ret.has_value())
					goto fail;

				if(op == TT::NotEqual)  return !ret.value();
				else                    return ret.value();
			}
			else
			{
				auto foozle = [](TT op, auto lhs, auto rhs) -> bool {
					switch(op)
					{
						case TT::LAngle:            return lhs < rhs;
						case TT::RAngle:            return lhs > rhs;
						case TT::LessThanEqual:     return lhs <= rhs;
						case TT::GreaterThanEqual:  return lhs >= rhs;

						default: return false;
					}
				};

				if(lhs.is_integer() && rhs.is_integer())    return foozle(op, lhs.get_integer(), rhs.get_integer());
				if(lhs.is_double() && rhs.is_integer())     return foozle(op, lhs.get_double(), rhs.get_integer());
				if(lhs.is_integer() && rhs.is_double())     return foozle(op, lhs.get_integer(), rhs.get_double());
				if(lhs.is_double() && rhs.is_double())      return foozle(op, lhs.get_double(), rhs.get_double());
				if(lhs.is_char() && rhs.is_integer())       return foozle(op, lhs.get_char(), rhs.get_integer());
				if(lhs.is_integer() && rhs.is_char())       return foozle(op, lhs.get_integer(), rhs.get_char());
				if(lhs.is_list() && rhs.is_list())          return foozle(op, lhs.get_list(), rhs.get_list());
				if(lhs.is_char() && rhs.is_char())          return foozle(op, lhs.get_char(), rhs.get_char());
				if(lhs.is_map() && rhs.is_map())            return foozle(op, lhs.get_map(), rhs.get_map());
			}

		fail:
			return zpr::sprint("invalid comparison '%s' between types '%s' and '%s'", op_str, lhs.type()->str(), rhs.type()->str());
		};

		for(size_t i = 0; i < this->exprs.size() - 1; i++)
		{
			auto left = this->exprs[i]->evaluate(fs, cs);
			if(!left) return left;

			auto right = this->exprs[i + 1]->evaluate(fs, cs);
			if(!right) return right;

			auto [ op, op_str ] = this->ops[i];

			auto res = compare(op, op_str, left.unwrap(), right.unwrap());
			if(!res.has_value())
				return res.error();

			else if(!res.unwrap())
				return Value::of_bool(false);
		}

		return Value::of_bool(true);
	}

	Result<Value> SubscriptOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto base = this->list->evaluate(fs, cs);
		if(!base) return base;

		auto idx = this->index->evaluate(fs, cs);
		if(!idx) return idx;

		auto out_of_range = []() -> Result<Value> {
			return zpr::sprint("index out of range");
		};

		if(base->is_list())
		{
			if(!idx->is_integer())
				return zpr::sprint("index on a list must be an integer");

			auto i = idx->get_integer();
			auto* list = &base->get_list();

			if(i < 0)
			{
				if((size_t) -i > list->size())
					return out_of_range();

				i = list->size() + i;
			}

			if((size_t) i >= list->size())
				return out_of_range();

			if(base->is_lvalue())   return Value::of_lvalue(&(*list)[i]);
			else                    return (*list)[i];
		}
		else if(base->is_map())
		{
			auto* map = &base->get_map();
			if(!base->type()->key_type()->is_same(idx->type()))
				return zpr::sprint("cannot index '%s' with key '%s'", base->type()->str(), idx->type()->str());

			if(auto it = map->find(idx.unwrap()); it != map->end())
			{
				if(base->is_lvalue())   return Value::of_lvalue(&it->second);
				else                    return it->second;
			}
			else
			{
				std::tie(it, std::ignore) = map->insert({ idx.unwrap(), Value::default_of(base->type()->elm_type()) });

				if(base->is_lvalue())   return Value::of_lvalue(&it->second);
				else                    return it->second;
			}
		}
		else
		{
			return zpr::sprint("type '%s' cannot be indexed", base->type()->str());
		}
	}

	Result<Value> SliceOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto base = this->list->evaluate(fs, cs);
		if(!base) return base;

		if(base->is_list())
		{
			auto& list = base->get_list();
			size_t size = list.size();

			auto empty_list = [&]() -> auto {
				return Value::of_list(base->type()->elm_type(), { });
			};

			if(size == 0)
				return empty_list();

			size_t first = 0;
			size_t last = size;

			if(this->start)
			{
				auto beg = this->start->evaluate(fs, cs);
				if(!beg) return beg;

				if(!beg->is_integer())
					return zpr::sprint("slice indices must be integers");

				auto tmp = beg->get_integer();
				if(tmp < 0)
				{
					// if the start index is out of range, just treat it as the beginning of the list
					// (ie. we ignore it. otherwise, use it.
					if((size_t) -tmp <= size)
						first = size + tmp;
				}
				else
				{
					if((size_t) tmp >= size)
						return empty_list();

					first = tmp;
				}
			}

			if(this->end)
			{
				auto end = this->end->evaluate(fs, cs);
				if(!end) return end;

				if(!end->is_integer())
					return zpr::sprint("slice indices must be integers");

				auto tmp = end->get_integer();
				if(tmp < 0)
				{
					// if the end index is too far negative, return an empty list.
					if((size_t) -tmp > size)
						return empty_list();

					last = size + tmp;
				}
				else
				{
					if((size_t) tmp < size)
						last = tmp;
				}
			}

			if(first >= last)
				return empty_list();

			if(base->is_lvalue())
			{
				std::vector<Value> refs;
				for(size_t i = first; i < last; i++)
					refs.push_back(Value::of_lvalue(&list[i]));

				return Value::of_list(base->type()->elm_type(), refs);
			}
			else
			{
				// just copy the list.
				auto foozle = ikura::span(list).subspan(first, last - first);
				return Value::of_list(base->type()->elm_type(), foozle.vec());
			}
		}
		else
		{
			return zpr::sprint("type '%s' cannot be sliced", base->type()->str());
		}
	}



	Result<Value> VarRef::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto [ val, ref ] = fs->resolveVariable(this->name, cs);
		if(ref) return Value::of_lvalue(ref);
		else    return Result<Value>::of(val, zpr::sprint("'%s' not found", this->name));
	}


	Result<Value> SplatOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto out = this->expr->evaluate(fs, cs);
		if(!out) return out;

		if(!out->is_list())
			return zpr::sprint("invalid splat on type '%s'", out->type()->str());

		return Value::of_variadic_list(out->type()->elm_type(), out->get_list());
	}

	Result<Value> DotOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto left = this->lhs->evaluate(fs, cs);
		if(!left) return left;

		// big hax.
		if(left->type()->is_list())
		{
			// big big hax.
			if(auto fn = dynamic_cast<FunctionCall*>(this->rhs))
			{
				if(auto cc = dynamic_cast<VarRef*>(fn->callee))
				{
					if(cc->name == "append")
					{
						if(!left->is_lvalue())
							return zpr::sprint("cannot append to non-rvalue");

						if(fn->arguments.empty())
							return zpr::sprint("expected at least one argument to append()");

						std::vector<Value> args;

						auto elmty = left->type()->elm_type();
						for(size_t i = 0; i < fn->arguments.size(); i++)
						{
							auto arg = fn->arguments[i]->evaluate(fs, cs);
							if(auto casted = arg->cast_to(elmty); casted.has_value())
							{
								args.push_back(std::move(casted.value()));
							}
							else
							{
								return zpr::sprint("element type mismatch for append() (arg %d); expected '%s', found '%s'",
									i, elmty->str(), arg->type()->str());
							}
						}

						auto lval = left->get_lvalue();
						assert(lval);

						lval->get_list().insert(lval->get_list().end(), args.begin(), args.end());
						return Value::of_lvalue(lval);
					}
					else if(cc->name == "len")
					{
						if(!fn->arguments.empty())
							return zpr::sprint("expected no arguments to len()");

						return Value::of_integer(left->get_list().size());
					}
					else
					{
						return zpr::sprint("list has no method '%s'", cc->name);
					}
				}
				else
				{
					goto fail;
				}
			}
			else
			{
			fail:
				return zpr::sprint("invalid rhs for dotop on list");
			}
		}
		else
		{
			return zpr::sprint("invalid dotop on lhs type '%s'", left->type()->str());
		}
	}




















	Result<Value> LitInteger::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(imag) return make_cmp(0, this->value);
		else     return make_int(this->value);
	}

	Result<Value> LitDouble::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(imag) return make_cmp(0, this->value);
		else     return make_flt(this->value);
	}

	Result<Value> LitList::evaluate(InterpState* fs, CmdContext& cs) const
	{
		// make sure all the elements have the same type.
		if(this->elms.empty())
			return Value::of_list(Type::get_void(), { });

		std::vector<Value> vals;
		for(auto e : this->elms)
		{
			auto x = e->evaluate(fs, cs);
			if(!x) return x;

			vals.push_back(x.unwrap());
		}

		auto ty = vals[0].type();
		for(size_t i = 1; i < vals.size(); i++)
			if(!vals[i].type()->is_same(ty))
				return zpr::sprint("conflicting types in list -- '%s' and '%s'", ty->str(), vals[i].type()->str());

		return Value::of_list(ty, std::move(vals));
	}

	Result<Value> LitChar::evaluate(InterpState* fs, CmdContext& cs) const       { return Value::of_char(this->codepoint); }
	Result<Value> LitString::evaluate(InterpState* fs, CmdContext& cs) const     { return Value::of_string(this->value); }
	Result<Value> LitBoolean::evaluate(InterpState* fs, CmdContext& cs) const    { return make_bool(this->value); }


	std::string LitChar::str() const        { return zpr::sprint("'%c'", codepoint); }
	std::string LitString::str() const      { return zpr::sprint("\"%s\"", value); }
	std::string LitInteger::str() const     { return zpr::sprint("%d%s", value, imag ? "i" : ""); }
	std::string LitDouble::str() const      { return zpr::sprint("%.3f%s", value, imag ? "i" : ""); }
	std::string LitBoolean::str() const     { return zpr::sprint("%s", value ? "true" : "false"); }
	std::string LitList::str() const        { return zfu::listToString(elms, [](auto e) { return e->str(); }); }

	std::string VarRef::str() const         { return name; }
	std::string SubscriptOp::str() const    { return zpr::sprint("%s[%s]", list->str(), index->str()); }
	std::string SliceOp::str() const        { return zpr::sprint("%s[%s:%s]", list->str(),
														start ? start->str() : "",
														end ? end->str() : "");
											}
	std::string UnaryOp::str() const        { return zpr::sprint("%s%s", op_str, expr->str()); }
	std::string SplatOp::str() const        { return zpr::sprint("%s...", expr->str()); }
	std::string BinaryOp::str() const       { return zpr::sprint("%s %s %s", lhs->str(), op_str, rhs->str()); }
	std::string AssignOp::str() const       { return zpr::sprint("%s %s %s", lhs->str(), op_str, rhs->str()); }
	std::string DotOp::str() const          { return zpr::sprint("%s.%s", lhs->str(), rhs->str()); }

	// TODO: don't cheat here
	std::string TernaryOp::str() const
	{
		if(this->op_str == "?")
			return zpr::sprint("%s ? %s : %s", op1->str(), op2->str(), op3->str());

		return "";
	}

	std::string ComparisonOp::str() const
	{
		std::string ret;
		for(size_t i = 0; i < this->ops.size(); i++)
			ret += zpr::sprint("%s %s ", this->exprs[i]->str(), this->ops[i].second);

		ret += this->exprs.back()->str();
		return ret;
	}






	// destructors
	LitChar::~LitChar() { }
	LitString::~LitString() { }
	LitInteger::~LitInteger() { }
	LitDouble::~LitDouble() { }
	LitBoolean::~LitBoolean() { }
	VarRef::~VarRef() { }

	LitList::~LitList()             { for(auto e : elms) delete e; }
	SubscriptOp::~SubscriptOp()     { delete list; delete index; }
	SliceOp::~SliceOp()             { delete list; if(start) delete start; if(end) delete end; }
	UnaryOp::~UnaryOp()             { delete expr; }
	SplatOp::~SplatOp()             { delete expr; }
	BinaryOp::~BinaryOp()           { delete lhs; delete rhs; }
	TernaryOp::~TernaryOp()         { delete op1; delete op2; delete op3; }
	ComparisonOp::~ComparisonOp()   { for(auto e : exprs) delete e; }
	AssignOp::~AssignOp()           { delete lhs; delete rhs; }
	DotOp::~DotOp()                 { delete lhs; delete rhs; }

}

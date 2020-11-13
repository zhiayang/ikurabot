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

	Value make_num(double re)               { return Value::of_number(re); }
	Value make_num(double re, double im)    { return Value::of_number(re, im); }
	Value make_num(ikura::complex cmp)      { return Value::of_number(std::move(cmp)); }

	auto make_bool = Value::of_bool;
	auto make_char = Value::of_char;
	auto make_void = Value::of_void;

	Result<Value> UnaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto _e = this->expr->evaluate(fs, cs);
		if(!_e) return _e;

		auto e = _e.unwrap().decay();

		if(this->op == TT::Plus)
		{
			if(e.is_number())
				return e;
		}
		else if(this->op == TT::Minus)
		{
			if(e.is_number())
			{
				// a real monkaS moment. if either the real or complex parts are zero,
				// we should maintain their sign instead. negative 0 gives wrong results
				// for certain operations.
				auto real = e.get_number().real();
				auto imag = e.get_number().imag();

				real *= -1;
				imag *= -1;

				if(real == .0 || real == -.0)
					real = 0;

				if(imag == .0 || imag == -.0)
					imag = 0;

				return make_num(real, imag);
			}
		}
		else if(this->op == TT::Exclamation)
		{
			if(e.is_bool())
				return make_bool(!e.get_bool());
		}

		return zpr::sprint("invalid unary '{}' on type '{}'  --  (in expr {}{})",
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
			if(lhs.is_number() && rhs.is_number())
			{
				return make_num(lhs.get_number() + rhs.get_number());
			}
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

				// lg::log("interp", "{} + {}", left->type()->str(), rhs.type()->str());

				if(rhs.is_list() && (left->type()->elm_type()->is_same(rhs.type()->elm_type())
					|| left->type()->elm_type()->is_void()
					|| rhs.type()->elm_type()->is_void()))
				{
					auto rl = rhs.decay().get_list();

					// plus equals will modify, plus will make a new temporary.
					if(op == TT::Plus)
					{
						auto tmp = left->decay().get_list();
						tmp.insert(tmp.end(), rl.begin(), rl.end());
						return Value::of_list(left->type()->elm_type(), std::move(tmp));
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
			if(lhs.is_number() && rhs.is_number())
				return make_num(lhs.get_number() - rhs.get_number());

			else if(lhs.is_char() && rhs.is_number())
				return make_char(lhs.get_char() - rhs.get_number().integer());
		}
		else if(op == TT::Asterisk || op == TT::TimesEquals)
		{
			if(lhs.is_number() && rhs.is_number())
				return make_num(lhs.get_number() * rhs.get_number());
		}
		else if(op == TT::Slash || op == TT::DivideEquals)
		{
			if(lhs.is_number() && rhs.is_number())
				return make_num(lhs.get_number() / rhs.get_number());
		}
		else if(op == TT::Percent || op == TT::RemainderEquals)
		{
			if(lhs.is_number() && rhs.is_number())
				return make_num(fmodl(lhs.get_number().real(), rhs.get_number().real()), 0);
		}
		else if(op == TT::Caret || op == TT::ExponentEquals)
		{
			if(lhs.is_number() && rhs.is_number())
				return make_num(std::pow(lhs.get_number(), rhs.get_number()));
		}

		return zpr::sprint("invalid binary '{}' between types '{}' and '{}' -- in expr ({} {} {})",
			op_str, lhs.type()->str(), rhs.type()->str(), lhs.str(), op_str, rhs.str());
	}

	Result<Value> BinaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(this->op == TT::LogicalAnd || this->op == TT::LogicalOr)
		{
			// do short-circuiting stuff.
			auto lhs = this->lhs->evaluate(fs, cs);
			if(!lhs) return lhs;

			if(!lhs->is_bool())
				return zpr::sprint("non-boolean type '{}' on lhs of '{}'", lhs->type()->str(), this->op_str);

			if(lhs->get_bool() && this->op == TT::LogicalOr)
				return Value::of_bool(true);

			else if(!lhs->get_bool() && this->op == TT::LogicalAnd)
				return Value::of_bool(false);

			else
			{
				auto rhs = this->rhs->evaluate(fs, cs);
				if(!rhs) return rhs;

				if(!rhs->is_bool())
					return zpr::sprint("non-boolean type '{}' on rhs of '{}'", rhs->type()->str(), this->op_str);

				return Value::of_bool(rhs->get_bool());
			}
		}
		else
		{
			auto lhs = this->lhs->evaluate(fs, cs);
			auto rhs = this->rhs->evaluate(fs, cs);

			if(!lhs) return lhs;
			if(!rhs) return rhs;

			return perform_binop(fs, this->op, this->op_str, lhs.unwrap(), rhs.unwrap());
		}
	}

	Result<Value> TernaryOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(this->op != TT::Question)
			return zpr::sprint("unsupported '{}'", this->op_str);

		auto cond = this->op1->evaluate(fs, cs);
		if(!cond) return cond;

		if(!cond->is_bool())
			return zpr::sprint("invalid use of ?: with type '{}' as first operand", cond->type()->str());

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
		if(auto right = rhs->cast_to(ltyp); !right)
		{
			return zpr::sprint("cannot assign value of type '{}' to variable of type '{}'",
				rhs->type()->str(), ltyp->str());
		}
		else
		{
			// ok
			*lhs->get_lvalue() = right.value().decay();
			return lhs;
		}
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

					if(lhs.is_number() && rhs.is_number())      return lhs.get_number() == rhs.get_number();
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

				if(lhs.is_number() && rhs.is_number())      return foozle(op, std::abs(lhs.get_number()), std::abs(rhs.get_number()));
				if(lhs.is_char() && rhs.is_number())        return foozle(op, lhs.get_char(), rhs.get_number().real());
				if(lhs.is_number() && rhs.is_char())        return foozle(op, lhs.get_number().real(), rhs.get_char());
				if(lhs.is_list() && rhs.is_list())          return foozle(op, lhs.get_list(), rhs.get_list());
				if(lhs.is_char() && rhs.is_char())          return foozle(op, lhs.get_char(), rhs.get_char());
				if(lhs.is_map() && rhs.is_map())            return foozle(op, lhs.get_map(), rhs.get_map());
			}

		fail:
			return zpr::sprint("invalid comparison '{}' between types '{}' and '{}'", op_str, lhs.type()->str(), rhs.type()->str());
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
			if(!idx->is_number() || !idx->get_number().is_integral())
				return zpr::sprint("index on a list must be an integer");

			auto i = idx->get_number().integer();
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
				return zpr::sprint("cannot index '{}' with key '{}'", base->type()->str(), idx->type()->str());

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
			return zpr::sprint("type '{}' cannot be indexed", base->type()->str());
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

				if(!beg->is_number() || !beg->get_number().is_integral())
					return zpr::sprint("slice indices must be integers");

				auto tmp = beg->get_number().integer();
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

				if(!end->is_number() || !end->get_number().is_integral())
					return zpr::sprint("slice indices must be integers");

				auto tmp = end->get_number().integer();
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

				return Value::of_list(base->type()->elm_type(), std::move(refs));
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
			return zpr::sprint("type '{}' cannot be sliced", base->type()->str());
		}
	}



	Result<Value> VarRef::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto [ val, ref ] = fs->resolveVariable(this->name, cs);
		if(ref) return Value::of_lvalue(ref);
		else    return Result<Value>::of(val, zpr::sprint("'{}' not found", this->name));
	}


	Result<Value> SplatOp::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto out = this->expr->evaluate(fs, cs);
		if(!out) return out;

		if(!out->is_list())
			return zpr::sprint("invalid splat on type '{}'", out->type()->str());

		return Value::of_variadic_list(out->type()->elm_type(), out->get_list());
	}

	Result<Value> LambdaExpr::evaluate(InterpState* fs, CmdContext& cs) const
	{
		return Value::of_function(std::make_shared<BuiltinFunction>("__lambda", this->signature,
			[this](InterpState* fs, CmdContext& cs) -> Result<Value> {
				return this->body->evaluate(fs, cs);
			}));
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
								return zpr::sprint("element type mismatch for append() (arg {}); expected '{}', found '{}'",
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

						return Value::of_number(left->get_list().size());
					}
					else
					{
						return zpr::sprint("list has no method '{}'", cc->name);
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
			return zpr::sprint("invalid dotop on lhs type '{}'", left->type()->str());
		}
	}


















	Result<Value> LitInteger::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(imag) return make_num(0.0, this->value);
		else     return make_num(this->value, +0.0);
	}

	Result<Value> LitDouble::evaluate(InterpState* fs, CmdContext& cs) const
	{
		if(imag) return make_num(0.0, this->value);
		else     return make_num(this->value, +0.0);
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
				return zpr::sprint("conflicting types in list -- '{}' and '{}'", ty->str(), vals[i].type()->str());

		return Value::of_list(ty, std::move(vals));
	}

	Result<Value> LitChar::evaluate(InterpState* fs, CmdContext& cs) const       { return Value::of_char(this->codepoint); }
	Result<Value> LitString::evaluate(InterpState* fs, CmdContext& cs) const     { return Value::of_string(this->value); }
	Result<Value> LitBoolean::evaluate(InterpState* fs, CmdContext& cs) const    { return make_bool(this->value); }


	std::string LitChar::str() const        { return zpr::sprint("'{}'", codepoint); }
	std::string LitString::str() const      { return zpr::sprint("\"{}\"", value); }
	std::string LitInteger::str() const     { return zpr::sprint("{}{}", value, imag ? "i" : ""); }
	std::string LitDouble::str() const      { return zpr::sprint("{.3f}{}", value, imag ? "i" : ""); }
	std::string LitBoolean::str() const     { return zpr::sprint("{}", value ? "true" : "false"); }
	std::string LitList::str() const        { return zfu::listToString(elms, [](auto e) { return e->str(); }); }

	std::string VarRef::str() const         { return name; }
	std::string SubscriptOp::str() const    { return zpr::sprint("{}[{}]", list->str(), index->str()); }
	std::string SliceOp::str() const        { return zpr::sprint("{}[{}:{}]", list->str(),
														start ? start->str() : "",
														end ? end->str() : "");
											}
	std::string UnaryOp::str() const        { return zpr::sprint("{}{}", op_str, expr->str()); }
	std::string SplatOp::str() const        { return zpr::sprint("{}...", expr->str()); }
	std::string BinaryOp::str() const       { return zpr::sprint("{} {} {}", lhs->str(), op_str, rhs->str()); }
	std::string AssignOp::str() const       { return zpr::sprint("{} {} {}", lhs->str(), op_str, rhs->str()); }
	std::string DotOp::str() const          { return zpr::sprint("{}.{}", lhs->str(), rhs->str()); }

	std::string LambdaExpr::str() const     { return zpr::sprint("\\%s %s", this->signature->str(), this->body->str()); }

	// TODO: don't cheat here
	std::string TernaryOp::str() const
	{
		if(this->op_str == "?")
			return zpr::sprint("{} ? {} : {}", op1->str(), op2->str(), op3->str());

		return "";
	}

	std::string ComparisonOp::str() const
	{
		std::string ret;
		for(size_t i = 0; i < this->ops.size(); i++)
			ret += zpr::sprint("{} {} ", this->exprs[i]->str(), this->ops[i].second);

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
	LambdaExpr::~LambdaExpr()       { delete body; }
}

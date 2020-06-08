// function.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "cmd.h"

namespace ikura::interp::ast
{












	Result<Value> FunctionDefn::evaluate(InterpState* fs, CmdContext& cs) const
	{
		return this->body->evaluate(fs, cs);
	}

	Result<Value> Block::evaluate(InterpState* fs, CmdContext& cs) const
	{
		// TODO: PUSH SCOPE

		for(size_t i = 0; i < this->stmts.size(); i++)
		{
			auto res = this->stmts[i]->evaluate(fs, cs);
			if(i + 1 == this->stmts.size() && dynamic_cast<Expr*>(this->stmts[i]))
				return res;
		}

		// TODO: POP SCOPE

		return Value::of_void();
	}



	// in milliseconds. 750ms should be super generous
	constexpr uint64_t EXECUTION_TIME_LIMIT = 750;

	Result<Value> FunctionCall::evaluate(InterpState* fs, CmdContext& cs) const
	{
		auto target = this->callee->evaluate(fs, cs);
		if(!target) return target;

		if(!target->type()->is_function())
			return zpr::sprint("type '%s' is not callable", target->type()->str());

		Command* function = target->get_function();
		if(!function) return zpr::sprint("error retrieving function");

		if(util::getMillisecondTimestamp() > cs.executionStart + EXECUTION_TIME_LIMIT)
			return zpr::sprint("time limit exceeded");

		// special handling for macros.
		if(auto macro = dynamic_cast<Macro*>(function))
		{
			// macros take in a list of strings, and return a list of strings.
			// so we just iterate over all our arguments, make convert them all to strings,
			// make a list, then pass it in.
			std::vector<Value> args;
			for(Expr* e : this->arguments)
			{
				auto res = e->evaluate(fs, cs);
				if(!res) return res;

				// check for splat
				if(auto splat = dynamic_cast<SplatOp*>(e))
				{
					assert(res->is_list());
					auto xs = res->get_list();
					for(const auto& x : xs)
						args.push_back(Value::of_string(x.raw_str()));
				}
				else
				{
					args.push_back(Value::of_string(res->raw_str()));
				}
			}

			// clone the thing, and replace the args. this way we keep the rest of the stuff
			// like caller, channel, etc.
			CmdContext params = cs;
			params.macro_args = std::move(args);

			auto ret = macro->run(fs, params);

			// zpr::println("return %s", ret->raw_str());
			return ret;
		}
		else
		{
			std::vector<Value> args;
			for(Expr* e : this->arguments)
			{
				auto res = e->evaluate(fs, cs);
				if(!res) return res;

				if(auto splat = dynamic_cast<SplatOp*>(e))
				{
					assert(res->is_list());
					auto xs = res->get_list();
					for(const auto& x : xs)
						args.push_back(x);
				}
				else
				{
					args.push_back(res.unwrap());
				}
			}

			CmdContext params = cs;
			params.macro_args = std::move(args);

			return function->run(fs, params);
		}
	}














	Block::~Block()                 { for(auto s : stmts) delete s; }
	FunctionDefn::~FunctionDefn()   { delete body; }
	FunctionCall::~FunctionCall()   { delete callee; for(auto e : arguments) delete e; }
}

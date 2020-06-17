// function.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "cmd.h"
#include "serialise.h"

namespace ikura::interp
{
	namespace ast
	{
		Result<Value> FunctionDefn::evaluate(InterpState* fs, CmdContext& cs) const
		{
			// zpr::println("args:");
			// for(auto& arg : cs.arguments)
			// 	zpr::println("%s   :: %s", arg.str(), arg.type()->str());

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



		// in milliseconds.
		constexpr uint64_t EXECUTION_TIME_LIMIT = 500;

		Result<Value> FunctionCall::evaluate(InterpState* fs, CmdContext& cs) const
		{
			auto target = this->callee->evaluate(fs, cs);
			if(!target) return target;

			if(!target->type()->is_function())
				return zpr::sprint("type '%s' is not callable", target->type()->str());

			auto function = target->get_function();
			if(!function) return zpr::sprint("error retrieving function");

			if(util::getMillisecondTimestamp() > cs.executionStart + EXECUTION_TIME_LIMIT)
				return zpr::sprint("time limit exceeded");

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

			// macros take in a list of strings, and return a list of strings.
			// so we just iterate over all our arguments, and convert them all to strings.
			if(dynamic_cast<Macro*>(function.get()))
			{
				args = zfu::map(std::move(args), [](Value&& arg) -> Value {
					return Value::of_string(arg.raw_str());
				});
			}
			else
			{
				auto res = coerceTypesForFunctionCall("fn", function->getSignature(), args);
				if(!res) return res.error();

				args = std::move(res.unwrap());
			}

			CmdContext params = cs;
			params.arguments = std::move(args);

			return function->run(fs, params);
		}









		std::string Block::str() const
		{
			if(this->stmts.size() == 1)
				if(auto e = dynamic_cast<Expr*>(this->stmts[0]); e != nullptr)
					return zpr::sprint("=> %s", e->str());

			return zpr::sprint("{ %s }", zfu::listToString(this->stmts, [](auto s) { return s->str(); },
				/* braces: */ false, /* sep: */ ";"));
		}

		std::string FunctionDefn::str() const
		{
			return zpr::sprint("fn %s %s%s%s %s %s",
				this->name,
				this->generics.empty() ? "" : "<",
				zfu::listToString(this->generics, [](auto s) { return s; }, false),
				this->generics.empty() ? "" : ">",
				this->signature->str(),
				this->body->str()
			);
		}

		std::string FunctionCall::str() const
		{
			return zpr::sprint("%s(%s)", this->callee->str(),
				zfu::listToString(this->arguments, [](auto e) { return e->str(); }, false)
			);
		}




		Block::~Block()                 { for(auto s : stmts) delete s; }
		FunctionDefn::~FunctionDefn()   { delete body; }
		FunctionCall::~FunctionCall()   { delete callee; for(auto e : arguments) delete e; }

	}







	int getFunctionOverloadDistance(const std::vector<Type::Ptr>& target, const std::vector<Type::Ptr>& given)
	{
		int cost = 0;
		auto target_size = target.size();
		if(!target.empty() && target.back()->is_variadic_list())
			target_size--;

		auto cnt = std::min(target_size, given.size());

		// if there are no variadics involved, you failed if the counts are different.
		if(target.empty() || !target.back()->is_variadic_list())
			if(target.size() != given.size())
				return -1;

		// make sure the normal args are correct first
		for(size_t i = 0; i < cnt; i++)
		{
			int k = given[i]->get_cast_dist(target[i]);
			if(k == -1) return -1;
			else        cost += k;
		}

		if(target_size != target.size())
		{
			// we got variadics.
			auto vla = target.back();
			auto elm = vla->elm_type();

			// the cost of doing business.
			cost += 10;
			for(size_t i = cnt; i < given.size(); i++)
			{
				int k = given[i]->get_cast_dist(elm);
				if(k == -1) return -1;
				else        cost += k;
			}
		}

		return cost;
	}











	// this one doesn't really need to care about generics.
	Result<std::vector<Value>> coerceTypesForFunctionCall(ikura::str_view name, Type::Ptr signature, std::vector<Value> given)
	{
		std::vector<Value> final_args;
		auto target = signature->arg_types();

		auto target_size = target.size();
		if(!target.empty() && target.back()->is_variadic_list())
			target_size--;

		auto cnt = std::min(target_size, given.size());

		// if there are no variadics involved, you failed if the counts are different.
		if((target.empty() || !target.back()->is_variadic_list()) && target.size() != given.size())
		{
			return zpr::sprint("call to '%s' with wrong number of arguments (expected %zu, found %zu)",
				name, target.size(), given.size());
		}

		auto type_mismatch = [name](size_t i, const Type::Ptr& exp, const Type::Ptr& got) -> std::string {
			return zpr::sprint("'%s': arg %zu: type mismatch, expected '%s', found '%s'",
				name, i + 1, exp->str(), got->str());
		};

		// make sure the normal args are correct first
		for(size_t i = 0; i < cnt; i++)
		{
			auto tmp = given[i].cast_to(target[i]);
			if(!tmp) return type_mismatch(i, target[i], given[i].type());

			final_args.push_back(tmp.value());
		}

		if(target_size != target.size())
		{
			// we got variadics.
			assert(target.back()->is_variadic_list());
			auto elm = target.back()->elm_type();

			std::vector<Value> vla;

			// even when forwarding, we still need to cast because this is a
			// half-static-half-dynamic frankenstein language.
			if(cnt + 1 == given.size() && given.back().type()->is_same(target.back()))
			{
				auto& list = given.back().get_list();
				for(size_t i = 0; i < list.size(); i++)
				{
					auto tmp = list[i].cast_to(elm);
					if(!tmp) return type_mismatch(i, elm, list[i].type());

					vla.push_back(tmp.value());
				}
			}
			else
			{
				// the cost of doing business.
				for(size_t i = cnt; i < given.size(); i++)
				{
					auto tmp = given[i].cast_to(elm);
					if(!tmp) return type_mismatch(i, elm, given[i].type());

					vla.push_back(tmp.value());
				}
			}

			final_args.push_back(Value::of_variadic_list(elm, std::move(vla)));
		}

		return final_args;
	}


















	Function::~Function() { delete this->defn; }
	Function::Function(ast::FunctionDefn* defn) : Command(defn->name)
	{
		this->defn = defn;
	}

	Type::Ptr Function::getSignature() const
	{
		return this->defn->signature;
	}

	Result<interp::Value> Function::run(InterpState* fs, CmdContext& cs) const
	{
		assert(defn);
		return this->defn->evaluate(fs, cs);
	}

	void Function::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// these come from the superclass
		wr.write(this->name);
		wr.write(this->permissions);

		wr.write(this->defn);
	}

	std::optional<Function*> Function::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		std::string name;
		PermissionSet permissions;

		if(!rd.read(&name))
			return { };

		if(!rd.read(&permissions))
			return { };

		ast::FunctionDefn* defn = rd.read<ast::FunctionDefn*>().value();
		if(!defn) return nullptr;

		auto ret = new Function(defn);
		ret->permissions = permissions;
		return ret;
	}
}

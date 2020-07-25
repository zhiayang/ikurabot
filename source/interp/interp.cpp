// runner.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "ast.h"
#include "cmd.h"
#include "zfu.h"
#include "synchro.h"
#include "serialise.h"

#include "tsl/robin_set.h"

namespace ikura::interp
{
	using interp::Value;

	static size_t parse_number_arg(ikura::str_view name, CmdContext& cs)
	{
		if('0' <= name[0] && name[0] <= '9')
		{
			size_t idx = name[0] - '0';
			name.remove_prefix(1);

			while('0' <= name[0] && name[0] <= '9')
			{
				idx = (10 * idx) + (name[0] - '0');
				name.remove_prefix(1);
			}

			if(name.size() != 0)
				return -1;

			if(idx >= cs.arguments.size())
			{
				lg::error("interp", "argument index out of bounds (want %zu, have %zu)", idx, cs.arguments.size());
				return -1;
			}

			return idx;
		}

		return -1;
	}

	static bool is_builtin_var(ikura::str_view name)
	{
		return zfu::match(name, "user", "self", "args", "channel", "raw_args");
	}

	static std::optional<Value> get_builtin_var(ikura::str_view name, CmdContext& cs)
	{
		if(name == "user")       return Value::of_string(cs.callername.str());
		if(name == "self")       return Value::of_string(cs.channel->getUsername());
		if(name == "channel")    return Value::of_string(cs.channel->getName());
		if(name == "args")       return Value::of_list(Type::get_string(), cs.arguments);
		if(name == "macro_args") return Value::of_string(cs.macro_args);

		return { };
	}

	std::pair<std::optional<Value>, Value*> InterpState::resolveVariable(ikura::str_view name, CmdContext& cs)
	{
		if(name.empty())
			return { std::nullopt, nullptr };

		if(name[0] == '$')
		{
			name.remove_prefix(1);

			if(name.empty())
				return { std::nullopt, nullptr };

			if('0' <= name[0] && name[0] <= '9')
			{
				if(size_t idx = parse_number_arg(name, cs); idx != (size_t) -1)
					return { cs.arguments[idx], nullptr };
			}
			else
			{
				// check for builtins
				if(auto b = get_builtin_var(name, cs); b.has_value())
					return { b.value(), nullptr };
			}
		}
		else
		{
			if(auto it = this->globals.find(name); it != this->globals.end())
				return { *it.value(), it.value() };

			// try builtin functions
			if(auto builtin = interp::getBuiltinFunction(name); builtin != nullptr)
				return { Value::of_function(builtin), nullptr };

			// try functions. commands are always rvalues, for obvious reasons.
			if(auto cmd = this->findCommand(name); cmd != nullptr)
				return { Value::of_function(cmd), nullptr };
		}

		// for now, nothing
		return { std::nullopt, nullptr };
	}

	Result<bool> InterpState::addGlobal(ikura::str_view name, Value val)
	{
		if(is_builtin_var(name) || name.find_first_of("0123456789") == 0)
			return zpr::sprint("'%s' is already a builtin global", name);

		if(auto it = this->globals.find(name); it != this->globals.end())
			return zpr::sprint("global '%s' already defined", name);

		if(val.type()->has_generics())
			return zpr::sprint("cannot create values of generic type ('%s')", val.type()->str());

		this->globals[name] = new Value(std::move(val));
		lg::log("interp", "added global '%s'", name);
		return true;
	}

	Result<bool> InterpState::removeGlobal(ikura::str_view name)
	{
		if(is_builtin_var(name) || name.find_first_of("0123456789") == 0)
			return zpr::sprint("cannot remove builtin globals");

		if(auto it = this->globals.find(name); it != this->globals.end())
		{
			this->globals.erase(it);
			return true;
		}
		else
		{
			return zpr::sprint("'%s' does not exist", name);
		}
	}

	Result<Value> InterpState::evaluateExpr(ikura::str_view expr, CmdContext& cs)
	{
		auto exp = ast::parseExpr(expr);
		if(!exp) return exp.error();

		auto ret = exp.unwrap()->evaluate(this, cs);
		delete exp.unwrap();

		return ret;
	}


	std::shared_ptr<Command> InterpState::findCommand(ikura::str_view name) const
	{
		ikura::string_set seen;

		// you can chain aliases, so we need to loop.
		Command* command = nullptr;
		while(!command)
		{
			if(auto it = this->commands.find(name); it != this->commands.end())
			{
				command = it->second;
				break;
			}

			if(auto it = this->aliases.find(name); it != this->aliases.end())
			{
				auto next = it->second;
				if(seen.find(next) != seen.end())
				{
					lg::error("cmd", "circular aliases: %s -> %s", name, next);
					return nullptr;
				}

				name = next;
				seen.insert(next);

				continue;
			}

			break;
		}

		// return with a no-op deleter! the only reason we return shared_ptr here
		// is so that the interpreter has an easier time dealing with ephemeral functions
		// (eg. lamdas, curried functions). but, the commands we return here are definitely
		// not ephemeral, so they should *NOT* be deleted once the reference count reaches 0
		return std::shared_ptr<Command>(command, [](Command*) { });
	}

	// undef will currently undef the entire overload set, which is probably not what we want.
	bool InterpState::removeCommandOrAlias(ikura::str_view name)
	{
		if(auto it = this->commands.find(name); it != this->commands.end())
		{
			auto cmd = it->second;
			this->commands.erase(it);

			delete cmd;
			return true;
		}
		else if(auto it = this->aliases.find(name); it != this->aliases.end())
		{
			this->aliases.erase(it);
			return true;
		}

		return false;
	}

	bool is_builtin_global(ikura::str_view name)
	{
		return name == "e" || name == "i" || name == "pi" || name == "tau" || name == "inf";
	}

	static auto const_i   = Value::of_complex(0, 1);
	static auto const_e   = Value::of_double(2.71828182845904523536028747135266);
	static auto const_pi  = Value::of_double(3.14159265358979323846264338327950);
	static auto const_tau = Value::of_double(6.28318530717958647692528676655900);
	static auto const_inf = Value::of_double(std::numeric_limits<double>::infinity());

	InterpState::InterpState()
	{
		// setup some globals.
		this->globals["i"]   = &const_i;
		this->globals["e"]   = &const_e;
		this->globals["pi"]  = &const_pi;
		this->globals["tau"] = &const_tau;
		this->globals["inf"] = &const_inf;
	}

	void InterpState::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// note: because values can contain references to commands (because this is an amazing scripting language)
		// we must serialise/deserialise all commands first; values containing commands simply store the name of
		// the command to disk, and on deserialisation they will read the Command* from the interp state. so we
		// must make sure the commands are available in the global lookup table by the time we do values.
		wr.write(this->commands);
		wr.write(this->aliases);
		wr.write(this->builtinCommandPermissions);

		ikura::string_map<interp::Value> globs;
		for(const auto& [ k, v ] : this->globals)
		{
			// skip the builtin globals.
			if(!is_builtin_global(k))
				globs.insert({ k, *v });
		}

		wr.write(globs);
	}

	std::optional<InterpState> InterpState::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		InterpState interp;
		if(!rd.read(&interp.commands))
			return { };

		if(!rd.read(&interp.aliases))
			return { };

		ikura::string_map<PermissionSet> builtinPerms;

		if(!rd.read(&builtinPerms))
			return { };

		// big hax: since the deserialisation of globals can potentially require the commands array, we just
		// override it, even though this isn't supposed to touch the current interp state... bleh.
		interpreter().wlock()->commands = interp.commands;

		ikura::string_map<interp::Value> globals;
		if(!rd.read(&globals))
			return { };

		for(const auto& [ k, v ] : globals)
			interp.globals[k] = new Value(v);

		if(builtinPerms.empty())
			builtinPerms = cmd::getDefaultBuiltinPermissions();

		interp.builtinCommandPermissions = builtinPerms;
		return interp;
	}

}

namespace ikura::db
{
	void DbInterpState::serialise(Buffer& buf) const
	{
		interpreter().rlock()->serialise(buf);
	}

	std::optional<DbInterpState> DbInterpState::deserialise(Span& buf)
	{
		auto it = interp::InterpState::deserialise(buf);
		if(!it) return { };

		DbInterpState ret;
		*interpreter().wlock().get() = std::move(it.value());

		return ret;
	}
}




namespace ikura
{
	static Synchronised<interp::InterpState> TheInterpreter;
	Synchronised<interp::InterpState>& interpreter()
	{
		return TheInterpreter;
	}
}



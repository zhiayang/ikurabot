// runner.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "ast.h"
#include "cmd.h"
#include "zfu.h"
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

			if(idx >= cs.macro_args.size())
			{
				lg::error("interp", "argument index out of bounds (want %zu, have %zu)", idx, cs.macro_args.size());
				return -1;
			}

			return idx;
		}

		return -1;
	}

	static bool is_builtin_var(ikura::str_view name)
	{
		return zfu::match(name, "user", "self", "args", "channel");
	}

	static std::optional<Value> get_builtin_var(ikura::str_view name, CmdContext& cs)
	{
		if(name == "user")      return Value::of_string(cs.caller.str());
		if(name == "self")      return Value::of_string(cs.channel->getUsername());
		if(name == "channel")   return Value::of_string(cs.channel->getName());
		if(name == "args")      return Value::of_list(Type::get_string(), cs.macro_args.vec());

		return { };
	}

	std::pair<std::optional<Value>, Value*> InterpState::resolveVariable(ikura::str_view name, CmdContext& cs)
	{
		auto INVALID = [name](ikura::str_view s = "") -> std::pair<std::optional<Value>, Value*> {
			lg::error("interp", "variable '%s' not found%s", name, s);
			return { std::nullopt, nullptr };
		};

		if(name.empty())
			return INVALID();

		if(name[0] == '$')
		{
			name.remove_prefix(1);

			if(name.empty())
				return INVALID();

			if('0' <= name[0] && name[0] <= '9')
			{
				if(size_t idx = parse_number_arg(name, cs); idx != (size_t) -1)
					return { cs.macro_args[idx], nullptr };
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
		return INVALID();
	}

	void InterpState::addGlobal(ikura::str_view name, Value val)
	{
		if(is_builtin_var(name) || name.find_first_of("0123456789") == 0)
		{
			lg::error("interp", "'%s' is a builtin global", name);
			return;
		}

		if(auto it = this->globals.find(name); it != this->globals.end())
		{
			lg::error("interp", "redefinition of global '%s'", name);
			return;
		}

		this->globals[name] = new Value(std::move(val));
		lg::log("interp", "added global '%s'", name);
	}

	std::optional<Value> InterpState::evaluateExpr(ikura::str_view expr, CmdContext& cs)
	{
		auto exp = ast::parseExpr(expr);
		if(!exp) return { };

		return exp->evaluate(this, cs);
	}


	Command* InterpState::findCommand(ikura::str_view name) const
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

		return command;
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
			globs.insert({ k, *v });

		wr.write(globs);
	}

	std::optional<InterpState> InterpState::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		InterpState interp;
		if(!rd.read(&interp.commands))
			return { };

		if(!rd.read(&interp.aliases))
			return { };

		ikura::string_map<uint32_t> builtinPerms;

		if(!rd.read(&builtinPerms))
			return { };

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
	static Synchronised<interp::InterpState, std::shared_mutex> TheInterpreter;
	Synchronised<interp::InterpState, std::shared_mutex>& interpreter()
	{
		return TheInterpreter;
	}
}



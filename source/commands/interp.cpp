// runner.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"

namespace ikura::cmd
{
	static Synchronised<InterpState, std::shared_mutex> TheInterpreter;

	Synchronised<InterpState, std::shared_mutex>& interpreter()
	{
		return TheInterpreter;
	}


	std::optional<interp::Value> InterpState::resolveVariable(ikura::str_view name, CmdContext& cs) const
	{
		auto INVALID = [name](ikura::str_view s = "") -> std::optional<interp::Value> {
			lg::error("interp", "variable '%s' not found%s", name, s);
			return { };
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
				size_t idx = name[0] - '0';
				name.remove_prefix(1);

				while('0' <= name[0] && name[0] <= '9')
				{
					idx = (10 * idx) + (name[0] - '0');
					name.remove_prefix(1);
				}

				if(name.size() != 0)
					return INVALID(zpr::sprint(" (junk '%s' at end)", name));

				if(idx >= cs.macro_args.size())
				{
					lg::error("interp", "argument index out of bounds (want %zu, have %zu)",
						idx, cs.macro_args.size());
					return { };
				}

				// macro args are always strings
				return interp::Value::of_string(cs.macro_args[idx].str());
			}
			else
			{
				if(name == "user")      return interp::Value::of_string(cs.caller.str());
				if(name == "self")      return interp::Value::of_string(cs.channel->getUsername());
				if(name == "channel")   return interp::Value::of_string(cs.channel->getName());
			}
		}

		// for now, nothing
		return INVALID();
	}

	const Command* InterpState::findCommand(ikura::str_view name) const
	{
		const Command* command = nullptr;
		while(!command)
		{
			bool action = interpreter().map_read([&name, &command](auto& interp) {

				if(auto it = interp.commands.find(name); it != interp.commands.end())
				{
					command = it->second;
					return true;
				}

				if(auto it = interp.aliases.find(name); it != interp.aliases.end())
				{
					name = it->second;
					return false;
				}

				return true;
			});

			if(action)  break;
			else        continue;
		}

		return command;
	}

	void InterpState::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->commands);
		wr.write(this->aliases);
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

		return interp;
	}


	void DbInterpState::serialise(Buffer& buf) const
	{
		interpreter().rlock()->serialise(buf);
	}

	std::optional<DbInterpState> DbInterpState::deserialise(Span& buf)
	{
		auto it = InterpState::deserialise(buf);
		if(!it) return { };

		DbInterpState ret;
		*interpreter().wlock().get() = std::move(it.value());

		return ret;
	}
}

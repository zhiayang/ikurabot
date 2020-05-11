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

	std::optional<Message> interpretCommandCode(InterpState* fs, CmdContext* cs, ikura::str_view _code)
	{
		Message ret;
		auto code = _code;

		while(code.size() > 0)
		{
			if(code[0] == '\\')
			{
				if(code.size() < 2)
					return ret;

				ret.add(code.take(2).drop(1));
				code.remove_prefix(2);
			}
			else if(code[0] == '$')
			{
				if(code.size() < 2)
					return ret;

				// grab to the next space.
				auto var = code.take(code.find(' ')).drop(1);
				code.remove_prefix(var.size() + 1);

				if(var == "user")       { ret.add(cs->caller); }
				else if(var == "self")  { ret.add(cs->channel->getUsername()); }
				else                    { ret.add("??"); }
			}
			else if(code[0] == '|')
			{
				code.remove_prefix(1);
			}
			else
			{
				ret.add(code.take(1));
				code.remove_prefix(1);
			}
		}

		return ret;
	}













	const Command* InterpState::findCommand(ikura::str_view name) const
	{
		const Command* command = nullptr;
		while(!command)
		{
			bool action = interpreter().map_read([&name, &command](auto& interp) {

				if(auto it = interp.commands.find(name); it != interp.commands.end())
				{
					command = &it->second;
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

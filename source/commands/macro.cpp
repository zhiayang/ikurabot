// macro.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"
#include "serialise.h"

namespace ikura::cmd
{
	Macro::Macro(std::string name, std::string raw_code) : Command(std::move(name)), code(util::split_copy(raw_code, ' '))
	{
	}

	Macro::Macro(std::string name, std::vector<std::string> words) : Command(std::move(name)), code(std::move(words))
	{
	}

	void Macro::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// just write the name and the source code.
		wr.write(this->name);
		wr.write(this->code);
	}

	std::optional<Macro*> Macro::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		std::string name;
		std::vector<std::string> code;

		if(!rd.read(&name))
			return { };

		if(!rd.read(&code))
			return { };

		return new Macro(name, code);
	}

	std::optional<Message> Macro::run(InterpState* fs, CmdContext& cs) const
	{
		// just echo words wholesale until we get to a '$'
		Message msg;

		for(const auto& x : this->code)
		{
			if(x.empty())
				continue;

			auto a = ikura::str_view(x);

			// syntax is :NAME for emotes
			// but you can escape the : with \:
			if(a[0] == '$')
			{
				auto v = fs->resolveVariable(a, cs);
				if(v) msg.add(v->str());
			}
			else if(a[0] == ':')
			{
				// an emote.
				msg.add(Emote(a.drop(1).str()));
			}
			else if(a[0] == '\\' && a.size() > 1 && a[1] == ':')
			{
				msg.add(a.drop(1).str());
			}
			else
			{
				msg.add(a);
			}
		}

		return msg;
	}
}

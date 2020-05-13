// macro.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"
#include "serialise.h"

namespace ikura::cmd
{
	Macro::Macro(std::string name, ikura::str_view code) : Command(std::move(name))
	{
		// split up the thing.
		size_t end = 0;
		while(end < code.size())
		{
			if(code.find("\\\\") == 0)
			{
				end += 2;
			}
			else if(code.find("\\") == 0)
			{
				end += 1;
				int parens = 0;
				int braces = 0;
				int squares = 0;

				while(end < code.size())
				{
					switch(code[end])
					{
						case '(':   parens++; break;
						case ')':   parens--; break;
						case '{':   braces++; break;
						case '}':   braces--; break;
						case '[':   squares++; break;
						case ']':   squares--; break;
						default:    break;
					}

					end++;
					if(code[end - 1] == ' ' && parens == 0 && braces == 0 && squares == 0)
						break;
				}

				if(end == code.size() && (parens > 0 || braces > 0 || squares > 0))
					lg::error("interp", "unterminated inline expr");

				else
					goto add_piece;
			}
			else if(code[end] != ' ')
			{
				end += 1;
			}
			else
			{
			add_piece:
				// lg::log("cmd", "piece = '%s'", code.take(end));
				this->code.push_back(code.take(end).str());
				code.remove_prefix(end);
				end = 0;

				while(code.size() > 0 && (code[0] == ' ' || code[0] == '\t'))
					code.remove_prefix(1);
			}
		}

		if(end > 0)
			this->code.push_back(code.take(end).str());
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


	std::optional<interp::Value> Macro::run(InterpState* fs, CmdContext& cs) const
	{
		// just echo words wholesale until we get to a '$'
		std::vector<interp::Value> list;

		for(const auto& x : this->code)
		{
			if(x.empty())
				continue;

			auto a = ikura::str_view(x);

			if(a.find("\\\\") == 0)
			{
				list.push_back(interp::Value::of_string(a.drop(1).str()));
			}
			else if(a[0] == '\\')
			{
				auto v = fs->evaluateExpr(a.drop(1), cs);
				if(v) list.push_back(v.value());
			}
			else
			{
				list.push_back(interp::Value::of_string(a.str()));
			}
		}

		return interp::Value::of_list(list);
	}
}

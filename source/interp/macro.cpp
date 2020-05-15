// macro.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"
#include "serialise.h"

namespace ikura::interp
{
	std::vector<std::string> performExpansion(ikura::str_view code)
	{
		std::vector<std::string> ret;

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
				ret.push_back(code.take(end).str());
				code.remove_prefix(end);
				end = 0;

				while(code.size() > 0 && (code[0] == ' ' || code[0] == '\t'))
					code.remove_prefix(1);
			}
		}

		if(end > 0)
			ret.push_back(code.take(end).str());

		return ret;
	}

	std::vector<interp::Value> evaluateMacro(InterpState* fs, CmdContext& cs, const std::vector<std::string>& code)
	{
		// just echo words wholesale until we get to a '$'
		std::vector<interp::Value> list;

		for(const auto& x : code)
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

		return list;
	}



	Macro::Macro(std::string name, std::vector<std::string> words)
		: Command(std::move(name), Type::get_macro_function()), code(std::move(words))
	{
	}

	Macro::Macro(std::string name, ikura::str_view code) : Command(std::move(name), Type::get_macro_function())
	{
		this->code = performExpansion(code);
	}

	std::optional<interp::Value> Macro::run(InterpState* fs, CmdContext& cs) const
	{
		return interp::Value::of_list(Type::get_string(), evaluateMacro(fs, cs, this->code));
	}

	const std::vector<std::string>& Macro::getCode() const
	{
		return this->code;
	}

	void Macro::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// these come from the superclass
		wr.write(this->name);
		wr.write(this->permissions);

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
		uint32_t permissions = 0;
		std::vector<std::string> code;

		if(!rd.read(&name))
			return { };

		if(!rd.read(&permissions))
			return { };

		if(!rd.read(&code))
			return { };

		auto ret = new Macro(name, code);
		ret->permissions = permissions;
		return ret;
	}

	Command::Command(std::string name, Type::Ptr sig) : name(std::move(name)), signature(std::move(sig)) { }

	std::optional<Command*> Command::deserialise(Span& buf)
	{
		auto tag = buf.peek();
		switch(tag)
		{
			case serialise::TAG_MACRO: {
				auto ret = Macro::deserialise(buf);
				if(ret) return ret.value();     // force unwrap to cast Macro* -> Command*
				else    return { };
			}

			case serialise::TAG_FUNCTION:
			default:
				lg::error("db", "type tag mismatch (unexpected '%02x')", tag);
				return { };
		}
	}
}









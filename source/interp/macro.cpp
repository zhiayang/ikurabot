// macro.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "cmd.h"
#include "serialise.h"

namespace ikura::interp
{
	std::vector<ikura::str_view> performExpansion(ikura::str_view code)
	{
		std::vector<ikura::str_view> ret;

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
					// TODO: this won't work if we have ( or { or [ inside a string...
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

					// TODO: this is also scuffed, if we have ';' in a string or something
					if((code[end - 1] == ' ' || code[end - 1] == ';')
						&& parens == 0 && braces == 0 && squares == 0)
					{
						break;
					}
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
				// lg::log("cmd", "piece = '{}'", code.take(end));
				ret.push_back(code.take(end));
				code.remove_prefix(end);
				end = 0;

				while(code.size() > 0 && (code[0] == ' ' || code[0] == '\t'))
					code.remove_prefix(1);
			}
		}

		if(end > 0)
			ret.push_back(code.take(end));

		return ret;
	}

	std::vector<interp::Value> evaluateMacro(InterpState* fs, CmdContext& cs, const std::vector<std::string>& code)
	{
		using interp::Value;

		// just echo words wholesale until we get to a '\'
		std::vector<Value> list;

		for(const auto& x : code)
		{
			if(x.empty())
				continue;

			auto a = ikura::str_view(x);

			if(a.find("\\\\") == 0)
			{
				list.push_back(Value::of_string(a.drop(1)));
			}
			else if(a[0] == '\\')
			{
				if(auto v = fs->evaluateExpr(a.drop(1), cs); v.has_value())
				{
					// dismantle the list, if it is one.
					if(v->is_list() && !v->is_string())
					{
						auto& l = v->get_list();
						for(auto& x : l)
							list.push_back(Value::of_string(x.raw_str()));
					}
					else
					{
						list.push_back(Value::of_string(v->raw_str()));
					}
				}
				else
				{
					// not sure if we should continue expanding... for now, we do.
					lg::warn("macro", "expansion error: {}", v.error());
					list.push_back(Value::of_string("<error>"));
				}
			}
			else
			{
				list.push_back(Value::of_string(a.str()));
			}
		}

		return list;
	}



	Macro::Macro(std::string name, std::vector<std::string> words)
		: Command(std::move(name)), code(std::move(words))
	{
	}

	Macro::Macro(std::string name, ikura::str_view code) : Command(std::move(name))
	{
		this->setCode(code);
	}

	void Macro::setCode(ikura::str_view code)
	{
		this->code = zfu::map(performExpansion(code), [](auto& sv) { return sv.str(); });
	}

	Result<interp::Value> Macro::run(InterpState* fs, CmdContext& cs) const
	{
		return interp::Value::of_list(Type::get_string(), evaluateMacro(fs, cs, this->code));
	}

	const std::vector<std::string>& Macro::getCode() const
	{
		return this->code;
	}

	Type::Ptr Macro::getSignature() const
	{
		return Type::get_macro_function();
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
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		std::string name;
		PermissionSet permissions;
		std::vector<std::string> code;

		if(!rd.read(&name))
			return { };

		if(!rd.read(&permissions))
			return { };

		if(!rd.read(&code))
			return { };

		// zpr::println("loaded perms '{x}' for cmd '{}'", permissions, name);
		auto ret = new Macro(name, code);
		ret->permissions = permissions;
		return ret;
	}

	Command::Command(std::string name) : name(std::move(name)) { }

	std::optional<Command*> Command::deserialise(Span& buf)
	{
		auto tag = buf.peek();
		switch(tag)
		{
			case serialise::TAG_MACRO: {
				auto ret = Macro::deserialise(buf);
				if(ret) return ret.value();
				else    return { };
			}

			case serialise::TAG_FUNCTION: {
				auto ret = Function::deserialise(buf);
				if(ret) return ret.value();
				else    return { };
			}

			default:
				return lg::error_o("db", "type tag mismatch (unexpected '{02x}')", tag);
		}
	}
}









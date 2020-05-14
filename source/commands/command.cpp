// command.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <chrono>

#include "zfu.h"
#include "cmd.h"
#include "ast.h"
#include "timer.h"

namespace ikura::cmd
{
	static void processCommand(str_view user, const Channel* chan, str_view cmd);
	static Message generateResponse(str_view user, const Channel* chan, str_view msg);

	void processMessage(str_view user, const Channel* chan, str_view message)
	{
		auto pref = chan->getCommandPrefix();
		if(message.find(pref) == 0)
		{
			processCommand(user, chan, message.drop(pref.size()));
		}
		else if(message.find(chan->getUsername()) != std::string::npos)
		{
			if(chan->shouldReplyMentions())
				chan->sendMessage(generateResponse(user, chan, message));
		}
	}



	static Message messageFromValue(const interp::Value& val)
	{
		Message msg;

		std::function<void (Message&, const interp::Value&)> do_one;
		do_one = [&do_one](Message& m, const interp::Value& v) {
			if(v.is_void())
				return;

			if(v.is_list())
			{
				auto& l = v.get_list();
				for(auto& x : l)
					do_one(m, x);
			}
			else if(v.is_string())
			{
				// don't include the quotes.
				auto s = v.get_string();
				auto sv = ikura::str_view(s);

				// syntax is :NAME for emotes
				// but you can escape the : with \:
				if(sv.find("\\:") == 0)     m.add(sv.drop(1));
				else if(sv.find(':') == 0)  m.add(Emote(sv.drop(1).str()));
				else                        m.add(sv);
			}
			else
			{
				m.add(v.str());
			}
		};

		do_one(msg, val);
		return msg;
	}



	static void processCommand(str_view user, const Channel* chan, str_view input)
	{
		CmdContext cs;
		cs.caller = user;
		cs.channel = chan;

		auto cmd_str = input.substr(0, input.find(' ')).trim();
		auto arg_str = input.drop(cmd_str.size()).trim();

		auto command = interpreter().rlock()->findCommand(cmd_str);

		if(command)
		{
			auto t = ikura::timer();
			auto arg_split = util::split(arg_str, ' ');
			auto args = zfu::map(arg_split, [](const auto& x) -> auto { return interp::Value::of_string(x.str()); });

			cs.macro_args = ikura::span(args);
			auto ret = interpreter().map_write([&](auto& fs) {
				return command->run(&fs, cs);
			});

			lg::log("interp", "command took %.3f ms to execute", t.measure());
			if(ret) chan->sendMessage(messageFromValue(ret.value()));
		}
		else if(cmd_str == "eval")
		{
			// syntax: eval <expr>
			auto t = ikura::timer();

			auto ret = interpreter().wlock()->evaluateExpr(arg_str, cs);
			lg::log("interp", "command took %.3f ms to execute", t.measure());

			if(ret) chan->sendMessage(messageFromValue(ret.value()));
		}
		else if(cmd_str == "global")
		{
			// syntax: global <name> <type>
			auto name = arg_str.substr(0, arg_str.find(' ')).trim();
			auto type_str = arg_str.drop(name.size()).trim();

			if(name.empty() || type_str.empty())
				return chan->sendMessage(Message("not enough arguments to global"));

			auto value = ast::parseType(type_str);
			if(!value)
				return chan->sendMessage(Message(zpr::sprint("invalid type '%s'", type_str)));


			interpreter().wlock()->addGlobal(name, value.value());
			chan->sendMessage(Message(zpr::sprint("added global '%s' with type '%s'", name, value->type_str())));
		}
		else if(cmd_str == "def")
		{
			// syntax: def <name> expansion...
			auto name = arg_str.substr(0, arg_str.find(' ')).trim();
			auto expansion = arg_str.drop(name.size()).trim();

			if(name.empty())
				return chan->sendMessage(Message("not enough arguments to 'def'"));

			if(expansion.empty())
				return chan->sendMessage(Message("'def' expansion cannot be empty"));

			if(interpreter().rlock()->findCommand(name) != nullptr)
				return chan->sendMessage(Message(zpr::sprint("'%s' is already defined", name)));


			interpreter().wlock()->commands.emplace(name, new Macro(name.str(), expansion));
			chan->sendMessage(Message(zpr::sprint("defined '%s'", name)));
		}
		else if(cmd_str == "undef")
		{
			// syntax: undef <name>
			if(arg_str.find(' ') != std::string::npos)
				return chan->sendMessage(Message("'undef' takes exactly 1 argument"));

			auto done = interpreter().wlock()->removeCommandOrAlias(arg_str);

			chan->sendMessage(Message(
				done ? zpr::sprint("removed '%s'", arg_str)
					 : zpr::sprint("'%s' does not exist", arg_str)
			));
		}
		else
		{
			lg::warn("cmd", "user '%s' tried non-existent command '%s'", user, cmd_str);
		}
	}

	static Message generateResponse(str_view user, const Channel* chan, str_view msg)
	{
		return Message(zpr::sprint("%s AYAYA /", user));
	}












	Command::Command(std::string name) : name(std::move(name)) { }

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

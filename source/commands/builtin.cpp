// builtin.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "ast.h"
#include "cmd.h"
#include "timer.h"

namespace ikura::cmd
{
	// defined in command.cpp
	Message value_to_message(const interp::Value& val);

	static void command_def(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_eval(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_show(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_redef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_undef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_chmod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_global(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);

	bool is_builtin(ikura::str_view x)
	{
		return zfu::match(x, "def", "eval", "show", "redef", "undef", "chmod", "global");
	}

	bool run_builtin_command(CmdContext& cs, const Channel* chan, ikura::str_view cmd_str, ikura::str_view arg_str)
	{
		auto user_perms = chan->getUserPermissions(cs.caller);
		auto denied = [&]() -> bool {
			lg::warn("cmd", "user '%s' tried to execute command '%s' with insufficient permissions (%x)",
				cs.caller, cmd_str, user_perms);

			chan->sendMessage(Message("insufficient permissions"));
			return true;
		};

		uint32_t perm = interpreter().map_read([&](auto& interp) -> uint32_t {
			if(auto it = interp.builtinCommandPermissions.find(cmd_str); it != interp.builtinCommandPermissions.end())
				return it->second;

			return 0;
		});

		if(cmd_str == "chmod")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_chmod(cs, chan, arg_str);
			return true;
		}
		else if(cmd_str == "eval")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_eval(cs, chan, arg_str);
			return true;
		}
		else if(cmd_str == "global")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_global(cs, chan, arg_str);
			return true;
		}
		else if(cmd_str == "def")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_def(cs, chan, arg_str);
			return true;
		}
		else if(cmd_str == "redef")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_redef(cs, chan, arg_str);
			return true;
		}
		else if(cmd_str == "undef")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_undef(cs, chan, arg_str);
			return true;
		}
		else if(cmd_str == "show")
		{
			if(!verifyPermissions(perm, user_perms))
				return denied();

			command_show(cs, chan, arg_str);
			return true;
		}

		return false;
	}







	static void command_eval(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: eval <expr>
		auto t = ikura::timer();

		auto ret = interpreter().wlock()->evaluateExpr(arg_str, cs);
		lg::log("interp", "command took %.3f ms to execute", t.measure());

		if(ret) chan->sendMessage(value_to_message(ret.value()));
	}

	static void command_chmod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: chmod <command> <permissions>
		auto cmd = arg_str.substr(0, arg_str.find(' ')).trim();
		auto perm_str = arg_str.drop(cmd.size()).trim();

		if(cmd.empty() || perm_str.empty())
			return chan->sendMessage(Message("not enough arguments to chmod"));
		auto tmp = perm_str.str();
		char* tmp2 = nullptr;

		auto perm = strtol(tmp.data(), &tmp2, /* base: */ 0x10);
		if(tmp2 != &tmp.back() + 1)
			return chan->sendMessage(Message(zpr::sprint("invalid permission string '%s'", tmp)));

		if(is_builtin(cmd))
		{
			interpreter().wlock()->builtinCommandPermissions[cmd] = perm;
		}
		else
		{
			auto command = interpreter().rlock()->findCommand(cmd);
			if(!command)
				return chan->sendMessage(Message(zpr::sprint("'%s' does not exist", cmd)));

			command->setPermissions(perm);
		}

		chan->sendMessage(Message(zpr::sprint("permissions for '%s' changed to %x", cmd, perm)));
	}

	static void command_global(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
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

	static void internal_def(const Channel* chan, bool redef, ikura::str_view name, ikura::str_view expansion)
	{
		if(interpreter().rlock()->findCommand(name) != nullptr)
		{
			if(!redef)  return chan->sendMessage(Message(zpr::sprint("'%s' is already defined", name)));
			else        interpreter().wlock()->removeCommandOrAlias(name);
		}
		else if(redef)
		{
			return chan->sendMessage(Message(zpr::sprint("'%s' does not exist", name)));
		}

		interpreter().wlock()->commands.emplace(name, new Macro(name.str(), expansion));
		chan->sendMessage(Message(zpr::sprint("%sdefined '%s'", redef ? "re" : "", name)));
	}

	static void command_def(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: def <name> expansion...
		auto name = arg_str.substr(0, arg_str.find(' ')).trim();
		auto expansion = arg_str.drop(name.size()).trim();

		if(name.empty())        return chan->sendMessage(Message("not enough arguments to 'def'"));
		if(expansion.empty())   return chan->sendMessage(Message("'def' expansion cannot be empty"));

		internal_def(chan, false, name, expansion);
	}

	static void command_redef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: def <name> expansion...
		auto name = arg_str.substr(0, arg_str.find(' ')).trim();
		auto expansion = arg_str.drop(name.size()).trim();

		if(name.empty())        return chan->sendMessage(Message("not enough arguments to 'redef'"));
		if(expansion.empty())   return chan->sendMessage(Message("'redef' expansion cannot be empty"));

		internal_def(chan, true, name, expansion);
	}

	static void command_undef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: undef <name>
		if(arg_str.find(' ') != std::string::npos || arg_str.empty())
			return chan->sendMessage(Message("'undef' takes exactly 1 argument"));

		auto done = interpreter().wlock()->removeCommandOrAlias(arg_str);

		chan->sendMessage(Message(
			done ? zpr::sprint("removed '%s'", arg_str)
				 : zpr::sprint("'%s' does not exist", arg_str)
		));
	}

	static void command_show(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: show <name>
		if(arg_str.find(' ') != std::string::npos || arg_str.empty())
			return chan->sendMessage(Message("'show' takes exactly 1 argument"));

		if(is_builtin(arg_str))
			return chan->sendMessage(Message(zpr::sprint("'%s' is a builtin command", arg_str)));

		auto cmd = interpreter().rlock()->findCommand(arg_str);
		if(cmd == nullptr)
			return chan->sendMessage(Message(zpr::sprint("'%s' does not exist", arg_str)));

		if(auto macro = dynamic_cast<Macro*>(cmd))
		{
			Message msg;
			msg.add(zpr::sprint("'%s' is defined as: ", arg_str));

			auto code = macro->getCode();
			for(const auto& c : code)
				msg.add(c);

			return chan->sendMessage(msg);
		}
		else
		{
			return chan->sendMessage(Message(zpr::sprint("'%s' cannot be shown", arg_str)).add(Emote("monkaTOS")));
		}
	}
}

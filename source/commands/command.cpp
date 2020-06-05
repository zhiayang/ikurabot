// command.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "cmd.h"
#include "ast.h"
#include "timer.h"
#include "markov.h"

namespace ikura::interp
{
	// defined in builtin.cpp. returns true if the command existed
	bool run_builtin_command(interp::CmdContext& cs, const Channel* chan, str_view cmd, str_view args);
}

namespace ikura::cmd
{
	static void process_command(ikura::str_view user, ikura::str_view username, const Channel* chan, ikura::str_view cmd);
	static Message generateResponse(ikura::str_view user, const Channel* chan, ikura::str_view msg);

	bool processMessage(ikura::str_view userid, ikura::str_view username, const Channel* chan, ikura::str_view message, bool enablePings)
	{
		auto pref = chan->getCommandPrefix();
		if(!pref.empty() && message.find(pref) == 0)
		{
			process_command(userid, username, chan, message.drop(pref.size()));
			return true;
		}
		else if(enablePings && chan->shouldReplyMentions())
		{
			if(message.find(chan->getUsername()) != std::string::npos)
				chan->sendMessage(generateResponse(userid, chan, message));
		}

		return false;
	}



	interp::Value message_to_value(const Message& msg)
	{
		using interp::Type;
		using interp::Value;

		std::vector<Value> list;

		for(const auto& f : msg.fragments)
		{
			if(f.isEmote)   list.push_back(Value::of_string(zpr::sprint(":%s", f.emote.name)));
			else            list.push_back(Value::of_string(f.str));
		}

		return Value::of_list(Type::get_string(), list);
	}

	Message value_to_message(const interp::Value& val)
	{
		Message msg;

		std::function<void (Message&, const interp::Value&)> do_one;
		do_one = [&do_one](Message& m, const interp::Value& v) {
			if(v.is_void())
				return;

			if(v.is_string())
			{
				auto s = v.str();

				// drop the quotes.
				auto sv = ikura::str_view(s).remove_prefix(1).remove_suffix(1);

				// syntax is :NAME for emotes
				// but you can escape the : with \:
				if(sv.find("\\:") == 0)     m.add(sv.drop(1));
				else if(sv.find(':') == 0)  m.add(Emote(sv.drop(1).str()));
				else                        m.add(sv);
			}
			else if(v.is_list())
			{
				auto& l = v.get_list();
				for(auto& x : l)
					do_one(m, x);
			}
			else
			{
				m.add(v.str());
			}
		};

		do_one(msg, val);
		return msg;
	}

	static std::vector<interp::Value> expand_arguments(interp::InterpState* fs, interp::CmdContext& cs, ikura::str_view input)
	{
		auto code = interp::performExpansion(input);
		return evaluateMacro(fs, cs, code);
	}

	static void process_command(ikura::str_view userid, ikura::str_view username, const Channel* chan, ikura::str_view input)
	{
		if(input.empty())
			return;

		interp::CmdContext cs;
		cs.callername = username;
		cs.callerid = userid;
		cs.channel = chan;

		auto cmd_str = input.substr(0, input.find(' ')).trim();
		auto arg_str = input.drop(cmd_str.size()).trim();

		auto command = interpreter().rlock()->findCommand(cmd_str);

		if(command)
		{
			if(!chan->checkUserPermissions(userid, command->perms()))
			{
				lg::warn("cmd", "user '%s' tried to execute command '%s' with insufficient permissions", username, command->getName());
				chan->sendMessage(Message("insufficient permissions"));
				return;
			}

			auto t = ikura::timer();
			auto args = expand_arguments(interpreter().wlock().get(), cs, arg_str);
			cs.macro_args = std::move(args);

			auto ret = interpreter().map_write([&](auto& fs) {
				return command->run(&fs, cs);
			});

			lg::log("interp", "command took %.3f ms to execute", t.measure());
			if(ret) chan->sendMessage(cmd::value_to_message(ret.unwrap()));
			else if(chan->shouldPrintInterpErrors()) chan->sendMessage(Message(ret.error()));
		}
		else
		{
			auto found = run_builtin_command(cs, chan, cmd_str, arg_str);
			if(found) return;

			lg::warn("cmd", "user '%s' tried non-existent command '%s'", username, cmd_str);
		}
	}


	ikura::string_map<PermissionSet> getDefaultBuiltinPermissions()
	{
		ikura::string_map<PermissionSet> ret;

		namespace perms = permissions;

		// most of these assignments are arbitrary.
		// users that are "trusted" to some extent, subs, mods, vips, and trusted.
		constexpr auto p_known = perms::OWNER | perms::BROADCASTER | perms::MODERATOR | perms::SUBSCRIBER | perms::VIP;

		// users that are explicitly trusted to a higher level -- basically just mods. probably restrict commands that
		// can drastically change the state of the interpreter (global, def, undef) to these.
		constexpr auto p_admin = perms::OWNER | perms::BROADCASTER | perms::MODERATOR;

		ret["chmod"]    = PermissionSet::fromFlags(perms::OWNER | perms::BROADCASTER);
		ret["eval"]     = PermissionSet::fromFlags(p_known);
		ret["global"]   = PermissionSet::fromFlags(p_admin);
		ret["def"]      = PermissionSet::fromFlags(p_admin);
		ret["redef"]    = PermissionSet::fromFlags(p_admin);
		ret["undef"]    = PermissionSet::fromFlags(p_admin);
		ret["list"]     = PermissionSet::fromFlags(p_known);
		ret["usermod"]  = PermissionSet::fromFlags(p_admin);
		ret["groupadd"] = PermissionSet::fromFlags(p_admin);
		ret["groupdel"] = PermissionSet::fromFlags(p_admin);
		ret["show"]     = PermissionSet::fromFlags(perms::EVERYONE);

		return ret;
	}





	static Message generateResponse(str_view userid, const Channel* chan, str_view msg)
	{
		return markov::generateMessage();
	}
}

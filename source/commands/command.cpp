// command.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "zfu.h"
#include "cmd.h"
#include "ast.h"
#include "timer.h"
#include "config.h"
#include "markov.h"
#include "synchro.h"

namespace ikura::interp
{
	// defined in builtin.cpp. returns true if the command existed
	bool run_builtin_command(interp::CmdContext& cs, const Channel* chan, str_view cmd, str_view args);
}

namespace ikura::cmd
{
	static void process_command(interp::CmdContext& cs, ikura::str_view user, ikura::str_view username, const Channel* chan, ikura::str_view cmd);
	static Message generateResponse(ikura::str_view user, const Channel* chan, ikura::str_view msg);

	Message value_to_message(const interp::Value& val);

	bool processMessage(ikura::str_view userid, ikura::str_view username, const Channel* chan, ikura::str_view message, bool enablePings)
	{
		interp::CmdContext cs;
		cs.executionStart = util::getMillisecondTimestamp();
		cs.callername = username;
		cs.callerid = userid;
		cs.channel = chan;

		auto match_prefix = [&chan](ikura::str_view msg) -> std::optional<size_t> {

			auto prefixes = chan->getCommandPrefixes();
			for(const auto& pfx : prefixes)
				if(msg.find(pfx) == 0)
					return pfx.size();

			return std::nullopt;
		};

		if(auto prefix_len = match_prefix(message); prefix_len.has_value())
		{
			process_command(cs, userid, username, chan, message.drop(*prefix_len));
			return true;
		}
		else if(enablePings && chan->shouldReplyMentions())
		{
			if(message.find(chan->getUsername()) != std::string::npos)
				chan->sendMessage(generateResponse(userid, chan, message));
		}

		// process on_message handlers
		// TODO: move this out of the big lock

		if(chan->shouldRunMessageHandlers() && ((chan->getBackend() == Backend::Twitch && username != config::twitch::getUsername())
			|| (chan->getBackend() == Backend::Discord && userid != config::discord::getUserId().str())))
		{
			interpreter().perform_write([&cs, &message, &chan](interp::InterpState& interp) {
				auto [ val, _ ] = interp.resolveVariable("__on_message", cs);
				if(!val.has_value())
					return;

				auto& handlers = val.value();
				if(!handlers.is_list() || !handlers.type()->elm_type()->is_function()
				|| !handlers.type()->elm_type()->is_same(interp::Type::get_function(interp::Type::get_string(), { interp::Type::get_string() })))
				{
					lg::warn("interp", "__on_message list has wrong type (expected [(str) -> str], found %s)", handlers.type()->str());
					return;
				}

				// TODO: pass more information to the handler (eg username, channel, etc)
				for(auto& handler : handlers.get_list())
				{
					const auto& fn = handler.get_function();
					if(!fn)
					{
						lg::error("interp", "handler was null");
						continue;
					}

					auto copy = cs;
					copy.arguments = { interp::Value::of_string(message.str()) };

					lg::dbglog("interp", "running message handler '%s'", handler.get_function()->getName());
					if(auto res = fn->run(&interp, copy); res && res->type()->is_string())
						chan->sendMessage(value_to_message(res.unwrap()));
				}
			});
		}

		return false;
	}




	static std::pair<ikura::str_view, ikura::str_view> split_pipeline(ikura::str_view msg, bool* expansion)
	{
		auto split = interp::performExpansion(msg);
		for(auto& sv : split)
		{
			if(sv == "|>" || sv == "|...>")
			{
				*expansion = (sv == "|...>");

				return {
					msg.take(sv.data() - msg.data()).trim(),
					msg.drop(sv.data() - msg.data()).drop(sv.size()).trim()
				};
			}
		}

		*expansion = false;
		return { msg, "" };
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
		std::function<Message& (Message&, const interp::Value&)> do_one;
		do_one = [&do_one](Message& m, const interp::Value& v) -> Message& {
			if(v.is_void())
				return m;

			if(v.is_string())
			{
				auto s = v.raw_str();
				auto sv = ikura::str_view(s).trim();

				// we actually need to handle emotes in the middle of strings.
				size_t cur = 0;
				bool nospace = false;

				auto flush = [&]() {
					if(cur > 0)
					{
						auto x = sv.take(cur).trim();
						if(nospace) m.addNoSpace(x);
						else        m.add(x);

						nospace = false;
						sv.remove_prefix(cur);
					}

					sv = sv.trim();
					cur = 0;
				};

				while(cur < sv.size())
				{
					if(sv.size() > cur + 1 && sv[cur] == '\\' && sv[cur + 1] == ':')
					{
						flush();
						sv.remove_prefix(1); // remove the '\' as well
						nospace = true;
					}
					else if((cur == 0 || sv[cur - 1] == ' ') && sv[cur] == ':')
					{
						flush();
						sv.remove_prefix(1);

						// get an emote.
						size_t k = 0;
						while(k < sv.size() && sv[k] != ' ')
							k++;

						m.add(Emote(sv.take(k).str()));
						sv.remove_prefix(k);
						flush();
					}
					else if(sv[cur] == ' ')
					{
						flush();
					}
					else
					{
						cur++;
					}
				}

				if(sv.size() > 0)
					flush();
			}
			else if(v.is_list())
			{
				auto& l = v.get_list();

				if(v.flags() & interp::Value::FLAG_DISMANTLE_LIST)
				{
					auto next = &m;
					for(const auto& x : l)
					{
						Message tmp; do_one(tmp, x);
						next = &next->link(std::move(tmp));
					}
				}
				else
				{
					for(const auto& x : l)
						do_one(m, x);
				}
			}
			else
			{
				m.add(v.str());
			}

			return m;
		};

		Message msg;
		do_one(msg, val);

		return msg;
	}

	static std::vector<interp::Value> expand_arguments(interp::InterpState* fs, interp::CmdContext& cs, ikura::str_view input)
	{
		auto code = zfu::map(interp::performExpansion(input), [](auto& sv) { return sv.str(); });
		return evaluateMacro(fs, cs, std::move(code));
	}





	static void process_one_command(interp::CmdContext& cs, ikura::str_view userid, ikura::str_view username, const Channel* chan,
		ikura::str_view cmd_str, ikura::str_view arg_str, bool pipelined, bool doExpand, std::string* out)
	{
		cmd_str = cmd_str.trim();
		arg_str = arg_str.trim();

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
			if(doExpand || dynamic_cast<interp::Macro*>(command.get()) != nullptr)
			{
				auto args = expand_arguments(interpreter().wlock().get(), cs, arg_str);
				cs.macro_args = util::join(zfu::map(args, [](auto& v) {
					return v.raw_str();
				}), " ");

				cs.arguments = std::move(args);
			}
			else
			{
				cs.macro_args = arg_str;
				cs.arguments = { interp::Value::of_string(arg_str) };
			}

			auto ret = interpreter().map_write([&](auto& fs) {
				return command->run(&fs, cs);
			});

			if(pipelined)
			{
				lg::log("interp", "pipeline sub-command took %.3f ms to execute", t.measure());
				*out = ret->raw_str();
			}
			else
			{
				lg::log("interp", "command took %.3f ms to execute", t.measure());
				if(ret)
					chan->sendMessage(cmd::value_to_message(ret.unwrap()));

				else if(chan->shouldPrintInterpErrors())
					chan->sendMessage(Message(ret.error()));

				else
					lg::error("interp", "%s", ret.error());
			}
		}
		else
		{
			auto found = run_builtin_command(cs, chan, cmd_str, arg_str);
			if(found) return;

			lg::warn("cmd", "user '%s' tried non-existent command '%s'", username, cmd_str);
			return;
		}
	}

	static void process_command(interp::CmdContext& cs, ikura::str_view userid, ikura::str_view username, const Channel* chan, ikura::str_view input)
	{
		if(input.empty())
			return;

		std::string piped_input;
		bool do_expand = true;     // since we start as a macro invocation, always expand by default
		while(true)
		{
			auto [ first, subsequent ] = split_pipeline(input, &do_expand);

			auto cmd_str = first.substr(0, first.find(' ')).trim();
			auto arg_str = zpr::sprint("%s %s", first.drop(cmd_str.size()).trim(), piped_input);

			// zpr::println("F = '%s'", first);
			// zpr::println("S = '%s'", subsequent);
			// zpr::println("A = '%s'", arg_str);

			auto pipelined = !subsequent.empty();
			process_one_command(cs, userid, username, chan, cmd_str, arg_str, pipelined, do_expand, &piped_input);

			input = subsequent;
			if(!pipelined)
				break;
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
		ret["listcmds"] = PermissionSet::fromFlags(p_admin);
		ret["defun"]    = PermissionSet::fromFlags(p_admin);
		ret["usermod"]  = PermissionSet::fromFlags(p_admin);
		ret["showmod"]  = PermissionSet::fromFlags(p_admin);
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

// builtin.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "zfu.h"
#include "ast.h"
#include "cmd.h"
#include "timer.h"
#include "markov.h"

namespace ikura::cmd
{
	// defined in command.cpp
	Message value_to_message(const interp::Value& val);
	interp::Value message_to_value(const Message& msg);
}

namespace ikura::interp
{
	static void command_def(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_eval(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_show(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_redef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_undef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_chmod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_global(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_usermod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_groupadd(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_groupdel(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);

	bool is_builtin_command(ikura::str_view x)
	{
		return zfu::match(x, "def", "eval", "show", "redef", "undef", "chmod", "global", "usermod", "groupadd", "groupdel");
	}

	// tsl::robin_map doesn't let us do this for some reason, so just fall back to std::unordered_map.
	static std::unordered_map<std::string, void (*)(CmdContext&, const Channel*, ikura::str_view)> builtin_cmds = {
		{ "chmod",      command_chmod    },
		{ "eval",       command_eval     },
		{ "global",     command_global   },
		{ "def",        command_def      },
		{ "redef",      command_redef    },
		{ "undef",      command_undef    },
		{ "show",       command_show     },
		{ "usermod",    command_usermod  },
		{ "groupadd",   command_groupadd },
		{ "groupdel",   command_groupdel },
	};

	static constexpr uint64_t DEFAULT_NEW_MACRO_PERMISSIONS
		= permissions::OWNER
		| permissions::BROADCASTER
		| permissions::VIP
		| permissions::SUBSCRIBER
		| permissions::MODERATOR;


	bool run_builtin_command(CmdContext& cs, const Channel* chan, ikura::str_view cmd_str, ikura::str_view arg_str)
	{
		auto denied = [&]() -> bool {
			lg::warn("cmd", "user '%s' tried to execute command '%s' with insufficient permissions",
				cs.callername, cmd_str);

			chan->sendMessage(Message("insufficient permissions"));
			return true;
		};

		auto perm = interpreter().map_read([&](auto& interp) -> PermissionSet {
			if(auto it = interp.builtinCommandPermissions.find(cmd_str); it != interp.builtinCommandPermissions.end())
				return it->second;

			return { };
		});

		if(is_builtin_command(cmd_str))
		{
			if(!chan->checkUserPermissions(cs.callerid, perm))
				return denied();

			builtin_cmds[cmd_str.str()](cs, chan, arg_str);
			return true;
		}

		return false;
	}

	template <typename T>
	static void add_to_list(std::vector<T>& list, T elm)
	{
		if(auto it = std::find(list.begin(), list.end(), elm); it == list.end())
			list.push_back(elm);
	}

	template <typename T>
	static void remove_from_list(std::vector<T>& list, T elm)
	{
		if(auto it = std::find(list.begin(), list.end(), elm); it != list.end())
			list.erase(it);
	}

	static Result<PermissionSet> parse_groups(const Channel* chan, ikura::str_view sv, PermissionSet perms)
	{
		while(sv[0] == '+' || sv[0] == '-' || sv[0] == '*')
		{
			int mode = (sv[0] == '+') ? 0 : (sv[0] == '-') ? 1 : 2;
			sv.remove_prefix(1);

			if(sv.empty())
				return zpr::sprint("unexpected end of input");

			bool discord = (sv[0] == '@');
			if(discord) sv.remove_prefix(1);

			std::string group;
			while(sv.size() > 0)
			{
				if(sv[0] == '\\')
					sv.remove_prefix(1), group += sv[0];

				else if(sv[0] == '+' || sv[0] == '-' || sv[0] == '*')
					break;

				else
					group += sv[0];

				sv.remove_prefix(1);
			}


			if(discord)
			{
				// TODO: not supported omegalul
			}
			else
			{
				auto grp = database().rlock()->sharedData.getGroup(group);
				if(grp == nullptr)
					return zpr::sprint("nonexistent group '%s'", group);

				auto gid = grp->id;
				if(mode == 0)
				{
					add_to_list(perms.whitelist, gid);      // add to whitelist
					remove_from_list(perms.blacklist, gid); // remove from blacklist
				}
				else if(mode == 1)
				{
					add_to_list(perms.blacklist, gid);      // add to blacklist
					remove_from_list(perms.whitelist, gid); // remove from whitelist
				}
				else
				{
					// remove from both
					remove_from_list(perms.blacklist, gid);
					remove_from_list(perms.whitelist, gid);
				}
			}

			// zpr::println("group = %s", group);
		}

		if(!sv.empty())
			return zpr::sprint("junk at end of permissions (%s)", sv);

		return perms;
	}




	static void command_eval(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: eval <expr>
		auto t = ikura::timer();

		auto ret = interpreter().wlock()->evaluateExpr(arg_str, cs);
		lg::log("interp", "command took %.3f ms to execute", t.measure());

		if(ret) chan->sendMessage(cmd::value_to_message(ret.unwrap()));
		else if(chan->shouldPrintInterpErrors()) chan->sendMessage(Message(ret.error()));
	}

	static void command_chmod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: chmod <command> <permissions>
		auto cmd = arg_str.substr(0, arg_str.find(' ')).trim();
		auto perm_str = arg_str.drop(cmd.size()).trim();

		if(cmd.empty() || perm_str.empty())
			return chan->sendMessage(Message("not enough arguments to chmod"));

		// goes something like this:
		// chmod foo +3a+group+group+@discord role-@blacklist role+group with \+ plus
		auto parse_perms = [](ikura::str_view sv, const Channel* chan, PermissionSet orig) -> Result<PermissionSet> {
			uint64_t flag = 0;

			bool merge = false;
			if(!sv.empty() && sv[0] == '+')
				sv.remove_prefix(1), merge = true;

			while(sv.size() > 0)
			{
				if('0' <= sv[0] && sv[0] <= '9')
					flag = (16 * flag) + (sv[0] - '0');

				else if('a' <= sv[0] && sv[0] <= 'f')
					flag = (16 * flag) + (10 + sv[0] - 'a');

				else if('A' <= sv[0] && sv[0] <= 'F')
					flag = (16 * flag) + (10 + sv[0] - 'A');

				else
					break;

				sv.remove_prefix(1);
			}

			auto newperms = orig;
			newperms.flags = (flag | (merge ? orig.flags : 0));

			if(sv.empty())
				return newperms;

			return parse_groups(chan, sv, newperms);
		};

		if(is_builtin_command(cmd))
		{
			interpreter().perform_write([&](auto& interp) {
				auto res = parse_perms(perm_str, chan, interp.builtinCommandPermissions[cmd]);
				if(!res.has_value())
					return chan->sendMessage(Message(res.error()));

				interp.builtinCommandPermissions[cmd] = res.unwrap();
			});
		}
		else
		{
			auto command = interpreter().rlock()->findCommand(cmd);
			if(!command)
				return chan->sendMessage(Message(zpr::sprint("'%s' does not exist", cmd)));

			auto res = parse_perms(perm_str, chan, command->perms());
			if(!res.has_value())
				return chan->sendMessage(Message(res.error()));

			command->perms() = res.unwrap();
		}

		chan->sendMessage(Message(zpr::sprint("permissions for '%s' changed", cmd)));
	}

	static void command_groupadd(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: groupadd <group>
		auto grp = arg_str.substr(0, arg_str.find(' ')).trim();

		if(grp.empty())
			return chan->sendMessage(Message("not enough arguments to groupadd"));

		database().perform_write([&](auto& db) {
			auto& s = db.sharedData;
			if(auto it = s.groups.find(grp); it != s.groups.end())
				return chan->sendMessage(Message(zpr::sprint("'%s' already exists", grp)));

			db::Group g;
			g.id = s.groups.size();
			g.name = grp;

			s.groups[grp] = g;
			chan->sendMessage(Message(zpr::sprint("created group '%s'", grp)));
		});
	}

	static void command_groupdel(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: groupdel <group>
		auto grp = arg_str.substr(0, arg_str.find(' ')).trim();

		if(grp.empty())
			return chan->sendMessage(Message("not enough arguments to groupdel"));

		database().perform_write([&](auto& db) {
			auto& s = db.sharedData;
			if(auto it = s.groups.find(grp); it == s.groups.end())
				return chan->sendMessage(Message(zpr::sprint("'%s' does not exist", grp)));

			else
				s.groups.erase(it);

			chan->sendMessage(Message(zpr::sprint("removed group '%s'", grp)));
		});
	}



	static void command_usermod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: usermod <user> <groups>
		auto user = arg_str.substr(0, arg_str.find(' ')).trim();
		auto perm_str = arg_str.drop(user.size()).trim();

		if(user.empty() || perm_str.empty())
			return chan->sendMessage(Message("not enough arguments to usermod"));

		// goes something like this:
		// usermod user +group1+group2-group3

		// we're treating this like a whitelist/blacklist input (ie. +group-group), but
		// we re-interpret it as add and remove.
		auto res = parse_groups(chan, perm_str, { });
		if(!res.has_value())
			return chan->sendMessage(Message(res.error()));

		if(!res->role_whitelist.empty() || !res->role_blacklist.empty())
			return chan->sendMessage(Message(zpr::sprint("cannot modify discord roles")));

		// handle for twitch and discord separately -- but we need to somehow update in both.
		// TODO: figure out a way to update both twitch and discord here?
		if(chan->getBackend() == Backend::Twitch)
		{
			database().perform_write([&](auto& db) {
				auto& twch = db.twitchData.channels[chan->getName()];
				auto userid = twch.usernameMapping[user];
				if(userid.empty())
				{
				fail:
					return chan->sendMessage(Message(zpr::sprint("unknown user '%s'", user)));
				}

				auto twusr = twch.getUser(userid);
				if(twusr == nullptr) { zpr::println("A"); goto fail; }

				for(auto x : res->whitelist)
					add_to_list(twusr->groups, x);

				for(auto x : res->blacklist)
					remove_from_list(twusr->groups, x);
			});
		}
		else if(chan->getBackend() == Backend::Discord)
		{
			// TODO:
		}
	}




	static void command_global(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: global <name> <type>
		auto name = arg_str.substr(0, arg_str.find(' ')).trim();
		auto type_str = arg_str.drop(name.size()).trim();

		if(name.empty() || type_str.empty())
			return chan->sendMessage(Message("not enough arguments to global"));

		auto type = ast::parseType(type_str);
		if(!type)
			return chan->sendMessage(Message(zpr::sprint("invalid type '%s'", type_str)));

		interpreter().wlock()->addGlobal(name, Value::default_of(type.value()));
		chan->sendMessage(Message(zpr::sprint("added global '%s' with type '%s'", name, type.value()->str())));
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

		auto macro = new Macro(name.str(), expansion);
		macro->perms() = PermissionSet::fromFlags(DEFAULT_NEW_MACRO_PERMISSIONS);

		interpreter().wlock()->commands.emplace(name, macro);
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

		if(is_builtin_command(arg_str))
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




namespace ikura::interp
{
	static constexpr auto t_fn = Type::get_function;
	static constexpr auto t_int = Type::get_integer;
	static constexpr auto t_cmp = Type::get_complex;
	static constexpr auto t_str = Type::get_string;
	static constexpr auto t_dbl = Type::get_double;
	static constexpr auto t_map = Type::get_map;
	static constexpr auto t_char = Type::get_char;
	static constexpr auto t_bool = Type::get_bool;
	static constexpr auto t_void = Type::get_void;
	static constexpr auto t_list = Type::get_list;
	static constexpr auto t_vla  = Type::get_variadic_list;

	static Result<interp::Value> fn_markov(InterpState* fs, CmdContext& cs);

	static Result<interp::Value> fn_int_to_int(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_str_to_int(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_dbl_to_int(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_char_to_int(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_bool_to_int(InterpState* fs, CmdContext& cs);

	static Result<interp::Value> fn_str_to_str(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_int_to_str(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_dbl_to_str(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_map_to_str(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_list_to_str(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_char_to_str(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_bool_to_str(InterpState* fs, CmdContext& cs);


	static Result<interp::Value> fn_rtod(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_dtor(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_atan2(InterpState* fs, CmdContext& cs);

	static Result<interp::Value> fn_ln_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_lg_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_log_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_exp_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_abs_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_sqrt_real(InterpState* fs, CmdContext& cs);

	static Result<interp::Value> fn_ln_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_lg_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_log_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_exp_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_abs_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_sqrt_complex(InterpState* fs, CmdContext& cs);

	static Result<interp::Value> fn_sin_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_cos_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_tan_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_asin_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_acos_real(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_atan_real(InterpState* fs, CmdContext& cs);

	static Result<interp::Value> fn_sin_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_cos_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_tan_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_asin_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_acos_complex(InterpState* fs, CmdContext& cs);
	static Result<interp::Value> fn_atan_complex(InterpState* fs, CmdContext& cs);

	static auto bfn_int_to_int  = BuiltinFunction("int", t_fn(t_int(), { t_int() }), &fn_int_to_int);
	static auto bfn_str_to_int  = BuiltinFunction("int", t_fn(t_int(), { t_str() }), &fn_str_to_int);
	static auto bfn_dbl_to_int  = BuiltinFunction("int", t_fn(t_int(), { t_dbl() }), &fn_dbl_to_int);
	static auto bfn_char_to_int = BuiltinFunction("int", t_fn(t_int(), { t_char() }), &fn_char_to_int);
	static auto bfn_bool_to_int = BuiltinFunction("int", t_fn(t_int(), { t_bool() }), &fn_bool_to_int);

	static auto bfn_str_to_str  = BuiltinFunction("str", t_fn(t_str(), { t_str() }), &fn_str_to_str);
	static auto bfn_int_to_str  = BuiltinFunction("str", t_fn(t_str(), { t_int() }), &fn_int_to_str);
	static auto bfn_dbl_to_str  = BuiltinFunction("str", t_fn(t_str(), { t_dbl() }), &fn_dbl_to_str);
	static auto bfn_bool_to_str = BuiltinFunction("str", t_fn(t_str(), { t_bool() }), &fn_bool_to_str);
	static auto bfn_char_to_str = BuiltinFunction("str", t_fn(t_str(), { t_char() }), &fn_char_to_str);
	static auto bfn_list_to_str = BuiltinFunction("str", t_fn(t_str(), { t_list(t_void()) }), &fn_list_to_str);
	static auto bfn_map_to_str  = BuiltinFunction("str", t_fn(t_str(), { t_map(t_void(), t_void()) }), &fn_map_to_str);

	static auto bfn_ln_real   = BuiltinFunction("ln",   t_fn(t_dbl(), { t_dbl() }), &fn_ln_real);
	static auto bfn_lg_real   = BuiltinFunction("lg",   t_fn(t_dbl(), { t_dbl() }), &fn_lg_real);
	static auto bfn_log_real  = BuiltinFunction("log",  t_fn(t_dbl(), { t_dbl(), t_dbl() }), &fn_log_real);
	static auto bfn_exp_real  = BuiltinFunction("exp",  t_fn(t_dbl(), { t_dbl() }), &fn_exp_real);
	static auto bfn_abs_real  = BuiltinFunction("abs",  t_fn(t_dbl(), { t_dbl() }), &fn_abs_real);
	static auto bfn_sqrt_real = BuiltinFunction("sqrt", t_fn(t_dbl(), { t_dbl() }), &fn_sqrt_real);

	static auto bfn_sin_real  = BuiltinFunction("sin",  t_fn(t_dbl(), { t_dbl() }), &fn_sin_real);
	static auto bfn_cos_real  = BuiltinFunction("cos",  t_fn(t_dbl(), { t_dbl() }), &fn_cos_real);
	static auto bfn_tan_real  = BuiltinFunction("tan",  t_fn(t_dbl(), { t_dbl() }), &fn_tan_real);
	static auto bfn_asin_real = BuiltinFunction("asin", t_fn(t_dbl(), { t_dbl() }), &fn_asin_real);
	static auto bfn_acos_real = BuiltinFunction("acos", t_fn(t_dbl(), { t_dbl() }), &fn_acos_real);
	static auto bfn_atan_real = BuiltinFunction("atan", t_fn(t_dbl(), { t_dbl() }), &fn_atan_real);

	static auto bfn_ln_complex   = BuiltinFunction("ln",   t_fn(t_cmp(), { t_cmp() }), &fn_ln_complex);
	static auto bfn_lg_complex   = BuiltinFunction("lg",   t_fn(t_cmp(), { t_cmp() }), &fn_lg_complex);
	static auto bfn_log_complex  = BuiltinFunction("log",  t_fn(t_cmp(), { t_cmp(), t_cmp() }), &fn_log_complex);
	static auto bfn_exp_complex  = BuiltinFunction("exp",  t_fn(t_cmp(), { t_cmp() }), &fn_exp_complex);
	static auto bfn_abs_complex  = BuiltinFunction("abs",  t_fn(t_cmp(), { t_cmp() }), &fn_abs_complex);
	static auto bfn_sqrt_complex = BuiltinFunction("sqrt", t_fn(t_cmp(), { t_cmp() }), &fn_sqrt_complex);

	static auto bfn_sin_complex  = BuiltinFunction("sin",  t_fn(t_cmp(), { t_cmp() }), &fn_sin_complex);
	static auto bfn_cos_complex  = BuiltinFunction("cos",  t_fn(t_cmp(), { t_cmp() }), &fn_cos_complex);
	static auto bfn_tan_complex  = BuiltinFunction("tan",  t_fn(t_cmp(), { t_cmp() }), &fn_tan_complex);
	static auto bfn_asin_complex = BuiltinFunction("asin", t_fn(t_cmp(), { t_cmp() }), &fn_asin_complex);
	static auto bfn_acos_complex = BuiltinFunction("acos", t_fn(t_cmp(), { t_cmp() }), &fn_acos_complex);
	static auto bfn_atan_complex = BuiltinFunction("atan", t_fn(t_cmp(), { t_cmp() }), &fn_atan_complex);

	static std::unordered_map<std::string, FunctionOverloadSet> builtin_overloaded_fns = {
		{
			"int", FunctionOverloadSet("int", {
				&bfn_int_to_int, &bfn_str_to_int, &bfn_dbl_to_int, &bfn_bool_to_int, &bfn_char_to_int,
			})
		},

		{
			"str", FunctionOverloadSet("str", {
				&bfn_str_to_str, &bfn_int_to_str, &bfn_dbl_to_str, &bfn_bool_to_str, &bfn_char_to_str,
				&bfn_list_to_str, &bfn_map_to_str,
			})
		},

		{ "ln",   FunctionOverloadSet("ln",   { &bfn_ln_real,   &bfn_ln_complex }) },
		{ "lg",   FunctionOverloadSet("lg",   { &bfn_lg_real,   &bfn_lg_complex }) },
		{ "log",  FunctionOverloadSet("log",  { &bfn_log_real,  &bfn_log_complex }) },
		{ "exp",  FunctionOverloadSet("exp",  { &bfn_exp_real,  &bfn_exp_complex }) },
		{ "abs",  FunctionOverloadSet("abs",  { &bfn_abs_real,  &bfn_abs_complex }) },
		{ "sqrt", FunctionOverloadSet("sqrt", { &bfn_sqrt_real, &bfn_sqrt_complex }) },

		{ "sin",  FunctionOverloadSet("sin",  { &bfn_sin_real,  &bfn_sin_complex }) },
		{ "cos",  FunctionOverloadSet("cos",  { &bfn_cos_real,  &bfn_cos_complex }) },
		{ "tan",  FunctionOverloadSet("tan",  { &bfn_tan_real,  &bfn_tan_complex }) },
		{ "asin", FunctionOverloadSet("asin", { &bfn_asin_real, &bfn_asin_complex }) },
		{ "acos", FunctionOverloadSet("acos", { &bfn_acos_real, &bfn_acos_complex }) },
		{ "atan", FunctionOverloadSet("atan", { &bfn_atan_real, &bfn_atan_complex }) },
	};

	static std::unordered_map<std::string, BuiltinFunction> builtin_fns = {
		{ "atan2",  BuiltinFunction("atan2",  t_fn(t_dbl(), { t_dbl(), t_dbl() }), &fn_atan2) },
		{ "rtod",   BuiltinFunction("rtod",   t_fn(t_dbl(), { t_dbl() }), &fn_rtod) },
		{ "dtor",   BuiltinFunction("dtor",   t_fn(t_dbl(), { t_dbl() }), &fn_dtor) },
		{ "__builtin_markov", BuiltinFunction("__builtin_markov", t_fn(t_list(t_str()), { t_vla(t_str()) }), &fn_markov) },
	};


	Command* getBuiltinFunction(ikura::str_view name)
	{
		if(auto it = builtin_fns.find(name.str()); it != builtin_fns.end())
			return &it->second;

		if(auto it = builtin_overloaded_fns.find(name.str()); it != builtin_overloaded_fns.end())
			return &it->second;

		return nullptr;
	}




	static int get_overload_dist(const std::vector<Type::Ptr>& target, const std::vector<Type::Ptr>& given)
	{
		int cost = 0;
		auto target_size = target.size();
		if(!target.empty() && target.back()->is_variadic_list())
			target_size--;

		auto cnt = std::min(target_size, given.size());

		// if there are no variadics involved, you failed if the counts are different.
		if(target.empty() || !target.back()->is_variadic_list())
			if(target.size() != given.size())
				return -1;

		// make sure the normal args are correct first
		for(size_t i = 0; i < cnt; i++)
		{
			int k = given[i]->get_cast_dist(target[i]);
			if(k == -1) return -1;
			else        cost += k;
		}

		if(target_size != target.size())
		{
			// we got variadics.
			auto vla = target.back();
			auto elm = vla->elm_type();

			// the cost of doing business.
			cost += 10;
			for(size_t i = cnt; i < given.size(); i++)
			{
				int k = given[i]->get_cast_dist(elm);
				if(k == -1) return -1;
				else        cost += k;
			}
		}

		return cost;
	}

	Result<interp::Value> BuiltinFunction::run(InterpState* fs, CmdContext& cs) const
	{
		const auto& given = cs.macro_args;
		auto target = this->signature->arg_types();

		auto target_size = target.size();
		if(!target.empty() && target.back()->is_variadic_list())
			target_size--;

		auto cnt = std::min(target_size, given.size());

		// if there are no variadics involved, you failed if the counts are different.
		if((target.empty() || !target.back()->is_variadic_list()) && target.size() != given.size())
		{
			return zpr::sprint("call to '%s' with wrong number of arguments (expected %zu, found %zu)",
				this->name, target.size(), cs.macro_args.size());
		}

		auto type_mismatch = [](size_t i, const Type::Ptr& exp, const Type::Ptr& got) -> Result<interp::Value> {
			return zpr::sprint("argument %zu: type mismatch, expected '%s', found '%s'",
				i, exp->str(), got->str());
		};

		std::vector<Value> final_args;

		// make sure the normal args are correct first
		for(size_t i = 0; i < cnt; i++)
		{
			auto tmp = given[i].cast_to(target[i]);
			if(!tmp)
				return type_mismatch(i, target[i], given[i].type());

			final_args.push_back(tmp.value());
		}

		if(target_size != target.size())
		{
			// we got variadics.
			assert(target.back()->is_variadic_list());
			auto elm = target.back()->elm_type();

			std::vector<Value> vla;

			// the cost of doing business.
			for(size_t i = cnt; i < given.size(); i++)
			{
				auto tmp = given[i].cast_to(elm);
				if(!tmp)
					return type_mismatch(i, elm, given[i].type());

				vla.push_back(tmp.value());
			}

			final_args.push_back(Value::of_variadic_list(elm, vla));
		}

		CmdContext params = cs;
		params.macro_args = final_args;

		return this->action(fs, params);
	}

	Result<interp::Value> FunctionOverloadSet::run(InterpState* fs, CmdContext& cs) const
	{
		int score = INT_MAX;
		Command* best = 0;

		std::vector<Type::Ptr> arg_types;
		for(const auto& a : cs.macro_args)
			arg_types.push_back(a.type());

		for(auto cand : this->functions)
		{
			auto cost = get_overload_dist(cand->getSignature()->arg_types(), arg_types);
			if(cost == -1)
			{
				continue;
			}
			else if(cost < score)
			{
				score = cost;
				best = cand;
			}
		}

		if(!best)
		{
			return zpr::sprint("no matching function for call to '%s'", this->name);
		}

		// TODO: need to do casting here
		return best->run(fs, cs);
	}

	static Result<interp::Value> fn_markov(InterpState* fs, CmdContext& cs)
	{
		std::vector<std::string> seeds;
		if(!cs.macro_args.empty() && cs.macro_args[0].is_list())
		{
			for(const auto& v : cs.macro_args[0].get_list())
				seeds.push_back(v.raw_str());
		}

		return cmd::message_to_value(markov::generateMessage(seeds));
	}










	static Result<interp::Value> fn_ln_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::log(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_lg_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::log10(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_log_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.size() != 2 || !(cs.macro_args[0].is_double() && cs.macro_args[1].is_double()))
			return zpr::sprint("invalid argument");

		// change of base
		return Value::of_double(std::log(cs.macro_args[1].get_double()) / std::log(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_exp_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::exp(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_abs_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::abs(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_sqrt_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::sqrt(cs.macro_args[0].get_double()));
	}




	static Result<interp::Value> fn_ln_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::log(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_lg_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::log10(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_log_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.size() != 2 || !(cs.macro_args[0].is_complex() && cs.macro_args[1].is_complex()))
			return zpr::sprint("invalid argument");

		// change of base
		return Value::of_complex(std::log(cs.macro_args[1].get_complex()) / std::log(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_exp_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::exp(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_abs_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::abs(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_sqrt_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::sqrt(cs.macro_args[0].get_complex()));
	}







	static Result<interp::Value> fn_sin_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::sin(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_cos_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::cos(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_tan_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::tan(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_asin_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::asin(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_acos_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::acos(cs.macro_args[0].get_double()));
	}

	static Result<interp::Value> fn_atan_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::atan(cs.macro_args[0].get_double()));
	}






	static Result<interp::Value> fn_sin_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::sin(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_cos_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::cos(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_tan_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::tan(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_asin_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::asin(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_acos_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::acos(cs.macro_args[0].get_complex()));
	}

	static Result<interp::Value> fn_atan_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::atan(cs.macro_args[0].get_complex()));
	}








	constexpr double PI = 3.14159265358979323846264338327950;
	static Result<interp::Value> fn_rtod(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(180.0 * (cs.macro_args[0].get_double() / PI));
	}

	static Result<interp::Value> fn_dtor(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(PI * (cs.macro_args[0].get_double() / 180.0));
	}

	static Result<interp::Value> fn_atan2(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.size() != 2 || !(cs.macro_args[0].is_double() && cs.macro_args[1].is_double()))
			return zpr::sprint("invalid arguments");

		return Value::of_double(::atan2(cs.macro_args[0].get_double(), cs.macro_args[1].get_double()));
	}















	static Result<interp::Value> fn_int_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_integer())
			return zpr::sprint("invalid argument");

		return cs.macro_args[0];
	}

	static Result<interp::Value> fn_str_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_string())
			return zpr::sprint("invalid argument");

		auto ret = util::stoi(cs.macro_args[0].raw_str());
		if(!ret) return zpr::sprint("invalid argument");

		return Value::of_integer(ret.value());
	}

	static Result<interp::Value> fn_dbl_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_double())
			return zpr::sprint("invalid argument");

		return Value::of_integer((int64_t) cs.macro_args[0].get_double());
	}

	static Result<interp::Value> fn_char_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_char())
			return zpr::sprint("invalid argument");

		return Value::of_integer((int64_t) cs.macro_args[0].get_char());
	}

	static Result<interp::Value> fn_bool_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_bool())
			return zpr::sprint("invalid argument");

		return Value::of_integer(cs.macro_args[0].get_bool() ? 1 : 0);
	}




	static Result<interp::Value> fn_str_to_str(InterpState* fs, CmdContext& cs)
	{
		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_string())
			return zpr::sprint("invalid argument");

		return cs.macro_args[0];
	}

	static Result<interp::Value> fn_int_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_integer())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.macro_args[0].str());
	}

	static Result<interp::Value> fn_dbl_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_double())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.macro_args[0].str());
	}

	static Result<interp::Value> fn_map_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_map())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.macro_args[0].str());
	}

	static Result<interp::Value> fn_list_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_list())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.macro_args[0].str());
	}

	static Result<interp::Value> fn_char_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_char())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.macro_args[0].str());
	}

	static Result<interp::Value> fn_bool_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.macro_args.empty() || !cs.macro_args[0].type()->is_bool())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.macro_args[0].str());
	}





























	FunctionOverloadSet::FunctionOverloadSet(std::string name, std::vector<Command*> fns)
		: Command(name, Type::get_macro_function()), functions(std::move(fns)) { }

	void FunctionOverloadSet::serialise(Buffer& buf) const { assert(!"not supported"); }
	void FunctionOverloadSet::deserialise(Span& buf) { assert(!"not supported"); }


	BuiltinFunction::BuiltinFunction(std::string name, Type::Ptr type,
		Result<interp::Value> (*action)(InterpState*, CmdContext&)) : Command(std::move(name), std::move(type)), action(action) { }

	void BuiltinFunction::serialise(Buffer& buf) const { assert(!"not supported"); }
	void BuiltinFunction::deserialise(Span& buf) { assert(!"not supported"); }
}

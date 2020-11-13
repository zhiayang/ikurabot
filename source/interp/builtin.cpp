// builtin.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "zfu.h"
#include "ast.h"
#include "cmd.h"
#include "perms.h"
#include "timer.h"
#include "markov.h"
#include "synchro.h"

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
	static void command_defun(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_global(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_usermod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_showmod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_listcmds(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_groupadd(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_groupdel(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_listgroups(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_stop_timer(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);
	static void command_start_timer(CmdContext& cs, const Channel* chan, ikura::str_view arg_str);

	bool is_builtin_command(ikura::str_view x)
	{
		return zfu::match(x, "def", "eval", "show", "redef", "undef", "chmod", "global",
			"usermod", "groupadd", "groupdel", "groups", "showmod", "defun", "listcmds",
			"stop_timer", "start_timer"
		);
	}

	// tsl::robin_map doesn't let us do this for some reason, so just fall back to std::unordered_map.
	static std::unordered_map<std::string, void (*)(CmdContext&, const Channel*, ikura::str_view)> builtin_cmds = {
		{ "chmod",      command_chmod       },
		{ "eval",       command_eval        },
		{ "global",     command_global      },
		{ "def",        command_def         },
		{ "redef",      command_redef       },
		{ "undef",      command_undef       },
		{ "show",       command_show        },
		{ "usermod",    command_usermod     },
		{ "groupadd",   command_groupadd    },
		{ "groupdel",   command_groupdel    },
		{ "groups",     command_listgroups  },
		{ "showmod",    command_showmod     },
		{ "defun",      command_defun       },
		{ "listcmds",   command_listcmds    },
		{ "stop_timer", command_stop_timer  },
		{ "start_timer",command_start_timer },
	};

	/* not sure if this is a good idea...
	static constexpr uint64_t DEFAULT_NEW_MACRO_PERMISSIONS
		= permissions::OWNER
		| permissions::BROADCASTER
		| permissions::VIP
		| permissions::SUBSCRIBER
		| permissions::MODERATOR;
	*/

	static constexpr uint64_t DEFAULT_NEW_MACRO_PERMISSIONS = permissions::EVERYONE;


	bool run_builtin_command(CmdContext& cs, const Channel* chan, ikura::str_view cmd_str, ikura::str_view arg_str)
	{
		auto denied = [&]() -> bool {
			lg::warn("cmd", "user '{}' tried to execute command '{}' with insufficient permissions",
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


	static void command_eval(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: eval <expr>
		auto t = ikura::timer();

		auto ret = interpreter().wlock()->evaluateExpr(arg_str, cs);
		lg::log("interp", "command took {.3f} ms to execute", t.measure());

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

		if(is_builtin_command(cmd))
		{
			interpreter().perform_write([&](auto& interp) {
				auto res = perms::parse(chan, perm_str, interp.builtinCommandPermissions[cmd]);
				if(!res.has_value())
					return chan->sendMessage(Message(res.error()));

				interp.builtinCommandPermissions[cmd] = res.unwrap();
			});
		}
		else
		{
			auto command = interpreter().rlock()->findCommand(cmd);
			if(!command)
				return chan->sendMessage(Message(zpr::sprint("'{}' does not exist", cmd)));

			auto res = perms::parse(chan, perm_str, command->perms());
			if(!res.has_value())
				return chan->sendMessage(Message(res.error()));

			command->perms() = res.unwrap();
		}

		chan->sendMessage(Message(zpr::sprint("permissions for '{}' changed", cmd)));
	}


	static void command_showmod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: showmod <command>
		auto cmd = arg_str.substr(0, arg_str.find(' ')).trim();

		if(cmd.empty())
			return chan->sendMessage(Message("not enough arguments to showmod"));

		PermissionSet perms;
		if(is_builtin_command(cmd))
		{
			perms = interpreter().rlock()->builtinCommandPermissions.at(cmd);
		}
		else
		{
			auto command = interpreter().rlock()->findCommand(cmd);
			if(!command)
				return chan->sendMessage(Message(zpr::sprint("'{}' does not exist", cmd)));

			perms = command->perms();
		}

		chan->sendMessage(Message(perms::print(chan, perms)));
	}

	static void command_listgroups(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: groups
		auto grps = database().map_read([](auto& db) -> auto {
			auto& grps = db.sharedData.getGroups();

			std::vector<const db::Group*> groups;
			for(const auto& [ n, grp ] : grps)
				groups.push_back(&grp);

			return groups;
		});

		auto list = zfu::listToString(grps, [](const db::Group* grp) -> auto {
			return zpr::sprint("({}, id: {}, cnt: {})", grp->name, grp->id, grp->members.size());
		}, /* braces: */ false);

		chan->sendMessage(Message(list));
	}

	static void command_groupadd(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: groupadd <group>
		auto grp = arg_str.substr(0, arg_str.find(' ')).trim();

		if(grp.empty())
			return chan->sendMessage(Message("not enough arguments to groupadd"));

		database().perform_write([&](auto& db) {
			auto& s = db.sharedData;

			if(s.addGroup(grp))
				chan->sendMessage(Message(zpr::sprint("created group '{}'", grp)));
			else
				return chan->sendMessage(Message(zpr::sprint("'{}' already exists", grp)));
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

			if(s.removeGroup(grp))
				chan->sendMessage(Message(zpr::sprint("removed group '{}'", grp)));
			else
				chan->sendMessage(Message(zpr::sprint("'{}' does not exist", grp)));
		});
	}



	static void command_usermod(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: usermod <user> <groups>
		auto user = arg_str.take(arg_str.find_first_of("+-")).trim();
		auto perm_str = arg_str.drop(user.size()).trim();

		if(user.empty())
		{
			return chan->sendMessage(Message("missing user"));
		}
		else if(perm_str.empty())
		{
			auto str = perms::printUserGroups(chan, user);
			if(!str.has_value())
				return chan->sendMessage(Message("error"));

			return chan->sendMessage(Message(zpr::sprint("member of: {}", str.value())));
		}

		if(perms::updateUserPermissions(chan, user, perm_str))
			chan->sendMessage(Message(zpr::sprint("updated groups")));
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
			return chan->sendMessage(Message(zpr::sprint("invalid type '{}'", type_str)));

		auto res = interpreter().wlock()->addGlobal(name, Value::default_of(type.value()));
		if(!res) chan->sendMessage(Message(res.error()));
		else     chan->sendMessage(Message(zpr::sprint("added global '{}' with type '{}'",
					name, type.value()->str())));
	}

	static bool internal_def(const Channel* chan, ikura::str_view name, Command* thing)
	{
		if(interpreter().rlock()->findCommand(name) != nullptr)
		{
			chan->sendMessage(Message(zpr::sprint("'{}' is already defined", name)));
			return false;
		}

		interpreter().wlock()->commands.emplace(name, thing);
		chan->sendMessage(Message(zpr::sprint("defined '{}'", name)));
		return true;
	}

	static void command_def(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: def <name> expansion...
		auto name = arg_str.substr(0, arg_str.find(' ')).trim();
		auto expansion = arg_str.drop(name.size()).trim();

		if(name.empty())        return chan->sendMessage(Message("not enough arguments to 'def'"));
		if(expansion.empty())   return chan->sendMessage(Message("'def' expansion cannot be empty"));

		auto macro = new Macro(name.str(), expansion);
		macro->perms() = PermissionSet::fromFlags(DEFAULT_NEW_MACRO_PERMISSIONS);

		if(!internal_def(chan, name, macro))
			delete macro;
	}

	static void command_redef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: def <name> expansion...
		auto name = arg_str.substr(0, arg_str.find(' ')).trim();
		auto expansion = arg_str.drop(name.size()).trim();

		if(name.empty())        return chan->sendMessage(Message("not enough arguments to 'redef'"));
		if(expansion.empty())   return chan->sendMessage(Message("'redef' expansion cannot be empty"));

		auto existing = interpreter().rlock()->findCommand(name);
		if(!existing)
			return chan->sendMessage(Message(zpr::sprint("'{}' does not exist", name)));

		auto macro = dynamic_cast<Macro*>(existing.get());
		if(!macro)
			return chan->sendMessage(Message(zpr::sprint("'{}' is not a macro", name)));

		macro->setCode(expansion);
		chan->sendMessage(Message(zpr::sprint("redefined '{}'", name)));
	}

	static void command_undef(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: undef <name>
		if(arg_str.find(' ') != std::string::npos || arg_str.empty())
			return chan->sendMessage(Message("'undef' takes exactly 1 argument"));

		std::string err;
		auto done = interpreter().wlock()->removeCommandOrAlias(arg_str);
		if(!done)
		{
			if(auto res = interpreter().wlock()->removeGlobal(arg_str); !res)
				err = res.error();
		}

		chan->sendMessage(Message(
			err.empty() ? zpr::sprint("removed '{}'", arg_str) : err
		));
	}

	static void command_show(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: show <name>
		if(arg_str.find(' ') != std::string::npos || arg_str.empty())
			return chan->sendMessage(Message("'show' takes exactly 1 argument"));

		if(is_builtin_command(arg_str))
			return chan->sendMessage(Message(zpr::sprint("'{}' is a builtin command", arg_str)));

		auto cmd = interpreter().rlock()->findCommand(arg_str);
		if(cmd == nullptr)
			return chan->sendMessage(Message(zpr::sprint("'{}' does not exist", arg_str)));

		if(auto macro = dynamic_cast<Macro*>(cmd.get()))
		{
			Message msg;
			msg.add(zpr::sprint("'{}' is defined as: ", arg_str));

			auto code = macro->getCode();
			for(const auto& c : code)
				msg.add(c);

			return chan->sendMessage(msg);
		}
		else if(auto function = dynamic_cast<Function*>(cmd.get()))
		{
			return chan->sendMessage(Message(zpr::sprint("'{}' is defined as: {}",
				arg_str, function->getDefinition()->str())));
		}
		else
		{
			if(is_builtin_command(arg_str) || getBuiltinFunction(arg_str))
				return chan->sendMessage(Message(zpr::sprint("'{}' is builtin", arg_str)));

			return chan->sendMessage(Message(zpr::sprint("'{}' cannot be shown", arg_str))
				.add(Emote("monkaTOS")));
		}
	}


	static void command_defun(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: defun <name> <type> <body>
		// but the name-type-body is all handled by the parser.
		auto f = ast::parseFuncDefn(arg_str);
		if(!f) return chan->sendMessage(Message(f.error()));

		auto& def = f.unwrap();
		auto name = def->name;

		auto func = new Function(def);
		if(!internal_def(chan, name, func))
			delete func;
	}


	static void command_listcmds(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: listcmds
		auto list = interpreter().map_read([&](auto& interp) {
			std::vector<std::string> cmds;
			for(const auto& [ name, cmd ] : interp.commands)
				cmds.push_back(cmd->getName());

			return zfu::listToString(cmds, zfu::identity(), /* braces: */ false);
		});
		chan->sendMessage(Message(list));
	}

	// silly
	static void command_start_timer(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		// syntax: start_timer [seconds]
		// if no seconds, counts up; else counts down

		int seconds = 0;
		if(auto [ x, xs ] = util::bisect(arg_str, ' '); !x.empty())
			seconds = util::stoi(x).value_or(0);


		if(auto dc = dynamic_cast<const discord::Channel*>(chan); dc != nullptr)
			dc->startTimer(seconds);

		else
			chan->sendMessage(Message("timers only work on discord"));
	}

	static void command_stop_timer(CmdContext& cs, const Channel* chan, ikura::str_view arg_str)
	{
		if(auto dc = dynamic_cast<const discord::Channel*>(chan); dc != nullptr)
			dc->stopTimer();
	}
}




namespace ikura::interp
{
	static constexpr auto t_fn = Type::get_function;
	static constexpr auto t_gen = Type::get_generic;
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
	static Result<interp::Value> fn_dismantle(InterpState* fs, CmdContext& cs);

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
			"int", FunctionOverloadSet("int", t_fn(t_int(), { t_gen("T", 0) }), {
				&bfn_int_to_int, &bfn_str_to_int, &bfn_dbl_to_int, &bfn_bool_to_int, &bfn_char_to_int,
			})
		},

		{
			"str", FunctionOverloadSet("str", t_fn(t_str(), { t_gen("T", 0) }), {
				&bfn_str_to_str, &bfn_int_to_str, &bfn_dbl_to_str, &bfn_bool_to_str, &bfn_char_to_str,
				&bfn_list_to_str, &bfn_map_to_str,
			})
		},

		{ "ln",   FunctionOverloadSet("ln",   t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_ln_real,   &bfn_ln_complex }) },
		{ "lg",   FunctionOverloadSet("lg",   t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_lg_real,   &bfn_lg_complex }) },
		{ "log",  FunctionOverloadSet("log",  t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_log_real,  &bfn_log_complex }) },
		{ "exp",  FunctionOverloadSet("exp",  t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_exp_real,  &bfn_exp_complex }) },
		{ "abs",  FunctionOverloadSet("abs",  t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_abs_real,  &bfn_abs_complex }) },
		{ "sqrt", FunctionOverloadSet("sqrt", t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_sqrt_real, &bfn_sqrt_complex }) },

		{ "sin",  FunctionOverloadSet("sin",  t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_sin_real,  &bfn_sin_complex }) },
		{ "cos",  FunctionOverloadSet("cos",  t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_cos_real,  &bfn_cos_complex }) },
		{ "tan",  FunctionOverloadSet("tan",  t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_tan_real,  &bfn_tan_complex }) },
		{ "asin", FunctionOverloadSet("asin", t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_asin_real, &bfn_asin_complex }) },
		{ "acos", FunctionOverloadSet("acos", t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_acos_real, &bfn_acos_complex }) },
		{ "atan", FunctionOverloadSet("atan", t_fn(t_gen("R", 0), { t_gen("T", 0) }), { &bfn_atan_real, &bfn_atan_complex }) },
	};

	static std::unordered_map<std::string, BuiltinFunction> builtin_fns = {
		{ "atan2",  BuiltinFunction("atan2",  t_fn(t_dbl(), { t_dbl(), t_dbl() }), &fn_atan2) },
		{ "rtod",   BuiltinFunction("rtod",   t_fn(t_dbl(), { t_dbl() }), &fn_rtod) },
		{ "dtor",   BuiltinFunction("dtor",   t_fn(t_dbl(), { t_dbl() }), &fn_dtor) },
		{ "__builtin_markov", BuiltinFunction("__builtin_markov", t_fn(t_list(t_str()), { t_vla(t_str()) }), &fn_markov) },
		{ "__builtin_dismantle", BuiltinFunction("__builtin_dismantle", t_fn(t_list(t_str()), { t_vla(t_str()) }), &fn_dismantle) }
	};


	Command* getBuiltinFunction(ikura::str_view name)
	{
		if(auto it = builtin_fns.find(name.str()); it != builtin_fns.end())
			return &it->second;

		if(auto it = builtin_overloaded_fns.find(name.str()); it != builtin_overloaded_fns.end())
			return &it->second;

		return nullptr;
	}



	Result<interp::Value> BuiltinFunction::run(InterpState* fs, CmdContext& cs) const
	{
		auto res = coerceTypesForFunctionCall(this->name, this->signature, cs.arguments);
		if(!res) return res.error();

		CmdContext params = cs;
		params.arguments = res.unwrap();

		return this->action(fs, params);
	}

	Result<interp::Value> FunctionOverloadSet::run(InterpState* fs, CmdContext& cs) const
	{
		int score = INT_MAX;
		Command* best = 0;

		std::vector<Type::Ptr> arg_types;
		for(const auto& a : cs.arguments)
			arg_types.push_back(a.type());

		for(auto cand : this->functions)
		{
			auto cost = getFunctionOverloadDistance(cand->getSignature()->arg_types(), arg_types);
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
			return zpr::sprint("no matching function for call to '{}'", this->name);
		}

		return best->run(fs, cs);
	}

	static Result<interp::Value> fn_markov(InterpState* fs, CmdContext& cs)
	{
		std::vector<std::string> seeds;
		if(!cs.arguments.empty() && cs.arguments[0].is_list())
		{
			for(const auto& v : cs.arguments[0].get_list())
				seeds.push_back(v.raw_str());
		}

		return cmd::message_to_value(markov::generateMessage(seeds));
	}

	static Result<Value> fn_dismantle(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_list())
			return zpr::sprint("invalid argument");

		auto ret = Value::of_list(cs.arguments[0].type()->elm_type(), cs.arguments[0].get_list());
		ret.set_flags(ret.flags() | Value::FLAG_DISMANTLE_LIST);

		lg::warn("cmd", "user '{}' tried to dismantle", cs.callername);

		return ret;
	}










	static Result<interp::Value> fn_ln_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::log(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_lg_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::log10(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_log_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.size() != 2 || !(cs.arguments[0].is_double() && cs.arguments[1].is_double()))
			return zpr::sprint("invalid argument");

		// change of base
		return Value::of_double(std::log(cs.arguments[1].get_double()) / std::log(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_exp_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::exp(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_abs_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::abs(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_sqrt_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(std::sqrt(cs.arguments[0].get_double()));
	}




	static Result<interp::Value> fn_ln_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::log(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_lg_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::log10(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_log_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.size() != 2 || !(cs.arguments[0].is_complex() && cs.arguments[1].is_complex()))
			return zpr::sprint("invalid argument");

		// change of base
		return Value::of_complex(std::log(cs.arguments[1].get_complex()) / std::log(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_exp_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::exp(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_abs_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::abs(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_sqrt_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::sqrt(cs.arguments[0].get_complex()));
	}







	static Result<interp::Value> fn_sin_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::sin(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_cos_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::cos(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_tan_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::tan(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_asin_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::asin(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_acos_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::acos(cs.arguments[0].get_double()));
	}

	static Result<interp::Value> fn_atan_real(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(::atan(cs.arguments[0].get_double()));
	}






	static Result<interp::Value> fn_sin_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::sin(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_cos_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::cos(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_tan_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::tan(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_asin_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::asin(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_acos_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::acos(cs.arguments[0].get_complex()));
	}

	static Result<interp::Value> fn_atan_complex(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_complex())
			return zpr::sprint("invalid argument");

		return Value::of_complex(std::atan(cs.arguments[0].get_complex()));
	}








	constexpr double PI = 3.14159265358979323846264338327950;
	static Result<interp::Value> fn_rtod(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(180.0 * (cs.arguments[0].get_double() / PI));
	}

	static Result<interp::Value> fn_dtor(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].is_double())
			return zpr::sprint("invalid argument");

		return Value::of_double(PI * (cs.arguments[0].get_double() / 180.0));
	}

	static Result<interp::Value> fn_atan2(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.size() != 2 || !(cs.arguments[0].is_double() && cs.arguments[1].is_double()))
			return zpr::sprint("invalid arguments");

		return Value::of_double(::atan2(cs.arguments[0].get_double(), cs.arguments[1].get_double()));
	}















	static Result<interp::Value> fn_int_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].type()->is_integer())
			return zpr::sprint("invalid argument");

		return cs.arguments[0];
	}

	static Result<interp::Value> fn_str_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].type()->is_string())
			return zpr::sprint("invalid argument");

		auto ret = util::stoi(cs.arguments[0].raw_str());
		if(!ret) return zpr::sprint("invalid argument");

		return Value::of_integer(ret.value());
	}

	static Result<interp::Value> fn_dbl_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].type()->is_double())
			return zpr::sprint("invalid argument");

		return Value::of_integer((int64_t) cs.arguments[0].get_double());
	}

	static Result<interp::Value> fn_char_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].type()->is_char())
			return zpr::sprint("invalid argument");

		return Value::of_integer((int64_t) cs.arguments[0].get_char());
	}

	static Result<interp::Value> fn_bool_to_int(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].type()->is_bool())
			return zpr::sprint("invalid argument");

		return Value::of_integer(cs.arguments[0].get_bool() ? 1 : 0);
	}




	static Result<interp::Value> fn_str_to_str(InterpState* fs, CmdContext& cs)
	{
		if(cs.arguments.empty() || !cs.arguments[0].type()->is_string())
			return zpr::sprint("invalid argument");

		return cs.arguments[0];
	}

	static Result<interp::Value> fn_int_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.arguments.empty() || !cs.arguments[0].type()->is_integer())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.arguments[0].str());
	}

	static Result<interp::Value> fn_dbl_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.arguments.empty() || !cs.arguments[0].type()->is_double())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.arguments[0].str());
	}

	static Result<interp::Value> fn_map_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.arguments.empty() || !cs.arguments[0].type()->is_map())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.arguments[0].str());
	}

	static Result<interp::Value> fn_list_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.arguments.empty() || !cs.arguments[0].type()->is_list())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.arguments[0].str());
	}

	static Result<interp::Value> fn_char_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.arguments.empty() || !cs.arguments[0].type()->is_char())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.arguments[0].str());
	}

	static Result<interp::Value> fn_bool_to_str(InterpState* fs, CmdContext& cs)
	{

		if(cs.arguments.empty() || !cs.arguments[0].type()->is_bool())
			return zpr::sprint("invalid argument");

		return Value::of_string(cs.arguments[0].str());
	}




























	FunctionOverloadSet::~FunctionOverloadSet() {  }
	FunctionOverloadSet::FunctionOverloadSet(std::string name, Type::Ptr sig, std::vector<BuiltinFunction*> fns)
		: Command(name), signature(std::move(sig)), functions(std::move(fns)) { }

	Type::Ptr FunctionOverloadSet::getSignature() const
	{
		return this->signature;
	}


	BuiltinFunction::BuiltinFunction(std::string name, Type::Ptr type, Result<interp::Value> (*action)(InterpState*, CmdContext&))
		: Command(std::move(name)), signature(std::move(type)), action(action) { }

	Type::Ptr BuiltinFunction::getSignature() const { return this->signature; }






	void FunctionOverloadSet::serialise(Buffer& buf) const { assert(!"not supported"); }
	void FunctionOverloadSet::deserialise(Span& buf) { assert(!"not supported"); }

	void BuiltinFunction::serialise(Buffer& buf) const { assert(!"not supported"); }
	void BuiltinFunction::deserialise(Span& buf) { assert(!"not supported"); }
}

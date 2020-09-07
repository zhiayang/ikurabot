// message.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

#include "irc.h"
#include "cmd.h"
#include "timer.h"
#include "markov.h"

namespace ikura::irc
{
	static void handle_ctcp(IRCServer* srv, const IRCMessage& msg);
	static void handle_msg(IRCServer* srv, const IRCMessage& msg);

	void IRCServer::processMessage(ikura::str_view sv)
	{
		auto sys = zpr::sprint("irc/%s", this->name);
		auto ret = parseMessage(sv);
		if(!ret.has_value())
		{
			lg::warn(sys, "invalid irc message");
			return;
		}

		auto msg = ret.value();
		if(msg.command == "PING")
		{
			lg::dbglog(sys, "ping-pong");
			this->sendRawMessage(zpr::sprint("PONG %s", msg.params.size() > 0 ? msg.params[0] : ""));
		}
		else if(msg.command == "JOIN")
		{
			if(msg.params.size() != 1)
				return lg::error(sys, "malformed JOIN: %s", sv);

			if(msg.nick == this->nickname)
				lg::log(sys, "joined %s", msg.params[0]);
		}
		else if(msg.command == "NOTICE")
		{
			auto channel = msg.params[0];
			auto message = msg.params[1];

			lg::log(sys, "notice in %s: %s", channel, message);
		}
		else if(msg.command == "NICK")
		{
			lg::log(sys, "nickname change: %s -> %s", msg.nick, msg.params[0]);
		}
		else if(msg.command == "PRIVMSG")
		{
			if(msg.params.size() < 2)
				return lg::error(sys, "malformed: less than 2 params for PRIVMSG");

			// special-case ACTION.
			if(msg.isCTCP && msg.ctcpCommand != "ACTION")
				return handle_ctcp(this, msg);

			else
				return handle_msg(this, msg);
		}
		else if(msg.command == "PART" || msg.command == "MODE" || msg.command == "QUIT")
		{
			// meh.
		}
		else if(zfu::match(msg.command[0], '0', '1', '2', '3'))
		{
			// stupid startup messages.
		}
		else
		{
			lg::log(sys, "unhandled irc command '%s' (msg = %s)", msg.command, sv);
		}
	}


	static void handle_msg(IRCServer* srv, const IRCMessage& msg)
	{
		auto time = timer();

		auto sys = zpr::sprint("irc/%s", srv->name);

		auto username = msg.user;

		// just dumb irc things.
		if(username.find('~') == 0)
			username.remove_prefix(1);

		// ignore self
		if(username == srv->username)
			return;

		if(srv->ignoredUsers.contains(username) || srv->ignoredUsers.contains(msg.nick))
			return;

		// check the server
		if(msg.params[0].find("#") == 0)
		{
			auto channel = msg.params[0];
			auto message = msg.params[1];

			bool ran_cmd = false;
			if(!srv->channels[channel].shouldLurk() && !msg.isCTCP)
				ran_cmd = cmd::processMessage(username, username, &srv->channels[channel], message, /* enablePings: */ true);

			// don't train on commands. (no emotes btw)
			if(!ran_cmd)
				markov::process(message, { });

			srv->logMessage(util::getMillisecondTimestamp(), username, msg.nick, &srv->channels[channel], message, ran_cmd);

			lg::log("msg", "irc/%s: (%.2f ms) <%s> %s", channel, time.measure(), msg.nick, message);
		}
		else
		{
			if(msg.params[0] != srv->nickname)
				lg::warn(sys, "received privmsg that wasn't directed at us");

			// this is a pm... just print it out
			lg::log("privmsg", "%s: <%s> %s", sys, msg.nick, msg.params.back());
		}
	}


	static void handle_ctcp(IRCServer* srv, const IRCMessage& msg)
	{
		auto sys = zpr::sprint("irc/%s", srv->name);

		if(msg.ctcpCommand == "VERSION")
		{
			lg::log(sys, "replied to ctcp VERSION");
			srv->sendRawMessage(zpr::sprint("NOTICE %s :\x01VERSION %s\x01", msg.nick, "ikura ver-0.1.0"));
		}
		else if(msg.ctcpCommand == "CLIENTINFO")
		{
			lg::log(sys, "replied to ctcp CLIENTINFO");
			srv->sendRawMessage(zpr::sprint("NOTICE %s :\x01CLIENTINFO CLIENTINFO ACTION VERSION PING TIME\x01", msg.nick));
		}
		else if(msg.ctcpCommand == "PING")
		{
			lg::log(sys, "replied to ctcp PING");
			srv->sendRawMessage(zpr::sprint("NOTICE %s :\x01PING %s\x01", msg.nick, msg.params[0]));
		}
		else if(msg.ctcpCommand == "TIME")
		{
			lg::log(sys, "replied to ctcp TIME");
			srv->sendRawMessage(zpr::sprint("NOTICE %s :\x01TIME %s\x01", msg.nick, util::getCurrentTimeString()));
		}
		else
		{
			lg::warn(sys, "unsupported ctcp command '%s'", msg.ctcpCommand);
		}
	}
}

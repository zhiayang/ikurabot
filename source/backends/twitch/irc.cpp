// irc.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "twitch.h"

namespace ikura::twitch
{
	template <typename... Args> void log(const std::string& fmt, Args&&... args) { lg::log("twitch", fmt, args...); }
	template <typename... Args> void warn(const std::string& fmt, Args&&... args) { lg::warn("twitch", fmt, args...); }
	template <typename... Args> void error(const std::string& fmt, Args&&... args) { lg::error("twitch", fmt, args...); }

	void processRawMessage(TwitchState* st, std::string_view msg)
	{
		while(msg.size() > 0)
		{
			auto i = msg.find("\r\n");
			processMessage(st, msg.substr(0, i));
			msg.remove_prefix(i + 2);
		}
	}

	void processMessage(TwitchState* st, std::string_view msg)
	{
		if(msg.size() < 2)
			return;

		// :<user>!<user>@<user>.tmi.twitch.tv PRIVMSG #<channel> :This is a sample message
		log("<<  %s", msg);

		if(msg.find("PING") == 0)
		{
			auto server = msg.substr(msg.find(':') + 1);
			sendRawMessage(st, zpr::sprint("PONG :%s", server));

			return;
		}

		// there should be a prefix
		if(msg[0] != ':')
			return warn("discarding message without prefix\n%s", msg);

		// first, see if it's a privmsg. if not, ignore these for now.
		auto pmidx = msg.find("PRIVMSG");
		if(pmidx == std::string::npos)
			return;

		// get the user.
		auto at = msg.find('!');
		if(at == 0 || at == std::string::npos)
			return error("discarding malformed message\n%s", msg);

		auto user = msg.substr(1, at - 1);

		// ignore messages from self
		if(user == st->username)
			return;


		// find the channel
		auto chidx = msg.find('#');
		if(chidx == 0 || chidx == std::string::npos)
			return warn("discarding message without channel\n%s", msg);

		auto channel = msg.substr(chidx);
		channel = channel.substr(1, channel.find(' ') - 1);

		auto message = msg.substr(msg.substr(1).find(':') + 2);

		log("message from '%s' in #%s: '%s'", user, channel, message);

		if(st->channels[std::string(channel)].lurk)
			return;

		if(message == "!quit")
			twitch::shutdown();

		if(message == "!ayaya")
			sendMessage(st, channel, zpr::sprint("%s AYAYA /", user));
	}

	void sendRawMessage(TwitchState* st, std::string_view msg)
	{
		st->sendQueue.wlock()->push_back(zpr::sprint("%s\r\n", msg));
		st->haveQueued.set(true);
	}

	void sendMessage(TwitchState* st, std::string_view channel, std::string_view msg)
	{
		sendRawMessage(st, zpr::sprint("PRIVMSG #%s :%s", channel, msg));
	}
}

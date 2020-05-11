// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"
#include "defs.h"
#include "twitch.h"

namespace ikura::twitch
{
	template <typename... Args> void log(const std::string& fmt, Args&&... args) { lg::log("twitch", fmt, args...); }
	template <typename... Args> void warn(const std::string& fmt, Args&&... args) { lg::warn("twitch", fmt, args...); }
	template <typename... Args> void error(const std::string& fmt, Args&&... args) { lg::error("twitch", fmt, args...); }

	void TwitchState::processMessage(ikura::str_view msg)
	{
		if(msg.size() < 2)
			return;

		auto parts = util::splitString(msg, ' ');
		if(parts.empty())
			return;

		// :<user>!<user>@<user>.tmi.twitch.tv PRIVMSG #<channel> :This is a sample message

		if(parts[0] == "PING")
		{
			// PING :tmi.twitch.tv
			return this->sendRawMessage(zpr::sprint("PONG %s", parts[1]));
		}

		// there should be a prefix
		if(parts[0][0] != ':')
			return warn("discarding message without prefix\n%s", msg);

		// get the prefix
		auto prefix = parts[0];
		if(prefix.find('!') == std::string::npos)
			return;

		// get the nick between the : and the !
		// twitch says that all 3 components (the nick, the username, and the host)
		// will all be the twitch username, so we just take the first one.
		auto user = prefix.drop(1).take(prefix.find('!') - 1);
		auto command = parts[1];

		// for now, only handle privmsgs.
		if(command != "PRIVMSG")
			return;

		if(parts.size() < 4)
			return warn("discarding malformed message\n%s", msg);

		auto channel = parts[2].drop(1);
		auto message = parts[3].drop(1);

		// ignore messages from self
		if(user == this->username)
			return;

		log("message from '%s' in #%s: '%s'", user, channel, message);

		if(this->channels[channel].lurk)
			return;

		cmd::processMessage(user, &this->channels[channel], message);
	}

	void TwitchState::sendRawMessage(ikura::str_view msg, ikura::str_view chan)
	{
		// check whether we are a moderator in this channel
		auto mod = false;
		if(!chan.empty() && this->channels[chan].mod)
			mod = true;

		this->sendQueue.wlock()->emplace_back(zpr::sprint("%s\r\n", msg), mod);
		this->haveQueued.set(true);
	}

	void TwitchState::sendMessage(ikura::str_view channel, ikura::str_view msg)
	{
		this->sendRawMessage(zpr::sprint("PRIVMSG #%s :%s", channel, msg), channel);
	}

	std::string TwitchChannel::getCommandPrefix() const
	{
		return config::twitch::getCommandPrefix();
	}

	std::string TwitchChannel::getUsername() const
	{
		return config::twitch::getUsername();
	}

	void TwitchChannel::sendMessage(const Message& msg) const
	{
		std::string str;
		for(const auto& frag : msg.fragments)
		{
			if(frag.isEmote)    str += frag.emote.name;
			else                str += frag.str;
		}

		this->state->sendMessage(this->name, str);
	}
}

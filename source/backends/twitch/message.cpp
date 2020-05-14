// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "cmd.h"
#include "irc.h"
#include "defs.h"
#include "twitch.h"

namespace ikura::twitch
{
	template <typename... Args> void log(const std::string& fmt, Args&&... args) { lg::log("twitch", fmt, args...); }
	template <typename... Args> void warn(const std::string& fmt, Args&&... args) { lg::warn("twitch", fmt, args...); }
	template <typename... Args> void error(const std::string& fmt, Args&&... args) { lg::error("twitch", fmt, args...); }

	static void update_user_creds(const ikura::string_map<std::string>& tags)
	{

	}

	void TwitchState::processMessage(ikura::str_view input)
	{
		auto m = irc::parseMessage(input);
		if(!m) return error("malformed: '%s'", input);

		auto msg = m.value();

		if(msg.command == "PING")
		{
			log("responded to PING");
			return this->sendRawMessage(zpr::sprint("PONG %s", msg.params.size() > 0 ? msg.params[0] : ""));
		}
		else if(msg.command == "CAP")
		{
			// :tmi.twitch.tv CAP * ACK :twitch.tv/tags
			if(msg.params.size() != 3)
				return error("malformed CAP: %s", input);

			log("negotiated capability %s", msg.params[2]);
		}
		else if(msg.command == "JOIN")
		{
			// :user!user@user.tmi.twitch.tv JOIN #channel
			if(msg.params.size() != 1)
				return error("malformed JOIN (%zu): %s", input);

			log("joined %s", msg.params[0]);
		}
		else if(msg.command == "PART")
		{
			// :user!user@user.tmi.twitch.tv PART #channel
			if(msg.params.size() != 2)
				return error("malformed PART: %s", input);

			log("parted %s", msg.params[1]);
		}
		else if(msg.command == "PRIVMSG")
		{
			if(msg.params.size() < 2)
				return error("malformed: less than 2 params for PRIVMSG");

			auto user = msg.user;

			// check for self
			if(user == this->username)
				return;

			auto channel = msg.params[0];
			if(channel[0] != '#')
				return error("malformed: channel '%s'", channel);

			// drop the '#'
			channel.remove_prefix(1);

			auto message = msg.params[1];
			lg::log("msg", "twitch/#%s: <%s>  %s", channel, user, message);

			// update the credentials of the user.
			update_user_creds(msg.tags);

			if(this->channels[channel].lurk)
				return;

			cmd::processMessage(user, &this->channels[channel], message);
		}
		else if(msg.command == "353" || msg.command == "366")
		{
			// ignore
		}
		else
		{
			warn("ignoring unhandled irc command '%s'", msg.command);
			for(size_t i = 0; i < msg.command.size(); i++)
				printf(" %02x", msg.command[i]);

			zpr::println("\n");
		}
	}

	void TwitchState::sendRawMessage(ikura::str_view msg, ikura::str_view chan)
	{
		// check whether we are a moderator in this channel
		auto is_moderator = false;
		if(!chan.empty() && this->channels[chan].mod)
			is_moderator = true;

		// log("queued msg at %d", std::chrono::system_clock::now().time_since_epoch().count());

		twitch::message_queue().emplace_send(
			zpr::sprint("%s\r\n", msg),
			is_moderator
		);
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

	std::string TwitchChannel::getName() const
	{
		return this->name;
	}

	bool TwitchChannel::shouldReplyMentions() const
	{
		return this->respondToPings;
	}

	void TwitchChannel::sendMessage(const Message& msg) const
	{
		std::string str;
		for(size_t i = 0; i < msg.fragments.size(); i++)
		{
			const auto& frag = msg.fragments[i];

			if(frag.isEmote)    str += frag.emote.name;
			else                str += frag.str;

			if(i + 1 != msg.fragments.size())
				str += ' ';
		}

		this->state->sendMessage(this->name, str);
	}
}

// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "cmd.h"
#include "irc.h"
#include "defs.h"
#include "twitch.h"

namespace ikura::twitch
{
	template <typename... Args> void log(const std::string& fmt, Args&&... args) { lg::log("twitch", fmt, args...); }
	template <typename... Args> void warn(const std::string& fmt, Args&&... args) { lg::warn("twitch", fmt, args...); }
	template <typename... Args> void error(const std::string& fmt, Args&&... args) { lg::error("twitch", fmt, args...); }

	static void update_user_creds(ikura::str_view user, ikura::str_view channel, const ikura::string_map<std::string>& tags)
	{
		// all users get the everyone credential.
		uint32_t perms = permissions::EVERYONE;
		uint32_t sublen = 0;

		std::string userid;
		std::string displayname;

		if(config::twitch::getOwner() == user)
			perms |= permissions::OWNER;

		// see https://dev.twitch.tv/docs/irc/tags. we are primarily interested in badges, badge-info, user-id, and display-name
		for(const auto& [ key, val ] : tags)
		{
			if(key == "user-id")
			{
				userid = val;
			}
			else if(key == "display-name")
			{
				displayname = val;
			}
			else if(key == "badges")
			{
				auto badges = util::split(val, ',');
				for(const auto& badge : badges)
				{
					// founder is a special kind of subscriber.
					if(badge.find("subscriber") == 0 || badge.find("founder") == 0)
						perms |= permissions::SUBSCRIBER;

					else if(badge.find("vip") == 0)
						perms |= permissions::VIP;

					else if(badge.find("moderator") == 0)
						perms |= permissions::MODERATOR;

					else if(badge.find("broadcaster") == 0)
						perms |= permissions::BROADCASTER;
				}
			}
			else if(key == "badge-info")
			{
				// we're only here to get the number of subscribed months.
				auto badges = util::split(val, ',');
				for(const auto& badge : badges)
				{
					// only care about this
					if(badge.find("subscriber") == 0 || badge.find("founder") == 0)
						sublen = std::stoi(badge.drop(badge.find('/') + 1).str());
				}
			}
		}

		if(userid.empty())
			return warn("message from '%s' contained no user id", user);

		// acquire a big lock.
		database().perform_write([&](auto& db) {

			// no need to check for existence; just use operator[] and create things as we go along.
			// update the user:
			{
				auto& tchan = db.twitchData.channels[channel];
				auto& tuser = tchan.knownUsers[user];

				tuser.username = user.str();
				tuser.displayname = displayname;

				const auto& existing_id = tuser.id;
				if(existing_id.empty())
				{
					log("adding user '%s'/'%s' to channel #%s", user, userid, channel);
					tuser.id = userid;
				}
				else if(existing_id != userid)
				{
					warn("user '%s' changed id from '%s' to '%s'", user, existing_id, userid);
					tuser.id = userid;
				}
			}

			// update the credentials:
			{
				auto& creds = db.twitchData.channels[channel].userCredentials[userid];
				creds.permissions = perms;
				creds.subscribedMonths = sublen;
			}
		});
	}

	void TwitchState::processMessage(ikura::str_view input)
	{
		auto m = irc::parseMessage(input);
		if(!m) return error("malformed: '%s'", input);

		auto msg = m.value();
		// zpr::println("(%zu) %s", input.size(), input);

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

			// update the credentials of the user (for the channel)
			update_user_creds(user, channel, msg.tags);

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
			for(size_t i = 0; i < input.size(); i++)
				printf(" %02x (%c)", input[i], input[i]);

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

	uint32_t TwitchChannel::getUserPermissions(ikura::str_view user) const
	{
		// mfw "const correctness", so we can't use operator[]
		return database().map_read([&](auto& db) -> uint32_t {
			if(auto it = db.twitchData.channels.find(this->name); it != db.twitchData.channels.end())
			{
				auto& chan = it.value();
				if(auto it = chan.knownUsers.find(user); it != chan.knownUsers.end())
				{
					auto userid = it->second.id;
					if(auto it = chan.userCredentials.find(userid); it != chan.userCredentials.end())
						return it->second.permissions;
				}
			}

			return 0;
		});
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

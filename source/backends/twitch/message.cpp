// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "zfu.h"
#include "cmd.h"
#include "irc.h"
#include "defs.h"
#include "markov.h"
#include "twitch.h"

namespace ikura::twitch
{
	template <typename... Args> void log(const std::string& fmt, Args&&... args) { lg::log("twitch", fmt, args...); }
	template <typename... Args> void warn(const std::string& fmt, Args&&... args) { lg::warn("twitch", fmt, args...); }
	template <typename... Args> void error(const std::string& fmt, Args&&... args) { lg::error("twitch", fmt, args...); }

	static std::string update_user_creds(ikura::str_view user, ikura::str_view channel, const ikura::string_map<std::string>& tags)
	{
		// all users get the everyone credential.
		uint64_t perms = permissions::EVERYONE;
		uint64_t sublen = 0;

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
						sublen = util::stou(badge.drop(badge.find('/') + 1)).value();
				}
			}
		}

		if(userid.empty())
		{
			warn("message from '%s' contained no user id", user);
			return "";
		}

		// acquire a big lock.
		database().perform_write([&](auto& db) {

			// no need to check for existence; just use operator[] and create things as we go along.
			// update the user:
			{
				auto& tchan = db.twitchData.channels[channel];
				auto& tuser = tchan.knownUsers[userid];

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

		return userid;
	}

	// this returns a vector of str_views with the text of the emote position, but more importantly,
	// each "view" has a pointer and a size, which represents the position of the emote in the original
	// byte stream. eg. if you use KEKW twice in a message, then you will get one str_view for each
	// instance of the emote.
	static std::vector<ikura::str_view> get_emote_indices(const ikura::string_map<std::string>& tags,
		ikura::str_view utf8, const std::vector<int32_t>& utf32)
	{
		// first split the tags.
		auto pos_arr = [&tags]() -> std::vector<std::pair<size_t, size_t>> {

			std::vector<std::pair<size_t, size_t>> ret;

			// first get the 'emotes' tag
			if(auto it = tags.find(ikura::str_view("emotes")); it != tags.end() && !it->second.empty())
			{
				// format: emotes=ID:begin-end,begin-end/ID:begin-end/...
				auto list = util::split(it->second, '/');
				for(const auto& emt : list)
				{
					// get the id:
					auto id = emt.take(emt.find(':'));
					auto indices = emt.drop(id.size() + 1);
					auto pos = util::split(indices, ',');
					for(const auto& p : pos)
					{
						// split by the '-'
						auto k = p.find('-');
						if(k != std::string::npos)
						{
							auto a = util::stou(p.take(k));
							auto b = util::stou(p.drop(k + 1));

							if(a && b) ret.emplace_back(a.value(), b.value());
						}
					}
				}

				return ret;
			}

			return { };
		}();

		// no emotes.
		if(pos_arr.empty())
			return { };

		// sort by the start index. since you can't have overlapping ranges, this will suffice.
		std::sort(pos_arr.begin(), pos_arr.end(), [](auto& a, auto& b) -> bool { return a.first < b.first; });
		ikura::span positions = pos_arr;

		std::vector<ikura::str_view> ret;

		// TODO: support ffz and bttv emotes. we just need to scan the utf8 stream in-parallel with the
		// utf-32 stream, i think.

		size_t idx8 = 0;
		size_t idx32 = 0;
		size_t first_idx8 = 0;

		while(idx8 < utf8.size() && idx32 < utf32.size())
		{
			if(positions.empty())
				break;

			// how this will go is that we keep the "current" emote index pair at positions[0]; when we
			// find a match, then it gets removed from the span. that way we don't need a 4th index.

			if(idx32 == positions[0].first)
			{
				first_idx8 = idx8;
			}
			else if(idx32 == positions[0].second)
			{
				// it's begin,end inclusive, so calc the length and substr that.
				ret.push_back(utf8.substr(first_idx8, idx8 + 1 - first_idx8));
				positions.remove_prefix(1);
			}

			idx8 += unicode::get_byte_length(utf32[idx32]);
			idx32 += 1;
		}

		return ret;
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

			auto username = msg.user;

			// check for self
			if(username == this->username)
				return;

			auto channel = msg.params[0];
			if(channel[0] != '#')
				return error("malformed: channel '%s'", channel);

			// drop the '#'
			channel.remove_prefix(1);

			// update the credentials of the user (for the channel)
			auto userid = update_user_creds(username, channel, msg.tags);

			// if there was something wrong with the message (no id, for example), then bail.
			if(userid.empty()) return;

			auto message = msg.params[1].trim();
			lg::log("msg", "twitch/#%s: <%s> %s", channel, username, message);

			// zpr::println("%s", input);
			// for(size_t i = 0; i < message.size(); i++)
			// 	printf(" %02x", (uint8_t) message[i]);

			auto message_u32 = unicode::to_utf32(message);
			auto message_u8  = unicode::to_utf8(message_u32);
			auto emote_idxs  = get_emote_indices(msg.tags, message_u8, message_u32);

			// only process commands if we're not lurking
			bool ran_cmd = false;
			if(!this->channels[channel].lurk)
				ran_cmd = cmd::processMessage(userid, username, &this->channels[channel], message_u8);

			auto tmp = util::stou(msg.tags["tmi-sent-ts"]);
			uint64_t ts = (tmp.has_value()
				? tmp.value()
				: util::getMillisecondTimestamp()
			);

			std::vector<ikura::relative_str> rel_emote_idxs;
			for(const auto& em : emote_idxs)
				rel_emote_idxs.emplace_back(em.data() - message_u8.data(), em.size());

			// don't train on commands.
			if(!ran_cmd)
				markov::process(message_u8, std::move(rel_emote_idxs));

			this->logMessage(ts, userid, &this->channels[channel], message_u8, emote_idxs);
		}
		else if(msg.command == "353" || msg.command == "366")
		{
			// ignore. these are MOTD markers i think
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

	std::string Channel::getCommandPrefix() const
	{
		return this->commandPrefix;
	}

	std::string Channel::getUsername() const
	{
		return config::twitch::getUsername();
	}

	std::string Channel::getName() const
	{
		return this->name;
	}

	uint64_t Channel::getUserPermissions(ikura::str_view userid) const
	{
		// mfw "const correctness", so we can't use operator[]
		return database().map_read([&](auto& db) -> uint64_t {
			auto chan = db.twitchData.getChannel(this->name);
			if(!chan) return 0;

			auto creds = chan->getUserCredentials(userid);
			if(!creds) return 0;

			return creds->permissions;


			// if(auto it = db.twitchData.channels.find(this->name); it != db.twitchData.channels.end())
			// {
			// 	auto& chan = it.value();
			// 	if(auto it = chan.knownUsers.find(user); it != chan.knownUsers.end())
			// 	{
			// 		auto userid = it->second.id;
			// 		if(auto it = chan.userCredentials.find(userid); it != chan.userCredentials.end())
			// 			return it->second.permissions;
			// 	}
			// }

			// return 0;
		});
	}

	bool Channel::shouldPrintInterpErrors() const
	{
		return !this->silentInterpErrors;
	}

	bool Channel::shouldReplyMentions() const
	{
		return this->respondToPings;
	}

	void Channel::sendMessage(const Message& msg) const
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

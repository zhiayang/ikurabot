// message.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "zfu.h"
#include "cmd.h"
#include "irc.h"
#include "defs.h"
#include "timer.h"
#include "config.h"
#include "markov.h"
#include "twitch.h"

namespace ikura::twitch
{
	template <typename... Args> void log(const std::string& fmt, Args&&... args) { lg::log("twitch", fmt, args...); }
	template <typename... Args> void warn(const std::string& fmt, Args&&... args) { lg::warn("twitch", fmt, args...); }
	template <typename... Args> void error(const std::string& fmt, Args&&... args) { lg::error("twitch", fmt, args...); }

	static std::string update_user_creds(ikura::str_view user, ikura::str_view channel, const ikura::string_map<std::string>& tags);

	// this returns a vector of str_views with the text of the emote position, but more importantly,
	// each "view" has a pointer and a size, which represents the position of the emote in the original
	// byte stream. eg. if you use KEKW twice in a message, then you will get one str_view for each
	// instance of the emote.
	static std::vector<ikura::str_view> extract_emotes(ikura::str_view channel, const ikura::string_map<std::string>& tags,
		ikura::str_view utf8, const std::vector<int32_t>& utf32);


	void TwitchState::processMessage(ikura::str_view input)
	{
		auto time = timer();

		auto m = irc::parseMessage(input);
		if(!m) return error("malformed: '%s'", input);

		auto msg = m.value();

		if(msg.command == "PING")
		{
			lg::dbglog("twitch", "ping-pong");
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
				return error("malformed JOIN: %s", input);

			log("joined %s", msg.params[0]);
		}
		else if(msg.command == "PART")
		{
			// :user!user@user.tmi.twitch.tv PART #channel
			if(msg.params.size() != 2)
				return error("malformed PART: %s", input);

			log("parted %s", msg.params[1]);
		}
		else if(msg.command == "NOTICE")
		{
			auto channel = msg.params[0];
			auto message = msg.params[1];

			lg::log("twitch", "notice in %s: %s", channel, message);
		}
		else if(msg.command == "PRIVMSG")
		{
			if(msg.params.size() < 2)
				return error("malformed: less than 2 params for PRIVMSG");

			// ignore other ctcp commands like VERSION or whatever.
			if(msg.isCTCP && msg.ctcpCommand != "ACTION")
				return;

			auto username = msg.user;

			// check for self
			if(username == this->username)
				return;

			// check for ignored users
			if(config::twitch::isUserIgnored(username))
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

			// zpr::println("%s", input);
			// for(size_t i = 0; i < message.size(); i++)
			// 	printf(" %02x", (uint8_t) message[i]);

			auto message_u32 = unicode::to_utf32(message);
			auto message_u8  = unicode::to_utf8(message_u32);
			auto rel_emotes  = zfu::map(extract_emotes(channel, msg.tags, message_u8, message_u32),
				[&message_u8](const auto& sv) -> auto {
					return ikura::relative_str(sv.data() - message_u8.data(), sv.size());
				}
			);


			// only process commands if we're not lurking and it's not a ctcp.
			bool ran_cmd = false;
			if(!this->channels[channel].lurk && !msg.isCTCP)
				ran_cmd = cmd::processMessage(userid, username, &this->channels[channel], message_u8, /* enablePings: */ true);

			auto tmp = util::stou(msg.tags["tmi-sent-ts"]);
			uint64_t ts = (tmp.has_value()
				? tmp.value()
				: util::getMillisecondTimestamp()
			);

			// don't train on commands.
			if(!ran_cmd)
				markov::process(message_u8, rel_emotes);

			this->logMessage(ts, userid, &this->channels[channel], message_u8, rel_emotes, ran_cmd);

			// lg::log("msg", "twitch/#%s: (%.2f ms) <%s> %s", channel, time.measure(), username, message_u8);
			console::logMessage(Backend::Twitch, "", channel, time.measure(), username, message_u8);
		}
		else if(zfu::match(msg.command, "353", "366"))
		{
			// ignore. these are MOTD markers i think
		}
		else if(zfu::match(msg.command, "CLEARCHAT", "CLEARMSG", "HOSTTARGET", "RECONNECT", "ROOMSTATE", "USERNOTICE", "USERSTATE"))
		{
			// don't care about these. we only did the commands capability to get NOTICEs.
		}
		else
		{
			lg::warn("twitch", "ignoring unhandled irc command %s", msg.command);
		}
	}



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

				// update the credentials:
				tuser.permissions = perms;
				tuser.subscribedMonths = sublen;

				tchan.usernameMapping[tuser.username] = tuser.id;
			}
		});

		return userid;
	}

	static std::vector<ikura::str_view> extract_emotes(ikura::str_view channelName, const ikura::string_map<std::string>& tags,
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


		std::vector<ikura::str_view> ret;

		if(!pos_arr.empty())
		{
			// sort by the start index. since you can't have overlapping ranges, this will suffice.
			std::sort(pos_arr.begin(), pos_arr.end(), [](auto& a, auto& b) -> bool { return a.first < b.first; });
			ikura::span positions = pos_arr;

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
		}

		ret += getExternalEmotePositions(utf8, channelName);
		return ret;
	}




	void TwitchState::sendRawMessage(ikura::str_view msg, ikura::str_view chan)
	{
		// check whether we are a moderator in this channel
		auto is_moderator = false;
		if(!chan.empty() && this->channels[chan].mod)
			is_moderator = true;

		// cut off any \r or \n
		msg = msg.take(msg.find_first_of("\r\n"));

		twitch::mqueue().emplace_send(
			zpr::sprint("%s\r\n", msg),
			is_moderator
		);
	}

	void TwitchState::sendMessage(ikura::str_view channel, ikura::str_view msg)
	{
		// twitch actually says it's 500 characters, ie. codepoints.
		constexpr size_t LIMIT = 500;
		msg = msg.trim();

		if(msg.size() > LIMIT)
		{
			auto codepoints = unicode::to_utf32(msg);
			auto span = ikura::span(codepoints);
			while(span.size() > 0)
			{
				// try to split at a space if possible.
				auto frag = span;

				if(frag.size() > LIMIT)
				{
					auto spl = span.take(LIMIT).find_last((int32_t) ' ');
					if(spl == (size_t) -1) spl = LIMIT;

					frag = span.take(spl);
				}

				span = span.drop(frag.size() + 1);
				if(frag.empty())
					continue;

				this->sendMessage(channel, unicode::to_utf8(frag.vec()));
			}
		}
		else
		{
			if(msg.empty())
				return;

			else if(msg[0] == '/' || msg[0] == '.')
				this->sendMessage("Jebaited", channel);

			else
				this->sendRawMessage(zpr::sprint("PRIVMSG #%s :%s", channel, msg), channel);
		}
	}
}

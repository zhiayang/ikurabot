// log.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "twitch.h"
#include "serialise.h"

namespace ikura::twitch
{
	void TwitchState::logMessage(uint64_t timestamp, ikura::str_view userid, Channel* chan, ikura::str_view message,
		const std::vector<ikura::str_view>& emote_idxs, bool isCmd)
	{
		TwitchMessage tmsg;

		auto tchan = database().rlock()->twitchData.getChannel(chan->getName());
		if(!tchan) return;

		auto user = tchan->getUser(userid);
		if(!user) return;

		tmsg.timestamp = timestamp;

		tmsg.userid = user->id;
		tmsg.username = user->username;
		tmsg.displayname = user->displayname;

		tmsg.channel = chan->getName();
		tmsg.isCommand = isCmd;

		tmsg.message = database().wlock()->messageData.logMessageContents(message);
		for(const auto& em : emote_idxs)
			tmsg.emotePositions.emplace_back(em.data() - message.data(), em.size());

		database().wlock()->twitchData.messageLog.messages.push_back(std::move(tmsg));
	}













	void TwitchMessage::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->timestamp);
		wr.write(this->userid);
		wr.write(this->username);
		wr.write(this->displayname);
		wr.write(this->channel);
		wr.write(this->message);
		wr.write(this->emotePositions);
		wr.write(this->isCommand);
	}

	std::optional<TwitchMessage> TwitchMessage::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		TwitchMessage ret;

		if(!rd.read(&ret.timestamp))        return { };
		if(!rd.read(&ret.userid))           return { };
		if(!rd.read(&ret.username))         return { };
		if(!rd.read(&ret.displayname))      return { };
		if(!rd.read(&ret.channel))          return { };
		if(!rd.read(&ret.message))          return { };
		if(!rd.read(&ret.emotePositions))   return { };
		if(!rd.read(&ret.isCommand))        return { };

		return ret;
	}









	void TwitchMessageLog::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->messages);
	}

	std::optional<TwitchMessageLog> TwitchMessageLog::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		TwitchMessageLog ret;
		if(!rd.read(&ret.messages))
			return { };

		return ret;
	}
}

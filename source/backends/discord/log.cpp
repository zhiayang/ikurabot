// log.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "twitch.h"
#include "serialise.h"

namespace ikura::discord
{
	void DiscordState::logMessage(uint64_t timestamp, DiscordUser& user, DiscordChannel& channel, DiscordGuild& guild,
		Snowflake messageId, ikura::str_view message, const std::vector<ikura::relative_str>& emote_idxs,
		bool isCmd, bool isEdit)
	{
		DiscordMessage msg;
		msg.timestamp = timestamp;
		msg.messageId = messageId;

		msg.userId = user.id;
		msg.username = user.username;
		msg.nickname = user.nickname;

		msg.guildId = guild.id;
		msg.guildName = guild.name;

		msg.channelId = channel.id;
		msg.channelName = channel.name;

		msg.message = database().wlock()->messageData.logMessageContents(message);
		msg.emotePositions = emote_idxs;

		msg.isEdit = isEdit;
		msg.isCommand = isCmd;

		database().wlock()->discordData.messageLog.messages.push_back(std::move(msg));
	}






	void DiscordMessage::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->timestamp);
		wr.write(this->messageId);
		wr.write(this->userId);
		wr.write(this->username);
		wr.write(this->nickname);
		wr.write(this->guildId);
		wr.write(this->guildName);
		wr.write(this->channelId);
		wr.write(this->channelName);
		wr.write(this->message);
		wr.write(this->emotePositions);
		wr.write(this->isEdit);
		wr.write(this->isCommand);
	}

	std::optional<DiscordMessage> DiscordMessage::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		DiscordMessage ret;

		if(!rd.read(&ret.timestamp))        return { };
		if(!rd.read(&ret.messageId))        return { };
		if(!rd.read(&ret.userId))           return { };
		if(!rd.read(&ret.username))         return { };
		if(!rd.read(&ret.nickname))         return { };
		if(!rd.read(&ret.guildId))          return { };
		if(!rd.read(&ret.guildName))        return { };
		if(!rd.read(&ret.channelId))        return { };
		if(!rd.read(&ret.channelName))      return { };
		if(!rd.read(&ret.message))          return { };
		if(!rd.read(&ret.emotePositions))   return { };
		if(!rd.read(&ret.isEdit))           return { };
		if(!rd.read(&ret.isCommand))        return { };

		return ret;
	}









	void DiscordMessageLog::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->messages);
	}

	std::optional<DiscordMessageLog> DiscordMessageLog::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		DiscordMessageLog ret;
		if(!rd.read(&ret.messages))
			return { };

		return ret;
	}
}

// log.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

#include "db.h"
#include "irc.h"
#include "serialise.h"

namespace ikura::irc
{
	void IRCServer::logMessage(uint64_t timestamp, ikura::str_view username, ikura::str_view nickname, Channel* chan,
		ikura::str_view message, bool isCmd)
	{
		db::IRCMessage msg;
		msg.timestamp = timestamp;

		msg.username = username;
		msg.nickname = nickname;

		msg.channel = chan->getName();
		msg.server  = chan->server->name;

		msg.message = database().wlock()->messageData.logMessageContents(message);
		msg.isCommand = isCmd;

		database().wlock()->ircData.messageLog.messages.push_back(std::move(msg));
	}

	void db::IRCMessage::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->timestamp);
		wr.write(this->nickname);
		wr.write(this->username);
		wr.write(this->channel);
		wr.write(this->server);
		wr.write(this->message);
		wr.write(this->isCommand);
	}

	std::optional<db::IRCMessage> db::IRCMessage::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		db::IRCMessage ret;

		if(!rd.read(&ret.timestamp))        return { };
		if(!rd.read(&ret.nickname))         return { };
		if(!rd.read(&ret.username))         return { };
		if(!rd.read(&ret.channel))          return { };
		if(!rd.read(&ret.server))           return { };
		if(!rd.read(&ret.message))          return { };
		if(!rd.read(&ret.isCommand))        return { };

		return ret;
	}









	void db::IRCMessageLog::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->messages);
	}

	std::optional<db::IRCMessageLog> db::IRCMessageLog::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);

		db::IRCMessageLog ret;
		if(!rd.read(&ret.messages))
			return { };

		return ret;
	}
}

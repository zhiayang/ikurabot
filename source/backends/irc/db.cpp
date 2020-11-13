// db.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

#include "db.h"
#include "irc.h"
#include "serialise.h"


namespace ikura::irc::db
{
	const IRCServer* IrcDB::getServer(ikura::str_view name) const
	{
		if(auto it = this->servers.find(name); it != this->servers.end())
			return &it.value();

		return nullptr;
	}

	const IRCChannel* IRCServer::getChannel(ikura::str_view name) const
	{
		if(auto it = this->channels.find(name); it != this->channels.end())
			return &it.value();

		return nullptr;
	}

	const IRCUser* IRCChannel::getUser(ikura::str_view name) const
	{
		if(auto it = this->knownUsers.find(name); it != this->knownUsers.end())
			return &it.value();

		return nullptr;
	}



	IRCServer* IrcDB::getServer(ikura::str_view name)
	{
		return const_cast<IRCServer*>(static_cast<const IrcDB*>(this)->getServer(name));
	}

	IRCChannel* IRCServer::getChannel(ikura::str_view name)
	{
		return const_cast<IRCChannel*>(static_cast<const IRCServer*>(this)->getChannel(name));
	}

	IRCUser* IRCChannel::getUser(ikura::str_view name)
	{
		return const_cast<IRCUser*>(static_cast<const IRCChannel*>(this)->getUser(name));
	}







	void IrcDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->servers);
		wr.write(this->messageLog);
	}

	std::optional<IrcDB> IrcDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		IrcDB ret;

		if(!rd.read(&ret.servers))
			return { };

		if(!rd.read(&ret.messageLog))
			return { };

		return ret;
	}

	void IRCServer::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
		wr.write(this->hostname);
		wr.write(this->channels);
	}

	std::optional<IRCServer> IRCServer::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		IRCServer ret;

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.hostname))
			return { };

		if(!rd.read(&ret.channels))
			return { };

		return ret;
	}

	void IRCChannel::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
		wr.write(this->knownUsers);
		wr.write(this->nicknameMapping);
	}

	std::optional<IRCChannel> IRCChannel::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		IRCChannel ret;

		if(!rd.read(&ret.name))
			return { };

		if(!rd.read(&ret.knownUsers))
			return { };

		if(!rd.read(&ret.nicknameMapping))
			return { };

		return ret;
	}

	void IRCUser::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->nickname);
		wr.write(this->username);

		wr.write(this->groups);
		wr.write(this->permissions);
	}

	std::optional<IRCUser> IRCUser::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
			return lg::error_o("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);

		IRCUser ret;
		if(!rd.read(&ret.nickname))
			return { };

		if(!rd.read(&ret.username))
			return { };

		if(!rd.read(&ret.groups))
			return { };

		if(!rd.read(&ret.permissions))
			return { };

		return ret;
	}
}


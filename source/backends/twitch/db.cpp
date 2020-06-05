// db.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "twitch.h"
#include "serialise.h"

namespace ikura::twitch
{
	TwitchUser* TwitchChannel::getUser(ikura::str_view id)
	{
		if(auto it = this->knownUsers.find(id); it != this->knownUsers.end())
			return &it.value();

		return nullptr;
	}

	const TwitchUser* TwitchChannel::getUser(ikura::str_view id)const
	{
		return const_cast<TwitchChannel*>(this)->getUser(id);
	}

	const TwitchChannel* TwitchDB::getChannel(ikura::str_view name) const
	{
		if(auto it = this->channels.find(name); it != this->channels.end())
			return &it.value();

		return nullptr;
	}








	void TwitchDB::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->channels);
		wr.write(this->messageLog);
	}

	std::optional<TwitchDB> TwitchDB::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		TwitchDB ret;

		if(!rd.read(&ret.channels))
			return { };

		if(!rd.read(&ret.messageLog))
			return { };

		return ret;
	}

	void TwitchChannel::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->knownUsers);
	}

	std::optional<TwitchChannel> TwitchChannel::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		TwitchChannel ret;
		if(!rd.read(&ret.knownUsers))
			return { };

		return ret;
	}

	void TwitchUser::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->id);
		wr.write(this->username);
		wr.write(this->displayname);

		wr.write(this->groups);
		wr.write(this->permissions);
		wr.write(this->subscribedMonths);
	}

	std::optional<TwitchUser> TwitchUser::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '%02x', expected '%02x')", t, TYPE_TAG);
			return { };
		}

		TwitchUser ret;
		if(!rd.read(&ret.id))
			return { };

		if(!rd.read(&ret.username))
			return { };

		if(!rd.read(&ret.displayname))
			return { };

		if(!rd.read(&ret.groups))
			return { };

		if(!rd.read(&ret.permissions))
			return { };

		if(!rd.read(&ret.subscribedMonths))
			return { };

		return ret;
	}
}

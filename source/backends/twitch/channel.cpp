// channel.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "twitch.h"

namespace ikura::twitch
{
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
		// massive hack but idgaf
		if(userid == MAGIC_OWNER_USERID)
			return permissions::OWNER;

		// mfw "const correctness", so we can't use operator[]
		return database().map_read([&](auto& db) -> uint64_t {
			auto chan = db.twitchData.getChannel(this->name);
			if(!chan) return 0;

			auto creds = chan->getUserCredentials(userid);
			if(!creds) return 0;

			return creds->permissions;
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

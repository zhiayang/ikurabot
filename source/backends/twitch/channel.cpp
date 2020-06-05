// channel.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "cmd.h"
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

	bool Channel::checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const
	{
		// massive hack but idgaf
		if(userid == MAGIC_OWNER_USERID)
			return true;

		return database().map_read([&](auto& db) {
			// mfw "const correctness", so we can't use operator[]
			auto chan = db.twitchData.getChannel(this->name);
			if(!chan) return false;

			auto user = chan->getUser(userid);
			if(!user) return false;

			return required.check(user->permissions, user->groups, { });
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

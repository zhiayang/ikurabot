// channel.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

#include "db.h"
#include "irc.h"
#include "perms.h"

namespace ikura::irc
{
	std::vector<std::string> Channel::getCommandPrefixes() const
	{
		return this->commandPrefixes;
	}

	std::string Channel::getUsername() const
	{
		return this->nickname;
	}

	std::string Channel::getName() const
	{
		return this->name;
	}

	bool Channel::checkUserPermissions(ikura::str_view username, const PermissionSet& required) const
	{
		// massive hack but idgaf
		if(username == irc::MAGIC_OWNER_USERID || username == this->server->owner)
			return true;

		return database().map_read([&](auto& db) {
			auto srv = db.ircData.getServer(this->server->name);
			if(!srv) return false;

			auto chan = srv->getChannel(this->name);
			if(!chan) return false;

			auto user = chan->getUser(username);
			if(!user) return false;

			return required.check(user->permissions, user->groups, { });
		});

		return true;
	}

	bool Channel::shouldPrintInterpErrors() const
	{
		return !this->silentInterpErrors;
	}

	bool Channel::shouldReplyMentions() const
	{
		return this->respondToPings;
	}

	bool Channel::shouldRunMessageHandlers() const
	{
		return this->runMessageHandlers;
	}

	bool Channel::shouldLurk() const
	{
		return this->lurk;
	}

	void Channel::sendMessage(const Message& msg) const
	{
		std::string out;
		for(size_t i = 0; i < msg.fragments.size(); i++)
		{
			const auto& frag = msg.fragments[i];

			if(frag.isEmote)    out += frag.emote.name;
			else                out += frag.str;

			if(i + 1 != msg.fragments.size())
				out += ' ';
		}

		ikura::str_view str = out;
		str = str.trim();

		if(!str.empty())
		{
			this->server->sendMessage(this->getName(), out);
			lg::log("msg", "irc/{}: {}>>>{} {}", this->getName(),
				colours::GREEN_BOLD, colours::COLOUR_RESET, str);
		}

		if(msg.next)
			this->sendMessage(*msg.next);
	}
}

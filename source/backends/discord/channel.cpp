// channel.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "discord.h"
#include "picojson.h"

namespace ikura::discord
{
	std::string Channel::getName() const
	{
		assert(this->guild);
		return this->guild->channels[this->channelId].name;
	}

	std::string Channel::getUsername() const
	{
		return config::discord::getUsername();
	}

	std::string Channel::getCommandPrefix() const
	{
		return this->commandPrefix;
	}

	bool Channel::shouldPrintInterpErrors() const
	{
		return !this->silentInterpErrors;
	}

	bool Channel::shouldReplyMentions() const
	{
		return this->respondToPings;
	}

	uint64_t Channel::getUserPermissions(ikura::str_view userid) const
	{
		return 1;
	}

	void Channel::sendMessage(const Message& msg) const
	{
		assert(this->guild);

		std::string str;
		for(size_t i = 0; i < msg.fragments.size(); i++)
		{
			const auto& frag = msg.fragments[i];

			if(!frag.isEmote)
			{
				str += frag.str;
			}
			else
			{
				auto name = frag.emote.name;
				auto [ id, anim ] = this->guild->emotes[name];

				if(id.value != 0)
					str += zpr::sprint("<%s:%s:%s>", anim ? "a" : "", name, id.str());

				else
					str += name;
			}

			if(i + 1 != msg.fragments.size())
				str += ' ';
		}


		auto body = pj::value(std::map<std::string, pj::value> {
			{ "content", pj::value(str) }
		});

		// why tf i need to send http for messages
		auto [ hdr, res ] = request::post(URL(zpr::sprint("%s/v%d/channels/%s/messages",
			DiscordState::API_URL, DiscordState::API_VERSION, this->channelId.str())),
			{ /* no params */ }, {
				request::Header("Authorization", zpr::sprint("Bot %s", config::discord::getOAuthToken())),
				request::Header("User-Agent", "DiscordBot (https://github.com/zhiayang/ikurabot, 0.1.0)"),
			},
			"application/json", body.serialise()
		);

		if(hdr.statusCode() != 200)
			lg::error("discord", "send error %d: %s", hdr.statusCode(), res);
	}
}

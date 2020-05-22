// discord.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "discord.h"
#include "network.h"

static constexpr const char* DISCORD_API_URL = "https://discord.com/api/v6";

namespace ikura::discord
{
	void init()
	{
		auto [ hdr, res ] = request::get(URL(zpr::sprint("%s/gateway/bot", DISCORD_API_URL)),
			{ /* no params */ }, {
				request::Header("Authorization", zpr::sprint("Bot %s", config::discord::getOAuthToken())),
				request::Header("User-Agent", "DiscordBot (https://github.com/zhiayang/ikurabot, 0.1.0)"),
				request::Header("Connection", "close"),
			}
		);

		zpr::println("status: %d, %s", hdr.statusCode(), res);
	}

	void shutdown()
	{
	}
}

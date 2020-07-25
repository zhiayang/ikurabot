// channel.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "defs.h"
#include "rate.h"
#include "perms.h"
#include "async.h"
#include "config.h"
#include "discord.h"
#include "picojson.h"

using namespace std::chrono_literals;

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

	bool Channel::shouldRunMessageHandlers() const
	{
		return this->runMessageHandlers;
	}

	bool Channel::checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const
	{
		if(Snowflake(userid) == config::discord::getOwner())
			return true;

		if(userid == twitch::MAGIC_OWNER_USERID)
			return true;

		return database().map_read([&](auto& db) {
			// mfw "const correctness", so we can't use operator[]
			auto guild = this->getGuild();
			if(!guild) { lg::warn("discord", "no guild"); return false; }

			auto user = guild->getUser(Snowflake(userid));
			if(!user) { lg::warn("discord", "no user"); return false; }

			return required.check(user->permissions, user->groups, user->discordRoles);
		});
	}


	static std::string message_to_string(const Message& msg, DiscordGuild* guild)
	{
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
				auto [ id, anim ] = guild->emotes[name];

				if(id.value != 0)
					str += zpr::sprint("<%s:%s:%s>", anim ? "a" : "", name, id.str());

				else
					str += name;
			}

			if(i + 1 != msg.fragments.size())
				str += ' ';
		}

		return str;

	}


	void Channel::sendMessage(const Message& msg) const
	{
		assert(this->guild);
		auto str = message_to_string(msg, this->guild);

		if(!str.empty())
			mqueue().emplace_send(str, this->channelId, this->getGuild()->name, this->getName());

		if(msg.next)
			this->sendMessage(*msg.next);
	}






	struct RateLimitWrapper
	{
		ikura::string_map<RateLimit> limits;
		ikura::string_map<std::string> buckets;

		// assume that, unless we discover the rate limit, we are free to send.
		std::optional<RateLimit::time_point_t> attempt(const std::string& endpoint) const
		{
			if(auto it = this->buckets.find(endpoint); it != this->buckets.end())
			{
				if(auto it2 = this->limits.find(it->second); it2 != this->limits.end())
				{
					if(!it2->second.attempt())
					{
						if(it2->second.exceeded())
							lg::warn("discord", "exceeded rate limit");

						return it2->second.next();
					}
				}
			}

			return { };
		}
	};


	void send_worker()
	{
		RateLimitWrapper rateLimit;

		while(true)
		{
			auto tx = mqueue().pop_send();
			if(tx.disconnected)
				break;

			auto& message = tx.msg;

			auto endpoint = zpr::sprint("%s/v%d/channels/%s/messages", DiscordState::API_URL, DiscordState::API_VERSION,
				tx.channelId.str());

			{
				auto wait = rateLimit.attempt(endpoint);
				if(wait.has_value())
					std::this_thread::sleep_until(wait.value());
			}


			auto body = pj::value(std::map<std::string, pj::value> { { "content", pj::value(message) } });

		again:
			auto resp = request::post(URL(endpoint), { /* no params */ },
				{
					request::Header("Authorization", zpr::sprint("Bot %s", config::discord::getOAuthToken())),
					request::Header("User-Agent", "DiscordBot (https://github.com/zhiayang/ikurabot, 0.1.0)"),
					request::Header("X-RateLimit-Precision", "millisecond")
				},
				"application/json", body.serialise()
			);

			auto& hdrs = resp.headers;
			auto& res  = resp.content;

			// update the rate limits
			if(auto bucket = hdrs.get("x-ratelimit-bucket"); !bucket.empty())
			{
				rateLimit.buckets[endpoint] = bucket;

				auto bkt = &(rateLimit.limits.find(bucket) != rateLimit.limits.end()
					? rateLimit.limits.at(bucket)
					: rateLimit.limits.emplace(bucket, RateLimit(5, 5s, 0.5s)).first.value()
				);

				if(auto x = util::stoi(hdrs.get("x-ratelimit-limit")); x.has_value())
					bkt->set_limit(x.value());

				if(auto x = util::stoi(hdrs.get("x-ratelimit-remaining")); x.has_value())
					bkt->set_tokens(x.value());

				if(auto x = std::stod(hdrs.get("x-ratelimit-reset-after")); x >= 0.0)
					bkt->set_reset_after(std::chrono::duration<double>(x));
			}

			if(hdrs.statusCode() == 429)
			{
				// oof
				auto j = util::parseJson(res);
				if(j.has_value())
				{
					auto& json = j.unwrap().as_obj();
					auto wait = json["retry_after"].as_int();
					lg::warn("discord", "rate limited; retry after %d ms", wait);

					// add some buffer
					wait += 100;
					util::sleep_for(std::chrono::milliseconds(wait));
					goto again;
				}
			}
			else if(hdrs.statusCode() != 200)
			{
				lg::error("discord", "send error %d: %s", hdrs.statusCode(), res);
			}
			else
			{
				lg::log("msg", "discord/%s/#%s: %s>>>%s %s", tx.guildName, tx.channelName,
					colours::GREEN_BOLD, colours::COLOUR_RESET, message);
			}
		}

		lg::dbglog("discord", "send worker exited");
	}
}

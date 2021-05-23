// channel.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "ast.h"
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

	std::vector<std::string> Channel::getCommandPrefixes() const
	{
		return this->commandPrefixes;
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
		if(Snowflake(userid) == config::discord::getOwner() || Snowflake(userid) == config::discord::getUserId())
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
			bool swallow_space = false;
			const auto& frag = msg.fragments[i];

			if(!frag.isEmote)
			{
				str += frag.str;
			}
			else
			{
				auto name = frag.emote.name;

				if(name.back() == '~')
				{
					swallow_space = true;
					name = str_view(name).drop_last(1).str();
				}

				auto [ id, flags ] = guild->emotes[name];

				// fuck discord, seriously. the problem here is that emotes with the same
				// name on the server *APPEAR* to have different names -- eg. emote and emote~1,
				// but in the backend they have the same name, and so discord reports the same
				// name to us, just with different ids.
				//! this is a hacky solution -- what we do is to allow specifying the emote id
				//! directly, by using :someEmote*19348182390123 or whatever special snowflake.
				//! since emote names can't contain '*' (or at least i hope they can't),
				//! this works.
				if(name.find("*") != std::string::npos)
				{
					auto x = name.find("*");
					auto the_name = str_view(name).take(x).str();
					auto the_id   = str_view(name).drop(x + 1).str();

					bool anim = (flags & EmoteFlags::IS_ANIMATED || name.find("*a") != std::string::npos);
					if(the_id.find("a") == 0)
						the_id = the_id.erase(0, 1);

					str += zpr::sprint("<{}:{}:{}>", anim ? "a" : "", the_name, the_id);
				}
				else if(id.value != 0 && (flags & EmoteFlags::NEEDS_COLONS))
				{
					str += zpr::sprint("<{}:{}:{}>", (flags & EmoteFlags::IS_ANIMATED) ? "a" : "", name, id.str());
				}
				else
				{
					str += name;
				}
			}

			if(i + 1 != msg.fragments.size() && !swallow_space && (!str.empty() && str.back() != '\n'))
				str += ' ';
		}

		return str;
	}

	bool Channel::shouldLurk() const
	{
		return this->lurk;
	}

	void Channel::sendMessage(const Message& msg) const
	{
		assert(this->guild);
		auto str = message_to_string(msg, this->guild);

		if(!str.empty())
		{
			mqueue().emplace_send(str, this->channelId, this->getGuild()->name, this->getName(),
				this->useReplies ? msg.discordReplyId : "");
		}

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

	static std::optional<Snowflake> send_one_message(RateLimitWrapper& rateLimit, TxMessage& tx,
		bool ignoreRates = false, bool edit = false, Snowflake msgId = { })
	{
		auto& message = tx.msg;

		std::string endpoint;
		if(edit == false)
		{
			endpoint = zpr::sprint("{}/v{}/channels/{}/messages", DiscordState::API_URL, DiscordState::API_VERSION,
				tx.channelId.str());
		}
		else
		{
			endpoint = zpr::sprint("{}/v{}/channels/{}/messages/{}", DiscordState::API_URL, DiscordState::API_VERSION,
				tx.channelId.str(), msgId.str());
		}

		assert(!endpoint.empty());

		if(!ignoreRates)
		{
			auto wait = rateLimit.attempt(endpoint);
			if(wait.has_value())
				std::this_thread::sleep_until(wait.value());
		}

		auto object = pj::object {
			{ "content", pj::value(message) },
		};

		if(!tx.replyId.empty())
		{
			object["message_reference"] = pj::value(pj::object {
				{ "message_id", pj::value(tx.replyId) },
				{ "channel_id", pj::value(tx.channelId.str()) }
			});

			object["allowed_mentions"] = pj::value(pj::object {
				{ "parse", pj::value(pj::array { pj::value("users") }) },
				{ "replied_user", pj::value(false) }
			});
		}

		auto body = pj::value(object);

	again:
		auto& method = (edit ? request::patch : request::post);
		auto resp = method(URL(endpoint), { /* no params */ },
			{
				request::Header("Authorization", zpr::sprint("Bot {}", config::discord::getOAuthToken())),
				request::Header("User-Agent", "DiscordBot (https://github.com/zhiayang/ikurabot, 0.1.0)"),
				request::Header("X-RateLimit-Precision", "millisecond")
			},
			"application/json", body.serialise()
		);

		auto& hdrs = resp.headers;
		auto& res  = resp.content;

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
				lg::warn("discord", "rate limited; retry after {} ms", wait);

				// add some buffer
				wait += 100;

				util::sleep_for(std::chrono::milliseconds(wait));
				goto again;
			}
		}
		else if(hdrs.statusCode() != 200)
		{
			lg::error("discord", "send error {}: {}", hdrs.statusCode(), res);
		}
		else
		{
			if(!edit)
			{
				lg::log("msg", "discord/{}/#{}: {}>>>{} {}", tx.guildName, tx.channelName,
					colours::GREEN_BOLD, colours::COLOUR_RESET, message);
			}

			if(auto j = util::parseJson(res); j.has_value())
			{
				auto& json = j.unwrap().as_obj();
				auto id = json["id"].as_str();

				return Snowflake(id);
			}
		}

		return { };
	}






	void send_worker()
	{
		RateLimitWrapper rateLimit;

		while(true)
		{
			auto tx = mqueue().pop_send();
			if(tx.disconnected)
				break;

			send_one_message(rateLimit, tx);
		}

		lg::dbglog("discord", "send worker exited");
	}




	// silly timer stuff
	struct TimerState
	{
		int millis = 0;
		bool down = false;
		bool stop = false;
		Snowflake messageId;

		int interval_ms = 1000;
		uint64_t start_ms = 0;

		int elapsed_ticks = 0;
		interp::ast::LambdaExpr* lambda = 0;

		std::thread worker;
	};

	static std::map<const Channel*, TimerState*> activeTimers;

	static void setup_worker(const Channel* chan, TimerState* timer)
	{
		timer->worker = std::thread([](const Channel* chan, TimerState* ts) {
			auto make_message = [chan, ts](int n, bool end = false) -> TxMessage {

				if(ts->lambda != nullptr)
				{
					using namespace interp::ast;

					// prepare a context
					interp::CmdContext cs;
					cs.executionStart = util::getMillisecondTimestamp();
					cs.recursionDepth = 0;
					cs.channel = chan;

					// prepare a function call
					auto fc = new FunctionCall(ts->lambda, { new LitInteger(ts->elapsed_ticks, false) });
					fc->weak_callee_ref = true;

					auto res = interpreter().map_write([&cs, &fc](auto& interp) -> auto {
						return fc->evaluate(&interp, cs);
					});

					if(!res.has_value())
					{
						ts->stop = true;
						return TxMessage("<expr error>", chan->getChannelId(), chan->getGuild()->name, chan->getName());
					}
					else
					{
						auto str = res->str();
						auto ret = TxMessage(str, chan->getChannelId(), chan->getGuild()->name, chan->getName());

						delete fc;
						return ret;
					}
				}
				else
				{
					constexpr const char* kek = "⏳";

					std::string str;
					if(end)
					{
						auto elapsed = (double) (util::getMillisecondTimestamp() - ts->start_ms) / 1000.0;
						str = zpr::sprint("{}: beep beep ({.1f}s)", kek, elapsed);
					}
					else
					{
						str = zpr::sprint("{}: {.1f}s", kek, std::max(0.0, (double) n / 1000.0));
					}

					return TxMessage(str, chan->getChannelId(), chan->getGuild()->name, chan->getName());
				}
			};

			RateLimitWrapper rate;

			// send the first one
			auto msg = make_message(ts->millis);

			auto& now = std::chrono::system_clock::now;

			auto last = now();
			auto next = last + 1000ms;

			if(auto sf = send_one_message(rate, msg); sf.has_value())
			{
				ts->messageId = *sf;
			}
			else
			{
				lg::error("discord", "timer init failed");
				goto end;
			}

			ts->start_ms = util::getMillisecondTimestamp();

			while(!ts->stop)
			{
				while(now() < next)
					util::sleep_for(100ms);

				auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now() - last).count();

				last = now();
				next = last + 1000ms;

				if(ts->down) ts->millis -= diff;
				else         ts->millis += diff;

				if(ts->down && ts->millis <= 0)
					break;

				msg = make_message(ts->millis);
				send_one_message(rate, msg, /* ignoreRates: */ true, /* edit: */ true, ts->messageId);
				ts->elapsed_ticks += 1;
			}

			// make the last one
			msg = make_message(ts->millis, /* end: */ true);
			send_one_message(rate, msg, /* ignoreRates: */ true, /* edit: */ true, ts->messageId);

		end:
			// must kill from another thread.
			dispatcher().run([chan]() {
				chan->stopTimer();
			}).discard();

			return;

		}, chan, timer);
	}

	void Channel::startTimer(int seconds) const
	{
		if(auto it = activeTimers.find(this); it != activeTimers.end() && it->second != nullptr)
		{
			this->sendMessage(Message("timer already active"));
			return;
		}

		auto t = new TimerState();
		t->millis       = seconds * 1000;  // milliseconds
		t->interval_ms  = 1000;
		t->down         = (seconds > 0);
		t->stop         = false;
		t->lambda       = 0;

		setup_worker(this, t);
		activeTimers[this] = t;
	}

	void Channel::startEvalTimer(double interval, interp::ast::LambdaExpr* lambda) const
	{
		if(auto it = activeTimers.find(this); it != activeTimers.end() && it->second != nullptr)
		{
			this->sendMessage(Message("timer already active"));
			return;
		}

		auto t = new TimerState();
		t->millis       = 0;
		t->interval_ms  = interval * 1000;
		t->down         = false;
		t->stop         = false;
		t->lambda       = lambda;

		setup_worker(this, t);
		activeTimers[this] = t;
	}



	void Channel::stopTimer() const
	{
		auto ts = activeTimers[this];
		if(ts == nullptr)
			return;

		ts->stop = true;
		while(ts->worker.joinable())
			;

		if(ts->lambda != nullptr)
			delete ts->lambda;

		delete ts;
		activeTimers.erase(this);
	}
}

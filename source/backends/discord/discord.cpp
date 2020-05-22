// discord.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#define PICOJSON_USE_INT64 1
#include "picojson.h"
namespace pj = picojson;

#include "defs.h"
#include "synchro.h"
#include "discord.h"
#include "network.h"

using namespace std::chrono_literals;

static constexpr int CONNECT_RETRIES            = 5;
static constexpr int DISCORD_API_VERSION        = 6;
static constexpr const char* DISCORD_API_URL    = "https://discord.com/api";

namespace ikura::discord
{
	static Synchronised<DiscordState>* _state = nullptr;
	static Synchronised<DiscordState>& state() { return *_state; }

	void heartbeat_worker()
	{
		auto last = std::chrono::system_clock::now();
		while(true)
		{
			if(!state().rlock()->connected)
				break;

			if(std::chrono::system_clock::now() - last >= state().rlock()->heartbeat_interval)
			{
				last = std::chrono::system_clock::now();
			}

			std::this_thread::sleep_for(250ms);
		}

		lg::log("discord", "heartbeat worker exited");
	}

	DiscordState::DiscordState(URL url, std::chrono::nanoseconds timeout) : ws(url, timeout)
	{
		auto backoff = 500ms;

		condvar<bool> didcon;

		// wait for the hello
		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.get<pj::object>();
			if(int op = obj["op"].get<int64_t>(); op == 10)
			{
				auto interval = obj["d"].get<pj::object>()["heartbeat_interval"].get<int64_t>();
				this->heartbeat_interval = std::chrono::milliseconds(interval);

				lg::log("discord", "connected (heartbeat = %ld ms)", interval);
				this->connected = true;
				didcon.set(true);
			}
			else
			{
				lg::warn("discord", "unhandled opcode %d", op);
			}
		});

		// try to connect.
		for(int i = 0; i < CONNECT_RETRIES; i++)
		{
			if(this->ws.connect())
				break;

			lg::warn("discord", "connection failed, retrying... (%d/%d)", i + 1, CONNECT_RETRIES);
			std::this_thread::sleep_for(backoff);
			backoff *= 2;
		}

		if(!this->ws.connected())
			lg::error("discord", "connection failed");

		if(!didcon.wait(true, 3000ms))
		{
			lg::error("discord", "connection failed (no hello)");
			this->ws.onReceiveText([](bool, ikura::str_view) { });
			this->ws.disconnect();
		}

		this->hb_thread = std::thread(heartbeat_worker);
	}

	void DiscordState::connect()
	{
		if(!this->ws.connected() || !this->connected)
			return;


	}

	void DiscordState::disconnect()
	{
		if(!this->ws.connected())
			return;

		this->connected = false;
		this->ws.disconnect();



		this->hb_thread.join();
	}























	void init()
	{
		assert(config::haveDiscord());

		auto [ hdr, res ] = request::get(URL(zpr::sprint("%s/v%d/gateway/bot", DISCORD_API_URL, DISCORD_API_VERSION)),
			{ /* no params */ }, {
				request::Header("Authorization", zpr::sprint("Bot %s", config::discord::getOAuthToken())),
				request::Header("User-Agent", "DiscordBot (https://github.com/zhiayang/ikurabot, 0.1.0)"),
				request::Header("Connection", "close"),
			}
		);

		pj::value resp; std::string err;
		pj::parse(resp, res.begin(), res.end(), &err);

		if(!err.empty())
		{
			lg::error("discord", "gateway json error: %s", err);
			return;
		}

		auto obj = resp.get<pj::object>();
		auto url = obj["url"].get<std::string>();

		auto limit = obj["session_start_limit"].get<pj::object>();
		auto rem = limit["remaining"].get<int64_t>();

		if(rem <= 20)
		{
			lg::warn("discord", "5 connection attempts remaining (reset in %ld seconds)",
				limit["reset_after"].get<int64_t>());
		}
		else if(rem == 0)
		{
			lg::error("discord", "connection rate limit reached (reset in %ld seconds)",
				limit["reset_after"].get<int64_t>());

			return;
		}

		// fixup the url with version and format.
		url = zpr::sprint("%s?v=%d&encoding=json", url, DISCORD_API_VERSION);
		lg::log("discord", "connecting to %s", url);

		_state = new Synchronised<DiscordState>(URL(url), 5000ms);
		state().wlock()->connect();
	}

	void shutdown()
	{
		if(!config::haveDiscord() || !_state || !state().rlock()->connected)
			return;

		state().wlock()->disconnect();
	}
}

// discord.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "picojson.h"

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
	static bool is_connected = false;
	static Synchronised<DiscordState>* _state = nullptr;
	static Synchronised<DiscordState>& state() { return *_state; }

	struct QueuedMsg
	{
		QueuedMsg() { }
		QueuedMsg(bool dc) : disconnected(dc) { }
		QueuedMsg(std::map<std::string, pj::value> m) : msg(std::move(m)) { }

		static QueuedMsg disconnect() { return QueuedMsg(true); }

		std::map<std::string, pj::value> msg;
		bool disconnected = false;
	};


	static MessageQueue<discord::QueuedMsg> msg_queue;
	MessageQueue<discord::QueuedMsg>& mqueue() { return msg_queue; }

	void heartbeat_worker()
	{
		auto last = std::chrono::system_clock::now();
		while(true)
		{
			if(!is_connected)
				break;

			if(state().rlock()->heartbeat_interval <= std::chrono::system_clock::now() - last)
			{
				state().perform_write([&](auto& st) {
					if(!st.didAckHeartbeat)
					{
						// no ack between the intervals -- disconnect now. discord says just send a non-1000
						// close code, so just use 1002 -- protocol_error.

						// TODO: support resume!
						lg::warn("discord", "did not receive heartbeat ack, disconnecting...");
						st.ws.disconnect(1002);
						st.connected = false;
						is_connected = false;
						return;
					}

					last = std::chrono::system_clock::now();
					st.didAckHeartbeat = false;

					st.ws.send(pj::value(std::map<std::string, pj::value> {
						{ "op", pj::value(opcode::HEARTBEAT) },
						{ "d",  (st.sequence == -1)
									? pj::value()
									: pj::value(st.sequence)
						}
					}).serialise());
					lg::log("discord", "sent heartbeat");
				});
			}

			std::this_thread::sleep_for(250ms);
		}

		lg::log("discord", "heartbeat worker exited");
	}

	void send_worker()
	{
		while(true)
		{
			auto msg = mqueue().pop_send();
			if(msg.disconnected)
				break;

			state().wlock()->ws.send(pj::value(std::move(msg.msg)).serialise());
		}

		lg::log("twitch", "send worker exited");
	}

	void recv_worker()
	{
		while(true)
		{
			auto msg = mqueue().pop_receive();
			if(msg.disconnected)
				break;

			state().wlock()->processMessage(msg.msg);
		}

		lg::log("twitch", "receive worker exited");
	}

	DiscordState::DiscordState(URL url, std::chrono::nanoseconds timeout) : ws(url, timeout)
	{
		auto backoff = 500ms;

		condvar<bool> didcon;

	retry:
		// wait for the hello
		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.get<pj::object>();
			auto op = obj["op"].get<int64_t>();

			if(op == opcode::HELLO)
			{
				auto interval = obj["d"].get<pj::object>()["heartbeat_interval"].get<int64_t>();
				this->heartbeat_interval = std::chrono::milliseconds(interval);

				lg::log("discord", "connected (heartbeat = %ld ms)", interval);

				// this is so dumb.
				this->didAckHeartbeat = true;
				this->connected = true;
				is_connected = true;
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

		if(!didcon.wait(true, 2000ms))
		{
			lg::error("discord", "connection failed (no hello)");
			this->ws.disconnect();

			goto retry;
		}

		this->hb_thread = std::thread(heartbeat_worker);
	}

	void DiscordState::connect()
	{
		if(!this->ws.connected() || !this->connected)
			return;

		bool success = false;
		condvar<bool> cv;

		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.get<pj::object>();
			auto op = obj["op"].get<int64_t>();

			if(op == opcode::DISPATCH)
			{
				// do a quick peek.
				if(obj["t"].is<std::string>() && obj["t"].get<std::string>() == "READY")
				{
					success = true;
					cv.set(true);
					lg::log("discord", "identified");
				}

				// we should still send this to the queue, because there is a chance that
				// we receive subsequent messages in fast succession (eg. GUILD_CREATE).
				mqueue().push_receive(std::move(obj));
			}
			else if(op == opcode::INVALID_SESS)
			{
				lg::warn("discord", "received invalid session");
				success = false;
				cv.set(true);
			}
			else
			{
				lg::warn("discord", "unhandled opcode '%d'", op);
			}
		});

		this->tx_thread = std::thread(send_worker);
		this->rx_thread = std::thread(recv_worker);

	retry:
		// send an identify
		this->ws.send(pj::value(std::map<std::string, pj::value> {
			{ "op", pj::value(opcode::IDENTIFY) },
			{
				"d", pj::value(std::map<std::string, pj::value> {
					{ "token",      pj::value(config::discord::getOAuthToken()) },
					{ "compress",   pj::value(false) },
					{ "intents",    pj::value(intent::GUILDS | intent::GUILD_MESSAGES | intent::GUILD_MESSAGE_REACTIONS) },
					{ "guild_subscriptions", pj::value(false) },
					{ "properties", pj::value(std::map<std::string, pj::value> {
						{ "$os",      pj::value("linux") },
						{ "$browser", pj::value("ikura") },
						{ "$device",  pj::value("ikura") }
					})}
				})
			}
		}).serialise());

		// wait for a ready
		if(!cv.wait(true, 1500ms) || !success)
		{
			lg::warn("discord", "identify timed out, waiting a little while...");
			std::this_thread::sleep_for(6s);
			goto retry;
		}

		// setup the real handler
		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.get<pj::object>();
			if(auto op = obj["op"].get<int64_t>(); op == opcode::HEARTBEAT)
			{
				// if we got a heartbeat, ack it.
				this->ws.send(pj::value(std::map<std::string, pj::value> {
					{ "op", pj::value(opcode::HEARTBEAT_ACK) }
				}).serialise());
			}
			else if(op == opcode::DISPATCH)
			{
				mqueue().push_receive(std::move(obj));
			}
			else if(op == opcode::HEARTBEAT_ACK)
			{
				this->didAckHeartbeat = true;
				lg::log("discord", "heartbeat ack");
			}
			else
			{
				lg::warn("discord", "unhandled opcode '%d'", op);
			}
		});
	}

	void DiscordState::disconnect()
	{
		if(!this->ws.connected())
			return;

		mqueue().push_send(QueuedMsg::disconnect());
		mqueue().push_receive(QueuedMsg::disconnect());

		this->connected = false;
		is_connected = false;

		this->hb_thread.join();
		this->tx_thread.join();
		this->rx_thread.join();

		this->ws.disconnect();
		lg::log("discord", "disconnected");
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
		else
		{
			lg::log("discord", "%ld connections left", rem);
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

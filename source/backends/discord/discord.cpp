// discord.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "picojson.h"

#include "defs.h"
#include "rate.h"
#include "async.h"
#include "config.h"
#include "synchro.h"
#include "discord.h"
#include "network.h"

using namespace std::chrono_literals;

constexpr int CONNECT_RETRIES            = 5;

namespace ikura::discord
{
	static bool should_heartbeat = false;
	static Synchronised<DiscordState>* _state = nullptr;
	static Synchronised<DiscordState>& state() { return *_state; }


	static MessageQueue<RxEvent, TxMessage> msg_queue;
	MessageQueue<RxEvent, TxMessage>& mqueue() { return msg_queue; }

	void heartbeat_worker()
	{
		auto last = std::chrono::system_clock::now();
		while(true)
		{
			if(!should_heartbeat)
				break;

			if(_state && state().rlock()->heartbeat_interval <= std::chrono::system_clock::now() - last)
			{
				state().perform_write([&](auto& st) {
					if(!st.didAckHeartbeat)
					{
						// no ack between the intervals -- disconnect now. discord says just send a non-1000
						// close code, so just use 1002 -- protocol_error.
						lg::warn("discord", "did not receive heartbeat ack, reconnecting...");

						dispatcher().run([&st]() {
							st.disconnect();
							st.resume();
						}).discard();

						should_heartbeat = false;
						return;
					}

					if(st.ws.connected())
					{
						last = std::chrono::system_clock::now();
						st.didAckHeartbeat = false;

						st.ws.send(pj::value(std::map<std::string, pj::value> {
							{ "op", pj::value(opcode::HEARTBEAT) },
							{ "d",  (st.sequence == -1)
										? pj::value()
										: pj::value(st.sequence)
							}
						}).serialise());
					}
				});
			}

			std::this_thread::sleep_for(250ms);
		}

		lg::dbglog("discord", "heartbeat worker exited");
	}

	void send_worker(); // defined in discord/channel.cpp
	void recv_worker()
	{
		while(true)
		{
			auto msg = mqueue().pop_receive();
			if(msg.disconnected)
				break;

			state().wlock()->processEvent(msg.msg);
		}

		lg::dbglog("discord", "receive worker exited");
	}

	DiscordState::DiscordState(URL url, std::chrono::nanoseconds timeout) : ws(url, timeout)
	{
	}

	bool DiscordState::init(bool _)
	{
		(void) _;

		auto backoff = 500ms;

		int retries = 0;
		condvar<bool> didcon;

	retry:
		// wait for the hello
		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.as_obj();
			auto op = obj["op"].as_int();

			if(op == opcode::HELLO)
			{
				auto interval = obj["d"].as_obj()["heartbeat_interval"].as_int();
				this->heartbeat_interval = std::chrono::milliseconds(interval);

				lg::log("discord", "connected (heartbeat = %ld ms)", interval);

				// this is so dumb.
				this->didAckHeartbeat = true;
				should_heartbeat = true;
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
			lg::warn("discord", "connection failed (no hello)");
			this->ws.disconnect();
			if(++retries > CONNECT_RETRIES)
			{
				lg::error("discord", "too many failures, aborting");
				return false;
			}

			goto retry;
		}

		this->didAckHeartbeat = true;
		this->hb_thread = std::thread(heartbeat_worker);
		return true;
	}

	void DiscordState::send_identify()
	{
		this->ws.send(pj::value(std::map<std::string, pj::value> {
			{ "op", pj::value(opcode::IDENTIFY) },
			{
				"d", pj::value(std::map<std::string, pj::value> {
					{ "token",      pj::value(config::discord::getOAuthToken()) },
					{ "compress",   pj::value(false) },
					{ "intents",    pj::value(intent::GUILDS
											| intent::GUILD_MESSAGES
											| intent::GUILD_MESSAGE_REACTIONS)
					},
					{ "guild_subscriptions", pj::value(false) },
					{ "properties", pj::value(std::map<std::string, pj::value> {
						{ "$os",      pj::value("linux") },
						{ "$browser", pj::value("ikura") },
						{ "$device",  pj::value("ikura") }
					})}
				})
			}
		}).serialise());
	}

	void DiscordState::send_resume()
	{
		this->ws.send(pj::value(std::map<std::string, pj::value> {
			{ "op", pj::value(opcode::RESUME) },
			{
				"d", pj::value(std::map<std::string, pj::value> {
					{ "token",      pj::value(config::discord::getOAuthToken()) },
					{ "session_id", pj::value(this->session_id) },
					{ "seq",        pj::value(this->sequence) },
				})
			}
		}).serialise());
	}

	bool DiscordState::internal_connect(bool resume)
	{
		if(!this->ws.connected())
			return false;

		bool resumable = true;
		bool success = false;
		condvar<bool> cv;

		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.as_obj();
			auto op = obj["op"].as_int();

			if(op == opcode::DISPATCH)
			{
				// do a quick peek.
				bool is_ready = (obj["t"].is_str() && obj["t"].as_str() == "READY");

				// we should still send this to the queue, because there is a chance that
				// we receive subsequent messages in fast succession (eg. GUILD_CREATE).
				mqueue().push_receive(std::move(obj));

				if(is_ready)
				{
					lg::log("discord", "%s", resume ? "resumed" : "identified");
					success = true;
					cv.set(true);
				}
			}
			else if(op == opcode::INVALID_SESS)
			{
				lg::warn("discord", "received invalid session");
				resumable = obj["d"].as_bool();
				success = false;

				cv.set(true);
			}
			else
			{
				lg::warn("discord", "unhandled opcode '%d'", op);
			}
		});

		int retries = 0;

		this->tx_thread = std::thread(send_worker);
		this->rx_thread = std::thread(recv_worker);

	retry:
		cv.set(false);

		if(resume) this->send_resume();
		else       this->send_identify();

		// wait for a ready
		if(!cv.wait(true, 1500ms) || !success)
		{
			if(++retries < CONNECT_RETRIES && this->ws.connected())
			{
				if(!resume || resumable)
				{
					lg::warn("discord", "%s timed out, waiting a little while...", resume ? "resume" : "identify");
					std::this_thread::sleep_for(6s);
				}
				else
				{
					lg::warn("discord", "resume failed, reconnecting normally");
					resume = false;
				}

				goto retry;
			}
			else
			{
				lg::warn("discord", "%s timed out", resume ? "resume" : "identify");
				this->disconnect();
				return false;
			}
		}


		// setup the real handler
		this->ws.onReceiveText([&](bool, ikura::str_view msg) {
			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.as_obj();
			auto op = obj["op"].as_int();

			if(op == opcode::HEARTBEAT)
			{
				// if we got a heartbeat, ack it.
				this->ws.send(pj::value(std::map<std::string, pj::value> {
					{ "op", pj::value(opcode::HEARTBEAT_ACK) }
				}).serialise());
			}
			else if(op == opcode::DISPATCH)
			{
				if(obj["t"].as_str() == "MESSAGE_CREATE" && obj["d"].as_obj()["content"].as_str().find("'x") == 0)
					goto uwu;

				mqueue().push_receive(std::move(obj));
			}
			else if(op == opcode::HEARTBEAT_ACK)
			{
				this->didAckHeartbeat = true;
				// lg::log("discord", "heartbeat ack");
			}
			else if(op == opcode::RECONNECT)
			{
			uwu:
				// since we are in the callback thread, we cannot disconnect from here.
				// so, use the dispatcher to disconnect and reconnect externally.
				dispatcher().run([this]() {

					lg::warn("discord", "server requested reconnect...");
					this->disconnect();
					this->resume();

				}).discard();
			}
			else
			{
				lg::warn("discord", "unhandled opcode '%d'", op);
			}
		});

		this->ws.onDisconnect([&]() {
			lg::warn("discord", "server disconnected us, attempting resume...");

			dispatcher().run([&]() {
				std::this_thread::sleep_for(1000ms);

				this->disconnect();
				this->resume();
			}).discard();
		});

		return true;
	}


	bool DiscordState::resume()
	{
		if(!this->init(/* resume: */ true))
			return false;

		return this->internal_connect(/* resume: */ true);
	}

	bool DiscordState::connect()
	{
		if(!this->init(/* resume: */ false))
			return false;

		return this->internal_connect(/* resume: */ false);
	}

	void DiscordState::disconnect(uint16_t code)
	{
		if(!this->ws.connected())
			return;

		this->ws.onReceiveText([](auto, auto) { });

		this->sequence = -1;
		mqueue().push_send(TxMessage::disconnect());
		mqueue().push_receive(RxEvent::disconnect());

		should_heartbeat = false;

		if(this->hb_thread.joinable())
			this->hb_thread.join();

		if(this->tx_thread.joinable())
			this->tx_thread.join();

		if(this->rx_thread.joinable())
			this->rx_thread.join();

		// this prevents us from reconnecting when we wanted to disconnect, lmao
		this->ws.onDisconnect([]() { });
		this->ws.disconnect(code);
		lg::log("discord", "disconnected");
	}























	void init()
	{
		assert(config::haveDiscord());

		auto [ hdr, res ] = request::get(URL(zpr::sprint("%s/v%d/gateway/bot",
			DiscordState::API_URL, DiscordState::API_VERSION)),
			{ /* no params */ }, {
				request::Header("Authorization", zpr::sprint("Bot %s", config::discord::getOAuthToken())),
				request::Header("User-Agent", "DiscordBot (https://github.com/zhiayang/ikurabot, 0.1.0)"),
				request::Header("Connection", "close"),
			}
		);

		pj::value resp; std::string err;
		pj::parse(resp, res.begin(), res.end(), &err);

		if(!err.empty())
			return lg::error("discord", "gateway json error: %s", err);

		auto obj = resp.as_obj();
		auto url = obj["url"].as_str();

		auto limit = obj["session_start_limit"].as_obj();
		auto rem = limit["remaining"].as_int();

		if(rem <= 20)
		{
			lg::warn("discord", "5 connection attempts remaining (reset in %ld seconds)",
				limit["reset_after"].as_int());
		}
		else if(rem == 0)
		{
			return lg::error("discord", "connection rate limit reached (reset in %ld seconds)",
				limit["reset_after"].as_int());
		}
		else
		{
			lg::log("discord", "%ld connections left", rem);
		}

		// fixup the url with version and format.
		url = zpr::sprint("%s?v=%d&encoding=json", url, DiscordState::API_VERSION);
		lg::log("discord", "connecting to %s", url);

		_state = new Synchronised<DiscordState>(URL(url), 5000ms);
		state().wlock()->connect();
	}

	void shutdown()
	{
		if(!config::haveDiscord() || !_state || !should_heartbeat)
			return;

		state().wlock()->disconnect();
	}
}

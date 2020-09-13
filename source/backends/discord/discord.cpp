// discord.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "picojson.h"

#include "db.h"
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
						lg::error("discord", "did not receive heartbeat ack, reconnecting...");

						dispatcher().run([&st]() {
							auto [ seq, ses ] = std::make_tuple(st.sequence, st.session_id);

							st.disconnect();
							st.resume(seq, ses);
						}).discard();

						// should_heartbeat = false;
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

			util::sleep_for(250ms);
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

	bool DiscordState::init()
	{
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
				lg::error("discord", "unhandled opcode %d", op);
			}
		});

		// try to connect.
		for(int i = 0; i < CONNECT_RETRIES; i++)
		{
			if(this->ws.connect())
				break;

			lg::warn("discord", "connection failed, retrying... (%d/%d)", i + 1, CONNECT_RETRIES);
			util::sleep_for(backoff);
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

		this->ws.onReceiveText([](auto...) { });
		this->didAckHeartbeat = true;

		// massive hax
		if(this->hb_thread.joinable())
			this->hb_thread.detach();

		this->hb_thread = std::thread(heartbeat_worker);
		return true;
	}

	void DiscordState::send_identify()
	{
		lg::log("discord", "identifying...");
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

	void DiscordState::send_resume(int64_t seq, const std::string& ses)
	{
		lg::log("discord", "resuming session '%s', seq %d", ses, seq);
		this->ws.send(pj::value(std::map<std::string, pj::value> {
			{ "op", pj::value(opcode::RESUME) },
			{
				"d", pj::value(std::map<std::string, pj::value> {
					{ "token",      pj::value(config::discord::getOAuthToken()) },
					{ "session_id", pj::value(ses) },
					{ "seq",        pj::value(seq) },
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

		int retries = 0;

		this->tx_thread = std::thread(send_worker);
		this->rx_thread = std::thread(recv_worker);


	retry:
		this->ws.onReceiveText([resume, &cv, &success, &resumable](bool, ikura::str_view msg) {
			// if(cv.get() || success)
			//	return;

			pj::value json; std::string err;
			pj::parse(json, msg.begin(), msg.end(), &err);

			auto obj = json.as_obj();
			auto op = obj["op"].as_int();

			if(op == opcode::DISPATCH)
			{
				if(resume)
				{
					// if we received a dispatch while we're trying to resume, then we can assume that
					// the resume has succeeded.
					mqueue().push_receive(std::move(obj));

					lg::log("discord", "resumed");
					success = true;
					cv.set(true);
				}
				else
				{
					// do a quick peek.
					bool is_ready = (obj["t"].is_str() && obj["t"].as_str() == "READY");

					if(!is_ready)
						lg::warn("discord", "received dispatch before identify");

					// we should still send this to the queue, because there is a chance that
					// we receive subsequent messages in fast succession (eg. GUILD_CREATE).
					mqueue().push_receive(std::move(obj));

					if(is_ready)
					{
						lg::log("discord", "identified");
						success = true;
						cv.set(true);
					}
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

		cv.set_quiet(false);

		if(resume && !this->session_id.empty())
			this->send_resume(this->sequence, this->session_id);

		else
			this->send_identify();

		// wait for a ready
		if(!cv.wait(true, 3000ms) || !success)
		{
			if(++retries < CONNECT_RETRIES && this->ws.connected())
			{
				if(!resume || resumable)
				{
					lg::warn("discord", "%s timed out, waiting a little while...", resume ? "resume" : "identify");
					util::sleep_for(6s);
				}
				else
				{
					lg::warn("discord", "resume failed, reconnecting normally");
					this->session_id = "";
					this->sequence = -1;

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
		this->ws.onReceiveText([this](bool, ikura::str_view msg) {
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
					auto [ seq, ses ] = std::make_tuple(this->sequence, this->session_id);

					this->disconnect();
					this->resume(seq, ses);

				}).discard();
			}
			else
			{
				lg::warn("discord", "unhandled opcode '%d'", op);
			}
		});

		this->ws.onDisconnect([this]() {
			lg::warn("discord", "server disconnected us, attempting resume...");

			dispatcher().run([this]() {
				util::sleep_for(1000ms);

				this->disconnect();
				util::sleep_for(1000ms);

				this->resume();
			}).discard();
		});

		return true;
	}


	bool DiscordState::resume(int64_t seq, const std::string& ses)
	{
		this->sequence = seq;
		this->session_id = ses;

		if(!this->init())
			return false;

		bool r = true;
		while(!this->internal_connect(/* resume: */ r))
		{
			r = false;

			// try again after 10s?
			this->disconnect();

			lg::warn("discord", "retry after 10s...");
			util::sleep_for(10s);
			this->init();
		}

		return true;
	}

	bool DiscordState::connect()
	{
		if(!this->init())
			return false;

		return this->internal_connect(/* resume: */ false);
	}

	void DiscordState::disconnect(uint16_t code)
	{
		this->ws.onReceiveText([](auto, auto) { });

		database().perform_write([this](auto& db) {
			db.discordData.lastSequence = this->sequence;
			db.discordData.lastSession = this->session_id;
		});

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

		if(this->ws.connected())
			this->ws.disconnect(code);

		lg::log("discord", "disconnected");
	}


	const Channel* getChannel(Snowflake id)
	{
		return state().map_read([&id](auto& st) -> const Channel* {
			if(auto it = st.channels.find(id); it != st.channels.end())
				return &it->second;

			return nullptr;
		});
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

		int64_t seq = 0;
		std::string ses;
		std::tie(seq, ses) = database().map_read([](auto& db) -> auto {
			return std::pair(db.discordData.lastSequence, db.discordData.lastSession);
		});

		// try to resume.
		state().wlock()->resume(seq, ses);
	}

	void shutdown()
	{
		if(!config::haveDiscord() || !_state || !should_heartbeat)
			return;

		state().wlock()->disconnect();
	}
}

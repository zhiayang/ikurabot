// server.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

#include "db.h"
#include "irc.h"
#include "config.h"
#include "synchro.h"

using namespace std::chrono_literals;

namespace ikura::irc
{
	constexpr static int CONNECT_RETRIES = 5;

	template <typename Fn>
	void readMessagesFromSocket(Socket& socket, Buffer& buf, size_t& offset, Fn&& fn)
	{
		socket.onReceive([&](Span data) {

			if(buf.remaining() < data.size())
				buf.grow(data.size());

			buf.write(data);
			auto sv = buf.span().sv().drop(offset);

			while(true)
			{
				auto x = sv.find("\r\n");
				if(x == std::string::npos)
					return;

				// lg::log("irc", "<< {}", sv.take(x));

				fn(sv.take(x));
				auto len = x + strlen("\r\n");

				sv.remove_prefix(len);
				offset += len;
			}

			// if we got here, we cleared all the messages, so clear the buffer.
			buf.clear();
			offset = 0;
		});
	}









	IRCServer::IRCServer(const config::irc::Server& config, std::chrono::nanoseconds timeout)
		: socket(config.hostname, config.port, config.useSSL, timeout)
	{
		bool use_sasl = config.useSASL;

		auto sys = zpr::sprint("irc/{}", config.name);

		// try to connect.
		lg::log(sys, "connecting to {}:{}", config.hostname, config.port);

		auto backoff = 500ms;
		for(int i = 0; i < CONNECT_RETRIES; i++)
		{
			if(this->socket.connect())
				break;

			lg::warn(sys, "connection failed, retrying... ({}/{})", i + 1, CONNECT_RETRIES);
			util::sleep_for(backoff);
			backoff *= 2;
		}

		// TODO: we don't support server passwords

		// if we use SASL, we should CAP REQ it. the thing is, because of the weird "ident reponse" thing,
		// we'll probably not get a reply from the server with the CAP ACK till like 5-10 seconds later.
		// so, just fire off the request and check for the ACK after sending USER and NICK.
		if(use_sasl)
		{
			this->sendRawMessage("CAP REQ :sasl");
		}

		// send the nickname and the username.
		// (for username the hostname and servername are ignored so just send *)
		this->sendRawMessage(zpr::sprint("NICK {}", config.nickname));
		this->sendRawMessage(zpr::sprint("USER {} * * :{}", config.username, config.username));

		bool nicknameUsed = false;

		if(use_sasl)
		{
			// most places only support plain anyway. shouldn't be a problem if we're using SSL...
			this->sendRawMessage("AUTHENTICATE PLAIN");

			condvar<bool> cv;

			size_t offset = 0;
			auto buf = Buffer(1024);
			readMessagesFromSocket(this->socket, buf, offset, [&](ikura::str_view sv) {

				auto res = parseMessage(sv);
				if(!res.has_value())
				{
					lg::warn(sys, "invalid irc message");
					return;
				}

				auto& msg = res.value();

				if(msg.command == "AUTHENTICATE")
				{
					if(msg.params.size() != 1 || msg.params[0] != "+")
					{
						lg::warn(sys, "invalid AUTHENTICATE: {}", msg.params.empty() ? "<empty>" : msg.params[0]);
						use_sasl = false;
					}

					cv.set(true);
				}
				else if(msg.command == "CAP")
				{
					// we should get one of these:
					// :orwell.freenode.net CAP * ACK :sasl
					// :orwell.freenode.net CAP * NAK :sasl

					if(msg.params.size() != 3 || msg.params[1] == "NAK")
					{
						lg::warn(sys, "invalid CAP: {} {} {}",
							msg.params.size() < 1 ? "<?>" : msg.params[0],
							msg.params.size() < 2 ? "<?>" : msg.params[1],
							msg.params.size() < 3 ? "<?>" : msg.params[2]);

						use_sasl = false;
						cv.set(true);
					}
					else if(msg.params.size() == 3 && msg.params[1] == "ACK" && msg.params[2] == "sasl")
					{
						// just log it; don't wake up, because we need to actually see the AUTHENTICATE
						// response as well.
						lg::log(sys, "server supports SASL");
					}
				}
				else if(msg.command == "433")
				{
					nicknameUsed = true;
				}
			});

			auto res = cv.wait(true, 20s);

			// need to reset the callback on the socket
			this->socket.onReceive([](auto) { });

			if(!res || !use_sasl)
			{
				lg::error(sys, "did not receive SASL response from server: {}", res ? "invalid response" : "timed out");
				goto no_sasl;
			}

			// authenticate with the base-64 encoded auth string, which is
			// <username> NULL <username> NULL <password>
			// since it contains null bytes, use a Buffer instead of a string.

			{
				auto str_to_span = [](ikura::str_view sv) -> Span {
					return Span((const uint8_t*) sv.data(), sv.size());
				};

				auto buf = Buffer(400);
				buf.write(str_to_span(config.username));
				buf.write("\0", 1);
				buf.write(str_to_span(config.username));
				buf.write("\0", 1);
				buf.write(str_to_span(config.password));

				auto auth_str = base64::encode((const uint8_t*) buf.data(), buf.size());

				// TODO: split this properly.
				assert(auth_str.size() < 400);

				this->sendRawMessage(zpr::sprint("AUTHENTICATE {}", auth_str));
			}

			// reuse the cv
			cv.set(false);

			std::string reason;

			buf.clear();
			offset = 0;
			readMessagesFromSocket(this->socket, buf, offset, [&](ikura::str_view sv) {
				auto res = parseMessage(sv);
				if(!res.has_value())
				{
					lg::warn(sys, "invalid irc message");
					return;
				}
				auto& msg = res.value();


				if(msg.command == "903")
					cv.set(true);

				else if(msg.command == "902")
					reason = "nickname unavailable", cv.set(true);

				else if(msg.command == "904")
					reason = "invalid credentials", cv.set(true);
			});

			res = cv.wait(true, 3000ms);
			if(!res || !reason.empty())
			{
				lg::error(sys, "authentication failed: {}", !res ? "timeout" : reason);
				goto failure;
			}

			this->sendRawMessage("CAP END");
			this->socket.onReceive([](auto) { });

			lg::log(sys, "SASL authentication successful");
		}
		else if(!config.password.empty())
		{
		no_sasl:
			// message nickserv, i guess.
			// TODO: support authenticating with nickserv
			lg::error(sys, "NOT SUPPORTED KEKW");
			goto failure;
		}

		this->name      = config.name;
		this->owner     = config.owner;
		this->nickname  = config.nickname;
		this->username  = config.username;
		this->is_connected = true;

		if(nicknameUsed)
		{
			lg::warn(sys, "nickname '{}' is already in use", this->nickname);

			auto nick = this->nickname;
			for(int i = 0; i < 5 && nicknameUsed; i++)
			{
				nick += "_";

				size_t offset = 0;
				auto buf = Buffer(256);

				lg::log(sys, "trying '{}'...", nick);

				condvar<bool> cv;

				this->sendRawMessage(zpr::sprint("NICK {}", nick));
				readMessagesFromSocket(this->socket, buf, offset, [&](ikura::str_view sv) {
					auto res = parseMessage(sv);
					if(!res.has_value())
					{
						lg::warn(sys, "invalid irc message");
						return;
					}
					auto& msg = res.value();

					if(msg.command == "NICK" && msg.params.size() > 0 && msg.params[0] == nick)
						nicknameUsed = false, cv.set(true);

					else if(msg.command == "MODE" && msg.params.size() > 1 && msg.params[0] == nick)
						nicknameUsed = false, cv.set(true);

					else if(msg.command == "433")
						cv.set(true);
				});

				cv.wait(true, 2000ms);

				if(!nicknameUsed)
					break;
			}

			this->socket.onReceive([](auto) { });
		}

		this->rx_thread = std::thread(&IRCServer::recv_worker, this);
		this->tx_thread = std::thread(&IRCServer::send_worker, this);

		database().perform_write([&](auto& db) {
			auto& srv = db.ircData.servers[this->name];

			srv.name        = this->name;
			srv.hostname    = config.hostname;

			for(const auto& ch : config.channels)
			{
				this->channels[ch.name] = Channel(this, ch.name, config.nickname, ch.lurk, ch.respondToPings, ch.silentInterpErrors,
					ch.runMessageHandlers, ch.commandPrefixes);

				srv.channels[ch.name].name = ch.name;
			}
		});

		lg::log(sys, "connected");
		return;

	failure:
		this->socket.disconnect();
		this->is_connected = false;
	}

	IRCServer::~IRCServer()
	{
		this->mqueue.push_send(QueuedMsg::disconnect());
		this->mqueue.push_receive(QueuedMsg::disconnect());

		if(this->rx_thread.joinable())
			this->rx_thread.join();

		if(this->tx_thread.joinable())
			this->tx_thread.join();

		if(this->is_connected)
			this->disconnect();
	}

	void IRCServer::connect()
	{
		// join the channels.
		for(const auto& [ name, chan ] : this->channels)
			this->sendRawMessage(zpr::sprint("JOIN {}", name));
	}

	void IRCServer::disconnect()
	{
		this->is_connected = false;

		this->sendRawMessage("QUIT");
		this->socket.disconnect();

		// just quit, then close the socket.
		lg::log(zpr::sprint("irc/{}", this->name), "disconnected");
	}





	void IRCServer::recv_worker()
	{
		size_t offset = 0;
		auto buf = Buffer(512);

		// setup the socket handler
		readMessagesFromSocket(this->socket, buf, offset, [&](ikura::str_view sv) {
			this->mqueue.push_receive(sv.str());
		});

		while(true)
		{
			auto msg = this->mqueue.pop_receive();
			if(msg.disconnected)
				break;

			this->processMessage(msg.msg);
		}

		lg::dbglog(zpr::sprint("irc/{}", this->name), "receive worker exited");
		this->socket.onReceive([](auto) { });
	}

	void IRCServer::send_worker()
	{
		while(true)
		{
			auto msg = this->mqueue.pop_send();
			if(msg.disconnected)
				break;
		}

		lg::dbglog(zpr::sprint("irc/{}", this->name), "send worker exited");
	}
}

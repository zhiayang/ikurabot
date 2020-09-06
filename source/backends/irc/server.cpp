// server.cpp
// Copyright (c) 2020, zhiayang, Apache License 2.0.

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

				zpr::println("rx = %s", sv.take(x));
				auto _msg = irc::parseMessage(sv.take(x));

				if(_msg.has_value())
				{
					fn(_msg.value());
					auto len = x + strlen("\r\n");

					sv.remove_prefix(len);
					offset += len;
				}
				else
				{
					lg::warn("irc", "received invalid irc message");
				}
			}

			// if we got here, we cleared all the messages, so clear the buffer.
			buf.clear();
			offset = 0;
		});
	}









	IRCServer::IRCServer(const config::irc::Server& server, std::chrono::nanoseconds timeout)
		: socket(server.hostname, server.port, server.useSSL, timeout)
	{
		bool use_sasl = server.useSASL;

		auto sys = zpr::sprint("irc/%s", server.name);

		// try to connect.
		lg::log(sys, "connecting...");

		auto backoff = 500ms;
		for(int i = 0; i < CONNECT_RETRIES; i++)
		{
			if(this->socket.connect())
				break;

			lg::warn(sys, "connection failed, retrying... (%d/%d)", i + 1, CONNECT_RETRIES);
			util::sleep_for(backoff);
			backoff *= 2;
		}

		// TODO: we don't support server passwords

		// if we use SASL, we should CAP REQ it.
		if(use_sasl)
		{
			// wait for the server to respond with the ack.
			this->sendRawMessage("CAP REQ :sasl");

			condvar<bool> cv;

			size_t offset = 0;
			auto buf = Buffer(1024);
			readMessagesFromSocket(this->socket, buf, offset, [&](const IRCMessage& msg) {

				// we shouldn't even get cap ack at this point.
				if(msg.command == "CAP")
				{
					// we should get one of these:
					// :orwell.freenode.net CAP * ACK :sasl
					// :orwell.freenode.net CAP * NAK :sasl

					if(msg.params.size() != 3 || msg.params[1] == "NAK")
						use_sasl = false;

					cv.set(true);
				}

				zpr::println("msg: %s", msg.command);
			});

			cv.wait(true, 500ms);
			if(!use_sasl)
				lg::warn(sys, "failed to negotiate SASL, falling back on NickServ");

			// need to reset the callback on the socket
			this->socket.onReceive([](auto) { });
		}

		// send the nickname and the username.
		// (for username the hostname and servername are ignored so just send *)
		this->sendRawMessage(zpr::sprint("NICK %s", server.nickname));
		this->sendRawMessage(zpr::sprint("USER %s * * :%s", server.username, server.username));

		if(use_sasl)
		{
			// most places only support plain anyway. shouldn't be a problem if we're using SSL...
			this->sendRawMessage("AUTHENTICATE PLAIN");

			condvar<bool> cv;

			size_t offset = 0;
			auto buf = Buffer(1024);
			readMessagesFromSocket(this->socket, buf, offset, [&](const IRCMessage& msg) {

				if(msg.command == "AUTHENTICATE")
				{
					if(msg.params.size() != 1 || msg.params[0] != "+")
						use_sasl = false;

					cv.set(true);
				}
			});

			auto res = cv.wait(true, 10000ms);

			// need to reset the callback on the socket
			this->socket.onReceive([](auto) { });

			if(!res || !use_sasl)
			{
				lg::error(sys, "did not receive SASL response from server: %s", res ? "invalid response" : "timed out");
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
				buf.write(str_to_span(server.username));
				buf.write("\0", 1);
				buf.write(str_to_span(server.username));
				buf.write("\0", 1);
				buf.write(str_to_span(server.password));

				auto auth_str = base64::encode((const uint8_t*) buf.data(), buf.size());

				// TODO: split this properly.
				assert(auth_str.size() < 400);

				this->sendRawMessage(zpr::sprint("AUTHENTICATE %s", auth_str));
			}

			// reuse the cv
			cv.set(false);

			std::string reason;

			buf.clear();
			offset = 0;
			readMessagesFromSocket(this->socket, buf, offset, [&](const IRCMessage& msg) {
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
				lg::error(sys, "authentication failed: %s", !res ? "timeout" : reason);
				goto failure;
			}

			this->sendRawMessage("CAP END");
		}
		else if(!server.password.empty())
		{
		no_sasl:
			// message nickserv, i guess.
			// TODO: support authenticating with nickserv
			lg::error(sys, "NOT SUPPORTED KEKW");
			goto failure;
		}

		this->is_connected = true;
		return;

	failure:
		this->socket.disconnect();
		this->is_connected = false;
	}

	void IRCServer::sendRawMessage(ikura::str_view msg)
	{
		// imagine making copies in $YEAR
		auto s = zpr::sprint("%s\r\n", msg);
		this->socket.send(ikura::Span((const uint8_t*) s.data(), s.size()));

		// zpr::println(">> %s", s);
	}
}

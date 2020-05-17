// socket.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura
{
	Socket::Socket() { this->is_connected = false; }

	Socket::Socket(const URL& url, bool ssl, std::chrono::nanoseconds timeout) : Socket(url.hostname(), url.port(), ssl, timeout) { }

	Socket::Socket(ikura::str_view h, uint16_t p, bool ssl, std::chrono::nanoseconds timeout) : _host(h), _port(p)
	{
		this->is_connected = false;
		this->rx_callback = [](Span) { };

		this->socket = kissnet::socket(
			ssl ? kissnet::protocol::tcp_ssl : kissnet::protocol::tcp,
			kissnet::endpoint(this->_host, this->_port)
		);

		if(timeout > 0ns)
		{
			auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
			this->socket.set_timeout(micros);

			this->timeout = timeout;
		}
	}

	Socket::~Socket()
	{
		if(this->is_connected)
		{
			this->is_connected = false;
			this->socket.close();
		}
	}

	bool Socket::connected()
	{
		return this->is_connected;
	}

	bool Socket::connect()
	{
		this->is_connected = this->socket.connect();

		if(!this->is_connected)
		{
			this->socket.close();
			lg::error("socket", "connection failed");
			return false;
		}

		// make sure there is some timeout, so that the socket can be disconnected externally
		// and the thread will be able to respond and break out of its loop. this timeout is
		// cannot be set by user code; that only controls the timeout for the initial connection.
		this->socket.set_timeout((200'000us).count());

		this->thread = std::thread([this]() {
			while(true)
			{
				// fprintf(stderr, ".");
				if(!this->is_connected)
					break;

				// do a blocking read.
				auto [ len, status ] = this->socket.recv(this->internal_buffer, BufferSize);
				if(status == kissnet::socket_status::cleanly_disconnected)
				{
					break;
				}
				else if(!status)
				{
					lg::error("socket", "read failed: status: %d", status.get_value());
					break;
				}
				else if(len > 0 && this->rx_callback)
				{
					this->rx_callback(Span(this->internal_buffer, len));
				}
			}
		});

		return true;
	}

	void Socket::disconnect()
	{
		if(this->is_connected)
		{
			this->is_connected = false;
			this->socket.close();
		}

		// don't try to call join from the thread itself. this can happen when we want
		// to call disconnect while inside a packet handler; for example, the remote end
		// tells us to die.
		if(this->thread.joinable() && this->thread.get_id() != std::this_thread::get_id())
			this->thread.join();
	}

	size_t Socket::availableBytes()
	{
		return this->socket.bytes_available();
	}

	void Socket::send(Span sv)
	{
		this->socket.send(sv.data(), sv.size());
	}

	void Socket::onReceive(std::function<RxCallbackFn>&& fn)
	{
		this->rx_callback = std::move(fn);
	}
}

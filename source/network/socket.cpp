// socket.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "network.h"

using namespace std::chrono_literals;

namespace ikura
{
	Socket::Socket() { }

	Socket::Socket(const URL& url, bool ssl, std::chrono::nanoseconds timeout) : Socket(url.hostname(), url.port(), ssl, timeout) { }

	Socket::Socket(std::string_view h, uint16_t p, bool ssl, std::chrono::nanoseconds timeout) : _host(h), _port(p)
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
		}
	}

	Socket::~Socket()
	{
		this->is_connected = false;
		this->socket.close();
	}

	bool Socket::connected()
	{
		return this->is_connected.load();
	}

	bool Socket::connect()
	{
		this->is_connected = this->socket.connect();

		if(!this->is_connected)
			return false;

		this->thread = std::thread([this]() {
			while(true)
			{
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

			if(this->thread.joinable())
				this->thread.join();
		}
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

// connection.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "network.h"

namespace ikura
{
	Connection::Connection(std::string_view h, uint16_t p, bool ssl) : _host(h), _port(p)
	{
		this->is_connected = false;
		this->rx_callback = [](Span) { };

		this->socket = kissnet::socket(
			ssl ? kissnet::protocol::tcp_ssl : kissnet::protocol::tcp,
			kissnet::endpoint(zpr::sprint("%s:%u", this->_host, this->_port))
		);
	}

	Connection::~Connection()
	{
		this->is_connected = false;
		this->socket.close();
	}

	bool Connection::connected()
	{
		return this->is_connected.load();
	}

	bool Connection::connect()
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
				else if(len == 0 || !status)
				{
					error("read failed: len: %zu, status: %d", len, status.get_value());
					break;
				}
				else if(this->rx_callback)
				{
					this->rx_callback(Span(this->internal_buffer, len));
				}
			}
		});

		return true;
	}

	void Connection::disconnect()
	{
		if(this->is_connected)
		{
			this->is_connected = false;
			this->socket.close();

			if(this->thread.joinable())
				this->thread.join();
		}
	}

	size_t Connection::availableBytes()
	{
		return this->socket.bytes_available();
	}

	void Connection::send(Span sv)
	{
		this->socket.send(sv.data(), sv.size());
	}

	void Connection::onReceive(std::function<RxCallbackFn>&& fn)
	{
		this->rx_callback = std::move(fn);
	}
}

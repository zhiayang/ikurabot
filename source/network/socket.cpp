// socket.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <poll.h>

#include "defs.h"
#include "async.h"
#include "network.h"
#include "kissnet.h"

using namespace std::chrono_literals;

namespace ikura
{
	constexpr std::chrono::microseconds LOOP_PERIOD = 200'000us;

	Socket::Socket() { this->is_connected = false; }

	Socket::Socket(const URL& url, bool ssl, std::chrono::nanoseconds timeout) : Socket(url.hostname(), url.port(), ssl, timeout) { }

	Socket::Socket(ikura::str_view h, uint16_t p, bool s, std::chrono::nanoseconds timeout) : _host(h), _port(p), _ssl(s)
	{
		this->is_connected = false;
		this->rx_callback = [](Span) { };

		this->timeout = timeout;
	}

	Socket::Socket(std::string host, uint16_t port, kissnet::socket<4>* socket, std::chrono::nanoseconds timeout)
	{
		assert(socket);

		this->is_connected = false;
		this->rx_callback = [](Span) { };

		this->_port = port;
		this->_host = std::move(host);
		this->_ssl  = (socket->get_protocol() == kissnet::protocol::tcp_ssl);
		this->socket = socket;

		if(timeout > 0ns)
		{
			auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
			this->socket->set_timeout(micros);
			this->timeout = timeout;
		}
	}

	Socket::~Socket()
	{
		this->disconnect();
	}

	bool Socket::connected()
	{
		return this->is_connected;
	}

	void Socket::setup_receiver()
	{
		// make sure there is some timeout, so that the socket can be disconnected externally
		// and the thread will be able to respond and break out of its loop. this timeout is
		// cannot be set by user code; that only controls the timeout for the initial connection.
		this->socket->set_timeout(LOOP_PERIOD.count());

		// lg::error("socket", "SETUP_RECEIVER");
		this->thread = std::thread([this]() {
			while(true)
			{
				// fprintf(stderr, ".");
				if(!this->is_connected || this->socket == nullptr || this->socket->fd() == -1)
					break;

				// do a blocking read.
				auto [ len, status ] = this->socket->recv(this->internal_buffer, BufferSize);
				if(status == kissnet::socket_status::cleanly_disconnected || !this->is_connected)
				{
					break;
				}
				else if(!status)
				{
					if(status.get_value() != 0)
						lg::error("socket", "read failed: status: %d", status.get_value());

					break;
				}
				else if(len > 0 && this->rx_callback)
				{
					this->rx_callback(Span(this->internal_buffer, len));
				}
			}
		});
	}

	bool Socket::connect()
	{
		assert(this->socket == nullptr);
		this->socket = new kissnet::socket<4>(
			this->_ssl ? kissnet::protocol::tcp_ssl : kissnet::protocol::tcp,
			kissnet::endpoint(this->_host, this->_port)
		);

		if(this->timeout > 0ns)
		{
			auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
			this->socket->set_timeout(micros);
		}

		this->is_connected = this->socket->connect();

		if(!this->is_connected)
		{
			lg::error("socket", "connection failed: %s", strerror(errno));
			this->socket->close();
			return false;
		}

		this->setup_receiver();
		return true;
	}

	void Socket::force_disconnect()
	{
		this->is_connected = false;

		this->onReceive([](auto) { });
		this->onDisconnect([]() { });

		// the socket will be closed, and we set it to null -- since we cannot join the thread,
		// just detach it and trust that it will exit properly.
		this->thread.detach();

		if(this->socket != nullptr)
		{
			// this is really quite dumb. to prevent SSL from throwing a fit because we deleted
			// the socket (and hence deleted pSSL and pContext), we can only perform this in a
			// separate thread, after waiting for some amount of time to ensure that the handler
			// has definitely (a) timed out from SSL_read, and (b) exited.

			// we can set it to nullptr, just store it first.
			auto sock = this->socket;
			this->socket = nullptr;

			dispatcher().run([](kissnet::socket<4>* sock) {
				util::sleep_for(2 * LOOP_PERIOD);
				sock->close();
				delete sock;
			}, sock);
		}
	}

	void Socket::disconnect(bool quietly)
	{
		if(this->thread.get_id() == std::this_thread::get_id())
			lg::fatal("socket", "cannot disconnect from handler thread!");


		this->is_connected = false;
		this->onReceive([](auto) { });

		if(this->thread.joinable())
			this->thread.join();

		// must join the thread before closing the socket, if not we risk closing the socket
		// from under the recv() call, which SSL does *not* like.
		if(this->socket != nullptr)
		{
			this->socket->close();
			delete this->socket;

			this->socket = nullptr;

			if(!quietly && this->close_callback)
				this->close_callback();

			this->onDisconnect([]() { });
		}
	}

	size_t Socket::availableBytes()
	{
		return this->socket->bytes_available();
	}

	void Socket::send(Span sv)
	{
		this->socket->send(sv.data(), sv.size());
	}

	void Socket::onReceive(std::function<RxCallbackFn> fn)
	{
		this->rx_callback = std::move(fn);
	}

	void Socket::onDisconnect(std::function<void (void)> fn)
	{
		this->close_callback = std::move(fn);
	}

	bool Socket::listen()
	{
		this->socket = new kissnet::socket<4>(
			this->_ssl ? kissnet::protocol::tcp_ssl : kissnet::protocol::tcp,
			kissnet::endpoint(this->_host, this->_port)
		);

		if(this->timeout > 0ns)
		{
			auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
			this->socket->set_timeout(micros);
		}

		this->socket->set_non_blocking(true);

		int yes = 1;
		setsockopt(this->socket->fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		if(!this->socket->bind() || !this->socket->listen())
			return false;

		this->is_connected = true;
		return true;
	}

	Socket* Socket::accept(std::chrono::nanoseconds timeout)
	{
		if(!this->is_connected)
		{
			lg::error("socket", "cannot accept() when not listening");
			return nullptr;
		}

		auto fd = this->socket->fd();
		auto fds = pollfd {
			.fd     = fd,
			.events = POLLIN
		};

		auto ret = poll(&fds, 1, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
		if(ret == 0)    return nullptr;
		if(ret == -1)   { lg::error("socket", "poll error: %s", strerror(errno)); return nullptr; }

		if(fds.revents & POLLIN)
		{
			auto sock = new kissnet::socket<4>(this->socket->accept());
			if(!sock->is_valid())
				return nullptr;

			sock->set_non_blocking(false);
			sock->set_timeout(LOOP_PERIOD.count());
			auto ret = new Socket(this->_host, this->_port, sock);
			ret->is_connected = true;
			ret->setup_receiver();
			return ret;
		}
		else
		{
			return nullptr;
		}
	}

	std::string Socket::getAddress()
	{
		if(!this->is_connected)
			return "";

		return this->socket->get_bind_loc().address;
	}
}

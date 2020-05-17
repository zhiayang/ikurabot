// network.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <chrono>
#include <thread>
#include <string>

#include "types.h"
#include "buffer.h"
#include "kissnet.h"

namespace ikura
{
	struct URL
	{
		explicit URL(ikura::str_view url);

		std::string& protocol()             { return this->_protocol; }
		const std::string& protocol() const { return this->_protocol; }

		std::string& hostname()             { return this->_hostname; }
		const std::string& hostname() const { return this->_hostname; }

		std::string& resource()             { return this->_resource; }
		const std::string& resource() const { return this->_resource; }

		uint16_t port() const               { return this->_port; }
		std::string str() const;

	private:
		std::string _protocol;
		std::string _hostname;
		std::string _resource;
		uint16_t _port = 0;
	};



	struct Socket
	{
		friend struct WebSocket;

		using RxCallbackFn = void(Span);

		Socket(const URL& url, bool ssl = false, std::chrono::nanoseconds timeout = { });
		Socket(ikura::str_view host, uint16_t port, bool ssl, std::chrono::nanoseconds timeout = { });
		~Socket();

		bool connect();
		void disconnect();

		size_t availableBytes();
		bool connected();

		void send(Span data);
		void onReceive(std::function<RxCallbackFn>&& fn);

		const std::string& host() const { return this->_host; }
		uint16_t port() const { return this->_port; }

	private:
		Socket();

		std::string _host;
		uint16_t _port;

		std::thread thread;
		kissnet::socket<> socket;
		bool is_connected = false;

		std::chrono::nanoseconds timeout;

		std::function<RxCallbackFn> rx_callback;

		static constexpr size_t BufferSize = 4096;
		uint8_t internal_buffer[BufferSize] = { };
	};

	struct WebSocket
	{
		static constexpr size_t DEFAULT_FRAME_BUFFER_SIZE = 8192;

		// first argument is true if the FIN bit was set.
		using RxTextCallbackFn = void(bool, ikura::str_view);
		using RxBinaryCallbackFn = void(bool, Span);

		WebSocket(const URL& url, std::chrono::nanoseconds timeout = { });
		WebSocket(ikura::str_view host, uint16_t port, bool ssl, std::chrono::nanoseconds timeout = { });
		~WebSocket();

		void resizeBuffer(size_t sz);

		bool connect();
		void disconnect();

		bool connected();

		void send(Span data);
		void send(ikura::str_view sv);

		void sendFragment(Span data, bool last);
		void sendFragment(ikura::str_view sv, bool last);

		void onReceiveText(std::function<RxTextCallbackFn>&& fn);
		void onReceiveBinary(std::function<RxBinaryCallbackFn>&& fn);

	private:
		void send_raw(uint8_t opcode, bool fin, Buffer&& buf);
		void send_raw(uint8_t opcode, bool fin, const Buffer& buf);

		void handle_frame(uint8_t opcode, bool fin, Span data);

		void send_pong(Span data);

		Socket conn;
		Buffer buffer;

		std::function<RxTextCallbackFn> text_callback;
		std::function<RxBinaryCallbackFn> binary_callback;

		uint8_t cur_rx_cont_op = 0;
		uint8_t cur_tx_cont_op = 0;
	};

	struct HttpHeaders
	{
		HttpHeaders(ikura::str_view status);

		HttpHeaders& add(std::string&& key, std::string&& value);
		HttpHeaders& add(const std::string& key, const std::string& value);

		std::string bytes() const;
		std::string status() const;
		const std::vector<std::pair<std::string, std::string>>& headers() const;

		static std::optional<HttpHeaders> parse(ikura::str_view data);
		static std::optional<HttpHeaders> parse(const Buffer& data);

		std::string get(ikura::str_view key) const;

	private:
		size_t expected_len = 0;

		std::string _status;
		std::vector<std::pair<std::string, std::string>> _headers;
	};

}







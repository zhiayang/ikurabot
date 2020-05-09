// network.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <thread>
#include <atomic>
#include <string>
#include <optional>
#include <functional>
#include <string_view>

#include "defs.h"
#include "kissnet.h"

namespace ikura
{
	struct Connection
	{
		using RxCallbackFn = void(Span);

		Connection(std::string_view host, uint16_t port, bool ssl);
		~Connection();

		bool connect();
		void disconnect();

		size_t availableBytes();
		bool connected();

		void send(Span data);
		void onReceive(std::function<RxCallbackFn>&& fn);

		const std::string& host() const { return this->_host; }
		uint16_t port() const { return this->_port; }

	private:
		std::string _host;
		uint16_t _port;

		std::thread thread;
		std::atomic<bool> is_connected;
		kissnet::socket<> socket;

		std::function<RxCallbackFn> rx_callback;

		static constexpr size_t BufferSize = 4096;
		uint8_t internal_buffer[BufferSize] = { };
	};

	struct WebSocket
	{
		static constexpr size_t DEFAULT_FRAME_BUFFER_SIZE = 8192;

		// first argument is true if the FIN bit was set.
		using RxTextCallbackFn = void(bool, std::string_view);
		using RxBinaryCallbackFn = void(bool, Span);

		WebSocket(std::string_view host, uint16_t port, bool ssl, size_t bufferSize = DEFAULT_FRAME_BUFFER_SIZE);
		~WebSocket();

		bool connect();
		void disconnect();

		bool connected();

		void send(Span data);
		void send(std::string_view sv);

		void sendFragment(Span data, bool last);
		void sendFragment(std::string_view sv, bool last);

		void onReceiveText(std::function<RxTextCallbackFn>&& fn);
		void onReceiveBinary(std::function<RxBinaryCallbackFn>&& fn);

	private:
		void send_raw(uint8_t opcode, bool fin, Buffer&& buf);
		void send_raw(uint8_t opcode, bool fin, const Buffer& buf);

		void handle_frame(uint8_t opcode, bool fin, Span data);

		void send_pong(Span data);

		Connection conn;
		Buffer buffer;

		std::function<RxTextCallbackFn> text_callback;
		std::function<RxBinaryCallbackFn> binary_callback;

		uint8_t cur_rx_cont_op = 0;
		uint8_t cur_tx_cont_op = 0;
	};

	struct HttpHeaders
	{
		HttpHeaders(std::string_view status);

		HttpHeaders& add(std::string&& key, std::string&& value);
		HttpHeaders& add(const std::string& key, const std::string& value);

		std::string bytes() const;
		std::string status() const;
		const std::vector<std::pair<std::string, std::string>>& headers() const;

		static std::optional<HttpHeaders> parse(std::string_view data);
		static std::optional<HttpHeaders> parse(const Buffer& data);

		std::string get(std::string_view key) const;

	private:
		size_t expected_len = 0;

		std::string _status;
		std::vector<std::pair<std::string, std::string>> _headers;
	};
}







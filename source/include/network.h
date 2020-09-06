// network.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <chrono>
#include <thread>
#include <string>

#include "types.h"
#include "buffer.h"

namespace kissnet
{
	template <size_t N>
	class socket;
}

namespace ikura
{
	struct URL
	{
		explicit URL(ikura::str_view url);
		URL(ikura::str_view hostname, uint16_t port);

		std::string& protocol()                 { return this->_protocol; }
		const std::string& protocol() const     { return this->_protocol; }

		std::string& hostname()                 { return this->_hostname; }
		const std::string& hostname() const     { return this->_hostname; }

		std::string& parameters()               { return this->_parameters; }
		const std::string& parameters() const   { return this->_parameters; }

		std::string& resource()                 { return this->_resource; }
		const std::string& resource() const     { return this->_resource; }

		uint16_t port() const                   { return this->_port; }
		std::string str() const;

	private:
		std::string _protocol;
		std::string _hostname;
		std::string _resource;
		std::string _parameters;
		uint16_t _port = 0;
	};



	struct Socket
	{
		friend struct WebSocket;

		using RxCallbackFn = void(Span);

		Socket(const URL& url, bool ssl = false, std::chrono::nanoseconds timeout = { });
		Socket(ikura::str_view host, uint16_t port, bool ssl, std::chrono::nanoseconds timeout = { });
		~Socket();

		// client stuff
		bool connect();
		void disconnect(bool quietly = false);

		size_t availableBytes();
		bool connected();

		void send(Span data);
		void onReceive(std::function<RxCallbackFn>&& fn);
		void onDisconnect(std::function<void (void)>&& fn);

		const std::string& host() const { return this->_host; }
		uint16_t port() const { return this->_port; }

		// server stuff
		bool listen();
		Socket* accept(std::chrono::nanoseconds timeout);

	private:
		Socket();
		Socket(std::string host, uint16_t port, kissnet::socket<4>* socket, std::chrono::nanoseconds = { });

		void setup_receiver();
		void force_disconnect();

		std::string _host;
		uint16_t _port;
		bool _ssl;

		std::thread thread;
		kissnet::socket<4>* socket = nullptr;
		bool is_connected = false;

		bool server_mode = false;
		std::chrono::nanoseconds timeout;

		std::function<RxCallbackFn> rx_callback;
		std::function<void (void)> close_callback;

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
		void disconnect()              { this->disconnect(false, 1000); }
		void disconnect(uint16_t code) { this->disconnect(false, code); }
		void disconnect(bool quietly, uint16_t code = 1000);

		bool connected();

		void send(Span data);
		void send(ikura::str_view sv);

		void sendFragment(Span data, bool last);
		void sendFragment(ikura::str_view sv, bool last);

		void onDisconnect(std::function<void (void)>&& fn);
		void onReceiveText(std::function<RxTextCallbackFn>&& fn);
		void onReceiveBinary(std::function<RxBinaryCallbackFn>&& fn);

	private:
		void send_raw(uint8_t opcode, bool fin, Buffer&& buf);
		void send_raw(uint8_t opcode, bool fin, const Buffer& buf);

		void handle_frame(uint8_t opcode, bool fin, Span data);

		void send_pong(Span data);

		Socket conn;
		Buffer buffer;

		URL url;

		std::function<void (void)> close_callback;
		std::function<RxTextCallbackFn> text_callback;
		std::function<RxBinaryCallbackFn> binary_callback;

		uint8_t cur_rx_cont_op = 0;
		uint8_t cur_tx_cont_op = 0;
	};

	struct HttpHeaders
	{
		HttpHeaders() { }
		HttpHeaders(ikura::str_view status);

		HttpHeaders& add(std::string&& key, std::string&& value);
		HttpHeaders& add(const std::string& key, const std::string& value);

		std::string bytes() const;
		std::string status() const;
		int statusCode() const;
		const std::vector<std::pair<std::string, std::string>>& headers() const;

		static std::optional<HttpHeaders> parse(ikura::str_view data);
		static std::optional<HttpHeaders> parse(const Buffer& data);

		std::string get(ikura::str_view key) const;

	private:
		size_t expected_len = 0;

		std::string _status;
		std::vector<std::pair<std::string, std::string>> _headers;
	};



	namespace request
	{
		struct Param
		{
			Param(std::string name, std::string value) : name(std::move(name)), value(std::move(value)) { }

			std::string name;
			std::string value;
		};

		struct Header
		{
			Header(std::string name, std::string value) : name(std::move(name)), value(std::move(value)) { }

			std::string name;
			std::string value;
		};

		struct Response
		{
			HttpHeaders headers;
			std::string content;
		};

		std::string urlencode(ikura::str_view s);

		Response get(const URL& url, const std::vector<Param>& params = { }, const std::vector<Header>& headers = { });

		Response post(const URL& url, const std::vector<Param>& params = { }, const std::vector<Header>& headers = { },
			const std::string& contentType = "", const std::string& body = "");
		Response put(const URL& url, const std::vector<Param>& params = { }, const std::vector<Header>& headers = { },
			const std::string& contentType = "", const std::string& body = "");
	}
}







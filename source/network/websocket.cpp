// websocket.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "network.h"

namespace ikura
{
	struct raw_frame_t
	{
	#if __LITTLE_ENDIAN_BITFIELD
		uint8_t fin : 1;
		uint8_t rsv : 3;
		uint8_t opcode : 4;

		uint8_t mask : 1;
		uint8_t len1 : 7;
	#else
		uint8_t opcode : 4;
		uint8_t rsv : 3;
		uint8_t fin : 1;

		uint8_t len1 : 7;
		uint8_t mask : 1;
	#endif
	};

	static_assert(sizeof(raw_frame_t) == 2);

	constexpr uint8_t OP_CONTINUATION   = 0x0;
	constexpr uint8_t OP_TEXT           = 0x1;
	constexpr uint8_t OP_BINARY         = 0x2;

	constexpr uint8_t OP_CLOSE          = 0x8;
	constexpr uint8_t OP_PING           = 0x9;
	constexpr uint8_t OP_PONG           = 0xA;




	WebSocket::WebSocket(std::string_view host, uint16_t port, bool ssl, size_t bufSize)
		: conn(host, port, ssl), buffer(bufSize)
	{

	}

	WebSocket::~WebSocket()
	{
		this->disconnect();
	}

	bool WebSocket::connected()
	{
		return this->conn.connected();
	}

	bool WebSocket::connect()
	{
		if(!this->conn.connect())
			return false;

		auto http = HttpHeaders("GET / HTTP/1.1")
						.add("Host", this->conn.host())
						.add("Upgrade", "websocket")
						.add("Connection", "Upgrade")
						.add("Sec-WebSocket-Key", "aWt1cmEK")
						.add("Sec-WebSocket-Version", "13");

		bool success = false;
		auto cv = condvar<bool>();

		auto buf = Buffer(1024);
		this->conn.onReceive([&](Span data) {

			buf.write(data);
			auto _hdrs = HttpHeaders::parse(buf);

			// if the response is invalid (ie. there's no \r\n on the last line), then we get
			// an empty status.
			if(!_hdrs.has_value())
				return;

			cv.set(true);

			success = true;

			// ok, we got the headers.
			auto hdrs = _hdrs.value();
			if(hdrs.status().find("HTTP/1.1 101") != 0)
			{
				success = false;
				error("websocket: unexpected http status '%s' (expected 101)", hdrs.status());
			}
			else if(hdrs.get("Upgrade") != "websocket" || hdrs.get("Connection") != "Upgrade")
			{
				success = false;
				error("websocket: no upgrade header");
			}
			else if(auto key = hdrs.get("Sec-WebSocket-Accept"); key != "BIrH2fXtdYwV1IU9u+MiGYCsuTA=")
			{
				// 'aWt1cmEK' + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
				// -> sha1 = 048ac7d9f5ed758c15d4853dbbe3221980acb930
				// -> base64 = BIrH2fXtdYwV1IU9u+MiGYCsuTA=

				success = false;
				error("websocket: invalid key (got '%s')", key);
			}

			cv.notify_one();
		});

		this->conn.send(Span::fromString(http.bytes()));
		cv.wait(true);

		if(!success)
			return false;



		// setup the handler.
		this->conn.onReceive([this](Span data) {

			if(this->buffer.remaining() < data.size())
				this->buffer.grow(data.size());

			this->buffer.write(data);

			// check the thing.
			if(this->buffer.size() < sizeof(raw_frame_t))
				return;

			auto frame = this->buffer.as<raw_frame_t>();

			// insta-reject nonsensical things:
			if(frame->mask || frame->opcode >= 0x0B)
			{
				this->buffer.clear();
				return;
			}

			// most importantly we need the length. see if we even have enough bytes for it
			// 16-bit length has 2 bytes, making 4 bytes total
			if(frame->len1 == 126 && this->buffer.size() < 4)
				return;

			// 64-bit length has 8 bytes, making 10 bytes total
			else if(frame->len1 == 127 && this->buffer.size() < 10)
				return;

			// ok, now we at least know the length of the frame.
			size_t payloadLen = 0;
			size_t totalLen = sizeof(raw_frame_t);
			if(frame->len1 == 126)
			{
				uint16_t tmp = 0;
				memcpy(&tmp, this->buffer.data() + sizeof(raw_frame_t), sizeof(uint16_t));

				totalLen += sizeof(uint16_t);
				payloadLen = value::to_native<uint16_t>(tmp);
			}
			else if(frame->len1 == 127)
			{
				uint64_t tmp = 0;
				memcpy(&tmp, this->buffer.data() + sizeof(raw_frame_t), sizeof(uint64_t));

				totalLen += sizeof(uint64_t);
				payloadLen = value::to_native<uint64_t>(tmp);
			}
			else
			{
				payloadLen = frame->len1;
			}

			totalLen += payloadLen;

			if(this->buffer.size() < totalLen)
				return;

			// ok, we have the entire packet.
			this->handle_frame(frame->opcode, frame->fin, this->buffer.span().remove_prefix(totalLen - payloadLen));

			// discard the frame now.
			this->buffer.clear();
		});

		log("websocket: connected");
		return true;
	}

	void WebSocket::disconnect()
	{
		auto cv = condvar<bool>();

		auto buf = Buffer(256);
		this->conn.onReceive([&](Span data) {

			buf.write(data);

			if(buf.size() < sizeof(raw_frame_t))
				return;

			auto frame = buf.as<raw_frame_t>();
			if(frame->opcode == OP_CLOSE)
			{
				cv.set(true);
				cv.notify_one();
			}
		});

		this->send_raw(OP_CLOSE, /* fin: */ true, Buffer::empty());
		cv.wait(true);

		this->conn.disconnect();
	}


	void WebSocket::send_raw(uint8_t opcode, bool fin, const Buffer& payload)
	{
		this->send_raw(opcode, fin, payload.clone());
	}

	void WebSocket::send_raw(uint8_t opcode, bool fin, Buffer&& payload)
	{
		if((opcode & 0xF0) != 0 || opcode >= 0x0B)
		{
			error("websocket: invalid opcode '%x', opcode");
			return;
		}

		// first calculate the size of the frame header. (including the mask)
		size_t hdrsz = sizeof(raw_frame_t) + sizeof(uint32_t);
		uint8_t len1 = 0;

		if(payload.size() > 65535)
			len1 = 127, hdrsz += sizeof(uint64_t);

		else if(payload.size() > 125)
			len1 = 126, hdrsz += sizeof(uint16_t);

		else
			len1 = payload.size();

		auto buf = new uint8_t[hdrsz];
		auto frame = (raw_frame_t*) buf;

		frame->fin = (fin ? 1 : 0);
		frame->rsv = 0;
		frame->opcode = opcode;
		frame->mask = 1;
		frame->len1 = len1;

		uint8_t* cur_ptr = (buf + sizeof(raw_frame_t));

		if(len1 == 126)
		{
			uint16_t len = value::to_network<uint16_t>(payload.size());
			memcpy(cur_ptr, &len, sizeof(uint16_t));
			cur_ptr += sizeof(uint16_t);
		}
		else if(len1 == 127)
		{
			uint64_t len = value::to_network<uint64_t>(payload.size());
			memcpy(cur_ptr, &len, sizeof(uint64_t));
			cur_ptr += sizeof(uint64_t);
		}

		// make some mask key
		// auto mask_value = random::get<uint32_t>();
		auto mask_value = 0x55555555;

		// split up the mask into bytes
		uint8_t mask[4] = {
			(uint8_t) ((mask_value & 0xFF00'0000) >> 24), (uint8_t) ((mask_value & 0x00FF'0000) >> 16),
			(uint8_t) ((mask_value & 0x0000'FF00) >> 8),  (uint8_t) ((mask_value & 0x0000'00FF) >> 0)
		};

		*cur_ptr++ = mask[0];
		*cur_ptr++ = mask[1];
		*cur_ptr++ = mask[2];
		*cur_ptr++ = mask[3];

		// we have ownership of the buffer here, so we can perform stuff directly.
		uint8_t* bytes = payload.data();

		for(size_t i = 0; i < payload.size(); i++)
			bytes[i] ^= mask[i & 0x3];


		// masked. to get around having to make one big buffer and copy stuff over, we
		// just call send() on the underlying socket twice. if the OS is competent enough,
		// the two calls ought to end up in the same TCP frame.

		// auto combined = Buffer(hdrsz + payload.size());
		// combined.write(buf, hdrsz);
		// combined.write(payload);
		// this->conn.send(combined.span());

		this->conn.send(Span(buf, hdrsz));
		this->conn.send(payload.span());
	}

	void WebSocket::handle_frame(uint8_t opcode, bool fin, Span data)
	{
		// error("HANDLE %x / %zu (%.*s)", opcode, data.size(), data.size(), (char*) data.data());
		if(opcode == OP_PING)
		{
			this->send_pong(data);
		}
		else if(opcode == OP_TEXT)
		{
			if(this->text_callback)
				this->text_callback(fin, std::string_view((const char*) data.data(), data.size()));

			if(!fin) this->cur_rx_cont_op = OP_TEXT;
			else     this->cur_rx_cont_op = 0;
		}
		else if(opcode == OP_BINARY)
		{
			if(this->binary_callback)
				this->binary_callback(fin, data);

			if(!fin) this->cur_rx_cont_op = OP_BINARY;
			else     this->cur_rx_cont_op = 0;
		}
		else if(opcode == OP_CONTINUATION)
		{
			if(this->cur_rx_cont_op == OP_TEXT && this->text_callback)
				this->text_callback(fin, std::string_view((const char*) data.data(), data.size()));

			else if(this->cur_rx_cont_op == OP_BINARY && this->binary_callback)
				this->binary_callback(fin, data);
		}
	}

	void WebSocket::send_pong(Span data)
	{
		this->send_raw(OP_PONG, /* fin: */ true, data.reify());
	}






	void WebSocket::send(Span data)
	{
		this->send_raw(OP_BINARY, /* fin: */ true, data.reify());

		// always reset the continuation here
		this->cur_tx_cont_op = 0;
	}

	void WebSocket::send(std::string_view sv)
	{
		auto buf = Buffer(sv.size());
		buf.write(Span::fromString(sv));

		this->send_raw(OP_TEXT, /* fin: */ true, std::move(buf));

		// always reset the continuation here
		this->cur_tx_cont_op = 0;
	}

	void WebSocket::sendFragment(Span data, bool last)
	{
		// if this is the first fragment,
		auto op = (this->cur_tx_cont_op == 0
			? OP_BINARY
			: OP_CONTINUATION
		);

		this->send_raw(op, /* fin: */ last, data.reify());

		// if this is the last frame in the fragment, reset the continuation.
		if(last) this->cur_tx_cont_op = 0;
		else     this->cur_tx_cont_op = OP_BINARY;
	}

	void WebSocket::sendFragment(std::string_view sv, bool last)
	{
		auto op = (this->cur_tx_cont_op == 0
			? OP_TEXT
			: OP_CONTINUATION
		);

		auto buf = Buffer(sv.size());
		buf.write(Span::fromString(sv));

		this->send_raw(op, /* fin: */ true, std::move(buf));

		// if this is the last frame in the fragment, reset the continuation.
		if(last) this->cur_tx_cont_op = 0;
		else     this->cur_tx_cont_op = OP_TEXT;
	}



	void WebSocket::onReceiveText(std::function<RxTextCallbackFn>&& fn)
	{
		this->text_callback = fn;
	}

	void WebSocket::onReceiveBinary(std::function<RxBinaryCallbackFn>&& fn)
	{
		this->binary_callback = fn;
	}
}

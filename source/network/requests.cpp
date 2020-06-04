// requests.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "network.h"
#include "synchro.h"

using namespace std::chrono_literals;

namespace ikura::request
{
	static std::string encode_string(const std::string& url)
	{
		std::string ret; ret.reserve(url.size());
		for(char c : url)
		{
			if(('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '-' || c == '.' || c == '_')
				ret += c;

			else
				ret += "%" + zpr::sprint("%02x", c);
		}

		return ret;
	}

	static std::string encode_params(const std::vector<Param>& params)
	{
		std::string ret;

		if(params.size() > 0)
		{
			ret += "?";
			for(const auto& p : params)
				ret += zpr::sprint("%s=%s&", encode_string(p.name), encode_string(p.value));

			assert(ret.back() == '&');
			ret.pop_back();
		}

		return ret;
	}

	constexpr auto DEFAULT_TIMEOUT = 2000ms;
	static std::optional<std::pair<HttpHeaders, std::string>> get_response(Socket* sock)
	{
		auto cv = condvar<bool>();
		auto buf = Buffer(1024);

		std::string content;

		bool isChunked = false;
		size_t contentLength = 0;
		size_t processedChunkOffset = 0;

		sock->onReceive([&](Span data) {

			if(buf.remaining() < data.size())
				buf.grow(data.size());

			bool finished = false;

			buf.write(data);
			auto _hdrs = HttpHeaders::parse(buf);

			// if the response is invalid (ie. there's no \r\n on the last line), then most likely
			// we haven't finished receiving it yet.
			if(!_hdrs.has_value())
				return;

			// ok, we got the headers.
			auto hdrs = _hdrs.value();
			if(auto len = hdrs.get("content-length"); contentLength == 0 && !len.empty())
				contentLength = util::stou(len).value();

			else if(hdrs.get("transfer-encoding") == "chunked")
				isChunked = true;

			if(!isChunked)
			{
				auto tmp = buf.sv();
				auto i = tmp.find("\r\n\r\n");
				if(i != (size_t) -1)
					content = tmp.drop(i + 4).str();

				if(content.size() >= contentLength)
					finished = true;
			}
			else
			{
				// fucking hell
				ikura::str_view chunk;

				if(processedChunkOffset == 0)
				{
					auto tmp = buf.sv();
					auto i = tmp.find("\r\n\r\n");
					if(i == (size_t) -1)
						return;

					chunk = tmp.drop(i + 4);
				}
				else
				{
					chunk = buf.sv().drop(processedChunkOffset);
				}

				// first line contains the chunk size, as well as other nonsense.
				auto tmp2 = chunk.take(chunk.find("\r\n")).take(chunk.find(';'));
				auto size = util::stou(tmp2, /* base: */ 16).value();

				// remove the first line
				auto body = chunk.drop(chunk.find("\r\n") + 2);

				// chunks are also apparently terminated by \r\n. so if it's not there,
				// we don't have the whole chunk yet, so bail for now.
				if(body.rfind("\r\n") != body.size() - 2)
					return;

				content += body.take(body.size() - 2);

				// adjust the offset so we know where to start next round.
				processedChunkOffset = ((const uint8_t*) chunk.data() - buf.data()) + chunk.size();

				// chunks are "null-terminated". (ie. last chunk has a size of 0)
				if(size == 0)
					finished = true;
			}

			if(finished)
				cv.set(true);
		});

		if(!cv.wait(true, DEFAULT_TIMEOUT))
		{
			sock->disconnect();
			lg::error("http", "request timed out");
			return { };
		}

		sock->onReceive([](auto) { });
		return std::make_pair(HttpHeaders::parse(buf).value(), std::move(content));
	}







	Response get(const URL& url, const std::vector<Param>& params, const std::vector<Header>& headers)
	{
		auto address = URL(zpr::sprint("%s://%s", url.protocol(), url.hostname()));
		auto path = url.resource();

		// open a socket, write, wait for response, close.
		auto sock = Socket(address, /* ssl: */ url.protocol() == "https");
		if(!sock.connect())
		{
			lg::log("http", "connect failed");
			return { };
		}

		auto hdr = HttpHeaders(zpr::sprint("GET %s%s HTTP/1.1", path, encode_params(params)));
		hdr.add("Host", url.hostname());
		for(const auto& h : headers)
			hdr.add(h.name, h.value);

		sock.send(Span::fromString(hdr.bytes()));

		auto resp = get_response(&sock);
		if(!resp) return { };

		auto [ hdrs, content ] = resp.value();
		sock.disconnect();

		return Response {
			.headers = hdrs,
			.content = content
		};
	}

	Response post(const URL& url, const std::vector<Param>& params, const std::vector<Header>& headers,
		const std::string& contentType, const std::string& body)
	{
		auto address = URL(zpr::sprint("%s://%s", url.protocol(), url.hostname()));
		auto path = url.resource();

		// open a socket, write, wait for response, close.
		auto sock = Socket(address, /* ssl: */ url.protocol() == "https");
		if(!sock.connect())
		{
			lg::log("http", "connect failed");
			return { };
		}

		auto hdr = HttpHeaders(zpr::sprint("POST %s%s HTTP/1.1", path, encode_params(params)));
		hdr.add("Host", url.hostname());
		for(const auto& h : headers)
			hdr.add(h.name, h.value);

		hdr.add("Content-Length", std::to_string(body.size()));
		hdr.add("Content-Type", contentType);

		auto h = hdr.bytes();
		auto buf = Buffer(h.size() + body.size());

		buf.write(Span::fromString(h));
		buf.write(Span::fromString(body));

		sock.send(buf.span());

		auto resp = get_response(&sock);
		if(!resp) return { };

		auto [ hdrs, content ] = resp.value();
		sock.disconnect();

		return Response {
			.headers = hdrs,
			.content = content
		};
	}
}

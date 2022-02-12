// http.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "network.h"

#include <unordered_map>

namespace ikura
{
	static std::unordered_map<std::string, uint16_t> default_ports = {
		{ "http", 80 },
		{ "https", 443 },

		{ "ws", 80 },
		{ "wss", 443 },
	};

	URL::URL(ikura::str_view url)
	{
		do {
			auto i = url.find("://");
			if(i == std::string::npos || i == 0)
				break;

			this->_protocol = std::string(url.substr(0, i));
			url.remove_prefix(i + 3);

			// you don't need to have a slash, but if you do it can't be the first thing.
			i = url.find_first_of("?/");
			if(i == 0)
				break;

			auto tmp = url.substr(0, i);
			if(i != (size_t) -1)
				url.remove_prefix(i);   // include the leading / for the path

			this->_resource = std::string(url);
			if(i == (size_t) -1 || this->_resource.empty())
				this->_resource = "/";

			// need to check for ports. (if i = 0, then your hostname was empty, which is bogus)
			i = tmp.find(':');
			if(i == 0)
				break;

			if(i != std::string::npos)
			{
				// tmp only contains 'basename:PORT'
				auto val = util::stoi(tmp.drop(i + 1));
				if(!val) break;

				this->_port = val.value();
				this->_hostname = std::string(tmp.take(i));
			}
			else
			{
				this->_hostname = std::string(tmp);
				this->_port = default_ports[this->_protocol];
			}

			if(auto tmp = this->_resource.find('?'); tmp != (size_t) -1)
			{
				this->_resource = this->_resource.substr(0, tmp);
				if(this->_resource.empty())
					this->_resource = "/";

				this->_parameters = url.drop(tmp + 1).str();
			}

			// ok, success. return to skip the error message.
			return;
		} while(false);

		lg::error("url", "invalid url '{}'", url);
	}

	URL::URL(ikura::str_view hostname, uint16_t port)
	{
		this->_hostname = hostname.str();
		this->_port = port;

		// probably, but it's not important
		this->_protocol = "http";
	}

	std::string URL::str() const
	{
		return zpr::sprint("{}://{}:{}{}", this->_protocol, this->_hostname, this->_port, this->_resource);
	}
















	HttpHeaders::HttpHeaders(ikura::str_view status)
	{
		this->_status = status;
		this->expected_len = this->_status.size() + 2;
	}

	HttpHeaders& HttpHeaders::add(const std::string& key, const std::string& value)
	{
		this->expected_len += 4 + key.size() + value.size();
		this->_headers.emplace_back(key, value);

		return *this;
	}

	HttpHeaders& HttpHeaders::add(std::string&& key, std::string&& value)
	{
		this->expected_len += 4 + key.size() + value.size();
		this->_headers.emplace_back(std::move(key), std::move(value));

		return *this;
	}

	std::string HttpHeaders::bytes() const
	{
		std::string ret;
		ret.reserve(this->expected_len + 2);

		ret += this->_status;
		ret += "\r\n";

		for(auto& [ k, v] : this->_headers)
			ret += k, ret += ": ", ret += v, ret += "\r\n";

		ret += "\r\n";
		return ret;
	}

	std::string HttpHeaders::status() const
	{
		return this->_status;
	}

	int HttpHeaders::statusCode() const
	{
		if(this->_status.empty())
			return 0;

		auto xs = util::split(this->_status, ' ');
		if(xs.size() < 3)
			return 0;

		// http version <space> code <space> message
		return (int) util::stoi(xs[1]).value();
	}

	const std::vector<std::pair<std::string, std::string>>& HttpHeaders::headers() const
	{
		return this->_headers;
	}

	std::string HttpHeaders::get(ikura::str_view key) const
	{
		for(const auto& [ k, v ] : this->_headers)
			if(k == key)
				return v;

		return "";
	}

	std::optional<HttpHeaders> HttpHeaders::parse(const Buffer& buf)
	{
		return parse(buf.sv());
	}


	std::optional<HttpHeaders> HttpHeaders::parse(ikura::str_view data)
	{
		// we actually want to return null if the header is incomplete.
		auto x = data.find("\r\n");
		if(x == std::string::npos)
			return std::nullopt;

		auto hdrs = HttpHeaders(data.substr(0, x));
		data.remove_prefix(x + 2);

		while(data.find("\r\n") > 0)
		{
			auto ki = data.find(':');
			if(ki == std::string::npos)
				return std::nullopt;

			auto key = util::lowercase(data.substr(0, ki));
			data.remove_prefix(ki + 1);

			// strip spaces
			while(data.size() > 0 && data[0] == ' ')
				data.remove_prefix(1);

			if(data.size() == 0)
				return std::nullopt;

			auto vi = data.find("\r\n");
			if(vi == std::string::npos)
				return std::nullopt;

			auto value = std::string(data.substr(0, vi));

			hdrs.add(std::move(key), std::move(value));
			data.remove_prefix(vi + 2);
		}

		if(data.find("\r\n") != 0)
			{ lg::warn("http", "E"); return std::nullopt; }

		return hdrs;
	}
}

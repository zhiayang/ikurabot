// rate.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <chrono>

namespace ikura
{
	struct RateLimit
	{
		using duration = std::chrono::steady_clock::duration;
		using time_point = std::chrono::time_point<std::chrono::steady_clock>;

		RateLimit(uint64_t limit, duration interval) : limit(limit), interval(interval) { }

		uint64_t tokens;
		time_point lastRefilled;

		uint64_t limit;
		duration interval;

		bool attempt()
		{
			if(now() >= lastRefilled + interval)
			{
				tokens = std::max(limit, tokens + limit);
				lastRefilled = now();
			}

			if(tokens > 0)
			{
				tokens -= 1;
				return true;
			}

			return false;
		}

		time_point next()       { return now() + (lastRefilled + interval - now()); }
		static time_point now() { return std::chrono::steady_clock::now(); }
	};
}

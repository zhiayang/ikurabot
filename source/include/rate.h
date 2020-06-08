// rate.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <chrono>

namespace ikura
{
	struct RateLimit
	{
		using duration_t = std::chrono::system_clock::duration;
		using time_point_t = std::chrono::time_point<std::chrono::system_clock>;

		// RateLimit() : limit(0), period(0), min_interval(0) { }

		template <typename DP, typename DI>
		RateLimit(uint64_t limit, DP period, DI min_interval)
			: limit(limit),
			  period(std::chrono::duration_cast<duration_t>(period)),
			  min_interval(std::chrono::duration_cast<duration_t>(min_interval)) { }

		bool attempt() const
		{
			if(now() >= last_refilled + period)
			{
				tokens = std::max(limit, tokens + limit);
				last_refilled = now();
			}

			if(tokens == 0)
				return false;

			if(now() - last_attempted < min_interval)
				return false;

			tokens -= 1;
			last_attempted = now();
			return true;
		}

		time_point_t next() const
		{
			auto now = RateLimit::now();
			if(tokens > 0)
			{
				if(now - last_attempted >= min_interval)
					return now;

				return now + ((last_attempted + min_interval) - now);
			}
			else
			{
				return now + (last_refilled + period - now);
			}
		}

		bool exceeded() const { return tokens == 0; }

		static time_point_t now() { return std::chrono::system_clock::now(); }

		uint64_t get_limit() const { return this->limit; }
		uint64_t get_tokens() const { return this->tokens; }
		duration_t get_min_interval() const { return this->min_interval; }

		void set_tokens(uint64_t x) { this->tokens = x; }
		void set_limit(uint64_t x) { this->limit = x; }

		template <typename Dt>
		void set_reset_after(Dt t)
		{
			this->last_refilled = now();
			this->period = std::chrono::duration_cast<duration_t>(t);
		}

		template <typename Dt>
		void set_min_interval(Dt t)
		{
			this->min_interval = std::chrono::duration_cast<duration_t>(t);
		}

	private:
		mutable uint64_t tokens;
		mutable time_point_t last_refilled;
		mutable time_point_t last_attempted;

		uint64_t limit;
		duration_t period;
		duration_t min_interval;
	};
}

// defs.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

#include "zpr.h"
#include "utils.h"
#include "buffer.h"

namespace ikura
{
	template <typename T>
	struct condvar
	{
		condvar() : value() { }
		condvar(const T& x) : value(x) { }

		void set(const T& x)
		{
			this->set_quiet(x);
			this->notify_all();
		}

		void set_quiet(const T& x)
		{
			auto lk = std::lock_guard<std::mutex>(this->mtx);
			this->value = x;
		}

		T get()
		{
			return this->value;
		}

		void wait(const T& x)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			this->cv.wait(lk, [&]{ return this->value == x; });
		}

		// returns true only if the value was set; if we timed out, it returns false.
		bool wait(const T& x, std::chrono::nanoseconds timeout)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			return this->cv.wait_for(lk, timeout, [&]{ return this->value == x; });
		}

		void notify_one() { this->cv.notify_one(); }
		void notify_all() { this->cv.notify_all(); }

	private:
		T value;
		std::mutex mtx;
		std::condition_variable cv;
	};



	namespace config
	{
		bool load(std::string_view path);

		bool haveTwitch();
		bool haveDiscord();

		namespace twitch
		{
			std::string getUsername();
			std::string getOAuthToken();
			std::vector<std::pair<std::string, bool>> getJoinChannels();
		}
	}

	namespace util
	{
		size_t getFileSize(const std::string& path);
		std::pair<uint8_t*, size_t> readEntireFile(const std::string& path);
	}

	namespace random
	{
		template <typename T> T get();
	}

	namespace value
	{
		template <typename T> T to_native(T x);
		template <typename T> T to_network(T x);
	}

	namespace base64
	{
		std::string encode(const uint8_t* src, size_t len);
		std::string decode(std::string_view src);
	}

	namespace colours
	{
		constexpr const char* COLOUR_RESET  = "\033[0m";
		constexpr const char* BLACK         = "\033[30m";
		constexpr const char* RED           = "\033[31m";
		constexpr const char* GREEN         = "\033[32m";
		constexpr const char* YELLOW        = "\033[33m";
		constexpr const char* BLUE          = "\033[34m";
		constexpr const char* MAGENTA       = "\033[35m";
		constexpr const char* CYAN          = "\033[36m";
		constexpr const char* WHITE         = "\033[37m";
		constexpr const char* BLACK_BOLD    = "\033[1m";
		constexpr const char* RED_BOLD      = "\033[1m\033[31m";
		constexpr const char* GREEN_BOLD    = "\033[1m\033[32m";
		constexpr const char* YELLOW_BOLD   = "\033[1m\033[33m";
		constexpr const char* BLUE_BOLD     = "\033[1m\033[34m";
		constexpr const char* MAGENTA_BOLD  = "\033[1m\033[35m";
		constexpr const char* CYAN_BOLD     = "\033[1m\033[36m";
		constexpr const char* WHITE_BOLD    = "\033[1m\033[37m";
		constexpr const char* GREY_BOLD     = "\033[30;1m";

		constexpr bool USE_COLOURS = true;
		constexpr const char* RESET = (USE_COLOURS ? COLOUR_RESET : "");
		constexpr const char* SUBSYS = (USE_COLOURS ? BLUE_BOLD : "");

		constexpr const char* DBG = (USE_COLOURS ? WHITE : "");
		constexpr const char* LOG = (USE_COLOURS ? GREY_BOLD : "");
		constexpr const char* WRN = (USE_COLOURS ? YELLOW_BOLD : "");
		constexpr const char* ERR = (USE_COLOURS ? RED_BOLD : "");
	}

	namespace lg
	{
		constexpr bool ENABLE_DEBUG = false;

		template <typename... Args>
		static inline void __generic_log(int lvl, std::string_view sys, const std::string& fmt, Args&&... args)
		{
			if(!ENABLE_DEBUG && lvl < 0)
				return;

			const char* col = 0;
			const char* str = 0;

			if(lvl == -1)       { col = colours::DBG;  str = "[dbg]"; }
			else if(lvl == 0)   { col = colours::LOG;  str = "[log]"; }
			else if(lvl == 1)   { col = colours::WRN;  str = "[wrn]"; }
			else if(lvl == 2)   { col = colours::ERR;  str = "[err]"; }

			auto out = zpr::sprint("%s%s%s %s%s%s: ", col, str, colours::RESET, colours::SUBSYS, sys, colours::RESET);
			out += zpr::sprint(fmt, args...);

			if(lvl >= 2)    fprintf(stderr, "%s\n", out.c_str());
			else            printf("%s\n", out.c_str());
		}

		template <typename... Args>
		static void log(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(0, sys, fmt, args...);
		}

		template <typename... Args>
		static void warn(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(1, sys, fmt, args...);
		}

		template <typename... Args>
		static void error(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(2, sys, fmt, args...);
		}

		template <typename... Args>
		static void dbglog(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(-1, sys, fmt, args...);
		}
	}
}





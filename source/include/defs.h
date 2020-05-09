// defs.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <atomic>
#include <thread>

#include "zpr.h"
#include "utils.h"
#include "buffer.h"

namespace ikura
{
	template <typename T>
	struct condvar
	{
		condvar() : value(T()) { }
		condvar(const T& x) : value(x) { }

		void set(const T& x)
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

		void notify_one() { this->cv.notify_one(); }
		void notify_all() { this->cv.notify_all(); }


	private:
		T value;
		std::mutex mtx;
		std::condition_variable cv;
	};





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
		constexpr const char* RESET         = "\033[0m";
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
	}

	template <typename... Args>
	static void error(const std::string& fmt, Args&&... args)
	{
		fprintf(stderr, " %s*%s %s\n", colours::RED_BOLD, colours::RESET, zpr::sprint(fmt, args...).c_str());
	}

	template <typename... Args>
	static void log(const std::string& fmt, Args&&... args)
	{
		printf(" %s*%s %s\n", colours::GREEN_BOLD, colours::RESET, zpr::sprint(fmt, args...).c_str());
	}

	template <typename... Args>
	static void info(const std::string& fmt, Args&&... args)
	{
		printf(" %s*%s %s\n", colours::BLUE_BOLD, colours::RESET, zpr::sprint(fmt, args...).c_str());
	}

	template <typename... Args>
	static void warn(const std::string& fmt, Args&&... args)
	{

		printf(" %s*%s %s\n", colours::YELLOW_BOLD, colours::RESET, zpr::sprint(fmt, args...).c_str());
	}
}





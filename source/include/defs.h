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
#include <shared_mutex>

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

		template <typename Predicate>
		void wait_pred(const T& x, Predicate p)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			this->cv.wait(lk, p);
		}

		// returns true only if the value was set; if we timed out, it returns false.
		template <typename Predicate>
		bool wait_pred(const T& x, std::chrono::nanoseconds timeout, Predicate p)
		{
			auto lk = std::unique_lock<std::mutex>(this->mtx);
			return this->cv.wait_for(lk, timeout, p);
		}

		void notify_one() { this->cv.notify_one(); }
		void notify_all() { this->cv.notify_all(); }

	private:
		T value;
		std::mutex mtx;
		std::condition_variable cv;
	};

	template <typename T, typename Lk>
	struct Synchronised
	{
	private:
		struct ReadLockedInstance;
		struct WriteLockedInstance;

		Lk lk;
		T value;

	public:
		~Synchronised() { }

		template <typename = std::enable_if_t<std::is_default_constructible_v<T>>>
		Synchronised() { }

		template <typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
		Synchronised(const T& x) : value(x) { }

		template <typename = std::enable_if_t<std::is_move_constructible_v<T>>>
		Synchronised(T&& x) : value(std::move(x)) { }

		Synchronised(Synchronised&&) = delete;
		Synchronised(const Synchronised&) = delete;

		Synchronised& operator = (Synchronised&&) = delete;
		Synchronised& operator = (const Synchronised&) = delete;


		template <typename Functor>
		void perform_read(Functor&& fn)
		{
			std::shared_lock lk(this->lk);
			fn(this->value);
		}

		template <typename Functor>
		void perform_write(Functor&& fn)
		{
			std::unique_lock lk(this->lk);
			fn(this->value);
		}


		Lk& getLock()
		{
			return this->lk;
		}

		ReadLockedInstance rlock()
		{
			return ReadLockedInstance(*this);
		}

		WriteLockedInstance wlock()
		{
			return WriteLockedInstance(*this);
		}

	private:

		// static Lk& assert_not_held(Lk& lk) { if(lk.held()) assert(!"cannot move held Synchronised"); return lk; }

		struct ReadLockedInstance
		{
			T* operator -> () { return &this->sync.value; }
			T* get() { return &this->sync.value; }
			~ReadLockedInstance() { this->sync.lk.unlock(); }

		private:
			ReadLockedInstance(Synchronised& sync) : sync(sync) { this->sync.lk.lock(); }

			ReadLockedInstance(ReadLockedInstance&&) = delete;
			ReadLockedInstance(const ReadLockedInstance&) = delete;

			ReadLockedInstance& operator = (ReadLockedInstance&&) = delete;
			ReadLockedInstance& operator = (const ReadLockedInstance&) = delete;

			Synchronised& sync;

			friend struct Synchronised;
		};

		struct WriteLockedInstance
		{
			T* operator -> () { return &this->sync.value; }
			T* get() { return &this->sync.value; }
			~WriteLockedInstance() { this->sync.lk.unlock_shared(); }

		private:
			WriteLockedInstance(Synchronised& sync) : sync(sync) { this->sync.lk.lock_shared(); }

			WriteLockedInstance(WriteLockedInstance&&) = delete;
			WriteLockedInstance(const WriteLockedInstance&) = delete;

			WriteLockedInstance& operator = (WriteLockedInstance&&) = delete;
			WriteLockedInstance& operator = (const WriteLockedInstance&) = delete;

			Synchronised& sync;

			friend struct Synchronised;
		};
	};


	namespace twitch
	{
		void init();
		void shutdown();

		struct TwitchChannel;
	}



	namespace config
	{
		bool load(std::string_view path);

		bool haveTwitch();
		bool haveDiscord();

		namespace twitch
		{
			std::string getUsername();
			std::string getOAuthToken();
			std::vector<ikura::twitch::TwitchChannel> getJoinChannels();
		}
	}

	namespace util
	{
		uint64_t getMillisecondTimestamp();

		size_t getFileSize(const std::string& path);
		std::pair<uint8_t*, size_t> readEntireFile(const std::string& path);

		std::tuple<int, uint8_t*, size_t> mmapEntireFile(const std::string& path);
		void munmapEntireFile(int fd, uint8_t* buf, size_t len);
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
		constexpr const char* COLOUR_RESET  = "\x1b[0m";
		constexpr const char* BLACK         = "\x1b[30m";
		constexpr const char* RED           = "\x1b[31m";
		constexpr const char* GREEN         = "\x1b[32m";
		constexpr const char* YELLOW        = "\x1b[33m";
		constexpr const char* BLUE          = "\x1b[34m";
		constexpr const char* MAGENTA       = "\x1b[35m";
		constexpr const char* CYAN          = "\x1b[36m";
		constexpr const char* WHITE         = "\x1b[37m";
		constexpr const char* BLACK_BOLD    = "\x1b[1m";
		constexpr const char* RED_BOLD      = "\x1b[1m\x1b[31m";
		constexpr const char* GREEN_BOLD    = "\x1b[1m\x1b[32m";
		constexpr const char* YELLOW_BOLD   = "\x1b[1m\x1b[33m";
		constexpr const char* BLUE_BOLD     = "\x1b[1m\x1b[34m";
		constexpr const char* MAGENTA_BOLD  = "\x1b[1m\x1b[35m";
		constexpr const char* CYAN_BOLD     = "\x1b[1m\x1b[36m";
		constexpr const char* WHITE_BOLD    = "\x1b[1m\x1b[37m";
		constexpr const char* GREY_BOLD     = "\x1b[30;1m";

		constexpr const char* WHITE_BOLD_RED_BG = "\x1b[1m\x1b[37m\x1b[48;5;9m";

		constexpr bool USE_COLOURS = true;
		constexpr const char* RESET = (USE_COLOURS ? COLOUR_RESET : "");
		constexpr const char* SUBSYS = (USE_COLOURS ? BLUE_BOLD : "");

		constexpr const char* DBG = (USE_COLOURS ? WHITE : "");
		constexpr const char* LOG = (USE_COLOURS ? GREY_BOLD : "");
		constexpr const char* WRN = (USE_COLOURS ? YELLOW_BOLD : "");
		constexpr const char* ERR = (USE_COLOURS ? RED_BOLD : "");
		constexpr const char* FTL = (USE_COLOURS ? WHITE_BOLD_RED_BG : "");
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
			else if(lvl == 3)   { col = colours::FTL;  str = "[ftl]"; }

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

		// for convenience, this returns false.
		template <typename... Args>
		static bool error(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(2, sys, fmt, args...);
			return false;
		}

		template <typename... Args>
		static void fatal(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(3, sys, fmt, args...);
			exit(1);
		}

		template <typename... Args>
		static void dbglog(std::string_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(-1, sys, fmt, args...);
		}
	}
}





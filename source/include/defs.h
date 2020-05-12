// defs.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <vector>

#include "zpr.h"
#include "span.h"
#include "tsl/robin_map.h"

namespace ikura
{
	namespace twitch
	{
		void init();
		void shutdown();
	}

	namespace config
	{
		bool load(std::string_view path);

		bool haveTwitch();
		bool haveDiscord();

		namespace twitch
		{
			struct Chan
			{
				std::string name;
				bool lurk;
				bool mod;
				bool respondToPings;
			};

			std::string getUsername();
			std::string getOAuthToken();
			std::string getCommandPrefix();
			std::vector<Chan> getJoinChannels();
		}
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


	namespace __detail
	{
		struct equal_string
		{
			using is_transparent = void;
			bool operator () (const std::string& a, std::string_view b) const   { return a == b; }
			bool operator () (std::string_view a, const std::string& b) const   { return a == b; }
			bool operator () (const std::string& a, const std::string& b) const { return a == b; }
		};

		struct hash_string
		{
			size_t operator () (const std::string& x) const { return std::hash<std::string>()(x); }
			size_t operator () (std::string_view x) const   { return std::hash<std::string_view>()(x); }
		};
	}

	template <typename T>
	struct string_map : public tsl::robin_map<std::string, T, __detail::hash_string, __detail::equal_string>
	{
		T& operator [] (const std::string_view& key)
		{
			auto it = this->find(key);
			if(it == this->end())   return this->try_emplace(std::string(key), T()).first.value();
			else                    return it.value();
		}

		T& operator [] (std::string_view&& key)
		{
			auto it = this->find(key);
			if(it == this->end())   return this->try_emplace(std::string(std::move(key)), T()).first.value();
			else                    return it.value();
		}
	};

	struct str_view : public std::string_view
	{
		str_view()                          : std::string_view("") { }
		str_view(std::string&& s)           : std::string_view(std::move(s)) { }
		str_view(std::string_view&& s)      : std::string_view(std::move(s)) { }
		str_view(const std::string& s)      : std::string_view(s) { }
		str_view(const std::string_view& s) : std::string_view(s) { }
		str_view(const char* s)             : std::string_view(s) { }
		str_view(const char* s, size_t l)   : std::string_view(s, l) { }

		std::string_view sv() const   { return *this; }
		str_view drop(size_t n) const { return (this->size() > n ? str_view(this->substr(n)) : ""); }
		str_view take(size_t n) const { return (this->size() > n ? str_view(this->substr(0, n)) : *this); }

		std::string str() const { return std::string(*this); }
	};

	namespace util
	{
		std::vector<ikura::str_view> split(ikura::str_view view, char delim);
		std::vector<std::string> split_copy(ikura::str_view view, char delim);
		std::string join(const ikura::span<ikura::str_view>&, const std::string& delim);
		std::string join(const ikura::span<std::string>&, const std::string& delim);

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


	struct Emote
	{
		Emote() : name("") { }
		explicit Emote(std::string name) : name(std::move(name)) { }

		std::string name;
	};

	struct Message
	{
		struct Fragment
		{
			Fragment(ikura::str_view sv) : isEmote(false), str(sv.str()) { }
			Fragment(const Emote& emote) : isEmote(true), emote(emote) { }

			~Fragment() { if(isEmote) this->emote.~Emote(); else this->str.~decltype(this->str)(); }

			Fragment(const Fragment& frag)
			{
				this->isEmote = frag.isEmote;
				if(isEmote) new (&this->str) std::string(frag.str);
				else        new (&this->emote) Emote(frag.emote);
			}

			Fragment& operator = (const Fragment& frag)
			{
				if(this->isEmote)   this->emote.~Emote();
				else                this->str.~decltype(this->str)();

				this->isEmote = frag.isEmote;
				if(isEmote) new (&this->str) std::string(frag.str);
				else        new (&this->emote) Emote(frag.emote);

				return *this;
			}


			bool isEmote = 0;
			union {
				std::string str;
				Emote emote;
			};
		};

		std::vector<Fragment> fragments;

		Message& add(ikura::str_view sv) { fragments.emplace_back(sv); return *this; }
		Message& add(const Emote& emote) { fragments.emplace_back(emote); return *this; }
	};

	struct Channel
	{
		virtual ~Channel() { }

		virtual bool shouldReplyMentions() const = 0;
		virtual std::string getName() const = 0;
		virtual std::string getUsername() const = 0;
		virtual std::string getCommandPrefix() const = 0;

		virtual void sendMessage(const Message& msg) const = 0;
	};
}

namespace zpr
{
	template<>
	struct print_formatter<ikura::str_view>
	{
		std::string print(ikura::str_view sv, const format_args& args)
		{
			return print_formatter<std::string_view>().print(sv.sv(), args);
		}
	};
}




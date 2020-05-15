// defs.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "zpr.h"
#include "types.h"

namespace ikura
{
	struct Buffer;
	struct Serialisable
	{
		virtual ~Serialisable() { }
		virtual void serialise(Buffer& out) const = 0;
	};

	namespace serialise
	{
		constexpr uint8_t TAG_U8                = 0x01;
		constexpr uint8_t TAG_U16               = 0x02;
		constexpr uint8_t TAG_U32               = 0x03;
		constexpr uint8_t TAG_U64               = 0x04;
		constexpr uint8_t TAG_S8                = 0x05;
		constexpr uint8_t TAG_S16               = 0x06;
		constexpr uint8_t TAG_S32               = 0x07;
		constexpr uint8_t TAG_S64               = 0x08;
		constexpr uint8_t TAG_STRING            = 0x09;
		constexpr uint8_t TAG_STL_UNORD_MAP     = 0x0A;
		constexpr uint8_t TAG_TSL_HASHMAP       = 0x0B;
		constexpr uint8_t TAG_F32               = 0x0C;
		constexpr uint8_t TAG_F64               = 0x0D;
		constexpr uint8_t TAG_BOOL_TRUE         = 0x0E;
		constexpr uint8_t TAG_BOOL_FALSE        = 0x0F;
		constexpr uint8_t TAG_STL_VECTOR        = 0x10;
		constexpr uint8_t TAG_STL_ORD_MAP       = 0x11;
		constexpr uint8_t TAG_SMALL_U64         = 0x12;

		constexpr uint8_t TAG_TWITCH_DB         = 0x41;
		constexpr uint8_t TAG_COMMAND_DB        = 0x42;
		constexpr uint8_t TAG_TWITCH_USER       = 0x43;
		constexpr uint8_t TAG_COMMAND           = 0x44;
		constexpr uint8_t TAG_INTERP_STATE      = 0x45;
		constexpr uint8_t TAG_MACRO             = 0x46;
		constexpr uint8_t TAG_FUNCTION          = 0x47;
		constexpr uint8_t TAG_INTERP_VALUE      = 0x48;
		constexpr uint8_t TAG_TWITCH_USER_CREDS = 0x49;
		constexpr uint8_t TAG_TWITCH_CHANNEL    = 0x4A;

		// if the byte has 0x80 set, then the lower 7 bits represents a truncated 64-bit number. it's a further
		// extension of the SMALL_U64 thing, but literally only uses 1 byte for sizes between 0 - 127
		constexpr uint8_t TAG_TINY_U64          = 0x80;
	}

	namespace permissions
	{
		constexpr uint64_t EVERYONE         = 0x001;
		constexpr uint64_t FOLLOWER         = 0x002;
		constexpr uint64_t TRUSTED          = 0x004;
		constexpr uint64_t VIP              = 0x008;
		constexpr uint64_t SUBSCRIBER       = 0x010;
		constexpr uint64_t MODERATOR        = 0x020;
		constexpr uint64_t BROADCASTER      = 0x040;
		constexpr uint64_t OWNER            = 0x080;
		constexpr uint64_t WHITELIST        = 0x100;
	}

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
				bool silentInterpErrors;
				std::string commandPrefix;
			};

			std::string getOwner();
			std::string getUsername();
			std::string getOAuthToken();
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

	namespace util
	{
		template <typename T> T to_native(T x);
		template <typename T> T to_network(T x);

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

		Message() { }
		Message(ikura::str_view sv) { this->add(sv); }

		std::vector<Fragment> fragments;

		Message& add(ikura::str_view sv) { fragments.emplace_back(sv); return *this; }
		Message& add(const Emote& emote) { fragments.emplace_back(emote); return *this; }
	};

	struct Channel
	{
		virtual ~Channel() { }

		virtual bool shouldReplyMentions() const = 0;
		virtual bool shouldPrintInterpErrors() const = 0;
		virtual std::string getName() const = 0;
		virtual std::string getUsername() const = 0;
		virtual std::string getCommandPrefix() const = 0;
		virtual uint64_t getUserPermissions(ikura::str_view user) const = 0;

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




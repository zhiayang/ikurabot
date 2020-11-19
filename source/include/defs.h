// defs.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "zpr.h"
#include "types.h"

// forward decls
namespace picojson { class value; }

namespace ikura
{
	template <typename T>
	struct Synchronised;

	struct PermissionSet;

	struct Span;
	struct Buffer;

	template <typename T>
	struct future;
}

namespace ikura
{
	enum class Backend
	{
		Invalid,

		IRC,
		Twitch,
		Discord,
	};

	namespace permissions
	{
		constexpr uint64_t EVERYONE         = 0x001;
		constexpr uint64_t FOLLOWER         = 0x002;
		constexpr uint64_t VIP              = 0x008;
		constexpr uint64_t SUBSCRIBER       = 0x010;
		constexpr uint64_t MODERATOR        = 0x020;
		constexpr uint64_t BROADCASTER      = 0x040;
		constexpr uint64_t OWNER            = 0x080;
	}

	namespace console
	{
		void init();
		void logMessage(Backend backend, ikura::str_view server, ikura::str_view channel,
			double time, ikura::str_view user, ikura::str_view message);
	}

	namespace unicode
	{
		// all of these functions return 0 on false, and the number of bytes (> 0) on true.
		size_t is_digit(ikura::str_view str);
		size_t is_letter(ikura::str_view str);
		size_t is_symbol(ikura::str_view str);
		size_t is_any_symbol(ikura::str_view str);
		size_t is_punctuation(ikura::str_view str);
		size_t is_category(ikura::str_view str, const std::initializer_list<int>& categories);
		size_t get_codepoint_length(ikura::str_view str);

		std::string normalise(ikura::str_view str);
		size_t codepoint_count(ikura::str_view str);

		size_t get_byte_length(int32_t codepoint);
		std::vector<int32_t> to_utf32(ikura::str_view str);
		std::string to_utf8(std::vector<int32_t> codepoints);
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
		std::string getCurrentTimeString();

		// only does latin characters. i'm lazy.
		std::string lowercase(ikura::str_view s);
		std::string uppercase(ikura::str_view s);

		size_t getFileSize(const std::string& path);
		std::pair<uint8_t*, size_t> readEntireFile(const std::string& path);

		std::tuple<int, uint8_t*, size_t> mmapEntireFile(const std::string& path);
		void munmapEntireFile(int fd, uint8_t* buf, size_t len);

		std::optional<int64_t>  stoi(ikura::str_view s, int base = 10);
		std::optional<uint64_t> stou(ikura::str_view s, int base = 10);

		std::pair<ikura::str_view, ikura::str_view> bisect(ikura::str_view input, char delim);

		Result<picojson::value> parseJson(ikura::str_view json);

		void sleep_for(const std::chrono::nanoseconds& dur);

		std::string getEnvironmentVar(const std::string& name);
	}

	namespace lg
	{
		bool isDebugEnabled();
		std::string getLogMessagePreambleString(int lvl, ikura::str_view sys);

		template <typename... Args>
		static inline void __generic_log(int lvl, ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			if(!isDebugEnabled() && lvl < 0)
				return;

			auto out = getLogMessagePreambleString(lvl, sys);

			out += zpr::sprint(fmt, args...);

			if(lvl >= 2)    fprintf(stderr, "%s\n", out.c_str());
			else            printf("%s\n", out.c_str());
		}

		template <typename... Args>
		static void log(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(0, sys, fmt, args...);
		}

		template <typename... Args>
		static void warn(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(1, sys, fmt, args...);
		}

		template <typename... Args>
		static void error(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(2, sys, fmt, args...);
		}

		// for convenience, this returns false.
		template <typename... Args>
		static bool error_b(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(2, sys, fmt, args...);
			return false;
		}

		// for convenience, this returns false.
		template <typename... Args>
		static std::nullopt_t error_o(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(2, sys, fmt, args...);
			return std::nullopt;
		}

		template <typename... Args>
		static void fatal(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(3, sys, fmt, args...);
			abort();
		}

		template <typename... Args>
		static void dbglog(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(-1, sys, fmt, args...);
		}
	}


	namespace random
	{
		template <typename T> T get();
		template <typename T> T get(T min, T max);

		template <typename T> T get_normal(T mean, T stddev);
	}

	namespace base64
	{
		std::string encode(const uint8_t* src, size_t len);
		std::string decode(ikura::str_view src);
	}

	namespace hash
	{
		void sha256(uint8_t out[32], const void* input, size_t length);
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

		Message(Message&&) = default;
		Message& operator = (Message&& other)
		{
			if(&other != this)
			{
				this->fragments = std::move(other.fragments);
				this->next = std::move(other.next);
			}

			return *this;
		}

		bool empty() const { return this->fragments.empty() && this->next == nullptr; }

		std::vector<Fragment> fragments;
		std::unique_ptr<Message> next;
		std::string discordReplyId; // the id of the message we are replying to

		Message& link(Message m)
		{
			this->next = std::make_unique<Message>(std::move(m));
			return *this->next;
		}

		Message& add(ikura::str_view sv) { fragments.emplace_back(sv); return *this; }
		Message& addNoSpace(ikura::str_view sv)
		{
			if(fragments.empty() || fragments.back().isEmote)
				return add(sv);

			fragments.back().str += sv;
			return *this;
		}
		Message& add(const Emote& emote) { fragments.emplace_back(emote); return *this; }
	};

	struct Channel
	{
		virtual ~Channel() { }

		virtual bool shouldReplyMentions() const = 0;
		virtual bool shouldPrintInterpErrors() const = 0;
		virtual bool shouldRunMessageHandlers() const = 0;
		virtual std::string getName() const = 0;
		virtual std::string getUsername() const = 0;
		virtual std::vector<std::string> getCommandPrefixes() const = 0;
		virtual Backend getBackend() const = 0;
		virtual bool shouldLurk() const = 0;
		virtual bool checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const = 0;

		virtual void sendMessage(const Message& msg) const = 0;
	};
}

namespace zpr
{
	// template<>
	// struct print_formatter<ikura::str_view>
	// {
	// 	std::string print(ikura::str_view sv, const format_args& args)
	// 	{
	// 		return print_formatter<std::string_view>().print(sv.sv(), args);
	// 	}
	// };
}




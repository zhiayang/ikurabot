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
	enum class Backend
	{
		Invalid,

		Twitch,
		Discord,
	};

	namespace serialise
	{
		constexpr uint8_t TAG_U8                    = 0x01;
		constexpr uint8_t TAG_U16                   = 0x02;
		constexpr uint8_t TAG_U32                   = 0x03;
		constexpr uint8_t TAG_U64                   = 0x04;
		constexpr uint8_t TAG_S8                    = 0x05;
		constexpr uint8_t TAG_S16                   = 0x06;
		constexpr uint8_t TAG_S32                   = 0x07;
		constexpr uint8_t TAG_S64                   = 0x08;
		constexpr uint8_t TAG_STRING                = 0x09;
		constexpr uint8_t TAG_STL_UNORD_MAP         = 0x0A;
		constexpr uint8_t TAG_TSL_HASHMAP           = 0x0B;
		constexpr uint8_t TAG_F32                   = 0x0C;
		constexpr uint8_t TAG_F64                   = 0x0D;
		constexpr uint8_t TAG_BOOL_TRUE             = 0x0E;
		constexpr uint8_t TAG_BOOL_FALSE            = 0x0F;
		constexpr uint8_t TAG_STL_VECTOR            = 0x10;
		constexpr uint8_t TAG_STL_ORD_MAP           = 0x11;
		constexpr uint8_t TAG_SMALL_U64             = 0x12;
		constexpr uint8_t TAG_STL_PAIR              = 0x13;
		constexpr uint8_t TAG_REL_STRING            = 0x14;

		constexpr uint8_t TAG_TWITCH_DB             = 0x41;
		constexpr uint8_t TAG_COMMAND_DB            = 0x42;
		constexpr uint8_t TAG_TWITCH_USER           = 0x43;
		constexpr uint8_t TAG_COMMAND               = 0x44;
		constexpr uint8_t TAG_INTERP_STATE          = 0x45;
		constexpr uint8_t TAG_MACRO                 = 0x46;
		constexpr uint8_t TAG_FUNCTION              = 0x47;
		constexpr uint8_t TAG_INTERP_VALUE          = 0x48;
		constexpr uint8_t TAG_SHARED_DB             = 0x49;
		constexpr uint8_t TAG_TWITCH_CHANNEL        = 0x4A;
		constexpr uint8_t TAG_MARKOV_DB             = 0x4B;
		constexpr uint8_t TAG_MARKOV_WORD_LIST      = 0x4C;
		constexpr uint8_t TAG_MARKOV_WORD           = 0x4D;
		constexpr uint8_t TAG_TWITCH_LOG            = 0x4E;
		constexpr uint8_t TAG_TWITCH_LOG_MSG        = 0x4F;
		constexpr uint8_t TAG_MESSAGE_DB            = 0x50;
		constexpr uint8_t TAG_MARKOV_STORED_WORD    = 0x51;
		constexpr uint8_t TAG_DISCORD_DB            = 0x52;
		constexpr uint8_t TAG_DISCORD_GUILD         = 0x53;
		constexpr uint8_t TAG_DISCORD_CHANNEL       = 0x54;
		constexpr uint8_t TAG_DISCORD_USER          = 0x55;
		constexpr uint8_t TAG_DISCORD_ROLE          = 0x56;
		constexpr uint8_t TAG_PERMISSION_SET        = 0x57;
		constexpr uint8_t TAG_GROUP                 = 0x58;
		constexpr uint8_t TAG_GENERIC_USER          = 0x59;

		// if the byte has 0x80 set, then the lower 7 bits represents a truncated 64-bit number. it's a further
		// extension of the SMALL_U64 thing, but literally only uses 1 byte for sizes between 0 - 127
		constexpr uint8_t TAG_TINY_U64              = 0x80;
	}

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
	}

	namespace config
	{
		bool load(ikura::str_view path);

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
			std::vector<std::string> getIgnoredUsers();
			bool isUserIgnored(ikura::str_view id);
		}

		namespace discord
		{
			struct Guild
			{
				std::string id;
				bool lurk;
				bool respondToPings;
				bool silentInterpErrors;
				std::string commandPrefix;
			};

			std::string getUsername();
			std::string getOAuthToken();
			std::vector<Guild> getJoinGuilds();
			ikura::discord::Snowflake getUserId();
			std::vector<ikura::discord::Snowflake> getIgnoredUserIds();
			bool isUserIgnored(ikura::discord::Snowflake userid);
		}

		namespace global
		{
			int getConsolePort();
			bool stripMentionsFromMarkov();

			size_t getMinMarkovLength();
			size_t getMaxMarkovRetries();
		}
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
	}

	namespace lg
	{
		constexpr bool ENABLE_DEBUG = false;

		std::string getLogMessagePreambleString(int lvl, ikura::str_view sys);

		template <typename... Args>
		static inline void __generic_log(int lvl, ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			if(!ENABLE_DEBUG && lvl < 0)
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

		// for convenience, this returns false.
		template <typename... Args>
		static bool error(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(2, sys, fmt, args...);
			return false;
		}

		template <typename... Args>
		static void fatal(ikura::str_view sys, const std::string& fmt, Args&&... args)
		{
			__generic_log(3, sys, fmt, args...);
			exit(1);
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
		Message& addNoSpace(ikura::str_view sv)
		{
			if(fragments.empty() || fragments.back().isEmote)
				return add(sv);

			fragments.back().str += sv;
			return *this;
		}
		Message& add(const Emote& emote) { fragments.emplace_back(emote); return *this; }
	};

	struct PermissionSet : Serialisable
	{
		uint64_t flags = 0;     // see defs.h/ikura::permissions
		std::vector<uint64_t> whitelist;
		std::vector<uint64_t> blacklist;

		std::vector<discord::Snowflake> role_whitelist;
		std::vector<discord::Snowflake> role_blacklist;

		bool check(uint64_t flags, const std::vector<uint64_t>& groups, const std::vector<discord::Snowflake>& discordRoles) const;

		static PermissionSet fromFlags(uint64_t f)
		{
			PermissionSet ret;
			ret.flags = f;

			return ret;
		}

		virtual void serialise(Buffer& buf) const override;
		static std::optional<PermissionSet> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_PERMISSION_SET;
	};

	struct Channel
	{
		virtual ~Channel() { }

		virtual bool shouldReplyMentions() const = 0;
		virtual bool shouldPrintInterpErrors() const = 0;
		virtual std::string getName() const = 0;
		virtual std::string getUsername() const = 0;
		virtual std::string getCommandPrefix() const = 0;
		virtual Backend getBackend() const = 0;
		virtual bool checkUserPermissions(ikura::str_view userid, const PermissionSet& required) const = 0;

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




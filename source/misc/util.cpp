
// util.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <limits>
#include <random>
#include <chrono>
#include <fstream>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "defs.h"
#include "picojson.h"

namespace ikura
{
	namespace lg
	{
		constexpr bool ENABLE_DEBUG = false;
		constexpr bool USE_COLOURS  = true;

		constexpr const char* WHITE_BOLD_RED_BG = "\x1b[1m\x1b[37m\x1b[48;5;9m";

		constexpr const char* DBG    = (USE_COLOURS ? colours::WHITE : "");
		constexpr const char* LOG    = (USE_COLOURS ? colours::GREY_BOLD : "");
		constexpr const char* WRN    = (USE_COLOURS ? colours::YELLOW_BOLD : "");
		constexpr const char* ERR    = (USE_COLOURS ? colours::RED_BOLD : "");
		constexpr const char* FTL    = (USE_COLOURS ? WHITE_BOLD_RED_BG : "");

		constexpr const char* RESET  = (USE_COLOURS ? colours::COLOUR_RESET : "");
		constexpr const char* SUBSYS = (USE_COLOURS ? colours::BLUE_BOLD : "");

		std::string getLogMessagePreambleString(int lvl, ikura::str_view sys)
		{
			const char* lvlcolour = 0;
			const char* str = 0;

			if(lvl == -1)       { lvlcolour = DBG;  str = "[dbg]"; }
			else if(lvl == 0)   { lvlcolour = LOG;  str = "[log]"; }
			else if(lvl == 1)   { lvlcolour = WRN;  str = "[wrn]"; }
			else if(lvl == 2)   { lvlcolour = ERR;  str = "[err]"; }
			else if(lvl == 3)   { lvlcolour = FTL;  str = "[ftl]"; }

			auto timestamp = zpr::sprint("%s %s|%s", util::getCurrentTimeString(), colours::WHITE_BOLD, RESET);
			auto loglevel  = zpr::sprint("%s%s%s", lvlcolour, str, RESET);
			auto subsystem = zpr::sprint("%s%s%s", SUBSYS, sys, RESET);

			return zpr::sprint("%s %s %s: ", timestamp, loglevel, subsystem);
		}

		bool isDebugEnabled() { return ENABLE_DEBUG; }
	}

	namespace util
	{
		void sleep_for(const std::chrono::nanoseconds& dur)
		{
			// sleep_for is totally broken on windows. fuckin hell.
			std::this_thread::sleep_until(std::chrono::system_clock::now() + dur);
		}




		std::pair<ikura::str_view, ikura::str_view> bisect(ikura::str_view input, char delim)
		{
			// note that ikura::str_view has well-defined behaviour for drop and take if
			// the size is overrun -- they return empty views.
			auto x = input.take(input.find(delim));
			auto xs = input.drop(x.size() + 1).trim_front();

			return { x, xs };
		}


		Result<pj::value> parseJson(ikura::str_view str)
		{
			pj::value json; std::string err;
			pj::parse(json, str.begin(), str.end(), &err);
			if(!err.empty()) return err;

			return json;
		}

		std::string lowercase(ikura::str_view s)
		{
			std::string ret; ret.reserve(s.size());
			for(char c : s)
			{
				if('A' <= c && c <= 'Z')
					ret += (char) (c | 0x20);
				else
					ret += c;
			}
			return ret;
		}

		std::string uppercase(ikura::str_view s)
		{
			std::string ret; ret.reserve(s.size());
			for(char c : s)
			{
				if('a' <= c && c <= 'z')
					ret += (char) (c & ~0x20);
				else
					ret += c;
			}
			return ret;
		}


		std::mutex localTimeLock;
		std::string getCurrentTimeString()
		{
			auto tp = std::chrono::system_clock::now();
			auto t = std::chrono::system_clock::to_time_t(tp);

			std::tm* tm = nullptr;
			{
				// this shit ain't threadsafe.
				auto lk = std::unique_lock<std::mutex>(localTimeLock);
				tm = std::localtime(&t);
			}

			if(!tm) return "??";

			return zpr::sprint("%02d:%02d:%02d",
				tm->tm_hour, tm->tm_min, tm->tm_sec);
		}

		std::optional<int64_t> stoi(ikura::str_view s, int base)
		{
			if(s.empty())
				return { };

			char* tmp = 0;
			auto ss = s.str();
			int64_t ret = strtoll(ss.c_str(), &tmp, base);
			if(tmp != ss.data() + ss.size())
				return { };

			return ret;
		}

		std::optional<uint64_t> stou(ikura::str_view s, int base)
		{
			if(s.empty())
				return { };

			char* tmp = 0;
			auto ss = s.str();
			uint64_t ret = strtoull(ss.c_str(), &tmp, base);
			if(tmp != ss.data() + ss.size())
				return { };

			return ret;
		}

		std::string join(const ikura::span<ikura::str_view>& xs, const std::string& delim)
		{
			std::string ret;
			for(size_t i = 0; i < xs.size(); i++)
			{
				ret += xs[i];
				if(i + 1 != xs.size())
					ret += delim;
			}

			return ret;
		}

		std::string join(const ikura::span<std::string>& xs, const std::string& delim)
		{
			std::string ret;
			for(size_t i = 0; i < xs.size(); i++)
			{
				ret += xs[i];
				if(i + 1 != xs.size())
					ret += delim;
			}

			return ret;
		}




		std::vector<ikura::str_view> split(ikura::str_view view, char delim)
		{
			std::vector<ikura::str_view> ret;

			while(true)
			{
				size_t ln = view.find(delim);

				if(ln != ikura::str_view::npos)
				{
					ret.emplace_back(view.data(), ln);
					view.remove_prefix(ln + 1);
				}
				else
				{
					break;
				}
			}

			// account for the case when there's no trailing newline, and we still have some stuff stuck in the view.
			if(!view.empty())
				ret.emplace_back(view.data(), view.length());

			return ret;
		};

		std::vector<std::string> split_copy(ikura::str_view view, char delim)
		{
			auto xs = split(view, delim);
			std::vector<std::string> ret; ret.reserve(xs.size());

			for(auto& x : xs)
				ret.push_back(x.str());

			return ret;
		};


		uint64_t getMillisecondTimestamp()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}


		size_t getFileSize(const std::string& path)
		{
			struct stat st;
			if(stat(path.c_str(), &st) != 0)
			{
				char buf[128] = { 0 };
				strerror_r(errno, buf, 127);
				lg::error("misc", "failed to get filesize for '%s' (error code %d / %s)", path, errno, buf);

				return -1;
			}

			return st.st_size;
		}

		std::pair<uint8_t*, size_t> readEntireFile(const std::string& path)
		{
			auto bad = std::pair(nullptr, 0);;

			auto sz = getFileSize(path);
			if(sz == static_cast<size_t>(-1)) return bad;

			// i'm lazy, so just use fstreams.
			auto fs = std::fstream(path);
			if(!fs.good()) return bad;


			uint8_t* buf = new uint8_t[sz + 1];
			fs.read(reinterpret_cast<char*>(buf), sz);
			fs.close();

			return std::pair(buf, sz);
		}

		std::tuple<int, uint8_t*, size_t> mmapEntireFile(const std::string& path)
		{
			auto bad = std::tuple(-1, nullptr, 0);;

			auto sz = getFileSize(path);
			if(sz == static_cast<size_t>(-1))
				return bad;

			auto fd = open(path.c_str(), O_RDONLY, 0);
			if(fd == -1)
				return bad;

			auto buf = (uint8_t*) mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0);
			if(buf == (void*) (-1))
			{
				perror("there was an error reading the file");
				exit(-1);
			}

			return { fd, buf, sz };
		}

		void munmapEntireFile(int fd, uint8_t* buf, size_t len)
		{
			munmap((void*) buf, len);
			close(fd);
		}
	}

	namespace random
	{
		// this is kinda dumb but... meh.
		template <typename T>
		struct rd_state_t
		{
			rd_state_t() : mersenne(std::random_device()()) { }

			std::mt19937 mersenne;
		};

		template <typename T>
		rd_state_t<T> rd_state;

		template <typename T>
		T get()
		{
			auto& st = rd_state<T>;
			return std::uniform_int_distribution<T>(std::numeric_limits<T>::min(), std::numeric_limits<T>::max())(st.mersenne);
		}

		template <typename T>
		T get(T min, T max)
		{
			auto& st = rd_state<T>;
			return std::uniform_int_distribution<T>(min, max)(st.mersenne);
		}

		template <typename T>
		T get_normal(T mean, T stddev)
		{
			auto& st = rd_state<T>;
			return std::normal_distribution<T>(mean, stddev)(st.mersenne);
		}

		template unsigned char  get<unsigned char>();
		template unsigned char  get<unsigned char>(unsigned char, unsigned char);

		template unsigned short get<unsigned short>();
		template unsigned short get<unsigned short>(unsigned short, unsigned short);

		template unsigned int get<unsigned int>();
		template unsigned int get<unsigned int>(unsigned int, unsigned int);

		template unsigned long get<unsigned long>();
		template unsigned long get<unsigned long>(unsigned long, unsigned long);

		template unsigned long long get<unsigned long long>();
		template unsigned long long get<unsigned long long>(unsigned long long, unsigned long long);

		// template size_t get<size_t>();      template size_t get<size_t>(size_t, size_t);

		template double get_normal<double>(double, double);
	}

	namespace util
	{
		constexpr bool IS_BIG = (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

		template <> uint16_t to_native(uint16_t x) { if constexpr (IS_BIG) return __builtin_bswap16(x); else return x; }
		template <> uint32_t to_native(uint32_t x) { if constexpr (IS_BIG) return __builtin_bswap32(x); else return x; }
		template <> uint64_t to_native(uint64_t x) { if constexpr (IS_BIG) return __builtin_bswap64(x); else return x; }

		template <> uint16_t to_network(uint16_t x) { if constexpr (IS_BIG) return __builtin_bswap16(x); else return x; }
		template <> uint32_t to_network(uint32_t x) { if constexpr (IS_BIG) return __builtin_bswap32(x); else return x; }
		template <> uint64_t to_network(uint64_t x) { if constexpr (IS_BIG) return __builtin_bswap64(x); else return x; }


		template <> int16_t to_native(int16_t x) { if constexpr (IS_BIG) return __builtin_bswap16(x); else return x; }
		template <> int32_t to_native(int32_t x) { if constexpr (IS_BIG) return __builtin_bswap32(x); else return x; }
		template <> int64_t to_native(int64_t x) { if constexpr (IS_BIG) return __builtin_bswap64(x); else return x; }

		template <> int16_t to_network(int16_t x) { if constexpr (IS_BIG) return __builtin_bswap16(x); else return x; }
		template <> int32_t to_network(int32_t x) { if constexpr (IS_BIG) return __builtin_bswap32(x); else return x; }
		template <> int64_t to_network(int64_t x) { if constexpr (IS_BIG) return __builtin_bswap64(x); else return x; }
	}
}

// serialise.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdio.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "defs.h"
#include "buffer.h"

namespace ikura::serialise
{
	constexpr uint8_t TAG_U8            = 0x01;
	constexpr uint8_t TAG_U16           = 0x02;
	constexpr uint8_t TAG_U32           = 0x03;
	constexpr uint8_t TAG_U64           = 0x04;
	constexpr uint8_t TAG_S8            = 0x05;
	constexpr uint8_t TAG_S16           = 0x06;
	constexpr uint8_t TAG_S32           = 0x07;
	constexpr uint8_t TAG_S64           = 0x08;
	constexpr uint8_t TAG_STRING        = 0x09;
	constexpr uint8_t TAG_STL_UNORD_MAP = 0x0A;
	constexpr uint8_t TAG_TSL_HASHMAP   = 0x0B;

	constexpr uint8_t TAG_TWITCH_DB     = 0x81;
	constexpr uint8_t TAG_COMMAND_DB    = 0x82;
	constexpr uint8_t TAG_TWITCH_USER   = 0x83;
	constexpr uint8_t TAG_COMMAND       = 0x84;
	constexpr uint8_t TAG_INTERP_STATE  = 0x85;

	struct Serialisable
	{
		virtual ~Serialisable() { }
		virtual void serialise(Buffer& out) const = 0;
	};

	template<typename>
	struct is_unordered_map : std::false_type { };

	template<typename K, typename V>
	struct is_unordered_map<std::unordered_map<K, V>> : std::true_type { };

	template<typename>
	struct is_tsl_hashmap : std::false_type { };

	template<typename K, typename V, typename H, typename E, typename A, bool S, typename G>
	struct is_tsl_hashmap<tsl::robin_map<K, V, H, E, A, S, G>> : std::true_type { };

	template<typename V>
	struct is_tsl_hashmap<ikura::string_map<V>> : std::true_type { };



	struct Writer
	{
		Writer(Buffer& buf) : buffer(buf) { }

		void tag(uint8_t t)
		{
			ensure(1);
			buffer.write(&t, 1);
		}

		void write(uint8_t x)   { ensure(1); tag(TAG_U8);  buffer.write(&x, 1); }
		void write(uint16_t x)  { ensure(2); tag(TAG_U16); buffer.write(&x, 2); }
		void write(uint32_t x)  { ensure(4); tag(TAG_U32); buffer.write(&x, 4); }
		void write(uint64_t x)  { ensure(8); tag(TAG_U64); buffer.write(&x, 4); }

		void write(int8_t x)    { ensure(1); tag(TAG_S8);  buffer.write(&x, 1); }
		void write(int16_t x)   { ensure(2); tag(TAG_S16); buffer.write(&x, 2); }
		void write(int32_t x)   { ensure(4); tag(TAG_S32); buffer.write(&x, 4); }
		void write(int64_t x)   { ensure(8); tag(TAG_S64); buffer.write(&x, 8); }

		void write(const std::string& s)
		{
			this->write(std::string_view(s));
		}

		void write(std::string_view sv)
		{
			ensure(8 + sv.size()); tag(TAG_STRING);
			auto sz = sv.size(); buffer.write(&sz, sizeof(uint64_t));
			for(size_t i = 0; i < sz; i++)
				buffer.write(&sv[i], 1);
		}

		template <typename K, typename V>
		void write(const std::unordered_map<K, V>& map)
		{
			ensure(9); tag(TAG_STL_UNORD_MAP);
			auto sz = map.size(); buffer.write(&sz, sizeof(uint64_t));
			for(const auto& [ k, v ] : map)
				write(k), write(v);
		}

		template<typename K, typename V>
		void write(const tsl::robin_map<K, V>& map)
		{
			ensure(9); tag(TAG_TSL_HASHMAP);
			auto sz = map.size(); buffer.write(&sz, sizeof(uint64_t));
			for(const auto& [ k, v ] : map)
				write(k), write(v);
		}

		template<typename V>
		void write(const ikura::string_map<V>& map)
		{
			ensure(9); tag(TAG_TSL_HASHMAP);
			auto sz = map.size(); buffer.write(&sz, sizeof(uint64_t));
			for(const auto& [ k, v ] : map)
				write(k), write(v);
		}

		template <typename T>
		void write(const T& x) { x.serialise(buffer); }


	private:
		void ensure(size_t x)
		{
			// account implicitly for the tag byte
			while(this->buffer.remaining() < x + 1)
				this->buffer.grow();
		}

		Buffer& buffer;
	};










	struct Reader
	{
		Reader(Span& buf) : span(buf) { }


		template <typename T>
		bool read(T* out)
		{
			auto ret = this->read<T>();
			if(!ret.has_value())
				return false;

			*out = ret.value();
			return true;
		}

		template <typename T>
		std::optional<T> read()
		{
			using namespace std;

			// tag
			if(!ensure(1))
				return { };

			if constexpr (is_same_v<T, uint8_t>)            { if(tag() != TAG_U8)  return { }; }
			else if constexpr (is_same_v<T, uint16_t>)      { if(tag() != TAG_U16) return { }; }
			else if constexpr (is_same_v<T, uint32_t>)      { if(tag() != TAG_U32) return { }; }
			else if constexpr (is_same_v<T, uint64_t>)      { if(tag() != TAG_U64) return { }; }
			else if constexpr (is_same_v<T, int8_t>)        { if(tag() != TAG_S8)  return { }; }
			else if constexpr (is_same_v<T, int16_t>)       { if(tag() != TAG_S16) return { }; }
			else if constexpr (is_same_v<T, int32_t>)       { if(tag() != TAG_S32) return { }; }
			else if constexpr (is_same_v<T, int64_t>)       { if(tag() != TAG_S64) return { }; }
			else if constexpr (is_same_v<T, int64_t>)       { if(tag() != TAG_S64) return { }; }
			else if constexpr (is_same_v<T, std::string>)   { if(tag() != TAG_STRING) return { }; }
			else if constexpr (is_unordered_map<T>::value)  { if(tag() != TAG_STL_UNORD_MAP) return { }; }
			else if constexpr (is_tsl_hashmap<T>::value)    { if(tag() != TAG_TSL_HASHMAP) return { }; }
			else                                            { return T::deserialise(span); }

			if constexpr (is_same_v<T, uint8_t> || is_same_v<T, uint16_t> || is_same_v<T, uint32_t> || is_same_v<T, uint64_t>
						|| is_same_v<T, int8_t> || is_same_v<T, int16_t> || is_same_v<T, int32_t> || is_same_v<T, int64_t>)
			{
				auto ret = *span.as<T>();
				span.remove_prefix(sizeof(T));
				return ret;
			}
			else if constexpr (is_same_v<T, std::string>)
			{
				// tag already verified. get size
				if(!ensure(sizeof(uint64_t)))
					return { };

				auto sz = *span.as<uint64_t>();
				span.remove_prefix(sizeof(uint64_t));

				if(!ensure(sz))
					return { };

				auto ret = std::string((const char*) span.data(), sz);
				span.remove_prefix(sz);

				return ret;
			}
			else if constexpr (is_unordered_map<T>::value)
			{
				using K = typename T::key_type;
				using V = typename T::mapped_type;

				// tag already verified. get size
				if(!ensure(sizeof(uint64_t)))
					return { };

				auto sz = *span.as<uint64_t>();
				span.remove_prefix(sizeof(uint64_t));

				T ret;
				for(size_t i = 0; i < sz; i++)
				{
					auto k = read<K>();
					auto v = read<V>();

					if(!k.has_value() || !v.has_value())
						return { };

					ret.emplace(k.value(), v.value());
				}

				return ret;
			}
			else if constexpr (is_tsl_hashmap<T>::value)
			{
				using K = typename T::key_type;
				using V = typename T::mapped_type;

				// tag already verified. get size
				if(!ensure(sizeof(uint64_t)))
					return { };

				auto sz = *span.as<uint64_t>();
				span.remove_prefix(sizeof(uint64_t));

				T ret;
				for(size_t i = 0; i < sz; i++)
				{
					auto k = read<K>();
					auto v = read<V>();

					if(!k.has_value() || !v.has_value())
						return { };

					ret.emplace(k.value(), v.value());
				}

				return ret;
			}
			else
			{
				printf("UNKNOWN TYPE!\n");
				return { };
			}
		}

		uint8_t tag()
		{
			auto t = span.peek();
			span.remove_prefix(1);
			return t;
		}

	private:
		bool ensure(size_t sz) { return span.size() >= sz; }

		Span& span;
	};
}

// serialise.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdio.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "buffer.h"

namespace ikura::serialise
{
	template<typename>
	struct is_unordered_map : std::false_type { };

	template<typename K, typename V>
	struct is_unordered_map<std::unordered_map<K, V>> : std::true_type { };

	template<typename>
	struct is_vector : std::false_type { };

	template<typename T>
	struct is_vector<std::vector<T>> : std::true_type { };

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

		void write(float x)     { ensure(4); tag(TAG_F32); buffer.write(&x, 4); }
		void write(double x)    { ensure(8); tag(TAG_F64); buffer.write(&x, 8); }

		void write(bool x)      { ensure(1); tag(x ? TAG_BOOL_TRUE : TAG_BOOL_FALSE); }

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

		template <typename T>
		void write(const std::vector<T>& vec)
		{
			ensure(9); tag(TAG_STL_VECTOR);
			auto sz = vec.size(); buffer.write(&sz, sizeof(uint64_t));
			for(const auto& x : vec)
				write(x);
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

		template <typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
		void write(const T x) { x->serialise(buffer); }

		template <typename T, typename = std::enable_if_t<!std::is_pointer_v<T>>>
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

			uint8_t the_tag = 0;
			if constexpr (is_same_v<T, uint8_t>)            { if(the_tag = tag(), the_tag != TAG_U8)  return { }; }
			else if constexpr (is_same_v<T, uint16_t>)      { if(the_tag = tag(), the_tag != TAG_U16) return { }; }
			else if constexpr (is_same_v<T, uint32_t>)      { if(the_tag = tag(), the_tag != TAG_U32) return { }; }
			else if constexpr (is_same_v<T, uint64_t>)      { if(the_tag = tag(), the_tag != TAG_U64) return { }; }
			else if constexpr (is_same_v<T, int8_t>)        { if(the_tag = tag(), the_tag != TAG_S8)  return { }; }
			else if constexpr (is_same_v<T, int16_t>)       { if(the_tag = tag(), the_tag != TAG_S16) return { }; }
			else if constexpr (is_same_v<T, int32_t>)       { if(the_tag = tag(), the_tag != TAG_S32) return { }; }
			else if constexpr (is_same_v<T, int64_t>)       { if(the_tag = tag(), the_tag != TAG_S64) return { }; }
			else if constexpr (is_same_v<T, int64_t>)       { if(the_tag = tag(), the_tag != TAG_S64) return { }; }
			else if constexpr (is_same_v<T, float>)         { if(the_tag = tag(), the_tag != TAG_F32) return { }; }
			else if constexpr (is_same_v<T, double>)        { if(the_tag = tag(), the_tag != TAG_F64) return { }; }
			else if constexpr (is_same_v<T, std::string>)   { if(the_tag = tag(), the_tag != TAG_STRING) return { }; }
			else if constexpr (is_vector<T>::value)         { if(the_tag = tag(), the_tag != TAG_STL_VECTOR) return { }; }
			else if constexpr (is_unordered_map<T>::value)  { if(the_tag = tag(), the_tag != TAG_STL_UNORD_MAP) return { }; }
			else if constexpr (is_tsl_hashmap<T>::value)    { if(the_tag = tag(), the_tag != TAG_TSL_HASHMAP) return { }; }
			else if constexpr (is_pointer_v<T>)             { return std::decay_t<decltype(*declval<T>())>::deserialise(span); }
			else if constexpr (is_same_v<T, bool>)          {
				if(the_tag = tag(), the_tag != TAG_BOOL_TRUE && the_tag != TAG_BOOL_FALSE)
					return { };
			}
			else                                            { return T::deserialise(span); }

			if constexpr (is_same_v<T, uint8_t> || is_same_v<T, uint16_t> || is_same_v<T, uint32_t> || is_same_v<T, uint64_t>
						|| is_same_v<T, int8_t> || is_same_v<T, int16_t> || is_same_v<T, int32_t> || is_same_v<T, int64_t>
						|| is_same_v<T, float> || is_same_v<T, double>)
			{
				T ret = 0;
				// use memcpy to circumvent alignment
				memcpy(&ret, span.as<T>(), sizeof(T));
				span.remove_prefix(sizeof(T));
				return ret;
			}
			else if constexpr (is_same_v<T, bool>)
			{
				return (the_tag == TAG_BOOL_TRUE);
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
			else if constexpr (is_vector<T>::value)
			{
				using V = typename T::value_type;

				// tag already verified. get size
				if(!ensure(sizeof(uint64_t)))
					return { };

				auto sz = *span.as<uint64_t>();
				span.remove_prefix(sizeof(uint64_t));

				T ret;
				for(size_t i = 0; i < sz; i++)
				{
					auto x = read<V>();
					if(!x.has_value())
						return { };

					ret.push_back(x.value());
				}

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

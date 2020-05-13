// types.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <vector>

#include "tsl/robin_map.h"

namespace ikura
{
	template <typename T>
	struct span
	{
		using iterator = T*;
		using const_iterator = const T*;

		~span() { }

		span() : ptr(nullptr), cnt(0) { }
		span(const T* xs, size_t len) : ptr(xs), cnt(len) { }

		span(const span& other) : span(other.ptr, other.cnt) { }
		span(span&& other) : ptr(other.ptr), cnt(other.cnt) { other.ptr = 0; other.cnt = 0; }

		span(const std::vector<T>& v) : ptr(v.data()), cnt(v.size()) { }

		span& operator = (const span& other)
		{
			if(this != &other)  return *this = span(other);
			else                return *this;
		}

		// move assign
		span& operator = (span&& other)
		{
			if(this != &other)
			{
				this->ptr = other.ptr; other.ptr = 0;
				this->cnt = other.cnt; other.cnt = 0;
			}

			return *this;
		}

		size_t find(const T& x) const
		{
			for(size_t i = 0; i < this->cnt; i++)
				if(this->ptr[i] == x)
					return i;

			return -1;
		}

		span subspan(size_t idx, size_t len = -1) const
		{
			if(len == -1 && idx >= this->cnt)
				return span();

			if(len == (size_t) -1)
				len = this->cnt - idx;

			return span(this->ptr + idx, len);
		}

		void clear() { this->ptr = 0; this->cnt = 0; }

		span& remove_prefix(size_t n)
		{
			assert(n <= this->cnt);
			this->ptr += n;
			this->cnt -= n;

			return *this;
		}

		span& remove_suffix(size_t n)
		{
			assert(n <= this->cnt);
			this->cnt -= n;

			return *this;
		}

		std::vector<T> vec() const              { return std::vector<T>(this->begin(), this->end()); }

		span drop(size_t n) const               { auto copy = *this; copy.remove_prefix(n); return copy; }
		span take(size_t n) const               { auto copy = *this; copy.remove_suffix(this->cnt - n); return copy; }

		const T& front() const                  { assert(this->cnt > 0); return this->ptr[0]; }
		const T& back() const                   { assert(this->cnt > 0); return this->ptr[this->cnt - 1]; }

		const T& operator [] (size_t idx) const { assert(this->cnt > idx); return this->ptr[idx]; }

		iterator begin()                        { return this->ptr; }
		iterator end()                          { return this->ptr + this->cnt; }

		const_iterator begin() const            { return this->ptr; }
		const_iterator end() const              { return this->ptr + this->cnt; }

		const char* data() const                { return this->ptr; }
		size_t size() const                     { return this->cnt; }
		bool empty() const                      { return this->cnt == 0; }

	private:
		const T* ptr = 0;
		size_t cnt = 0;
	};



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
}

// span.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

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

		void remove_prefix(size_t n)
		{
			assert(n <= this->cnt);
			this->ptr += n;
			this->cnt -= n;
		}

		void remove_suffix(size_t n)
		{
			assert(n <= this->cnt);
			this->cnt -= n;
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
}
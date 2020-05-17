// buffer.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "types.h"

namespace ikura
{
	struct Span;

	struct Buffer
	{
		explicit Buffer(size_t cap);
		~Buffer();

		Buffer(const Buffer&) = delete;
		Buffer& operator = (const Buffer&) = delete;

		Buffer(Buffer&& oth);
		Buffer& operator = (Buffer&& oth);


		template <typename T> T* as() { return (T*) this->ptr; }
		template <typename T> const T* as() const { return (T*) this->ptr; }

		Span span() const;
		Buffer clone() const;

		uint8_t* data();
		const uint8_t* data() const;

		size_t size() const;
		bool full() const;
		size_t remaining() const;

		void clear();

		size_t write(Span spn);
		size_t write(const Buffer& buf);
		size_t write(const void* data, size_t len);

		void grow();            // auto-expands by 1.6x
		void grow(size_t sz);   // expands only by the specified amount
		void resize(size_t sz); // changes the size to be sz. (only expands, never contracts)

		static Buffer empty();
		static Buffer fromString(const std::string& s);

	private:
		size_t len;
		size_t cap;
		uint8_t* ptr;
	};

	struct Span
	{
		Span(const uint8_t* p, size_t l) : ptr(p), len(l) { }

		Span(Span&&) = default;
		Span(const Span&) = default;
		Span& operator = (Span&&) = default;
		Span& operator = (const Span&) = default;

		Buffer reify() const
		{
			auto ret = Buffer(this->len);
			ret.write(this->ptr, this->len);

			return ret;
		}

		size_t size() const { return this->len; }
		const uint8_t* data() const { return this->ptr; }

		Span& remove_prefix(size_t n) { n = std::min(n, this->len); this->ptr += n; this->len -= n; return *this; }
		Span& remove_suffix(size_t n) { n = std::min(n, this->len); this->len -= n; return *this; }

		Span drop(size_t n) const { auto copy = *this; return copy.remove_prefix(n); }
		Span take(size_t n) const { auto copy = *this; return copy.remove_suffix(this->len > n ? this->len - n : 0); }

		template <typename T> T* as() { return (T*) this->ptr; }
		template <typename T> const T* as() const { return (T*) this->ptr; }

		uint8_t peek(size_t i = 0) const { return this->ptr[i]; }


		static Span fromString(ikura::str_view sv)
		{
			return Span((const uint8_t*) sv.data(), sv.size());
		}

	private:
		const uint8_t* ptr;
		size_t len;
	};
}




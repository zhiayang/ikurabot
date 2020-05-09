// buffer.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdio.h>

#include <string>
#include <string_view>

#include "buffer.h"

namespace ikura
{
	Buffer::Buffer(size_t cap) : len(0), cap(cap), ptr(new uint8_t[cap])
	{
	}

	Buffer::~Buffer()
	{
		if(this->ptr)
			delete[] this->ptr;
	}

	Buffer::Buffer(Buffer&& oth)
	{
		this->ptr = oth.ptr;    oth.ptr = nullptr;
		this->len = oth.len;    oth.len = 0;
		this->cap = oth.cap;    oth.cap = 0;
	}

	Buffer& Buffer::operator = (Buffer&& oth)
	{
		if(this == &oth)
			return *this;

		if(this->ptr)
			delete[] this->ptr;

		this->ptr = oth.ptr;    oth.ptr = nullptr;
		this->len = oth.len;    oth.len = 0;
		this->cap = oth.cap;    oth.cap = 0;

		return *this;
	}

	Buffer Buffer::clone() const
	{
		auto ret = Buffer(this->cap);
		ret.write(this->ptr, this->len);

		return ret;
	}

	Span Buffer::span() const
	{
		return Span(this->ptr, this->len);
	}

	uint8_t* Buffer::data()             { return this->ptr; }
	const uint8_t* Buffer::data() const { return this->ptr; }

	size_t Buffer::size() const         { return this->len; }
	bool Buffer::full() const           { return this->len == this->cap; }
	size_t Buffer::remaining() const    { return this->cap - this->len; }

	void Buffer::clear()                { memset(this->ptr, 0, this->len); this->len = 0; }

	size_t Buffer::write(const Buffer& buf)
	{
		return this->write(buf.data(), buf.size());
	}

	size_t Buffer::write(Span s)
	{
		return this->write(s.data(), s.size());
	}

	size_t Buffer::write(const uint8_t* data, size_t len)
	{
		auto todo = std::min(len, this->cap - this->len);

		memcpy(this->ptr + this->len, data, todo);
		this->len += todo;

		return todo;
	}

	void Buffer::grow(size_t sz) { this->resize(this->cap + sz); }
	void Buffer::resize(size_t sz)
	{
		if(sz < this->cap)
			return;

		auto tmp = new uint8_t[sz];
		if(this->ptr)
		{
			memcpy(tmp, this->ptr, this->len);
			delete[] this->ptr;
		}

		this->ptr = tmp;
		this->cap = sz;
	}


	Buffer Buffer::empty() { return Buffer(0); }
	Buffer Buffer::fromString(const std::string& s)
	{
		auto ret = Buffer(s.size());
		ret.write((const uint8_t*) s.data(), s.size());
		return ret;
	}
}

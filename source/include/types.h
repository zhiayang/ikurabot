// types.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <vector>
#include <optional>

#include "tsl/robin_map.h"
#include "tsl/robin_set.h"

namespace ikura
{
	template <typename T, typename E = std::string>
	struct Result
	{
	private:
		static constexpr int STATE_NONE = 0;
		static constexpr int STATE_VAL  = 1;
		static constexpr int STATE_ERR  = 2;

	public:
		// Result() : state(0) { }
		~Result()
		{
			if(state == STATE_VAL) this->val.~T();
			if(state == STATE_ERR) this->err.~E();
		}

		Result(const T& x) : state(STATE_VAL), val(x) { }
		Result(T&& x)      : state(STATE_VAL), val(std::move(x)) { }

		Result(const E& e) : state(STATE_ERR), err(e) { }
		Result(E&& e)      : state(STATE_ERR), err(std::move(e)) { }

		Result(const Result& other)
		{
			this->state = other.state;
			if(this->state == STATE_VAL) new(&this->val) T(other.val);
			if(this->state == STATE_ERR) new(&this->err) E(other.err);
		}

		Result(Result&& other)
		{
			this->state = other.state;
			other.state = STATE_NONE;

			if(this->state == STATE_VAL) new(&this->val) T(std::move(other.val));
			if(this->state == STATE_ERR) new(&this->err) E(std::move(other.err));
		}

		Result& operator=(const Result& other)
		{
			if(this != &other)
			{
				if(this->state == STATE_VAL) this->val.~T();
				if(this->state == STATE_ERR) this->err.~E();

				this->state = other.state;
				if(this->state == STATE_VAL) new(&this->val) T(other.val);
				if(this->state == STATE_ERR) new(&this->err) E(other.err);
			}
			return *this;
		}

		Result& operator=(Result&& other)
		{
			if(this != &other)
			{
				if(this->state == STATE_VAL) this->val.~T();
				if(this->state == STATE_ERR) this->err.~E();

				this->state = other.state;
				other.state = STATE_NONE;

				if(this->state == STATE_VAL) new(&this->val) T(std::move(other.val));
				if(this->state == STATE_ERR) new(&this->err) E(std::move(other.err));
			}
			return *this;
		}

		T* operator -> () { assert(this->state == STATE_VAL); return &this->val; }
		const T* operator -> () const { assert(this->state == STATE_VAL); return &this->val; }

		operator bool() const { return this->state == STATE_VAL; }
		bool has_value() const { return this->state == STATE_VAL; }

		const T& unwrap() const { assert(this->state == STATE_VAL); return this->val; }
		const E& error() const { assert(this->state == STATE_ERR); return this->err; }

		T& unwrap() { assert(this->state == STATE_VAL); return this->val; }
		E& error() { assert(this->state == STATE_ERR); return this->err; }

		using result_type = T;
		using error_type = E;

		static Result of(std::optional<T> opt, const E& err)
		{
			if(opt.has_value()) return Result<T, E>(opt.value());
			else                return Result<T, E>(err);
		}

	private:
		// 0 = schrodinger -- no error, no value.
		// 1 = valid
		// 2 = error
		int state = 0;
		union {
			T val;
			E err;
		};
	};

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
			if(len == (size_t) -1 && idx >= this->cnt)
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

		bool operator == (const std::vector<T>& other) const
		{
			if(this->cnt != other.size())
				return false;

			const T* a = this->ptr;
			const T* b = other.data();

			for(size_t i = 0; i < this->cnt; i++)
				if(*a != *b)
					return false;

			return true;
		}

		bool operator == (const span& other) const
		{
			if(this->cnt != other.cnt)
				return false;

			const T* a = this->ptr;
			const T* b = other.ptr;

			for(size_t i = 0; i < this->cnt; i++)
				if(*a != *b)
					return false;

			return true;
		}

		bool operator != (const span& other) const
		{
			return !(this->operator==(other));
		}

		bool operator != (const std::vector<T>& other) const
		{
			return !(this->operator==(other));
		}

		std::vector<T> vec() const              { return std::vector<T>(this->begin(), this->end()); }

		span drop(size_t n) const               { auto copy = *this; copy.remove_prefix(n); return copy; }
		span take(size_t n) const               { auto copy = *this; copy.remove_suffix(this->cnt - n); return copy; }
		span take_last(size_t n) const          { return (this->size() > n ? this->subspan(this->cnt - n) : *this); }

		const T& front() const                  { assert(this->cnt > 0); return this->ptr[0]; }
		const T& back() const                   { assert(this->cnt > 0); return this->ptr[this->cnt - 1]; }

		const T& operator [] (size_t idx) const { assert(this->cnt > idx); return this->ptr[idx]; }

		const_iterator begin() const            { return this->ptr; }
		const_iterator end() const              { return this->ptr + this->cnt; }

		const char* data() const                { return this->ptr; }
		size_t size() const                     { return this->cnt; }
		bool empty() const                      { return this->cnt == 0; }

	private:
		const T* ptr = 0;
		size_t cnt = 0;
	};


	template<typename T>
	void hash_combine(size_t& seed, const T& key)
	{
		std::hash<T> hasher;
		seed ^= hasher(key) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}

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

	using string_set = tsl::robin_set<std::string, __detail::hash_string, __detail::equal_string>;

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
		str_view drop(size_t n) const { return (this->size() > n ? this->substr(n) : ""); }
		str_view take(size_t n) const { return (this->size() > n ? this->substr(0, n) : *this); }
		str_view take_last(size_t n) const { return (this->size() > n ? this->substr(this->size() - n) : *this); }
		str_view substr(size_t pos = 0, size_t cnt = -1) const { return str_view(std::string_view::substr(pos, cnt)); }

		str_view& remove_prefix(size_t n) { std::string_view::remove_prefix(n); return *this; }
		str_view& remove_suffix(size_t n) { std::string_view::remove_suffix(n); return *this; }

		str_view trim_front() const
		{
			auto ret = *this;
			while(ret.size() > 0 && (ret[0] == ' ' || ret[0] == '\t'))
				ret.remove_prefix(1);
			return ret;
		}
		str_view trim_back() const
		{
			auto ret = *this;
			while(ret.size() > 0 && (ret.back() == ' ' || ret.back() == '\t'))
				ret.remove_suffix(1);
			return ret;
		}

		str_view trim() const
		{
			return this->trim_front().trim_back();
		}

		std::string str() const { return std::string(*this); }
	};
}

namespace std
{
	template<typename T>
	struct hash<std::vector<T>>
	{
		size_t operator () (const std::vector<T>& vec) const
		{
			size_t seed = 0;
			for(const auto& t : vec)
				ikura::hash_combine(seed, t);

			return seed;
		}
	};

	template<typename T>
	struct hash<ikura::span<T>>
	{
		size_t operator () (const ikura::span<T>& vec) const
		{
			size_t seed = 0;
			for(const auto& t : vec)
				ikura::hash_combine(seed, t);

			return seed;
		}
	};
}

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
	struct Span;
	struct Buffer;
	struct Serialisable
	{
		virtual ~Serialisable() { }
		virtual void serialise(Buffer& out) const = 0;
	};

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

		// we use this in the parser a lot, so why not lmao
		template <typename U, typename = std::enable_if_t<
			std::is_pointer_v<T> && std::is_pointer_v<U>
			&& std::is_base_of_v<std::remove_pointer_t<U>, std::remove_pointer_t<T>>
		>>
		operator Result<U, E> () const
		{
			if(state == STATE_VAL)  return Result<U, E>(this->val);
			if(state == STATE_ERR)  return Result<U, E>(this->err);

			abort();
		}

	#if 0
		// dumb stuff
		template <size_t I> decltype(auto) get() const
		{
			if constexpr (I == 0)       return (this->val); // parens here force a reference
			else if constexpr (I == 1)  return (this->err);
			else if constexpr (I == 2)  return (this->state == STATE_VAL);
		}

		template <size_t I> decltype(auto) get()
		{
			if constexpr (I == 0)       return (this->val); // parens here force a reference
			else if constexpr (I == 1)  return (this->err);
			else if constexpr (I == 2)  return (this->state == STATE_VAL);
		}
	#endif

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

		size_t find_last(const T& x) const
		{
			for(size_t i = this->cnt; i-- > 0; )
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

		span drop(size_t n) const               { return (this->size() > n ? this->subspan(n) : span()); }
		span take(size_t n) const               { return (this->size() > n ? this->subspan(0, n) : *this); }
		span take_last(size_t n) const          { return (this->size() > n ? this->subspan(this->cnt - n) : *this); }
		span drop_last(size_t n) const          { return (this->size() > n ? this->subspan(0, this->cnt - n) : *this); }

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
		str_view drop_last(size_t n) const { return (this->size() > n ? this->substr(0, this->size() - n) : *this); }
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

	struct relative_str
	{
		relative_str() : _start(0), _size(0) { }
		relative_str(size_t start, size_t size) : _start(start), _size(size) { }

		relative_str(relative_str&&) = default;
		relative_str(const relative_str&) = default;

		relative_str& operator = (relative_str&&) = default;
		relative_str& operator = (const relative_str&) = default;

		size_t start() const    { return this->_start; }
		size_t size()  const    { return this->_size; }
		size_t end_excl() const { return this->_start + this->_size; }
		size_t end_incl() const { return this->_start + this->_size - 1; }

		str_view get(str_view str) const { return str.substr(_start, _size); };
		str_view get(const char* ptr) const { return str_view(ptr + _start, _size); };
		str_view get(const std::string& str) const { return str_view(str).substr(_start, _size); };

	private:
		size_t _start;
		size_t _size;
	};

	struct move_only
	{
		move_only() = default;
		move_only(move_only&&) = default;
		move_only& operator= (move_only&&) = default;
		move_only(move_only const&) = delete;
		move_only& operator= (move_only const&) = delete;
		~move_only() = default;
	};

	struct move_only_fot : move_only
	{
		void operator() () const { std::cout << "meow!\n"; }
	};

	template <typename F>
	class unique_function : private std::function<F>
	{
		template <typename Fn>
		struct foozle
		{
			Fn fn;

			foozle(Fn p) : fn(std::move(p)) {}

			foozle(foozle&&) = default;
			foozle& operator = (foozle&&) = default;

			foozle(const foozle& x) : fn(const_cast<Fn&&>(x.fn)) { abort(); }
			foozle& operator = (const foozle&) { abort(); }

			template <typename... Args>
			auto operator () (Args&&... args) -> decltype(fn(std::forward<Args>(args)...))
			{
				return fn(std::forward<Args>(args)...);
			}
		};

	public:
		template <typename Fn>
		unique_function(Fn fun) : std::function<F>(foozle<Fn>(std::move(fun))) { }

		using std::function<F>::operator();

		unique_function() = default;
		~unique_function() = default;

		unique_function(unique_function&&) = default;
		unique_function& operator = (unique_function&&) = default;

		unique_function(const unique_function&) = delete;
		unique_function& operator = (const unique_function&) = delete;
	};




	namespace discord
	{
		struct Snowflake : Serialisable
		{
			Snowflake() : value(0) { }
			explicit Snowflake(uint64_t x) : value(x) { }

			explicit Snowflake(const std::string& s);
			explicit Snowflake(ikura::str_view sv);

			uint64_t value;

			bool empty() const { return this->value == 0; }
			std::string str() const { return std::to_string(value); }

			bool operator == (Snowflake s) const { return this->value == s.value; }
			bool operator != (Snowflake s) const { return this->value != s.value; }

			virtual void serialise(Buffer& buf) const override;
			static std::optional<Snowflake> deserialise(Span& buf);
		};
	}



	namespace serialise
	{
		// "primitive" types
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

		// interp part 1
		constexpr uint8_t TAG_AST_LIT_CHAR          = 0x30;
		constexpr uint8_t TAG_AST_LIT_STRING        = 0x31;
		constexpr uint8_t TAG_AST_LIT_LIST          = 0x32;
		constexpr uint8_t TAG_AST_LIT_INTEGER       = 0x33;
		constexpr uint8_t TAG_AST_LIT_DOUBLE        = 0x34;
		constexpr uint8_t TAG_AST_LIT_BOOLEAN       = 0x35;
		constexpr uint8_t TAG_AST_VAR_REF           = 0x36;
		constexpr uint8_t TAG_AST_OP_SUBSCRIPT      = 0x37;
		constexpr uint8_t TAG_AST_OP_SLICE          = 0x38;
		constexpr uint8_t TAG_AST_OP_SPLAT          = 0x39;
		constexpr uint8_t TAG_AST_OP_UNARY          = 0x3A;
		constexpr uint8_t TAG_AST_OP_BINARY         = 0x3B;
		constexpr uint8_t TAG_AST_OP_TERNARY        = 0x3C;
		constexpr uint8_t TAG_AST_OP_COMPARISON     = 0x3D;
		constexpr uint8_t TAG_AST_OP_ASSIGN         = 0x3E;
		constexpr uint8_t TAG_AST_FUNCTION_CALL     = 0x3F;
		constexpr uint8_t TAG_AST_BLOCK             = 0x40;

		// backend (twitch, discord, markov) stuff
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
		constexpr uint8_t TAG_CACHED_EMOTE          = 0x5A;
		constexpr uint8_t TAG_CACHED_EMOTE_DB       = 0x5B;
		constexpr uint8_t TAG_DISCORD_LOG           = 0x5C;
		constexpr uint8_t TAG_DISCORD_LOG_MSG       = 0x5D;

		// interp part 2
		constexpr uint8_t TAG_AST_FUNCTION_DEFN     = 0x68;


		// if the byte has 0x80 set, then the lower 7 bits represents a truncated 64-bit number. it's a further
		// extension of the SMALL_U64 thing, but literally only uses 1 byte for sizes between 0 - 127
		constexpr uint8_t TAG_TINY_U64              = 0x80;
	}
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


	template <>
	struct hash<ikura::discord::Snowflake>
	{
		size_t operator () (ikura::discord::Snowflake s) const
		{
			return std::hash<uint64_t>()(s.value);
		}
	};

#if 0
	// this is to allow
	// auto [ val, err, ok ] = result; if(!ok) { ... }
	template <typename T, typename E>
	struct tuple_size<ikura::Result<T, E>> : std::integral_constant<size_t, 3> { };

	template <size_t N, typename T, typename E>
	struct tuple_element<N, ikura::Result<T, E>>
	{
		using type = decltype(std::declval<ikura::Result<T, E>>().template get<N>());
	};
#endif
}

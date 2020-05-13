// parser.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "utils.h"

#include <tuple>
#include <utility>
#include <functional>

namespace ikura::cmd::ast
{
	using TT = lexer::TokenType;

	template <typename T, typename E = std::string>
	struct Result
	{
		Result() : valid(false) { }

		Result(const T& x) : valid(true), val(x) { }
		Result(T&& x) : valid(true), val(std::move(x)) { }

		Result(const E& e) : valid(false), err(e) { }
		Result(E&& e) : valid(false), err(std::move(e)) { }

		Result(Result&&) = default;
		Result(const Result&) = default;

		Result& operator=(const Result&) = default;
		Result& operator=(Result&&) = default;

		operator bool() const { return this->valid; }
		bool has_value() const { return this->valid; }

		T value() const { assert(this->valid); return this->val; }
		E error() const { assert(!this->valid); return this->err; }

		T unwrap() const { return value(); }

		using result_type = T;
		using error_type = E;

	private:
		bool valid = false;
		T val;
		E err;
	};

	namespace {
		template <typename> struct is_result : std::false_type { };
		template <typename T, typename E> struct is_result<Result<T, E>> : std::true_type { };

		template <typename T> struct remove_array { using type = T; };
		template <typename T, size_t N> struct remove_array<const T(&)[N]> { using type = const T*; };
		template <typename T, size_t N> struct remove_array<T(&)[N]> { using type = T*; };

		// uwu
		template <typename... Ts>
		using concatenator = decltype(std::tuple_cat(std::declval<Ts>()...));

		[[maybe_unused]] std::tuple<> __unwrap() { return { }; }

		template <typename A, typename = std::enable_if_t<is_result<A>::value>>
		std::tuple<std::optional<typename A::result_type>> __unwrap(const A& a)
		{
			return std::make_tuple(a
				? std::optional<typename A::result_type>(a.unwrap())
				: std::nullopt
			);
		}
		template <typename A, typename = std::enable_if_t<std::is_array_v<A>>>
		std::tuple<std::optional<const std::remove_extent_t<A>*>> __unwrap(const A& a) { return std::make_tuple(std::optional(a)); }

		template <typename A, typename = std::enable_if_t<!std::is_array_v<A> && !is_result<A>::value>>
		std::tuple<std::optional<A>> __unwrap(const A& a) { return std::make_tuple(std::optional(a)); }

		template <typename A, typename... As>
		auto __unwrap(const A& a, As&&... as)
		{
			auto x = __unwrap(std::forward<const A&>(a));
			auto xs = std::tuple_cat(__unwrap(as)...);

			return std::tuple_cat(x, xs);
		}

		template <typename A, size_t... Is, typename... As>
		std::tuple<As...> __drop_one_impl(std::tuple<A, As...> tup, std::index_sequence<Is...> seq)
		{
			return std::make_tuple(std::get<(1+Is)>(tup)...);
		}

		template <typename A, typename... As>
		std::tuple<As...> __drop_one(std::tuple<A, As...> tup)
		{
			return __drop_one_impl(tup, std::make_index_sequence<sizeof...(As)>());
		}

		[[maybe_unused]] std::optional<std::tuple<>> __transpose()
		{
			return std::make_tuple();
		}

		template <typename A>
		[[maybe_unused]] std::optional<std::tuple<A>> __transpose(std::tuple<std::optional<A>> tup)
		{
			auto elm = std::get<0>(tup);
			if(!elm.has_value())
				return std::nullopt;

			return elm.value();
		}

		template <typename A, typename... As, typename = std::enable_if_t<sizeof...(As) != 0>>
		[[maybe_unused]] std::optional<std::tuple<A, As...>> __transpose(std::tuple<std::optional<A>, std::optional<As>...> tup)
		{
			auto elm = std::get<0>(tup);
			if(!elm.has_value())
				return std::nullopt;

			auto next = __transpose(__drop_one(tup));
			if(!next.has_value())
				return std::nullopt;

			return std::tuple_cat(std::make_tuple(elm.value()), next.value());
		}



		[[maybe_unused]] std::tuple<> __get_error() { return { }; }

		template <typename A, typename = std::enable_if_t<is_result<A>::value>>
		[[maybe_unused]] std::tuple<typename A::error_type> __get_error(const A& a)
		{
			if(a) return typename A::error_type();
			return a.error();
		}

		template <typename A, typename = std::enable_if_t<!is_result<A>::value>>
		[[maybe_unused]] std::tuple<> __get_error(const A& a) { return std::tuple<>(); }


		template <typename A, typename... As>
		auto __get_error(const A& a, As&&... as)
		{
			auto x = __get_error(std::forward<const A&>(a));
			auto xs = std::tuple_cat(__get_error(as)...);

			return std::tuple_cat(x, xs);
		}

		template <typename... Err>
		std::vector<std::string> __concat_errors(Err&&... errs)
		{
			std::vector<std::string> ret;
			([&ret](auto e) { if(!e.empty()) ret.push_back(e); }(errs), ...);
			return ret;
		}
	}


	template <typename Ast, typename... Args>
	static Result<Expr*> makeAST(Args&&... args)
	{
		auto opts = __unwrap(std::forward<Args&&>(args)...);
		auto opt = __transpose(opts);
		if(opt.has_value())
		{
			auto foozle = [](auto... xs) -> Expr* {
				return new Ast(xs...);
			};

			return Result<Expr*>(std::apply(foozle, opt.value()));
		}

		auto errs = std::apply([](auto&&... xs) { return __concat_errors(xs...); }, __get_error(std::forward<Args&&>(args)...));
		return util::join(errs, "; ");
	}


	static bool is_assignment(TT op)
	{
		return ::util::match(op, TT::Equal, TT::PlusEquals, TT::MinusEquals, TT::TimesEquals,
			TT::DivideEquals, TT::RemainderEquals, TT::ShiftLeftEquals, TT::ShiftRightEquals,
			TT::BitwiseAndEquals, TT::BitwiseOrEquals, TT::BitwiseXorEquals);
	}

	static bool is_right_associative(TT op)
	{
		return op == TT::Caret;
	}

	static int get_binary_precedence(TT op)
	{
		switch(op)
		{
			case TT::Period:            return 8000;

			case TT::Caret:             return 2600;

			case TT::Asterisk:          return 2400;
			case TT::Slash:             return 2200;
			case TT::Percent:           return 2000;

			case TT::Plus:              return 1800;
			case TT::Minus:             return 1800;

			case TT::ShiftLeft:         return 1600;
			case TT::ShiftRight:        return 1600;

			case TT::Ampersand:         return 1400;

			case TT::Pipe:              return 1000;

			case TT::EqualTo:           return 800;
			case TT::NotEqual:          return 800;
			case TT::LAngle:            return 800;
			case TT::RAngle:            return 800;
			case TT::LessThanEqual:     return 800;
			case TT::GreaterThanEqual:  return 800;

			case TT::LogicalAnd:        return 600;

			case TT::LogicalOr:         return 400;

			case TT::Equal:             return 200;
			case TT::PlusEquals:        return 200;
			case TT::MinusEquals:       return 200;
			case TT::TimesEquals:       return 200;
			case TT::DivideEquals:      return 200;
			case TT::RemainderEquals:   return 200;
			case TT::ShiftLeftEquals:   return 200;
			case TT::ShiftRightEquals:  return 200;
			case TT::BitwiseAndEquals:  return 200;
			case TT::BitwiseOrEquals:   return 200;
			case TT::BitwiseXorEquals:  return 200;

			case TT::Pipeline:          return 1;

			default:
				return -1;
		}
	}

	struct State
	{
		State(ikura::span<lexer::Token>& ts) : tokens(ts) { }

		bool match(TT t)
		{
			return tokens.size() > 0 && tokens.front() == t;
		}

		const lexer::Token& peek()
		{
			return tokens.empty() ? eof : tokens.front();
		}

		void pop()
		{
			if(!tokens.empty())
				tokens.remove_prefix(1);
		}

		bool empty()
		{
			return tokens.empty();
		}

		lexer::Token eof = lexer::Token(TT::Invalid, "");
		ikura::span<lexer::Token> tokens;
	};


	static Result<Expr*> parseParenthesised(State& st);
	static Result<Expr*> parseIdentifier(State& st);
	static Result<Expr*> parsePrimary(State& st);
	static Result<Expr*> parseNumber(State& st);
	static Result<Expr*> parseString(State& st);
	static Result<Expr*> parseUnary(State& st);
	static Result<Expr*> parseStmt(State& st);
	static Result<Expr*> parseExpr(State& st);
	static Result<Expr*> parseBool(State& st);

	Expr* parse(ikura::str_view src)
	{
		auto tokens = lexer::lexString(src);
		ikura::span span = tokens;

		auto st = State(span);

		auto result = parseStmt(st);
		if(result)
			return result.unwrap();

		lg::error("cmd", "parse error: %s", result.error());
		return nullptr;
	}




	static Result<Expr*> parseParenthesised(State& st)
	{
		assert(st.peek() == TT::LParen);
		st.pop();

		auto inside = parseExpr(st);

		if(!st.match(TT::RParen))
			return zpr::sprint("expected ')'");

		return inside;
	}

	static Result<Expr*> parsePrimary(State& st)
	{
		switch(st.peek())
		{
			case TT::StringLit:
				return parseString(st);

			case TT::NumberLit:
				return parseNumber(st);

			case TT::BooleanLit:
				return parseBool(st);

			case TT::LParen:
				return parseParenthesised(st);

			case TT::Dollar:
			case TT::Identifier:
				return parseIdentifier(st);

			default:
				return zpr::sprint("unexpected token '%s'", st.peek().str());
		}
	}

	static Result<Expr*> parseUnary(State& st)
	{
		if(st.match(TT::Exclamation))   return makeAST<UnaryOp>(TT::Exclamation, "!", parseUnary(st));
		else if(st.match(TT::Minus))    return makeAST<UnaryOp>(TT::Minus, "-", parseUnary(st));
		else if(st.match(TT::Plus))     return makeAST<UnaryOp>(TT::Plus, "+", parseUnary(st));
		else if(st.match(TT::Tilde))    return makeAST<UnaryOp>(TT::Tilde, "~", parseUnary(st));
		else                            return parsePrimary(st);
	}

	static Result<Expr*> parseRhs(State& st, Result<Expr*> lhs, int prio)
	{
		if(!lhs)
			return lhs;

		else if(st.empty() || prio == -1)
			return lhs;

		while(true)
		{
			auto prec = get_binary_precedence(st.peek());
			if(prec < prio && !is_right_associative(st.peek()))
				return lhs;

			auto oper = st.peek();
			st.pop();

			auto rhs = parseUnary(st);
			if(!rhs) return rhs;

			auto next = get_binary_precedence(st.peek());
			if(next > prec || is_right_associative(st.peek()))
				rhs = parseRhs(st, rhs, prec + 1);

			if(is_assignment(oper.type))
				lhs = makeAST<AssignOp>(oper.type, oper.str().str(), lhs, rhs);

			else
				lhs = makeAST<BinaryOp>(oper.type, oper.str().str(), lhs, rhs);
		}
	}

	static Result<Expr*> parseExpr(State& st)
	{
		auto lhs = parseUnary(st);
		if(!lhs) return lhs;

		return parseRhs(st, lhs, 0);
	}


	static Result<Expr*> parseNumber(State& st)
	{
		assert(st.peek() == TT::NumberLit);
		auto num = st.peek().str();
		st.pop();

		auto npos = std::string::npos;
		bool is_floating = (num.find('.') != npos)
						|| (num.find('X') == npos && num.find('x') == npos
							&& (num.find('e') != npos || num.find('E') != npos)
						);

		int base = 10;
		if(num.find("0b") == 0 || num.find("0B") == 0) num.remove_prefix(2), base = 2;
		if(num.find("0x") == 0 || num.find("0X") == 0) num.remove_prefix(2), base = 16;

		if(is_floating) return makeAST<LitDouble>(std::stod(num.str()));
		else            return makeAST<LitInteger>(std::stoll(num.str(), nullptr, base));
	}

	static Result<Expr*> parseString(State& st)
	{
		assert(st.peek() == TT::StringLit);
		auto str = st.peek().str();
		st.pop();


		// TODO: handle all the escapes! (hex, unicode)
		std::string ret; ret.reserve(str.size());

		for(size_t i = 0; i < str.length(); i++)
		{
			if(str[i] == '\\')
			{
				i++;
				switch(str[i])
				{
					case 'n':   ret += "\n"; break;
					case 'b':   ret += "\b"; break;
					case 'r':   ret += "\r"; break;
					case 't':   ret += "\t"; break;
					case '"':   ret += "\""; break;
					case '\\':  ret += "\\"; break;

					default:
						ret += std::string("\\") + str[i];
						break;
				}
			}
			else
			{
				ret += str[i];
			}
		}

		return makeAST<LitString>(ret);
	}

	static Result<Expr*> parseBool(State& st)
	{
		assert(st.peek() == TT::NumberLit);
		auto x = st.peek().str();
		st.pop();

		return makeAST<LitBoolean>(x == "true");
	}

	static Result<Expr*> parseIdentifier(State& st)
	{
		std::string name = st.peek().str().str();
		st.pop();

		if(name == "$")
		{
			// the next thing must be either a number or an identifier.
			if(st.peek() == TT::Identifier)
				name += st.peek().str();

			else if(st.peek() == TT::NumberLit)
			{
				auto tmp = st.peek().str();
				if(tmp.find_first_not_of("0123456789") != std::string::npos)
					return zpr::sprint("invalid numeric literal after '$'", tmp);

				name += tmp.str();
			}
			else
			{
				return zpr::sprint("invalid token '%s' after '$'", st.peek().str());
			}

			st.pop();
		}

		return makeAST<VarRef>(name);
	}



	static Result<Expr*> parseStmt(State& st)
	{
		return parseExpr(st);
	}
}

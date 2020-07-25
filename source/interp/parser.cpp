// parser.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "zfu.h"

#include "utf8proc/utf8proc.h"

namespace ikura::interp::ast
{
	using TT = lexer::TokenType;

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
	static Result<Ast*> makeAST(Args&&... args)
	{
		auto opts = __unwrap(std::forward<Args&&>(args)...);
		auto opt = __transpose(opts);
		if(opt.has_value())
		{
			auto foozle = [](auto... xs) -> Ast* {
				return new Ast(xs...);
			};

			return Result<Ast*>(std::apply(foozle, opt.value()));
		}

		auto errs = std::apply([](auto&&... xs) { return __concat_errors(xs...); }, __get_error(std::forward<Args&&>(args)...));
		return util::join(errs, "; ");
	}


	static bool is_comparison_op(TT op)
	{
		return zfu::match(op, TT::EqualTo, TT::NotEqual, TT::LAngle, TT::LessThanEqual,
			TT::RAngle, TT::GreaterThanEqual);
	}

	static bool is_postfix_op(TT op)
	{
		return zfu::match(op, TT::LSquare, TT::LParen, TT::Ellipsis);
	}

	static bool is_assignment_op(TT op)
	{
		return zfu::match(op, TT::Equal, TT::PlusEquals, TT::MinusEquals, TT::TimesEquals,
			TT::DivideEquals, TT::RemainderEquals, TT::ShiftLeftEquals, TT::ShiftRightEquals,
			TT::BitwiseAndEquals, TT::BitwiseOrEquals, TT::ExponentEquals);
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

			case TT::LParen:            return 3000;

			case TT::LSquare:           return 2800;

			case TT::Caret:             return 2600;

			case TT::Asterisk:          return 2400;
			case TT::Slash:             return 2200;
			case TT::Percent:           return 2000;

			case TT::Plus:              return 1800;
			case TT::Minus:             return 1800;
			case TT::DoublePlus:        return 1800;

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
			case TT::ExponentEquals:    return 200;

			case TT::Question:          return 10;

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
			if(tokens.size() == 0 || tokens.front() != t)
				return false;

			this->pop();
			return true;
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

		void pushGenerics(ikura::span<std::string> g)
		{
			this->knownGenerics.push_back(g);
		}

		void popGenerics()
		{
			assert(this->knownGenerics.size() > 0);
			this->knownGenerics.pop_back();
		}

		bool isKnownGeneric(ikura::str_view name)
		{
			for(size_t i = this->knownGenerics.size(); i-- > 0;)
				if(zfu::contains(this->knownGenerics[i], name))
					return true;

			return false;
		}

		lexer::Token eof = lexer::Token(TT::EndOfFile, "");
		ikura::span<lexer::Token> tokens;
		std::vector<ikura::span<std::string>> knownGenerics;
	};
	Result<Type::Ptr> parseType(State& st, int grp = 0);
	Result<FunctionDefn*> parseFuncDefn(ikura::str_view src);
	static Result<FunctionDefn*> parseFuncDefn(State& st, bool requireKeyword);


	static Result<Expr*>  parsePostfix(State& st, Expr* lhs, TT op);
	static Result<Expr*>  parseParenthesised(State& st);
	static Result<Expr*>  parseIdentifier(State& st);
	static Result<Expr*>  parsePrimary(State& st);
	static Result<Expr*>  parseNumber(State& st);
	static Result<Expr*>  parseString(State& st);
	static Result<Expr*>  parseUnary(State& st);
	static Result<Expr*>  parseList(State& st);
	static Result<Expr*>  parseChar(State& st);
	static Result<Expr*>  parseBool(State& st);
	static Result<Expr*>  parseExpr(State& st);

	static Result<Stmt*> parseStmt(State& st);
	static Result<Block*> parseBlock(State& st);

	Result<Expr*> parseExpr(ikura::str_view src)
	{
		auto ts = lexer::lexString(src);
		if(!ts) return ts.error();

		ikura::span span = ts.unwrap();

		auto st = State(span);
		return parseExpr(st);
	}

	Result<Stmt*> parse(ikura::str_view src)
	{
		auto ts = lexer::lexString(src);
		if(!ts) return ts.error();

		ikura::span span = ts.unwrap();

		auto st = State(span);
		return parseStmt(st);
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

			case TT::CharLit:
				return parseChar(st);

			case TT::NumberLit:
				return parseNumber(st);

			case TT::BooleanLit:
				return parseBool(st);

			case TT::LParen:
				return parseParenthesised(st);

			case TT::LSquare:
				return parseList(st);

			case TT::Dollar:
			case TT::Identifier:
				return parseIdentifier(st);

			case TT::EndOfFile:
				return zpr::sprint("unexpected end of input");

			default:
				return zpr::sprint("unexpected token '%s' (%d)", st.peek().str(), st.peek().type);
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
			auto oper = st.peek();
			auto prec = get_binary_precedence(oper);
			if(prec < prio && !is_right_associative(oper) && !is_postfix_op(oper))
				return lhs;

			st.pop();

			if(is_postfix_op(oper.type) && lhs)
			{
				lhs = parsePostfix(st, lhs.unwrap(), oper.type);
				continue;
			}


			auto rhs = parseUnary(st);
			if(!rhs) return rhs;

			auto next = get_binary_precedence(st.peek());
			if(next > prec || is_right_associative(st.peek()))
				rhs = parseRhs(st, rhs, prec + 1);

			if(!rhs) return rhs;

			if(is_assignment_op(oper.type))
			{
				lhs = makeAST<AssignOp>(oper.type, oper.str().str(), lhs, rhs);
			}
			else if(oper.type == TT::Question)
			{
				if(!st.match(TT::Colon))
					return zpr::sprint("expected ':' after '?'");

				lhs = makeAST<TernaryOp>(oper.type, oper.str().str(), lhs, rhs, parseExpr(st));
			}
			else if(is_comparison_op(oper.type))
			{
				if(auto cmp = dynamic_cast<ComparisonOp*>(lhs.unwrap()); cmp)
				{
					cmp->addExpr(rhs.unwrap());
					cmp->addOp(oper.type, oper.str().str());

					lhs = cmp;
				}
				else
				{
					auto tmp = new ComparisonOp();
					tmp->addExpr(lhs.unwrap());
					tmp->addExpr(rhs.unwrap());
					tmp->addOp(oper.type, oper.str().str());

					lhs = Result<Expr*>(tmp);
				}
			}
			else if(oper.type == TT::Period)
			{
				lhs = makeAST<DotOp>(lhs, rhs);
			}
			else
			{
				lhs = makeAST<BinaryOp>(oper.type, oper.str().str(), lhs, rhs);
			}
		}
	}

	static Result<Expr*> parseExpr(State& st)
	{
		auto lhs = parseUnary(st);
		if(!lhs) return lhs;

		return parseRhs(st, lhs, 0);
	}




	static Result<Expr*> parseList(State& st)
	{
		assert(st.peek() == TT::LSquare);
		st.pop();

		std::vector<Expr*> elms;
		while(!st.empty() && st.peek() != TT::RSquare)
		{
			auto e = parseExpr(st);
			if(!e) return e;

			elms.push_back(e.unwrap());

			if(st.peek() == TT::Comma)
				st.pop();

			else if(st.peek() == TT::RSquare)
				break;

			else
				return zpr::sprint("expected ',' or ']' in list literal, found '%s'", st.peek().str());
		}

		if(st.peek() != TT::RSquare)
			return zpr::sprint("expected ']'");

		st.pop();
		return makeAST<LitList>(elms);
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

		// a little hacky, but whatever.
		bool imag = false;
		if(st.peek() == TT::Identifier && st.peek().str() == "i")
			imag = true, st.pop();

		if(is_floating) return makeAST<LitDouble>(std::stod(num.str()), imag);
		else            return makeAST<LitInteger>(std::stoll(num.str(), nullptr, base), imag);
	}

	static Result<Expr*> parseChar(State& st)
	{
		assert(st.peek() == TT::CharLit);
		auto str = st.peek().str();
		st.pop();

		int32_t codepoint = 0;
		auto bytes = utf8proc_iterate((const uint8_t*) str.data(), str.size(), &codepoint);
		assert((size_t) bytes == str.size());
		assert(codepoint != -1);

		return makeAST<LitChar>((uint32_t) codepoint);
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
		assert(st.peek() == TT::BooleanLit);
		auto x = st.peek().str();
		st.pop();

		return makeAST<LitBoolean>(x == "true");
	}

	static Result<Expr*> parsePostfix(State& st, Expr* lhs, TT op)
	{
		if(op == TT::Ellipsis)
		{
			return makeAST<SplatOp>(lhs);
		}
		else if(op == TT::LParen)
		{
			std::vector<Expr*> args;

			while(st.peek() != TT::RParen)
			{
				auto a = parseExpr(st);
				if(!a) return a;

				args.push_back(a.unwrap());
				if(st.match(TT::Comma))
					continue;

				else if(st.peek() == TT::RParen)
					break;

				else
					return zpr::sprint("expected ',' or ')'");
			}

			if(!st.match(TT::RParen))
				return zpr::sprint("expected ')'");

			return makeAST<FunctionCall>(lhs, args);
		}
		else if(op == TT::LSquare)
		{
			// 5 cases: [N], [:], [N:], [:M], [N:M]
			if(st.match(TT::Colon))
			{
				if(st.match(TT::RSquare))
				{
					st.pop();
					return makeAST<SliceOp>(lhs, nullptr, nullptr);
				}
				else
				{
					auto end = parseExpr(st);
					if(!st.match(TT::RSquare))
						return zpr::sprint("expected ']'");

					return makeAST<SliceOp>(lhs, nullptr, end);
				}
			}
			else
			{
				auto idx = parseExpr(st);
				if(st.match(TT::Colon))
				{
					if(st.match(TT::RSquare))
					{
						return makeAST<SliceOp>(lhs, idx, nullptr);
					}
					else
					{
						auto end = parseExpr(st);
						if(!st.match(TT::RSquare))
							return zpr::sprint("expected ']'");

						return makeAST<SliceOp>(lhs, idx, end);
					}
				}
				else if(st.match(TT::RSquare))
				{
					return makeAST<SubscriptOp>(lhs, idx);
				}
				else
				{
					return zpr::sprint("expected either ']' or ':', found '%s'", st.peek().str());
				}
			}
		}
		else
		{
			return zpr::sprint("invalid postfix operator");
		}
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

		return makeAST<VarRef>(unicode::normalise(name));
	}





	static Result<Stmt*> parseStmt(State& st)
	{
		if(st.peek() == TT::Function)
			return parseFuncDefn(st, /* requireKeyword: */ true);

		if(st.peek() == TT::LBrace || st.peek() == TT::FatRightArrow)
			return parseBlock(st);

		return parseExpr(st);
	}


	static Result<Block*> parseBlock(State& st)
	{
		if(st.peek() != TT::LBrace && st.peek() != TT::FatRightArrow)
			return zpr::sprint("expected either '{' or '=>'");

		bool single = (st.peek() == TT::FatRightArrow);
		st.pop();

		std::vector<Stmt*> stmts;
		while(!st.empty() && st.peek() != TT::RBrace)
		{
			auto s = parseStmt(st);
			if(!s) return s.error();

			stmts.push_back(s.unwrap());

			if(single)
				goto out;

			if(st.peek() != TT::Semicolon)
				return zpr::sprint("expected ';'");

			st.pop();
		}

		if(st.empty() || st.peek() != TT::RBrace)
			return zpr::sprint("expected '}'");

	out:
		return makeAST<Block>(stmts);
	}


	static Result<FunctionDefn*> parseFuncDefn(State& st, bool requireKeyword)
	{
		if(requireKeyword)
		{
			assert(st.peek() == TT::Function);
			st.pop();
		}

		std::string name;
		if(st.peek() != TT::Identifier)
			return zpr::sprint("expected identifier after 'fn'");

		name = st.peek().str().str();
		st.pop();

		std::vector<std::string> generics;
		if(st.peek() == TT::LAngle)
		{
			st.pop();

			while(!st.empty() && st.peek() != TT::RAngle)
			{
				if(st.peek() != TT::Identifier)
					return zpr::sprint("expected identifier in <>, found '%d'", st.peek().type);

				generics.emplace_back(st.peek().str());
				st.pop();

				if(st.peek() == TT::Comma)
					st.pop();

				else if(st.peek() == TT::RAngle)
					break;

				else
					zpr::sprint("unexpected token '%s' in <>", st.peek().str());
			}

			if(st.empty() || st.peek() != TT::RAngle)
				return zpr::sprint("expected '>'");

			st.pop();
		}

		st.pushGenerics(generics);

		auto type = parseType(st);
		if(!type) return type.error();

		if(!type.unwrap()->is_function())
			return zpr::sprint("'%s' is not a function type", type.unwrap()->str());

		auto body = parseBlock(st);
		if(!body) return body.error();

		st.popGenerics();

		return makeAST<FunctionDefn>(name, type.unwrap(), generics, body.unwrap());
	}


	Result<FunctionDefn*> parseFuncDefn(ikura::str_view src)
	{
		auto ts = lexer::lexString(src);
		if(!ts) return ts.error();

		ikura::span span = ts.unwrap();

		auto st = State(span);
		return parseFuncDefn(st, /* requireKeyword: */ false);
	}












	Result<interp::Type::Ptr> parseType(State& st, int group)
	{
		using interp::Type;
		if(st.empty())
			return zpr::sprint("unexpected end of input");

		if(st.peek() == TT::Identifier)
		{
			auto s = st.peek().str();
			st.pop();

			if(s == "int")         return Type::get_integer();
			else if(s == "double") return Type::get_double();
			else if(s == "bool")   return Type::get_bool();
			else if(s == "char")   return Type::get_char();
			else if(s == "str")    return Type::get_string();
			else if(s == "void")   return Type::get_void();
			else if(st.isKnownGeneric(s))
				return Type::get_generic(s.str(), group);

			return zpr::sprint("unknown type '%s'", s);
		}
		else if(st.peek() == TT::LSquare)
		{
			st.pop();

			auto et = parseType(st);
			if(!et) return et;

			if(st.peek() == TT::Colon)
			{
				st.pop();
				auto vt = parseType(st);
				if(!vt) return vt;

				if(st.peek() != TT::RSquare)
					return zpr::sprint("expected ']'");

				st.pop();
				return Type::get_map(et.unwrap(), vt.unwrap());
			}
			else if(st.peek() == TT::RSquare)
			{
				st.pop();
				return Type::get_list(et.unwrap());
			}
			else
			{
				return zpr::sprint("expected ']'");
			}
		}
		else if(st.peek() == TT::LParen)
		{
			st.pop();
			std::vector<Type::Ptr> tup;

			while(!st.empty() && st.peek() != TT::RParen)
			{
				auto t = parseType(st);
				if(!t) return t;

				tup.push_back(t.unwrap());

				if(st.peek() == TT::Comma)
					st.pop();

				else if(st.peek() == TT::RParen)
					break;

				else
					return zpr::sprint("expected either ',' or ')', found '%s'", st.peek().str());
			}

			if(st.empty() || st.peek() != TT::RParen)
				return zpr::sprint("expected ')'");

			st.pop();

			// no tuples for now
			if(st.peek() != TT::RightArrow)
				return zpr::sprint("expected '->'");

			st.pop();
			auto t = parseType(st);
			if(!t) return t;

			return Type::get_function(t.unwrap(), tup);
		}
		else
		{
			return zpr::sprint("unexpected token '%s' in type", st.peek().str());
		}
	}

	std::optional<interp::Type::Ptr> parseType(ikura::str_view src, int group)
	{
		auto ts = lexer::lexString(src);
		if(!ts) return { };

		ikura::span span = ts.unwrap();

		auto st = State(span);
		auto ty = parseType(st, group);

		if(ty) return ty.unwrap();
		else   return { };
	}
}

// ast.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include "defs.h"

namespace ikura::cmd
{
	struct InterpState;
	struct CmdContext;

	namespace lexer
	{
		enum class TokenType
		{
			Invalid,

			Function,
			If,
			Let,
			Else,
			While,
			Return,
			For,

			Semicolon,
			Dollar,
			Colon,
			Pipe,
			Ampersand,
			Period,
			Asterisk,
			Caret,
			Exclamation,
			Plus,
			Comma,
			Minus,
			Slash,
			LParen,
			RParen,
			LSquare,
			RSquare,
			LBrace,
			RBrace,
			LAngle,
			RAngle,
			Equal,
			Percent,
			LogicalOr,
			LogicalAnd,
			EqualTo,
			NotEqual,
			LessThanEqual,
			GreaterThanEqual,
			ShiftLeft,
			ShiftRight,
			Exponent,
			RightArrow,

			PlusEquals,
			MinusEquals,
			TimesEquals,
			DivideEquals,
			RemainderEquals,
			ShiftLeftEquals,
			ShiftRightEquals,
			BitwiseAndEquals,
			BitwiseOrEquals,
			BitwiseXorEquals,
			ExponentEquals,

			Pipeline,

			StringLit,
			NumberLit,
			BooleanLit,
			Identifier,

			EndOfFile,
		};

		struct Token
		{
			Token() { }
			Token(TokenType t, ikura::str_view s) : text(s), type(t) { }

			ikura::str_view text;
			TokenType type = TokenType::Invalid;

			operator TokenType() const { return this->type; }
			ikura::str_view str() const { return this->text; }
		};

		std::vector<Token> lexString(ikura::str_view src);
	}

	namespace interp
	{
		struct Value
		{
			static constexpr int TYPE_VOID      = 0;
			static constexpr int TYPE_INTEGER   = 1;
			static constexpr int TYPE_FLOATING  = 2;
			static constexpr int TYPE_BOOLEAN   = 3;
			static constexpr int TYPE_STRING    = 4;

			int type() const { return this->v_type; }

			bool isVoid() const     { return this->v_type == TYPE_VOID; }
			bool isInteger() const  { return this->v_type == TYPE_INTEGER; }
			bool isFloating() const { return this->v_type == TYPE_FLOATING; }
			bool isBoolean() const  { return this->v_type == TYPE_BOOLEAN; }
			bool isString() const   { return this->v_type == TYPE_STRING; }

			int64_t getInteger() const      { return this->v_integer; }
			double getFloating() const      { return this->v_floating; }
			bool getBool() const            { return this->v_bool; }
			std::string getString() const   { return this->v_string; }


			std::string str() const;

			static Value of_void();
			static Value of_bool(bool b);
			static Value of_double(double d);
			static Value of_string(const std::string& s);
			static Value of_integer(int64_t i);

		private:
			int v_type = TYPE_VOID;
			struct {
				int64_t     v_integer;
				double      v_floating;
				bool        v_bool;
				std::string v_string;
			};
		};
	}

	namespace ast
	{
		struct Expr
		{
			Expr() { }
			virtual ~Expr() { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const = 0;
		};

		struct LitString : Expr
		{
			LitString(std::string s) : str(std::move(s)) { }
			virtual ~LitString() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const override;

		private:
			std::string str;
		};

		struct LitInteger : Expr
		{
			LitInteger(int64_t v) : value(v) { }
			virtual ~LitInteger() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const override;

		private:
			int64_t value;
		};

		struct LitDouble : Expr
		{
			LitDouble(double v) : value(v) { }
			virtual ~LitDouble() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const override;

		private:
			double value;
		};

		struct LitBoolean : Expr
		{
			LitBoolean(bool v) : value(v) { }
			virtual ~LitBoolean() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const override;

		private:
			bool value;
		};

		struct UnaryOp : Expr
		{
			UnaryOp(lexer::TokenType op, std::string s, Expr* e) : op(op), op_str(std::move(s)), expr(e) { }
			virtual ~UnaryOp() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* expr;
		};

		struct BinaryOp : Expr
		{
			BinaryOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~BinaryOp() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, const CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;
		};


		Expr* parse(ikura::str_view src);
	}
}





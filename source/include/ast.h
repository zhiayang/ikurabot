// ast.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include "defs.h"
#include "interp.h"

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
			Tilde,
			LogicalOr,
			LogicalAnd,
			EqualTo,
			NotEqual,
			LessThanEqual,
			GreaterThanEqual,
			ShiftLeft,
			ShiftRight,
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

	namespace ast
	{
		struct Expr
		{
			Expr() { }
			virtual ~Expr() { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const = 0;
		};

		struct LitString : Expr
		{
			LitString(std::string s) : value(std::move(s)) { }
			virtual ~LitString() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			std::string value;
		};

		struct LitInteger : Expr
		{
			LitInteger(int64_t v) : value(v) { }
			virtual ~LitInteger() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			int64_t value;
		};

		struct LitDouble : Expr
		{
			LitDouble(double v) : value(v) { }
			virtual ~LitDouble() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			double value;
		};

		struct LitBoolean : Expr
		{
			LitBoolean(bool v) : value(v) { }
			virtual ~LitBoolean() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			bool value;
		};

		struct VarRef : Expr
		{
			VarRef(std::string name) : name(std::move(name)) { }
			virtual ~VarRef() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			std::string name;
		};


		struct UnaryOp : Expr
		{
			UnaryOp(lexer::TokenType op, std::string s, Expr* e) : op(op), op_str(std::move(s)), expr(e) { }
			virtual ~UnaryOp() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* expr;
		};

		struct BinaryOp : Expr
		{
			BinaryOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~BinaryOp() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;
		};

		struct AssignOp : Expr
		{
			AssignOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~AssignOp() override { }

			virtual std::optional<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;
		};

		Expr* parse(ikura::str_view src);
	}
}





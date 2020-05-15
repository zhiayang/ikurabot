// ast.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include <stdint.h>
#include <stddef.h>

#include "defs.h"
#include "interp.h"

namespace ikura::interp
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
			Question,
			LogicalOr,
			LogicalAnd,
			EqualTo,
			NotEqual,
			LessThanEqual,
			GreaterThanEqual,
			ShiftLeft,
			ShiftRight,
			RightArrow,
			DoublePlus,
			DoubleMinus,

			PlusEquals,
			MinusEquals,
			TimesEquals,
			DivideEquals,
			RemainderEquals,
			ShiftLeftEquals,
			ShiftRightEquals,
			BitwiseAndEquals,
			BitwiseOrEquals,
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

	namespace ast
	{
		struct Expr
		{
			Expr() { }
			virtual ~Expr() { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const = 0;
		};

		struct LitString : Expr
		{
			LitString(std::string s) : value(std::move(s)) { }
			virtual ~LitString() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			std::string value;
		};

		struct LitInteger : Expr
		{
			LitInteger(int64_t v) : value(v) { }
			virtual ~LitInteger() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			int64_t value;
		};

		struct LitDouble : Expr
		{
			LitDouble(double v) : value(v) { }
			virtual ~LitDouble() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			double value;
		};

		struct LitBoolean : Expr
		{
			LitBoolean(bool v) : value(v) { }
			virtual ~LitBoolean() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			bool value;
		};

		struct VarRef : Expr
		{
			VarRef(std::string name) : name(std::move(name)) { }
			virtual ~VarRef() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			std::string name;
		};

		struct SubscriptOp : Expr
		{
			SubscriptOp(Expr* arr, Expr* idx) : list(arr), index(idx) { }
			virtual ~SubscriptOp() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			Expr* list;
			Expr* index;
		};

		struct SliceOp : Expr
		{
			SliceOp(Expr* arr, Expr* start, Expr* end) : list(arr), start(start), end(end) { }
			virtual ~SliceOp() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			Expr* list;
			Expr* start;
			Expr* end;
		};

		struct UnaryOp : Expr
		{
			UnaryOp(lexer::TokenType op, std::string s, Expr* e) : op(op), op_str(std::move(s)), expr(e) { }
			virtual ~UnaryOp() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* expr;
		};

		struct BinaryOp : Expr
		{
			BinaryOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~BinaryOp() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;
		};

		struct TernaryOp : Expr
		{
			TernaryOp(lexer::TokenType op, std::string s, Expr* a, Expr* b, Expr* c) : op(op), op_str(std::move(s)),
				op1(a), op2(b), op3(c) { }
			virtual ~TernaryOp() override { }
			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* op1;
			Expr* op2;
			Expr* op3;
		};

		struct ComparisonOp : Expr
		{
			ComparisonOp() { }
			virtual ~ComparisonOp() override { }

			void addExpr(Expr* e) { this->exprs.push_back(e); }
			void addOp(lexer::TokenType t, std::string s) { this->ops.push_back({ t, s }); }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			std::vector<Expr*> exprs;
			std::vector<std::pair<lexer::TokenType, std::string>> ops;
		};

		struct AssignOp : Expr
		{
			AssignOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~AssignOp() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;
		};

		struct FunctionCall : Expr
		{
			FunctionCall(Expr* fn, std::vector<Expr*> args) : callee(fn), arguments(std::move(args)) { }
			virtual ~FunctionCall() override { }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;

		private:
			Expr* callee;
			std::vector<Expr*> arguments;
		};


		Result<Expr*> parse(ikura::str_view src);
		Result<Expr*> parseExpr(ikura::str_view src);

		// this returns a value, but the interesting bits are just the type. fortunately, the returned
		// value (if present) is a default-initialised value of that type.
		std::optional<interp::Type::Ptr> parseType(ikura::str_view str);
	}
}





// ast.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

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
			FatRightArrow,
			DoublePlus,
			DoubleMinus,
			Ellipsis,
			Backslash,

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
			CharLit,
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

		Result<std::vector<Token>> lexString(ikura::str_view src);
	}

	namespace ast
	{
		struct Stmt : Serialisable
		{
			Stmt() { }
			virtual ~Stmt() { }

			static Stmt* deserialise(Span& buf);

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const = 0;
			virtual std::string str() const = 0;
		};

		struct Expr : Stmt
		{
			Expr() { }

			static Expr* deserialise(Span& buf);
		};

		struct LitChar : Expr
		{
			LitChar(uint32_t cp) : codepoint(cp) { }
			virtual ~LitChar() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			uint32_t codepoint;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LIT_CHAR;
			virtual void serialise(Buffer& buf) const override;
			static LitChar* deserialise(Span& buf);
		};

		struct LitString : Expr
		{
			LitString(std::string s) : value(std::move(s)) { }
			virtual ~LitString() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			std::string value;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LIT_STRING;
			virtual void serialise(Buffer& buf) const override;
			static LitString* deserialise(Span& buf);
		};

		struct LitList : Expr
		{
			LitList(std::vector<Expr*> arr) : elms(std::move(arr)) { }
			virtual ~LitList() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			std::vector<Expr*> elms;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LIT_LIST;
			virtual void serialise(Buffer& buf) const override;
			static LitList* deserialise(Span& buf);
		};

		struct LitInteger : Expr
		{
			LitInteger(int64_t v, bool imag) : value(v), imag(imag) { }
			virtual ~LitInteger() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			int64_t value;
			bool imag;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LIT_INTEGER;
			virtual void serialise(Buffer& buf) const override;
			static LitInteger* deserialise(Span& buf);
		};

		struct LitDouble : Expr
		{
			LitDouble(double v, bool imag) : value(v), imag(imag) { }
			virtual ~LitDouble() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			double value;
			bool imag;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LIT_DOUBLE;
			virtual void serialise(Buffer& buf) const override;
			static LitDouble* deserialise(Span& buf);
		};

		struct LitBoolean : Expr
		{
			LitBoolean(bool v) : value(v) { }
			virtual ~LitBoolean() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			bool value;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LIT_BOOLEAN;
			virtual void serialise(Buffer& buf) const override;
			static LitBoolean* deserialise(Span& buf);
		};

		struct VarRef : Expr
		{
			VarRef(std::string name) : name(std::move(name)) { }
			virtual ~VarRef() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			std::string name;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_VAR_REF;
			virtual void serialise(Buffer& buf) const override;
			static VarRef* deserialise(Span& buf);
		};

		struct SubscriptOp : Expr
		{
			SubscriptOp(Expr* arr, Expr* idx) : list(arr), index(idx) { }
			virtual ~SubscriptOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			Expr* list;
			Expr* index;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_SUBSCRIPT;
			virtual void serialise(Buffer& buf) const override;
			static SubscriptOp* deserialise(Span& buf);
		};

		struct SliceOp : Expr
		{
			SliceOp(Expr* arr, Expr* start, Expr* end) : list(arr), start(start), end(end) { }
			virtual ~SliceOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			Expr* list;
			Expr* start;
			Expr* end;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_SLICE;
			virtual void serialise(Buffer& buf) const override;
			static SliceOp* deserialise(Span& buf);
		};

		struct SplatOp : Expr
		{
			SplatOp(Expr* e) : expr(e) { }
			virtual ~SplatOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			Expr* expr;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_SPLAT;
			virtual void serialise(Buffer& buf) const override;
			static SplatOp* deserialise(Span& buf);
		};

		struct UnaryOp : Expr
		{
			UnaryOp(lexer::TokenType op, std::string s, Expr* e) : op(op), op_str(std::move(s)), expr(e) { }
			virtual ~UnaryOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			lexer::TokenType op;
			std::string op_str;
			Expr* expr;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_UNARY;
			virtual void serialise(Buffer& buf) const override;
			static UnaryOp* deserialise(Span& buf);
		};

		struct BinaryOp : Expr
		{
			BinaryOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~BinaryOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_BINARY;
			virtual void serialise(Buffer& buf) const override;
			static BinaryOp* deserialise(Span& buf);
		};

		struct TernaryOp : Expr
		{
			TernaryOp(lexer::TokenType op, std::string s, Expr* a, Expr* b, Expr* c) : op(op), op_str(std::move(s)),
				op1(a), op2(b), op3(c) { }
			virtual ~TernaryOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			lexer::TokenType op;
			std::string op_str;
			Expr* op1;
			Expr* op2;
			Expr* op3;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_TERNARY;
			virtual void serialise(Buffer& buf) const override;
			static TernaryOp* deserialise(Span& buf);
		};

		struct ComparisonOp : Expr
		{
			ComparisonOp() { }
			virtual ~ComparisonOp() override;

			void addExpr(Expr* e) { this->exprs.push_back(e); }
			void addOp(lexer::TokenType t, std::string s) { this->ops.push_back({ t, s }); }

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			std::vector<Expr*> exprs;
			std::vector<std::pair<lexer::TokenType, std::string>> ops;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_COMPARISON;
			virtual void serialise(Buffer& buf) const override;
			static ComparisonOp* deserialise(Span& buf);
		};

		struct AssignOp : Expr
		{
			AssignOp(lexer::TokenType op, std::string s, Expr* l, Expr* r) : op(op), op_str(std::move(s)), lhs(l), rhs(r) { }
			virtual ~AssignOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			lexer::TokenType op;
			std::string op_str;
			Expr* lhs;
			Expr* rhs;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_ASSIGN;
			virtual void serialise(Buffer& buf) const override;
			static AssignOp* deserialise(Span& buf);
		};

		struct DotOp : Expr
		{
			DotOp(Expr* l, Expr* r) : lhs(l), rhs(r) { }
			virtual ~DotOp() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			Expr* lhs;
			Expr* rhs;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_OP_DOT;
			virtual void serialise(Buffer& buf) const override;
			static DotOp* deserialise(Span& buf);
		};

		struct FunctionCall : Expr
		{
			FunctionCall(Expr* fn, std::vector<Expr*> args) : callee(fn), arguments(std::move(args)) { }
			virtual ~FunctionCall() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			Expr* callee;
			std::vector<Expr*> arguments;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_FUNCTION_CALL;
			virtual void serialise(Buffer& buf) const override;
			static FunctionCall* deserialise(Span& buf);
		};

		struct Block : Stmt
		{
			Block(std::vector<Stmt*> stmts) : stmts(std::move(stmts)) { }
			virtual ~Block() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			std::vector<Stmt*> stmts;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_BLOCK;
			virtual void serialise(Buffer& buf) const override;
			static Block* deserialise(Span& buf);
		};

		struct LambdaExpr : Expr
		{
			LambdaExpr(Type::Ptr sig, Block* body) : signature(std::move(sig)), body(body) { }
			virtual ~LambdaExpr() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			Type::Ptr signature;
			Block* body;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_LAMBDA;
			virtual void serialise(Buffer& buf) const override;
			static LambdaExpr* deserialise(Span& buf);
		};


		struct FunctionDefn : Stmt
		{
			FunctionDefn(std::string name, Type::Ptr signature, std::vector<std::string> generics, Block* body)
				: name(std::move(name)), signature(std::move(signature)), generics(std::move(generics)), body(body) { }
			virtual ~FunctionDefn() override;

			virtual Result<interp::Value> evaluate(InterpState* fs, CmdContext& cs) const override;
			virtual std::string str() const override;

			std::string name;
			Type::Ptr signature;
			std::vector<std::string> generics;
			Block* body;

			static constexpr uint8_t TYPE_TAG = serialise::TAG_AST_FUNCTION_DEFN;
			virtual void serialise(Buffer& buf) const override;
			static FunctionDefn* deserialise(Span& buf);
		};

		Result<Stmt*> parse(ikura::str_view src);
		Result<Expr*> parseExpr(ikura::str_view src);
		Result<FunctionDefn*> parseFuncDefn(ikura::str_view src);

		// this returns a value, but the interesting bits are just the type. fortunately, the returned
		// value (if present) is a default-initialised value of that type.
		std::optional<interp::Type::Ptr> parseType(ikura::str_view str, int group = 0);
	}
}





// ser.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "db.h"
#include "ast.h"
#include "zfu.h"
#include "serialise.h"

namespace ikura::interp::ast
{
	void LitChar::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		// cast to u64 because we have TINY_U64 optimisation, and most chars will be ascii (< 128) anyway.
		wr.write((uint64_t) this->codepoint);
	}

	LitChar* LitChar::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		uint64_t cp = 0;
		if(!rd.read(&cp))
			return nullptr;

		return new LitChar(cp);
	}






	void LitString::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->value);
	}

	LitString* LitString::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		std::string s;
		if(!rd.read(&s))
			return nullptr;

		return new LitString(s);
	}






	void LitList::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->elms);
	}

	LitList* LitList::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		std::vector<Expr*> elms;
		if(!rd.read(&elms) || zfu::contains(elms, nullptr))
			return nullptr;

		return new LitList(elms);
	}






	void LitInteger::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write((uint64_t) this->value);
		wr.write(this->imag);
	}

	LitInteger* LitInteger::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		uint64_t val = 0;
		bool imag = false;
		if(!rd.read(&val))
			return nullptr;

		if(!rd.read(&imag))
			return nullptr;

		return new LitInteger((int64_t) val, imag);
	}






	void LitDouble::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->value);
		wr.write(imag);
	}

	LitDouble* LitDouble::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		double val = 0;
		bool imag = false;
		if(!rd.read(&val))
			return nullptr;

		if(!rd.read(&imag))
			return nullptr;

		return new LitDouble(val, imag);
	}






	void LitBoolean::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->value);
	}

	LitBoolean* LitBoolean::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		bool val = false;
		if(!rd.read(&val))
			return nullptr;

		return new LitBoolean(val);
	}






	void VarRef::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
	}

	VarRef* VarRef::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		std::string name;
		if(!rd.read(&name))
			return nullptr;

		return new VarRef(name);
	}






	void SubscriptOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->list);
		wr.write(this->index);
	}

	SubscriptOp* SubscriptOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		Expr* list = rd.read<Expr*>().value();
		if(!list) return nullptr;

		Expr* index = rd.read<Expr*>().value();
		if(!index) return nullptr;

		return new SubscriptOp(list, index);
	}






	void SliceOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->list);

		if(this->start)
		{
			wr.write(true);
			wr.write(this->start);
		}
		else
		{
			wr.write(false);
		}

		if(this->end)
		{
			wr.write(true);
			wr.write(this->end);
		}
		else
		{
			wr.write(false);
		}
	}

	SliceOp* SliceOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		Expr* list = rd.read<Expr*>().value();
		if(!list) return nullptr;

		bool have_start = false;
		if(!rd.read(&have_start))
			return nullptr;

		Expr* start = nullptr;
		Expr* end = nullptr;

		if(have_start)
		{
			start = rd.read<Expr*>().value();
			if(!start) return nullptr;
		}


		bool have_end = false;
		if(!rd.read(&have_end))
			return nullptr;

		if(have_end)
		{
			end = rd.read<Expr*>().value();
			if(!end) return nullptr;
		}

		return new SliceOp(list, start, end);
	}






	void SplatOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->expr);
	}

	SplatOp* SplatOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		Expr* e = rd.read<Expr*>().value();
		if(!e) return nullptr;

		return new SplatOp(e);
	}







	void UnaryOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write((uint64_t) this->op);
		wr.write(this->op_str);
		wr.write(this->expr);
	}

	UnaryOp* UnaryOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		uint64_t op;
		if(!rd.read(&op))
			return nullptr;

		std::string op_str;
		if(!rd.read(&op_str))
			return nullptr;

		Expr* expr = rd.read<Expr*>().value();
		if(!expr) return nullptr;

		return new UnaryOp((lexer::TokenType) op, op_str, expr);
	}







	void BinaryOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write((uint64_t) this->op);
		wr.write(this->op_str);
		wr.write(this->lhs);
		wr.write(this->rhs);
	}

	BinaryOp* BinaryOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		uint64_t op;
		if(!rd.read(&op))
			return nullptr;

		std::string op_str;
		if(!rd.read(&op_str))
			return nullptr;

		Expr* lhs = rd.read<Expr*>().value();
		if(!lhs) return nullptr;

		Expr* rhs = rd.read<Expr*>().value();
		if(!rhs) return nullptr;

		return new BinaryOp((lexer::TokenType) op, op_str, lhs, rhs);
	}







	void TernaryOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write((uint64_t) this->op);
		wr.write(this->op_str);
		wr.write(this->op1);
		wr.write(this->op2);
		wr.write(this->op3);
	}

	TernaryOp* TernaryOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		uint64_t op;
		if(!rd.read(&op))
			return nullptr;

		std::string op_str;
		if(!rd.read(&op_str))
			return nullptr;

		Expr* op1 = rd.read<Expr*>().value();
		if(!op1) return nullptr;

		Expr* op2 = rd.read<Expr*>().value();
		if(!op2) return nullptr;

		Expr* op3 = rd.read<Expr*>().value();
		if(!op3) return nullptr;

		return new TernaryOp((lexer::TokenType) op, op_str, op1, op2, op3);
	}






	void ComparisonOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->exprs);

		auto xs = zfu::map(this->ops, [](const auto& p) -> auto {
			return std::pair((uint64_t) p.first, p.second);
		});

		wr.write(xs);
	}

	ComparisonOp* ComparisonOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		std::vector<Expr*> exprs;
		if(!rd.read(&exprs))
			return nullptr;

		std::vector<std::pair<uint64_t, std::string>> ops;
		if(!rd.read(&ops))
			return nullptr;

		auto ret = new ComparisonOp();
		ret->exprs = exprs;
		ret->ops = zfu::map(ops, [](const auto& p) -> auto {
			return std::pair((lexer::TokenType) p.first, p.second);
		});

		return ret;
	}






	void AssignOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write((uint64_t) this->op);
		wr.write(this->op_str);
		wr.write(this->lhs);
		wr.write(this->rhs);
	}

	AssignOp* AssignOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		uint64_t op;
		if(!rd.read(&op))
			return nullptr;

		std::string op_str;
		if(!rd.read(&op_str))
			return nullptr;

		Expr* lhs = rd.read<Expr*>().value();
		if(!lhs) return nullptr;

		Expr* rhs = rd.read<Expr*>().value();
		if(!rhs) return nullptr;

		return new AssignOp((lexer::TokenType) op, op_str, lhs, rhs);
	}


	void DotOp::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->lhs);
		wr.write(this->rhs);
	}

	DotOp* DotOp::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		Expr* lhs = rd.read<Expr*>().value();
		if(!lhs) return nullptr;

		Expr* rhs = rd.read<Expr*>().value();
		if(!rhs) return nullptr;

		return new DotOp(lhs, rhs);
	}





	void FunctionCall::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->callee);
		wr.write(this->arguments);
	}

	FunctionCall* FunctionCall::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		Expr* callee = rd.read<Expr*>().value();
		if(!callee) return nullptr;

		std::vector<Expr*> args;
		if(!rd.read(&args))
			return nullptr;

		return new FunctionCall(callee, args);
	}

	void Block::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->stmts);
	}

	Block* Block::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		std::vector<Stmt*> stmts;
		if(!rd.read(&stmts))
			return nullptr;

		return new Block(stmts);
	}


	void VarDefn::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
		this->value->serialise(buf);
	}

	VarDefn* VarDefn::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		auto name = rd.read<std::string>();
		if(!name) return nullptr;

		auto init = rd.read<Expr*>();
		if(!init || !init.value()) return nullptr;

		return new VarDefn(name.value(), init.value());
	}


	void LambdaExpr::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		this->signature->serialise(buf);
		wr.write(this->body);
	}

	LambdaExpr* LambdaExpr::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		auto sig = Type::deserialise(buf);
		if(!sig) return nullptr;

		Block* body = rd.read<Block*>().value();
		if(!body) return nullptr;

		return new LambdaExpr(sig.value(), body);
	}


	void FunctionDefn::serialise(Buffer& buf) const
	{
		auto wr = serialise::Writer(buf);
		wr.tag(TYPE_TAG);

		wr.write(this->name);
		this->signature->serialise(buf);
		wr.write(this->generics);
		wr.write(this->body);
	}

	FunctionDefn* FunctionDefn::deserialise(Span& buf)
	{
		auto rd = serialise::Reader(buf);
		if(auto t = rd.tag(); t != TYPE_TAG)
		{
			lg::error("db", "type tag mismatch (found '{02x}', expected '{02x}')", t, TYPE_TAG);
			return nullptr;
		}

		std::string name;
		if(!rd.read(&name))
			return nullptr;

		auto sig = Type::deserialise(buf);
		if(!sig) return nullptr;

		std::vector<std::string> generics;
		if(db::getVersion() >= 23)
		{
			if(!rd.read(&generics))
				return nullptr;
		}

		Block* body = rd.read<Block*>().value();
		if(!body) return nullptr;

		return new FunctionDefn(name, sig.value(), generics, body);
	}






	Expr* Expr::deserialise(Span& buf)
	{
		auto tag = buf.peek();
		switch(tag)
		{
			case serialise::TAG_AST_LIT_CHAR:       return LitChar::deserialise(buf);
			case serialise::TAG_AST_LIT_STRING:     return LitString::deserialise(buf);
			case serialise::TAG_AST_LIT_LIST:       return LitList::deserialise(buf);
			case serialise::TAG_AST_LIT_INTEGER:    return LitInteger::deserialise(buf);
			case serialise::TAG_AST_LIT_DOUBLE:     return LitDouble::deserialise(buf);
			case serialise::TAG_AST_LIT_BOOLEAN:    return LitBoolean::deserialise(buf);
			case serialise::TAG_AST_VAR_REF:        return VarRef::deserialise(buf);
			case serialise::TAG_AST_OP_SUBSCRIPT:   return SubscriptOp::deserialise(buf);
			case serialise::TAG_AST_OP_SLICE:       return SliceOp::deserialise(buf);
			case serialise::TAG_AST_OP_SPLAT:       return SplatOp::deserialise(buf);
			case serialise::TAG_AST_OP_UNARY:       return UnaryOp::deserialise(buf);
			case serialise::TAG_AST_OP_BINARY:      return BinaryOp::deserialise(buf);
			case serialise::TAG_AST_OP_TERNARY:     return TernaryOp::deserialise(buf);
			case serialise::TAG_AST_OP_COMPARISON:  return ComparisonOp::deserialise(buf);
			case serialise::TAG_AST_OP_ASSIGN:      return AssignOp::deserialise(buf);
			case serialise::TAG_AST_FUNCTION_CALL:  return FunctionCall::deserialise(buf);
			case serialise::TAG_AST_OP_DOT:         return DotOp::deserialise(buf);
		}

		lg::error("db", "type tag mismatch (unexpected '{02x}')", tag);
		return nullptr;
	}

	Stmt* Stmt::deserialise(Span& buf)
	{
		auto tag = buf.peek();
		switch(tag)
		{
			case serialise::TAG_AST_VAR_DEFN:
				return VarDefn::deserialise(buf);

			default:
				break;
		}

		return Expr::deserialise(buf);
	}
}

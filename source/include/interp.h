// interp.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <string>

#include "buffer.h"
#include "serialise.h"

namespace ikura::cmd::interp
{
	struct Value : serialise::Serialisable
	{
		static constexpr uint8_t TYPE_VOID      = 0;
		static constexpr uint8_t TYPE_INTEGER   = 1;
		static constexpr uint8_t TYPE_FLOATING  = 2;
		static constexpr uint8_t TYPE_BOOLEAN   = 3;
		static constexpr uint8_t TYPE_STRING    = 4;

		uint8_t type() const { return this->v_type; }
		std::string type_str() const;

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

		virtual void serialise(Buffer& buf) const override;
		static std::optional<Value> deserialise(Span& buf);

		static constexpr uint8_t TYPE_TAG = serialise::TAG_INTERP_VALUE;

	private:
		uint8_t v_type = TYPE_VOID;
		struct {
			int64_t     v_integer;
			double      v_floating;
			bool        v_bool;
			std::string v_string;
		};
	};
}

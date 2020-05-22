// unicode.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "utf8proc/utf8proc.h"

namespace ikura::unicode
{
	size_t is_category(ikura::str_view str, const std::initializer_list<int>& categories)
	{
		utf8proc_int32_t cp = { };
		auto sz = utf8proc_iterate((const uint8_t*) str.data(), str.size(), &cp);
		if(cp == -1)
			return 0;

		auto cat = utf8proc_category(cp);
		for(auto c : categories)
			if(cat == c)
				return sz;

		return 0;
	}

	size_t get_codepoint_length(ikura::str_view str)
	{
		utf8proc_int32_t cp = { };
		auto sz = utf8proc_iterate((const uint8_t*) str.data(), str.size(), &cp);
		if(cp == -1)
			return 1;

		return sz;
	}

	size_t is_letter(ikura::str_view str)
	{
		return is_category(str, {
			UTF8PROC_CATEGORY_LU, UTF8PROC_CATEGORY_LL, UTF8PROC_CATEGORY_LT,
			UTF8PROC_CATEGORY_LM, UTF8PROC_CATEGORY_LO
		});
	}

	size_t is_digit(ikura::str_view str)
	{
		return is_category(str, {
			UTF8PROC_CATEGORY_ND
		});
	}

	size_t is_punctuation(ikura::str_view str)
	{
		return is_category(str, {
			UTF8PROC_CATEGORY_PC, UTF8PROC_CATEGORY_PD, UTF8PROC_CATEGORY_PS,
			UTF8PROC_CATEGORY_PE, UTF8PROC_CATEGORY_PI, UTF8PROC_CATEGORY_PF
		});
	}

	size_t is_symbol(ikura::str_view str)
	{
		return is_category(str, {
			UTF8PROC_CATEGORY_SM, UTF8PROC_CATEGORY_SC, UTF8PROC_CATEGORY_SK
		});
	}

	size_t is_any_symbol(ikura::str_view str)
	{
		return is_category(str, {
			UTF8PROC_CATEGORY_SM, UTF8PROC_CATEGORY_SC, UTF8PROC_CATEGORY_SK, UTF8PROC_CATEGORY_SO
		});
	}

	size_t count_codepoints(ikura::str_view str)
	{
		int32_t	dummy = 0;
		return utf8proc_decompose((const uint8_t*) str.data(), str.size(),
			&dummy, 0, (utf8proc_option_t) (UTF8PROC_LUMP | UTF8PROC_COMPOSE | UTF8PROC_STRIPNA));
	}

	std::vector<int32_t> to_utf32(ikura::str_view str)
	{
		size_t bufsz = str.size();

		std::vector<int32_t> buffer;

	again:
		buffer.resize(bufsz + 1);

		auto converted = utf8proc_decompose((const uint8_t*) str.data(), str.size(), &buffer[0], buffer.size(), (utf8proc_option_t) (
			UTF8PROC_LUMP | UTF8PROC_COMPOSE | UTF8PROC_STRIPNA));

		if(converted < 0)
		{
			lg::error("utf", "failed to convert '%s' to utf-32 (error = %d)", str, (int) converted);
			return { };
		}

		if((size_t) converted != bufsz)
		{
			bufsz = (size_t) converted;
			goto again;
		}

		buffer.resize(converted);
		return buffer;
	}

	std::string to_utf8(std::vector<int32_t> codepoints)
	{
		auto did = utf8proc_reencode(&codepoints[0], codepoints.size(), (utf8proc_option_t) (UTF8PROC_STRIPCC | UTF8PROC_COMPOSE));
		if(did < 0)
		{
			lg::error("unicode", "failed to convert codepoints to utf-8 (error = %d)", (int) did);
			return "";
		}

		return std::string((const char*) &codepoints[0], (size_t) did);
	}

	size_t get_byte_length(int32_t codepoint)
	{
		uint8_t tmp[4];
		return utf8proc_encode_char(codepoint, &tmp[0]);
	}

	std::string normalise(ikura::str_view str)
	{
		// this is just a case of converting to utf-32
		// then back to utf-8 with the right flags.
		return to_utf8(to_utf32(str));
	}
}



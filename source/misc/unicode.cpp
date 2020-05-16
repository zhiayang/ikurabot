// unicode.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "utf8proc/utf8proc.h"

namespace ikura::utf8
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


	std::string normalise_identifier(ikura::str_view str)
	{
		size_t bufsz = str.size();

	again:
		auto buffer = new int32_t[1 + bufsz];
		auto converted = utf8proc_decompose((const uint8_t*) str.data(), str.size(), buffer, bufsz, (utf8proc_option_t) (
			UTF8PROC_IGNORE | UTF8PROC_LUMP | UTF8PROC_STRIPMARK | UTF8PROC_COMPOSE | UTF8PROC_STRIPNA));

		if(converted < 0)
		{
			lg::error("utf", "failed to decompose '%s' (error = %d)", str, (int) converted);
			return "";
		}

		if((size_t) converted != bufsz)
		{
			bufsz = (size_t) converted;
			delete[] buffer;
			goto again;
		}

		{
			auto did = utf8proc_reencode(buffer, converted, (utf8proc_option_t) (UTF8PROC_STRIPCC | UTF8PROC_COMPOSE));
			if(did < 0)
			{
				lg::error("utf", "failed to encode '%s' (error = %d)", str, (int) converted);
				return "";
			}

			auto ret = std::string((const char*) buffer, (size_t) did);
			delete[] buffer;

			return ret;
		}
	}
}



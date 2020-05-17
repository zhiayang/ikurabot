// base64.cpp
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"

namespace ikura::base64
{
	constexpr const char decode_table[128] = {
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
		52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
		64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
		15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
		64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
	};

	constexpr const unsigned char encode_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string encode(const uint8_t* src, size_t len)
	{
		size_t olen = 4 * ((len + 2) / 3); /* 3-byte blocks to 4-byte */

		if(olen < len)
			return "";

		std::string outStr;
		outStr.resize(olen);

		uint8_t* out = (uint8_t*) &outStr[0];
		uint8_t* pos = out;

		const uint8_t* end = src + len;
		const uint8_t* in = src;
		while(end - in >= 3)
		{
			*pos++ = encode_table[in[0] >> 2];
			*pos++ = encode_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
			*pos++ = encode_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
			*pos++ = encode_table[in[2] & 0x3f];
			in += 3;
		}

		if(end - in)
		{
			*pos++ = encode_table[in[0] >> 2];
			if(end - in == 1)
			{
				*pos++ = encode_table[(in[0] & 0x03) << 4];
				*pos++ = '=';
			}
			else
			{
				*pos++ = encode_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
				*pos++ = encode_table[(in[1] & 0x0f) << 2];
			}
			*pos++ = '=';
		}

		return outStr;
	}


	std::string decode(ikura::str_view src)
	{
		std::string ret;
		int bits_collected = 0;
		unsigned int accumulator = 0;

		for(char c : src)
		{
			if(std::isspace(c) || c == '=')
				continue;

			if((c > 127) || (c < 0) || (decode_table[(int) c] > 63))
				return "";

			accumulator = (accumulator << 6) | decode_table[(int) c];
			bits_collected += 6;

			if(bits_collected >= 8)
			{
				bits_collected -= 8;
				ret += (char) ((accumulator >> bits_collected) & 0xFF);
			}
		}

		return ret;
	}
}

// timer.h
// Copyright (c) 2020, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#include <chrono>

namespace ikura
{
	struct timer
	{
		using hrc = std::chrono::high_resolution_clock;

		timer() : out(nullptr)              { start = hrc::now(); }
		explicit timer(double* t) : out(t)  { start = hrc::now(); }
		~timer()                            { if(out) *out = static_cast<double>((hrc::now() - start).count()) / 1000000.0; }
		double measure()                    { return static_cast<double>((hrc::now() - start).count()) / 1000000.0; }
		double reset()                      { auto ret = measure(); start = hrc::now(); return ret; }

		double* out = 0;
		std::chrono::time_point<hrc> start;
	};
}

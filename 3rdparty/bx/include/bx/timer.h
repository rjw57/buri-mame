/*
 * Copyright 2010-2017 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bx#license-bsd-2-clause
 */

#ifndef BX_TIMER_H_HEADER_GUARD
#define BX_TIMER_H_HEADER_GUARD

#include "bx.h"

namespace bx
{
	///
	int64_t getHPCounter();

	///
	int64_t getHPFrequency();

} // namespace bx

#include "timer.inl"

#endif // BX_TIMER_H_HEADER_GUARD

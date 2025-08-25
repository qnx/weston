/*
 * Copyright 2022 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

struct weston_compositor;

__attribute__((noreturn, format(printf, 2, 3)))
static inline void
weston_assert_fail_(const struct weston_compositor *compositor, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	abort();
}

#ifndef custom_assert_fail_
#define custom_assert_fail_ weston_assert_fail_
#endif

#define weston_assert_(compositor, a, b, val_type, val_fmt, cmp)		\
({										\
	struct weston_compositor *ec = compositor;				\
	val_type a_ = (a);							\
	val_type b_ = (b);							\
	bool cond = a_ cmp b_;							\
	if (!cond)								\
		custom_assert_fail_(ec, "%s:%u: Assertion %s %s %s (" val_fmt " %s " val_fmt ") failed!\n",	\
				    __FILE__, __LINE__, #a, #cmp, #b, a_, #cmp, b_);				\
	cond;									\
})

#define weston_assert_fn_(compositor, fn, a, b, val_type, val_fmt, cmp)		\
({										\
	struct weston_compositor *ec = compositor;				\
	val_type a_ = (a);							\
	val_type b_ = (b);							\
	bool cond = fn(a_, b_) cmp 0;						\
	if (!cond)								\
		custom_assert_fail_(ec, "%s:%u: Assertion %s %s %s (" val_fmt " %s " val_fmt ") failed!\n",	\
				    __FILE__, __LINE__, #a, #cmp, #b, a_, #cmp, b_);				\
	cond;									\
})

#define weston_assert_not_reached(compositor, reason)				\
do {										\
	struct weston_compositor *ec = compositor;				\
	custom_assert_fail_(ec, "%s:%u: Assertion failed! This should not be reached: %s\n",	\
			    __FILE__, __LINE__, reason);					\
} while (0)

#define weston_assert_true(compositor, a) \
	weston_assert_(compositor, a, true, bool, "%d", ==)

#define weston_assert_false(compositor, a) \
	weston_assert_(compositor, a, false, bool, "%d", ==)

#define weston_assert_ptr(compositor, a) \
	weston_assert_(compositor, a, NULL, const void *, "%p", !=)

#define weston_assert_ptr_is_null(compositor, a) \
	weston_assert_(compositor, a, NULL, const void *, "%p", ==)

#define weston_assert_ptr_eq(compositor, a, b) \
	weston_assert_(compositor, a, b, const void *, "%p", ==)

#define weston_assert_double_eq(compositor, a, b) \
	weston_assert_(compositor, a, b, double, "%.10g", ==)

#define weston_assert_uint32_eq(compositor, a, b) \
	weston_assert_(compositor, a, b, uint32_t, "%u", ==)

#define weston_assert_uint32_neq(compositor, a, b) \
	weston_assert_(compositor, a, b, uint32_t, "%u", !=)

#define weston_assert_uint32_gt(compositor, a, b) \
	weston_assert_(compositor, a, b, uint32_t, "%u", >)

#define weston_assert_uint32_gt_or_eq(compositor, a, b) \
	weston_assert_(compositor, a, b, uint32_t, "%u", >=)

#define weston_assert_uint32_lt(compositor, a, b) \
	weston_assert_(compositor, a, b, uint32_t, "%u", <)

#define weston_assert_uint64_eq(compositor, a, b) \
	weston_assert_(compositor, a, b, uint64_t, "%" PRIx64, ==)

#define weston_assert_str_eq(compositor, a, b) \
	weston_assert_fn_(compositor, strcmp, a, b, const char *, "%s", ==)

#define weston_assert_bit_is_set(compositor, value, bit)			\
({										\
	struct weston_compositor *ec = compositor;				\
	uint64_t v = (value);							\
	uint8_t b = (bit);							\
	bool cond = (v >> b) & 1;						\
	if (!cond)								\
		custom_assert_fail_(ec, "%s:%u: Assertion failed! Bit %s (%u) of %s (0x%" PRIx64 ") is not set.\n",	\
				    __FILE__, __LINE__, #bit, b, #value, v);	\
	cond;									\
})

#define weston_assert_legal_bits(compositor, value, mask)			\
({										\
	struct weston_compositor *ec = compositor;				\
	uint64_t v_ = (value);							\
	uint64_t m_ = (mask);							\
	uint64_t ill = v_ & ~m_;							\
	bool cond = ill == 0;							\
	if (!cond)								\
		custom_assert_fail_(ec, "%s:%u: Assertion failed! "		\
				    "Value %s (0x%" PRIx64 ") contains illegal bits 0x%" PRIx64 ". " \
				    "Legal mask is %s (0x%" PRIx64 ").\n",	\
				    __FILE__, __LINE__, #value, v_, ill, #mask, m_); \
	cond;									\
})

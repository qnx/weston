/*
 * Copyright © 2024 Collabora, Ltd.
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

#ifndef _WESTON_TEST_ASSERT_H_
#define _WESTON_TEST_ASSERT_H_

#include <errno.h>

#include "shared/weston-assert.h"

/* Boolean asserts. */

#define test_assert_true(a)  weston_assert_(NULL, a, true,  bool, "%d", ==)
#define test_assert_false(a) weston_assert_(NULL, a, false, bool, "%d", ==)

/* String asserts. */

#define test_assert_str_eq(a, b) weston_assert_fn_(NULL, strcmp, a, b, const char *, "%s", ==)

/* Pointer asserts. */

#define test_assert_ptr_null(a)     weston_assert_(NULL, a, NULL, const void *, "%p", ==)
#define test_assert_ptr_not_null(a) weston_assert_(NULL, a, NULL, const void *, "%p", !=)
#define test_assert_ptr_eq(a, b)    weston_assert_(NULL, a, b,    const void *, "%p", ==)
#define test_assert_ptr_ne(a, b)    weston_assert_(NULL, a, b,    const void *, "%p", !=)

/* Unsigned integer asserts. */

#define test_assert_u8_eq(a, b) weston_assert_(NULL, a, b, uint8_t, "%" PRIu8, ==)
#define test_assert_u8_ne(a, b) weston_assert_(NULL, a, b, uint8_t, "%" PRIu8, !=)
#define test_assert_u8_gt(a, b) weston_assert_(NULL, a, b, uint8_t, "%" PRIu8, >)
#define test_assert_u8_ge(a, b) weston_assert_(NULL, a, b, uint8_t, "%" PRIu8, >=)
#define test_assert_u8_lt(a, b) weston_assert_(NULL, a, b, uint8_t, "%" PRIu8, <)
#define test_assert_u8_le(a, b) weston_assert_(NULL, a, b, uint8_t, "%" PRIu8, <=)

#define test_assert_u16_eq(a, b) weston_assert_(NULL, a, b, uint16_t, "%" PRIu16, ==)
#define test_assert_u16_ne(a, b) weston_assert_(NULL, a, b, uint16_t, "%" PRIu16, !=)
#define test_assert_u16_gt(a, b) weston_assert_(NULL, a, b, uint16_t, "%" PRIu16, >)
#define test_assert_u16_ge(a, b) weston_assert_(NULL, a, b, uint16_t, "%" PRIu16, >=)
#define test_assert_u16_lt(a, b) weston_assert_(NULL, a, b, uint16_t, "%" PRIu16, <)
#define test_assert_u16_le(a, b) weston_assert_(NULL, a, b, uint16_t, "%" PRIu16, <=)

#define test_assert_u32_eq(a, b) weston_assert_(NULL, a, b, uint32_t, "%" PRIu32, ==)
#define test_assert_u32_ne(a, b) weston_assert_(NULL, a, b, uint32_t, "%" PRIu32, !=)
#define test_assert_u32_gt(a, b) weston_assert_(NULL, a, b, uint32_t, "%" PRIu32, >)
#define test_assert_u32_ge(a, b) weston_assert_(NULL, a, b, uint32_t, "%" PRIu32, >=)
#define test_assert_u32_lt(a, b) weston_assert_(NULL, a, b, uint32_t, "%" PRIu32, <)
#define test_assert_u32_le(a, b) weston_assert_(NULL, a, b, uint32_t, "%" PRIu32, <=)

#define test_assert_u64_eq(a, b) weston_assert_(NULL, a, b, uint64_t, "%" PRIu64, ==)
#define test_assert_u64_ne(a, b) weston_assert_(NULL, a, b, uint64_t, "%" PRIu64, !=)
#define test_assert_u64_gt(a, b) weston_assert_(NULL, a, b, uint64_t, "%" PRIu64, >)
#define test_assert_u64_ge(a, b) weston_assert_(NULL, a, b, uint64_t, "%" PRIu64, >=)
#define test_assert_u64_lt(a, b) weston_assert_(NULL, a, b, uint64_t, "%" PRIu64, <)
#define test_assert_u64_le(a, b) weston_assert_(NULL, a, b, uint64_t, "%" PRIu64, <=)

#define test_assert_uint_eq(a, b) weston_assert_(NULL, a, b, unsigned int, "%u", ==)
#define test_assert_uint_ne(a, b) weston_assert_(NULL, a, b, unsigned int, "%u", !=)
#define test_assert_uint_gt(a, b) weston_assert_(NULL, a, b, unsigned int, "%u", >)
#define test_assert_uint_ge(a, b) weston_assert_(NULL, a, b, unsigned int, "%u", >=)
#define test_assert_uint_lt(a, b) weston_assert_(NULL, a, b, unsigned int, "%u", <)
#define test_assert_uint_le(a, b) weston_assert_(NULL, a, b, unsigned int, "%u", <=)

/* Signed integer asserts. */

#define test_assert_s8_eq(a, b) weston_assert_(NULL, a, b, int8_t, "%" PRId8, ==)
#define test_assert_s8_ne(a, b) weston_assert_(NULL, a, b, int8_t, "%" PRId8, !=)
#define test_assert_s8_gt(a, b) weston_assert_(NULL, a, b, int8_t, "%" PRId8, >)
#define test_assert_s8_ge(a, b) weston_assert_(NULL, a, b, int8_t, "%" PRId8, >=)
#define test_assert_s8_lt(a, b) weston_assert_(NULL, a, b, int8_t, "%" PRId8, <)
#define test_assert_s8_le(a, b) weston_assert_(NULL, a, b, int8_t, "%" PRId8, <=)

#define test_assert_s16_eq(a, b) weston_assert_(NULL, a, b, int16_t, "%" PRId16, ==)
#define test_assert_s16_ne(a, b) weston_assert_(NULL, a, b, int16_t, "%" PRId16, !=)
#define test_assert_s16_gt(a, b) weston_assert_(NULL, a, b, int16_t, "%" PRId16, >)
#define test_assert_s16_ge(a, b) weston_assert_(NULL, a, b, int16_t, "%" PRId16, >=)
#define test_assert_s16_lt(a, b) weston_assert_(NULL, a, b, int16_t, "%" PRId16, <)
#define test_assert_s16_le(a, b) weston_assert_(NULL, a, b, int16_t, "%" PRId16, <=)

#define test_assert_s32_eq(a, b) weston_assert_(NULL, a, b, int32_t, "%" PRId32, ==)
#define test_assert_s32_ne(a, b) weston_assert_(NULL, a, b, int32_t, "%" PRId32, !=)
#define test_assert_s32_gt(a, b) weston_assert_(NULL, a, b, int32_t, "%" PRId32, >)
#define test_assert_s32_ge(a, b) weston_assert_(NULL, a, b, int32_t, "%" PRId32, >=)
#define test_assert_s32_lt(a, b) weston_assert_(NULL, a, b, int32_t, "%" PRId32, <)
#define test_assert_s32_le(a, b) weston_assert_(NULL, a, b, int32_t, "%" PRId32, <=)

#define test_assert_s64_eq(a, b) weston_assert_(NULL, a, b, int64_t, "%" PRId64, ==)
#define test_assert_s64_ne(a, b) weston_assert_(NULL, a, b, int64_t, "%" PRId64, !=)
#define test_assert_s64_gt(a, b) weston_assert_(NULL, a, b, int64_t, "%" PRId64, >)
#define test_assert_s64_ge(a, b) weston_assert_(NULL, a, b, int64_t, "%" PRId64, >=)
#define test_assert_s64_lt(a, b) weston_assert_(NULL, a, b, int64_t, "%" PRId64, <)
#define test_assert_s64_le(a, b) weston_assert_(NULL, a, b, int64_t, "%" PRId64, <=)

#define test_assert_int_eq(a, b) weston_assert_(NULL, a, b, int, "%d", ==)
#define test_assert_int_ne(a, b) weston_assert_(NULL, a, b, int, "%d", !=)
#define test_assert_int_gt(a, b) weston_assert_(NULL, a, b, int, "%d", >)
#define test_assert_int_ge(a, b) weston_assert_(NULL, a, b, int, "%d", >=)
#define test_assert_int_lt(a, b) weston_assert_(NULL, a, b, int, "%d", <)
#define test_assert_int_le(a, b) weston_assert_(NULL, a, b, int, "%d", <=)

/* Floating-point asserts. */

#define test_assert_f32_eq(a, b) weston_assert_(NULL, a, b, float, "%.10g", ==)
#define test_assert_f32_ne(a, b) weston_assert_(NULL, a, b, float, "%.10g", !=)
#define test_assert_f32_gt(a, b) weston_assert_(NULL, a, b, float, "%.10g", >)
#define test_assert_f32_ge(a, b) weston_assert_(NULL, a, b, float, "%.10g", >=)
#define test_assert_f32_lt(a, b) weston_assert_(NULL, a, b, float, "%.10g", <)
#define test_assert_f32_le(a, b) weston_assert_(NULL, a, b, float, "%.10g", <=)

#define test_assert_f64_eq(a, b) weston_assert_(NULL, a, b, double, "%.10g", ==)
#define test_assert_f64_ne(a, b) weston_assert_(NULL, a, b, double, "%.10g", !=)
#define test_assert_f64_gt(a, b) weston_assert_(NULL, a, b, double, "%.10g", >)
#define test_assert_f64_ge(a, b) weston_assert_(NULL, a, b, double, "%.10g", >=)
#define test_assert_f64_lt(a, b) weston_assert_(NULL, a, b, double, "%.10g", <)
#define test_assert_f64_le(a, b) weston_assert_(NULL, a, b, double, "%.10g", <=)

/* Various helpers. */

#define test_assert_bit_set(a, bit)     weston_assert_bit_is_set(NULL, a, bit)
#define test_assert_bit_not_set(a, bit) weston_assert_bit_is_not_set(NULL, a, bit)
#define test_assert_errno(a)            test_assert_int_eq(a, errno)
#define test_assert_enum(a, b)          test_assert_u64_eq(a, b)
#define test_assert_not_reached(reason) weston_assert_not_reached(NULL, reason)

#endif /* _WESTON_TEST_ASSERT_H_ */

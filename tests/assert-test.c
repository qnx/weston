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

#include "config.h"

#include <stdlib.h>

#include "weston-test-runner.h"
#include "weston-test-assert.h"

static void
abort_if_not(bool cond)
{
	if (!cond)
		abort();
}

enum my_enum {
	MY_ENUM_A,
	MY_ENUM_B,
};

struct my_type {
	int x;
	float y;
};

/* Demonstration of custom type comparison */
static int
my_type_cmp(const struct my_type *a, const struct my_type *b)
{
	if (a->x < b->x)
		return -1;
	if (a->x > b->x)
		return 1;
	if (a->y < b->y)
		return -1;
	if (a->y > b->y)
		return 1;
	return 0;
}

#define weston_assert_my_type_lt(compositor, a, b) \
	weston_assert_fn_(compositor, my_type_cmp, a, b, const struct my_type *, "my_type %p", <)

TEST(asserts_custom)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	struct my_type a = { 1, 2.0 };
	struct my_type b = { 0, 2.0 };

	ret = weston_assert_my_type_lt(compositor, &b, &a);
	abort_if_not(ret);
	ret = weston_assert_my_type_lt(compositor, &a, &b);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_boolean)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	ret = weston_assert_true(compositor, false);
	abort_if_not(ret == false);
	ret = weston_assert_true(compositor, true);
	abort_if_not(ret);
	ret = weston_assert_false(compositor, true);
	abort_if_not(ret == false);
	ret = weston_assert_false(compositor, false);
	abort_if_not(ret);
	ret = weston_assert_true(compositor, true && false);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_pointer)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	ret = weston_assert_ptr_not_null(compositor, &ret);
	abort_if_not(ret);
	ret = weston_assert_ptr_not_null(compositor, NULL);
	abort_if_not(ret == false);

	ret = weston_assert_ptr_null(compositor, NULL);
	abort_if_not(ret);
	ret = weston_assert_ptr_null(compositor, &ret);
	abort_if_not(ret == false);

	ret = weston_assert_ptr_eq(compositor, &ret, &ret);
	abort_if_not(ret);
	ret = weston_assert_ptr_eq(compositor, &ret, &ret + 1);
	abort_if_not(ret == false);

	ret = weston_assert_ptr_ne(compositor, &ret, &ret + 1);
	abort_if_not(ret);
	ret = weston_assert_ptr_ne(compositor, &ret, &ret);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_string)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	const char *nom = "bar";

	ret = weston_assert_str_eq(compositor, nom, "bar");
	abort_if_not(ret);
	ret = weston_assert_str_eq(compositor, nom, "baz");
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_bitmask)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	uint32_t bitfield = 0xffff;

	ret = weston_assert_bit_set(compositor, bitfield, 1ull << 2);
	abort_if_not(ret);
	ret = weston_assert_bit_set(compositor, bitfield, 1ull << 57);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_misc)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	ret = weston_assert_enum(compositor, MY_ENUM_A, MY_ENUM_A);
	abort_if_not(ret);
	ret = weston_assert_enum(compositor, MY_ENUM_A, MY_ENUM_B);
	abort_if_not(ret == false);

	/* weston_assert_not_reached is a bit awkward to test, so let's skip */

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_floating_point)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	/* Float asserts. */

	float sixteen = 16.0;
	ret = weston_assert_f32_eq(compositor, sixteen, 16.000001);
	abort_if_not(ret == false);
	ret = weston_assert_f32_eq(compositor, sixteen, 16);
	abort_if_not(ret);

	ret = weston_assert_f32_ne(compositor, sixteen, 16.000001);
	abort_if_not(ret);
	ret = weston_assert_f32_ne(compositor, sixteen, sixteen);
	abort_if_not(ret == false);

	ret = weston_assert_f32_gt(compositor, 16.000001, sixteen);
	abort_if_not(ret);
	ret = weston_assert_f32_gt(compositor, sixteen, 16.000001);
	abort_if_not(ret == false);

	ret = weston_assert_f32_ge(compositor, sixteen, sixteen);
	abort_if_not(ret);
	ret = weston_assert_f32_ge(compositor, 16.000001, sixteen);
	abort_if_not(ret);
	ret = weston_assert_f32_ge(compositor, sixteen, 16.000001);
	abort_if_not(ret == false);

	ret = weston_assert_f32_lt(compositor, sixteen, 16.000001);
	abort_if_not(ret);
	ret = weston_assert_f32_lt(compositor, 16.000001, sixteen);
	abort_if_not(ret == false);

	ret = weston_assert_f32_le(compositor, sixteen, sixteen);
	abort_if_not(ret);
	ret = weston_assert_f32_le(compositor, sixteen, 16.000001);
	abort_if_not(ret);
	ret = weston_assert_f32_le(compositor, 16.000001, sixteen);
	abort_if_not(ret == false);

	/* Double asserts. */

	double fifteen = 15.0;
	ret = weston_assert_f64_eq(compositor, fifteen, 15.000001);
	abort_if_not(ret == false);
	ret = weston_assert_f64_eq(compositor, fifteen, 15);
	abort_if_not(ret);

	ret = weston_assert_f64_ne(compositor, fifteen, 15.000001);
	abort_if_not(ret);
	ret = weston_assert_f64_ne(compositor, fifteen, fifteen);
	abort_if_not(ret == false);

	ret = weston_assert_f64_gt(compositor, 15.000001, fifteen);
	abort_if_not(ret);
	ret = weston_assert_f64_gt(compositor, fifteen, 15.000001);
	abort_if_not(ret == false);

	ret = weston_assert_f64_ge(compositor, fifteen, fifteen);
	abort_if_not(ret);
	ret = weston_assert_f64_ge(compositor, 15.000001, fifteen);
	abort_if_not(ret);
	ret = weston_assert_f64_ge(compositor, fifteen, 15.000001);
	abort_if_not(ret == false);

	ret = weston_assert_f64_lt(compositor, fifteen, 15.000001);
	abort_if_not(ret);
	ret = weston_assert_f64_lt(compositor, 15.000001, fifteen);
	abort_if_not(ret == false);

	ret = weston_assert_f64_le(compositor, fifteen, fifteen);
	abort_if_not(ret);
	ret = weston_assert_f64_le(compositor, fifteen, 15.000001);
	abort_if_not(ret);
	ret = weston_assert_f64_le(compositor, 15.000001, fifteen);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_unsigned_int)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	/* uint8_t asserts. */

	ret = weston_assert_u8_eq(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u8_eq(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u8_ne(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u8_ne(compositor, 5, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u8_gt(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u8_gt(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u8_ge(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u8_ge(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u8_ge(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u8_lt(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u8_lt(compositor, 6, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u8_le(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u8_le(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u8_le(compositor, 6, 5);
	abort_if_not(ret == false);

	/* uint16_t asserts. */

	ret = weston_assert_u16_eq(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u16_eq(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u16_ne(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u16_ne(compositor, 5, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u16_gt(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u16_gt(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u16_ge(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u16_ge(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u16_ge(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u16_lt(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u16_lt(compositor, 6, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u16_le(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u16_le(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u16_le(compositor, 6, 5);
	abort_if_not(ret == false);

	/* uint32_t asserts. */

	ret = weston_assert_u32_eq(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u32_eq(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u32_ne(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u32_ne(compositor, 5, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u32_gt(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u32_gt(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u32_ge(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u32_ge(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u32_ge(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u32_lt(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u32_lt(compositor, 6, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u32_le(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u32_le(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u32_le(compositor, 6, 5);
	abort_if_not(ret == false);

	/* uint64_t asserts. */

	ret = weston_assert_u64_eq(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u64_eq(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u64_ne(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u64_ne(compositor, 5, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u64_gt(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u64_gt(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u64_ge(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_u64_ge(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u64_ge(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_u64_lt(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u64_lt(compositor, 6, 5);
	abort_if_not(ret == false);

	ret = weston_assert_u64_le(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_u64_le(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_u64_le(compositor, 6, 5);
	abort_if_not(ret == false);

	/* unsigned int asserts. */

	ret = weston_assert_uint_eq(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_uint_eq(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_uint_ne(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_uint_ne(compositor, 5, 5);
	abort_if_not(ret == false);

	ret = weston_assert_uint_gt(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_uint_gt(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_uint_ge(compositor, 6, 5);
	abort_if_not(ret);
	ret = weston_assert_uint_ge(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_uint_ge(compositor, 5, 6);
	abort_if_not(ret == false);

	ret = weston_assert_uint_lt(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_uint_lt(compositor, 6, 5);
	abort_if_not(ret == false);

	ret = weston_assert_uint_le(compositor, 5, 6);
	abort_if_not(ret);
	ret = weston_assert_uint_le(compositor, 5, 5);
	abort_if_not(ret);
	ret = weston_assert_uint_le(compositor, 6, 5);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_signed_int)
{
	/* Unused by the macros for now, so let's just use NULL. */
	struct weston_compositor *compositor = NULL;
	bool ret;

	/* int8_t asserts. */

	ret = weston_assert_s8_eq(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s8_eq(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s8_ne(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s8_ne(compositor, -5, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s8_gt(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s8_gt(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s8_ge(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s8_ge(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s8_ge(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s8_lt(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s8_lt(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s8_le(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s8_le(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s8_le(compositor, -5, -6);
	abort_if_not(ret == false);

	/* int16_t asserts. */

	ret = weston_assert_s16_eq(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s16_eq(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s16_ne(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s16_ne(compositor, -5, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s16_gt(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s16_gt(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s16_ge(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s16_ge(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s16_ge(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s16_lt(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s16_lt(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s16_le(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s16_le(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s16_le(compositor, -5, -6);
	abort_if_not(ret == false);

	/* int32_t asserts. */

	ret = weston_assert_s32_eq(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s32_eq(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s32_ne(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s32_ne(compositor, -5, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s32_gt(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s32_gt(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s32_ge(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s32_ge(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s32_ge(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s32_lt(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s32_lt(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s32_le(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s32_le(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s32_le(compositor, -5, -6);
	abort_if_not(ret == false);

	/* int64_t asserts. */

	ret = weston_assert_s64_eq(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s64_eq(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s64_ne(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s64_ne(compositor, -5, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s64_gt(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s64_gt(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s64_ge(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_s64_ge(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s64_ge(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_s64_lt(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s64_lt(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_s64_le(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_s64_le(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_s64_le(compositor, -5, -6);
	abort_if_not(ret == false);

	/* int asserts. */

	ret = weston_assert_int_eq(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_int_eq(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_int_ne(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_int_ne(compositor, -5, -5);
	abort_if_not(ret == false);

	ret = weston_assert_int_gt(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_int_gt(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_int_ge(compositor, -5, -6);
	abort_if_not(ret);
	ret = weston_assert_int_ge(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_int_ge(compositor, -6, -5);
	abort_if_not(ret == false);

	ret = weston_assert_int_lt(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_int_lt(compositor, -5, -6);
	abort_if_not(ret == false);

	ret = weston_assert_int_le(compositor, -6, -5);
	abort_if_not(ret);
	ret = weston_assert_int_le(compositor, -5, -5);
	abort_if_not(ret);
	ret = weston_assert_int_le(compositor, -5, -6);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

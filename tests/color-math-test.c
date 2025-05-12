/*
 * Copyright 2025 Collabora, Ltd.
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

#include <math.h>

#include "weston-test-client-helper.h"
#include "weston-test-assert.h"
#include "libweston/color.h"
#include "shared/helpers.h"

float lut_ascendent[] =
	{0.0, 2.0, 3.0, 6.0, 9.0, 12.0, 15.0, 16.0, 20.0, 25.0};

float lut_descendent[] =
	{25.0, 20.0, 16.0, 15.0, 12.0, 9.0, 6.0, 3.0, 2.0, 0.0};

struct test_case {
	float *lut;
	uint32_t len_lut;
	float val;
	uint32_t index_A;
	uint32_t index_B;
};

struct test_case tests[] = {
	{
		/* Value at the extreme left. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 0.0f,
		.index_A = 0,
		.index_B = 1,
	},
	{
		/* Value at the extreme right. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 25.0f,
		.index_A = 8,
		.index_B = 9,
	},
	{
		/* Just a value that is present in the LUT. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 6.0f,
		.index_A = 2,
		.index_B = 3,
	},
	{
		/* Value not present on LUT. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 10.0f,
		.index_A = 4,
		.index_B = 5,
	},
	{
		/* Another value not present on LUT. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 1.0f,
		.index_A = 0,
		.index_B = 1,
	},
	{
		/* Another value not present on LUT. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 23.0f,
		.index_A = 8,
		.index_B = 9,
	},
	{
		/* Value that would be before the extreme left, but not present. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = -1.0f,
		.index_A = 0,
		.index_B = 1,
	},
	{
		/* Value that would be after the extreme right, but not present. */
		.lut = lut_ascendent,
		.len_lut = ARRAY_LENGTH(lut_ascendent),
		.val = 26.0f,
		.index_A = 8,
		.index_B = 9,
	},
	{
		/* Value at the extreme left. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 25.0f,
		.index_A = 0,
		.index_B = 1,
	},
	{
		/* Value at the extreme right. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 0.0f,
		.index_A = 8,
		.index_B = 9,
	},
	{
		/* Just a value that is present in the LUT. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 6.0f,
		.index_A = 5,
		.index_B = 6,
	},
	{
		/* Value not present on LUT. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 10.0f,
		.index_A = 4,
		.index_B = 5,
	},
	{
		/* Another value not present on LUT. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 1.0f,
		.index_A = 8,
		.index_B = 9,
	},
	{
		/* Another value not present on LUT. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 23.0f,
		.index_A = 0,
		.index_B = 1,
	},
	{
		/* Value that would be before the extreme right, but not present. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = -1.0f,
		.index_A = 8,
		.index_B = 9,
	},
	{
		/* Value that would be after the extreme left, but not present. */
		.lut = lut_descendent,
		.len_lut = ARRAY_LENGTH(lut_descendent),
		.val = 26.0f,
		.index_A = 0,
		.index_B = 1,
	},
};

TEST(find_neighbors_test)
{
	struct weston_compositor *compositor = NULL;
	unsigned int i;
	uint32_t index_neigh_A, index_neigh_B;

	for (i = 0; i < ARRAY_LENGTH(tests); i++) {
		find_neighbors(compositor, tests[i].len_lut, tests[i].lut,
			       tests[i].val, &index_neigh_A, &index_neigh_B);

		test_assert_u32_eq(index_neigh_A, tests[i].index_A);
		test_assert_u32_eq(index_neigh_B, tests[i].index_B);
	}

	return RESULT_OK;
}

static float
sample_power_22(float input)
{
	return powf(input, 2.2f);
}

static float
sample_power_22_complement(float input)
{
	return 1.0f - powf(input, 2.2f);
}

static enum test_result_code
test_inverse_lut_with_curve(float (*sample_fn)(float))
{
	const uint32_t len_lut = 1024;
	struct weston_compositor *compositor = NULL;
	float lut[len_lut];
	uint32_t divider = len_lut - 1;
	float input, output;
	unsigned int i;

	/* Create 1D LUT using function sample_fn. */
	for (i = 0; i < len_lut; i++) {
		input = (float)i / divider;
		lut[i] = sample_fn(input);
	}

	/**
	 * Sample data (dividing i by a prime number, 79) not well behaved on
	 * purpose. Sample that with the LUT, then its inverse, and ensure that
	 * the result is the same as evaluating with an identity curve.
	 */
	for (i = 0; i < 80; i++) {
		input = i / 79.0f;
		output = sample_fn(input);
		output = weston_inverse_evaluate_lut1d(compositor,
						       len_lut, lut,
						       output);
		test_assert_f32_lt(fabs(input - output), 1e-3);
	}

	return RESULT_OK;
}

TEST(inverse_lut)
{
	return test_inverse_lut_with_curve(sample_power_22);
}

TEST(inverse_lut_descendant)
{
	return test_inverse_lut_with_curve(sample_power_22_complement);
}

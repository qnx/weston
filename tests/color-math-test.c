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
#include <libweston/colorimetry.h>

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

struct npm_test_case {
	struct weston_color_gamut gm;
	struct weston_mat3f expected;
};

/*
 * The reference data is from https://www.colour-science.org/ Python library.
 * >>> import colour
 * We use the "Derived NPM" as the expected matrix.
 */
static const struct npm_test_case npm_test_cases[] = {
	{
		/* >>> print(colour.RGB_COLOURSPACES['sRGB']) */
		.gm = {
			.primary = { { 0.64, 0.33 }, /* RGB order */
				     { 0.30, 0.60 },
				     { 0.15, 0.06 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
		.expected = WESTON_MAT3F(
			0.4123908,   0.35758434,  0.18048079,
			0.21263901,  0.71516868,  0.07219232,
			0.01933082,  0.11919478,  0.95053215
		),
	},
	{
		/* >>> print(colour.RGB_COLOURSPACES['Adobe RGB (1998)']) */
		.gm = {
			.primary = { { 0.64, 0.33 }, /* RGB order */
				     { 0.21, 0.71 },
				     { 0.15, 0.06 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
		.expected = WESTON_MAT3F(
			0.57666904,  0.18555824,  0.18822865,
			0.29734498,  0.62736357,  0.07529146,
			0.02703136,  0.07068885,  0.99133754
		),
	},
	{
		/* >>> print(colour.RGB_COLOURSPACES['ITU-R BT.2020']) */
		.gm = {
			.primary = { { 0.708, 0.292 }, /* RGB order */
				     { 0.170, 0.797 },
				     { 0.131, 0.046 },
			},
			.white_point = { 0.3127, 0.3290 },
		},
		.expected = WESTON_MAT3F(
			6.36958048e-01,   1.44616904e-01,   1.68880975e-01,
			2.62700212e-01,   6.77998072e-01,   5.93017165e-02,
			4.99410657e-17,   2.80726930e-02,   1.06098506e+00
		),
	},
	{
		/* >>> print(colour.RGB_COLOURSPACES['NTSC (1953)']) */
		.gm = {
			.primary = { { 0.67, 0.33 }, /* RGB order */
				     { 0.21, 0.71 },
				     { 0.14, 0.08 },
			},
			.white_point = { 0.31006, 0.31616 },
		},
		.expected = WESTON_MAT3F(
			 6.06863809e-01,   1.73507281e-01,   2.00334881e-01,
			 2.98903070e-01,   5.86619855e-01,   1.14477075e-01,
			-5.02801622e-17,   6.60980118e-02,   1.11615148e+00
		),
	},
};

/** Return the equivalence precision in bits
 *
 * Infinity norm of the residual is our measure.
 * See https://gitlab.freedesktop.org/pq/fourbyfour/-/blob/master/README.d/precision_testing.md
 */
static float
diff_precision(struct weston_mat3f M, struct weston_mat3f ref)
{
	struct weston_mat3f E = weston_m3f_sub_m3f(M, ref);
	return -log2f(weston_m3f_inf_norm(E));
}

/*
 * Test that weston_normalized_primary_matrix() produces known-good results
 * for NPM, an that the NPM⁻¹ is actually the inverse matrix.
 */
TEST_P(npm, npm_test_cases)
{
	const float precision_bits = 21;
	const struct npm_test_case *t = data;
	struct weston_mat3f npm;
	struct weston_mat3f npm_inv;
	struct weston_mat3f roundtrip;

	test_assert_true(weston_normalized_primary_matrix_init(&npm, &t->gm, WESTON_NPM_FORWARD));
	test_assert_f32_ge(diff_precision(npm, t->expected), precision_bits);

	test_assert_true(weston_normalized_primary_matrix_init(&npm_inv, &t->gm, WESTON_NPM_INVERSE));
	roundtrip = weston_m3f_mul_m3f(npm_inv, npm);
	test_assert_f32_ge(diff_precision(roundtrip, WESTON_MAT3F_IDENTITY), precision_bits);

	return RESULT_OK;
}

/* https://www.color.org/chadtag.xalter */
TEST(bradform_adaptation_D65_D50)
{
	const struct weston_CIExy D65 = { 0.3127, 0.3290 };
	const struct weston_CIExy D50 = { 0.3457, 0.3585 };
	const struct weston_mat3f ref = WESTON_MAT3F(
		1.04790738171017, 0.0229333845542104, -0.0502016347980104,
		0.0296059594177168, 0.990456039910785, -0.01707552919587,
		-0.00924679432678241, 0.0150626801401488, 0.751791232609078
	);
	struct weston_mat3f M;

	M = weston_bradford_adaptation(D65, D50);
	test_assert_f32_ge(diff_precision(M, ref), 13);

	return RESULT_OK;
}

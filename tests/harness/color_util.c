/*
 * Copyright 2020 Collabora, Ltd.
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <libweston/linalg-3.h>
#include "color_util.h"
#include "weston-test-runner.h"
#include "weston-test-assert.h"
#include "shared/helpers.h"

static_assert(sizeof(struct color_float) == 4 * sizeof(float),
	      "unexpected padding in struct color_float");
static_assert(offsetof(struct color_float, r) == offsetof(struct color_float, rgb[COLOR_CHAN_R]),
	      "unexpected offset for struct color_float::r");
static_assert(offsetof(struct color_float, g) == offsetof(struct color_float, rgb[COLOR_CHAN_G]),
	      "unexpected offset for struct color_float::g");
static_assert(offsetof(struct color_float, b) == offsetof(struct color_float, rgb[COLOR_CHAN_B]),
	      "unexpected offset for struct color_float::b");

struct tone_curve_info {
	enum transfer_fn fn;
	enum transfer_fn inv_fn;
	const char *name;
	float (*apply)(float);

	/* LCMS2 API curve parameters */
	struct {
		int type;
		double param[5];
	} lcms2;
};

/**
 * NaN comes out as is
 *This function is not intended for hiding NaN.
 */
static float
ensure_unit_range(float v)
{
	const float tol = 1e-5f;
	const float lim_lo = -tol;
	const float lim_hi = 1.0f + tol;

	test_assert_f32_ge(v, lim_lo);
	if (v < 0.0f)
		return 0.0f;
	test_assert_f32_le(v, lim_hi);
	if (v > 1.0f)
		return 1.0f;
	return v;
}

static float
sRGB_two_piece(float e)
{
	e = ensure_unit_range(e);
	if (e <= 0.04045)
		return e / 12.92;
	else
		return pow((e + 0.055) / 1.055, 2.4);
}

static float
sRGB_two_piece_inv(float o)
{
	o = ensure_unit_range(o);
	if (o <= 0.04045 / 12.92)
		return o * 12.92;
	else
		return pow(o, 1.0 / 2.4) * 1.055 - 0.055;
}

static float
AdobeRGB_EOTF(float e)
{
	e = ensure_unit_range(e);
	return pow(e, 563./256.);
}

static float
AdobeRGB_EOTF_inv(float o)
{
	o = ensure_unit_range(o);
	return pow(o, 256./563.);
}

static float
Power2_2_EOTF(float e)
{
	e = ensure_unit_range(e);
	return pow(e, 2.2);
}

static float
Power2_2_EOTF_inv(float o)
{
	o = ensure_unit_range(o);
	return pow(o, 1./2.2);
}

static float
Power2_4_EOTF(float e)
{
	e = ensure_unit_range(e);
	return pow(e, 2.4);
}

static float
Power2_4_EOTF_inv(float o)
{
	o = ensure_unit_range(o);
	return pow(o, 1./2.4);
}

static float
identity(float v)
{
	return v;
}

static const struct tone_curve_info tone_curves[] = {
	[TRANSFER_FN_IDENTITY] = {
		.fn = TRANSFER_FN_IDENTITY,
		.name = "identity",
		.inv_fn = TRANSFER_FN_IDENTITY,
		.apply = identity,
	},
	[TRANSFER_FN_SRGB] = {
		.fn = TRANSFER_FN_SRGB,
		.name = "sRGB two-piece",
		.inv_fn = TRANSFER_FN_SRGB_INVERSE,
		.apply = sRGB_two_piece,
		.lcms2 = { 4, { 2.4, 1. / 1.055, 0.055 / 1.055, 1. / 12.92, 0.04045 }},
	},
	[TRANSFER_FN_SRGB_INVERSE] = {
		.fn = TRANSFER_FN_SRGB_INVERSE,
		.name = "inverse sRGB two-piece",
		.inv_fn = TRANSFER_FN_SRGB,
		.apply = sRGB_two_piece_inv,
		.lcms2 = { -4, { 2.4, 1. / 1.055, 0.055 / 1.055, 1. / 12.92, 0.04045 }},
	},
	[TRANSFER_FN_ADOBE_RGB_EOTF] = {
		.fn = TRANSFER_FN_ADOBE_RGB_EOTF,
		.name = "AdobeRGB EOTF",
		.inv_fn = TRANSFER_FN_ADOBE_RGB_EOTF_INVERSE,
		.apply = AdobeRGB_EOTF,
		.lcms2 = { 1, { 563./256., 0.0, 0.0, 0.0 , 0.0 }},
	},
	[TRANSFER_FN_ADOBE_RGB_EOTF_INVERSE] = {
		.fn = TRANSFER_FN_ADOBE_RGB_EOTF_INVERSE,
		.name = "inverse AdobeRGB EOTF",
		.inv_fn = TRANSFER_FN_ADOBE_RGB_EOTF,
		.apply = AdobeRGB_EOTF_inv,
		.lcms2 = { -1, { 563./256., 0.0, 0.0, 0.0 , 0.0 }},
	},
	[TRANSFER_FN_POWER2_2_EOTF] = {
		.fn = TRANSFER_FN_POWER2_2_EOTF,
		.name = "power 2.2",
		.inv_fn = TRANSFER_FN_POWER2_2_EOTF_INVERSE,
		.apply = Power2_2_EOTF,
		.lcms2 = { 1, { 2.2, 0.0, 0.0, 0.0 , 0.0 }},
	},
	[TRANSFER_FN_POWER2_2_EOTF_INVERSE] = {
		.fn = TRANSFER_FN_POWER2_2_EOTF_INVERSE,
		.name = "inverse power 2.2",
		.inv_fn = TRANSFER_FN_POWER2_2_EOTF,
		.apply = Power2_2_EOTF_inv,
		.lcms2 = { -1, { 2.2, 0.0, 0.0, 0.0 , 0.0 }},
	},
	[TRANSFER_FN_POWER2_4_EOTF] = {
		.fn = TRANSFER_FN_POWER2_4_EOTF,
		.name = "power 2.4",
		.inv_fn = TRANSFER_FN_POWER2_4_EOTF_INVERSE,
		.apply = Power2_4_EOTF,
		.lcms2 = { 1, { 2.4, 0.0, 0.0, 0.0 , 0.0 }},
	},
	[TRANSFER_FN_POWER2_4_EOTF_INVERSE] = {
		.fn = TRANSFER_FN_POWER2_4_EOTF_INVERSE,
		.name = "inverse power 2.4",
		.inv_fn = TRANSFER_FN_POWER2_4_EOTF,
		.apply = Power2_4_EOTF_inv,
		.lcms2 = { -1, { 2.4, 0.0, 0.0, 0.0 , 0.0 }},
	},
};

static const struct tone_curve_info *
find_tone_curve_info(enum transfer_fn fn)
{
	const struct tone_curve_info *tc;

	test_assert_int_ge(fn, 0);
	test_assert_int_lt(fn, ARRAY_LENGTH(tone_curves));

	tc = &tone_curves[fn];
	test_assert_int_eq(fn, tc->fn);

	return tc;
}

void
find_tone_curve_type(enum transfer_fn fn, int *type, double params[5])
{
	const struct tone_curve_info *t;

	t = find_tone_curve_info(fn);
	test_assert_ptr_not_null(t);

	*type = t->lcms2.type;
	memcpy(params, t->lcms2.param, sizeof (t->lcms2.param));
}

enum transfer_fn
transfer_fn_invert(enum transfer_fn fn)
{
	return find_tone_curve_info(fn)->inv_fn;
}

const char *
transfer_fn_name(enum transfer_fn fn)
{
	return find_tone_curve_info(fn)->name;
}

float
apply_tone_curve(enum transfer_fn fn, float r)
{
	return find_tone_curve_info(fn)->apply(r);
}

struct color_float
a8r8g8b8_to_float(uint32_t v)
{
	struct color_float cf;

	cf.a = ((v >> 24) & 0xff) / 255.f;
	cf.r = ((v >> 16) & 0xff) / 255.f;
	cf.g = ((v >>  8) & 0xff) / 255.f;
	cf.b = ((v >>  0) & 0xff) / 255.f;

	return cf;
}

struct color_float
color_float_apply_curve(enum transfer_fn fn, struct color_float c)
{
	unsigned i;

	for (i = 0; i < COLOR_CHAN_NUM; i++)
		c.rgb[i] = apply_tone_curve(fn, c.rgb[i]);

	return c;
}

void
sRGB_linearize(struct color_float *cf)
{
	*cf = color_float_apply_curve(TRANSFER_FN_POWER2_2_EOTF, *cf);
}

void
sRGB_delinearize(struct color_float *cf)
{
	*cf = color_float_apply_curve(TRANSFER_FN_POWER2_2_EOTF_INVERSE, *cf);
}

struct color_float
color_float_unpremult(struct color_float in)
{
	static const struct color_float transparent = {
		.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f,
	};
	struct color_float out;
	int i;

	if (in.a == 0.0f)
		return transparent;

	for (i = 0; i < COLOR_CHAN_NUM; i++)
		out.rgb[i] = in.rgb[i] / in.a;
	out.a = in.a;
	return out;
}

/*
 * Returns the result of the matrix-vector multiplication mat * c.
 */
struct color_float
color_float_apply_matrix(struct weston_mat3f mat, struct color_float c)
{
	struct weston_vec3f v = weston_m3f_mul_v3f(mat, WESTON_VEC3F(c.r, c.g, c.b));

	return (struct color_float){
		.rgb = { v.r, v.g, v.b },
		.a = c.a,
	};
}

bool
should_include_vcgt(const double vcgt_exponents[COLOR_CHAN_NUM])
{
	unsigned int i;

	for (i = 0; i < COLOR_CHAN_NUM; i++)
		if (vcgt_exponents[i] == 0.0)
			return false;

	return true;
}

void
process_pixel_using_pipeline(enum transfer_fn pre_curve,
			     struct weston_mat3f mat,
			     enum transfer_fn post_curve,
			     const double vcgt_exponents[COLOR_CHAN_NUM],
			     const struct color_float *in,
			     struct color_float *out)
{
	struct color_float cf;
	unsigned i;

	cf = color_float_apply_curve(pre_curve, *in);
	cf = color_float_apply_matrix(mat, cf);
	cf = color_float_apply_curve(post_curve, cf);

	if (should_include_vcgt(vcgt_exponents))
		for (i = 0; i < COLOR_CHAN_NUM; i++)
			cf.rgb[i] = pow(cf.rgb[i], vcgt_exponents[i]);

	*out = cf;
}

/** Update scalar statistics
 *
 * \param stat The statistics structure to update.
 * \param val A sample of the variable whose statistics you are collecting.
 * \param pos The "position" that generated the current value.
 *
 * Accumulates min, max, sum and count statistics with the given value.
 * Stores the position related to the current max and min each.
 *
 * To use this, declare a variable of type struct scalar_stat and
 * zero-initialize it. Repeatedly call scalar_stat_update() to accumulate
 * statistics. Then either directly read out what you are interested in from
 * the structure, or use the related accessor or printing functions.
 *
 * If you also want to collect a debug log of all calls to this function,
 * initialize the .dump member to a writable file handle. This is easiest
 * with fopen_dump_file(). Remember to fclose() the handle after you have
 * no more samples to add.
 */
void
scalar_stat_update(struct scalar_stat *stat,
		   double val,
		   const struct color_float *pos)
{
	if (stat->count == 0 || stat->min > val) {
		stat->min = val;
		stat->min_pos = *pos;
	}

	if (stat->count == 0 || stat->max < val) {
		stat->max = val;
		stat->max_pos = *pos;
	}

	stat->sum += val;
	stat->count++;

	if (stat->dump) {
		fprintf(stat->dump, "%.8g %.5g %.5g %.5g %.5g\n",
			val, pos->r, pos->g, pos->b, pos->a);
	}
}

/** Return the average of the previously seen values. */
float
scalar_stat_avg(const struct scalar_stat *stat)
{
	return stat->sum / stat->count;
}

/** Print scalar statistics with pos.r only */
void
scalar_stat_print_float(const struct scalar_stat *stat)
{
	testlog("    min %11.5g at %.5f\n", stat->min, stat->min_pos.r);
	testlog("    max %11.5g at %.5f\n", stat->max, stat->max_pos.r);
	testlog("    avg %11.5g\n", scalar_stat_avg(stat));
}

static void
print_stat_at_pos(const char *lim, double val, struct color_float pos, double scale)
{
	testlog("    %s %8.5f at rgb(%7.2f, %7.2f, %7.2f)\n",
		lim, val * scale, pos.r * scale, pos.g * scale, pos.b * scale);
}

static void
print_rgb_at_pos(const struct scalar_stat *stat, double scale)
{
	print_stat_at_pos("min", stat->min, stat->min_pos, scale);
	print_stat_at_pos("max", stat->max, stat->max_pos, scale);
	testlog("    avg %8.5f\n", scalar_stat_avg(stat) * scale);
}

/** Print min/max/avg for each R/G/B/two-norm statistics
 *
 * \param stat The statistics to print.
 * \param title A custom title to include in the heading which shall be printed
 * like "%s error statistics:".
 * \param scaling_bits Determines a scaling factor for the printed numbers as
 * 2^scaling_bits - 1.
 *
 * Usually RGB values are stored in unsigned integer representation. 8-bit
 * integer range is [0, 255] for example. Passing scaling_bits=8 will multiply
 * all values (differences, two-norm errors, and position values) by
 * 2^8 - 1 = 255. This makes interpreting the recorded errors more intuitive
 * through the integer encoding precision perspective.
 */
void
rgb_diff_stat_print(const struct rgb_diff_stat *stat,
		    const char *title, unsigned scaling_bits)
{
	const char *const chan_name[COLOR_CHAN_NUM] = { "r", "g", "b" };
	float scale = exp2f(scaling_bits) - 1.0f;
	unsigned i;

	test_assert_uint_gt(scaling_bits, 0);

	testlog("%s error statistics, %u samples, value range 0.0 - %.1f:\n",
		title, stat->two_norm.count, scale);
	for (i = 0; i < COLOR_CHAN_NUM; i++) {
		testlog("  ch %s (signed):\n", chan_name[i]);
		print_rgb_at_pos(&stat->rgb[i], scale);
	}
	testlog("  rgb two-norm:\n");
	print_rgb_at_pos(&stat->two_norm, scale);
}

/** Update RGB difference statistics
 *
 * \param stat The statistics structure to update.
 * \param ref The reference color to compare to.
 * \param val The color produced by the algorithm under test; a sample.
 * \param pos The position to be recorded with extremes.
 *
 * Computes the RGB difference by subtracting the reference color from the
 * sample. This signed difference is tracked separately for each color channel
 * in a scalar_stat to find the min, max, and average signed difference. The
 * two-norm (Euclidean length) of the RGB difference vector is tracked in
 * another scalar_stat.
 *
 * The position is stored separately for each of the eight min/max
 * R/G/B/two-norm values recorded. A good way to use position is to record
 * the algorithm input color.
 *
 * To use this, declare a variable of type struct rgb_diff_stat and
 * zero-initalize it. Repeatedly call rgb_diff_stat_update() to accumulate
 * statistics. Then either directly read out what you are interested in from
 * the structure or use rgb_diff_stat_print().
 *
 * If you also want to collect a debug log of all calls to this function,
 * initialize the .dump member to a writable file handle. This is easiest
 * with fopen_dump_file(). Remember to fclose() the handle after you have
 * no more samples to add.
 */
void
rgb_diff_stat_update(struct rgb_diff_stat *stat,
		     const struct color_float *ref,
		     const struct color_float *val,
		     const struct color_float *pos)
{
	unsigned i;
	double ssd = 0.0;
	double diff[COLOR_CHAN_NUM];
	double two_norm;

	for (i = 0; i < COLOR_CHAN_NUM; i++) {
		diff[i] = val->rgb[i] - ref->rgb[i];

		scalar_stat_update(&stat->rgb[i], diff[i], pos);
		ssd += diff[i] * diff[i];
	}
	two_norm = sqrt(ssd);

	scalar_stat_update(&stat->two_norm, two_norm, pos);

	if (stat->dump) {
		fprintf(stat->dump, "%.8g %.8g %.8g %.8g %.5g %.5g %.5g %.5g\n",
			two_norm,
			diff[COLOR_CHAN_R], diff[COLOR_CHAN_G], diff[COLOR_CHAN_B],
			pos->r, pos->g, pos->b, pos->a);
	}
}

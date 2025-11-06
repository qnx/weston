/*
 * Copyright 2025-2026 Collabora, Ltd.
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

#include "color.h"
#include "color-properties.h"
#include "color-operations.h"

#include "shared/helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"

static float
linpow(float x, const union weston_color_curve_parametric_chan_data *p)
{
	/* See WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW for details about LINPOW. */
	/* LINPOW uses mirroring for negative input values. */
	float abs_x = fabsf(x);

	if (abs_x >= p->d)
		return copysign(pow((p->a * abs_x) + p->b, p->g), x);

	return p->c * x;
}

static void
sample_linpow(const union weston_color_curve_parametric_data *p,
	      bool clamp_input, const struct weston_vec3f *in,
	      struct weston_vec3f *out, size_t len)
{
	struct weston_vec3f tmp;
	size_t i;

	for (i = 0; i < len; i++) {
		tmp = in[i];
		if (clamp_input)
			tmp = weston_v3f_clamp(tmp, 0.0f, 1.0f);

		out[i] = WESTON_VEC3F(linpow(tmp.x, &p->chan[0]),
				      linpow(tmp.y, &p->chan[1]),
				      linpow(tmp.z, &p->chan[2]));
	}
}

static float
powlin(float x, const union weston_color_curve_parametric_chan_data *p)
{
	/* See WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN for details about POWLIN. */
	/* POWLIN uses mirroring for negative input values. */
	float abs_x = fabsf(x);

	if (abs_x >= p->d)
		return copysign(p->a * pow(abs_x, p->g) + p->b, x);

	return p->c * x;
}

static void
sample_powlin(const union weston_color_curve_parametric_data *p,
	      bool clamp_input, const struct weston_vec3f *in,
	      struct weston_vec3f *out, size_t len)
{
	struct weston_vec3f tmp;
	size_t i;

	for (i = 0; i < len; i++) {
		tmp = in[i];
		if (clamp_input)
			tmp = weston_v3f_clamp(tmp, 0.0f, 1.0f);

		out[i] = WESTON_VEC3F(powlin(tmp.x, &p->chan[0]),
				      powlin(tmp.y, &p->chan[1]),
				      powlin(tmp.z, &p->chan[2]));
	}
}

static void
sample_parametric(struct weston_compositor *compositor,
		  const struct weston_color_curve_parametric *param,
		  const struct weston_vec3f *in,
		  struct weston_vec3f *out,
		  size_t len)
{
	switch (param->type) {
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW:
		sample_linpow(&param->params, param->clamped_input, in, out, len);
		return;
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN:
		sample_powlin(&param->params, param->clamped_input, in, out, len);
		return;
	}

	weston_assert_not_reached(compositor, "unknown parametric color curve");
}

static float
perceptual_quantizer(float x)
{
	float aux, c1, c2, c3, m1_inv, m2_inv;

	m1_inv = 1.0 / 0.1593017578125;
	m2_inv = 1.0 / 78.84375;
	c1 = 0.8359375;
	c2 = 18.8515625;
	c3 = 18.6875;
	aux = pow(x, m2_inv);

	/* Normalized result. We don't take into consideration the luminance
	 * levels, as we don't receive the input as nits, but normalized in the
	 * [0, 1] range. */
	return pow(MAX(aux - c1, 0.0) / (c2 - c3 * aux), m1_inv);
}

static float
perceptual_quantizer_inverse(float x)
{
	float aux, c1, c2, c3, m1, m2;

	m1 = 0.1593017578125;
	m2 = 78.84375;
	c1 = 0.8359375;
	c2 = 18.8515625;
	c3 = 18.6875;
	aux = pow(x, m1);

	/* Normalized result. We don't take into consideration the luminance
	 * levels, as we don't receive the input as nits, but normalized in the
	 * [0, 1] range. */
	return pow((c1 + c2 * aux) / (1.0 + c3 * aux), m2);
}

static void
sample_pq(enum weston_tf_direction tf_direction,
	  const struct weston_vec3f *in,
	  struct weston_vec3f *out,
	  size_t len)
{
	struct weston_vec3f tmp;
	size_t i;

	switch (tf_direction) {
	case WESTON_FORWARD_TF:
		for (i = 0; i < len; i++) {
			tmp = weston_v3f_clamp(in[i], 0.0f, 1.0f);
			out[i] = WESTON_VEC3F(perceptual_quantizer(tmp.x),
					      perceptual_quantizer(tmp.y),
					      perceptual_quantizer(tmp.z));
		}
		break;
	case WESTON_INVERSE_TF:
		for (i = 0; i < len; i++) {
			tmp = weston_v3f_clamp(in[i], 0.0f, 1.0f);
			out[i] = WESTON_VEC3F(perceptual_quantizer_inverse(tmp.x),
					      perceptual_quantizer_inverse(tmp.y),
					      perceptual_quantizer_inverse(tmp.z));
		}
		break;
	}
}

/**
 * Evaluate a color curve on an array
 *
 * This handles the parametric curves (LINPOW, POWLIN, etc) and enumerated color
 * curves. Others result in failure.
 *
 * @param compositor The Weston compositor
 * @param curve The color curve to evaluate
 * @param in The input array of length @c len .
 * @param out The output array of length @c len .
 * @param len The in and out arrays' length.
 */
WL_EXPORT void
weston_color_curve_sample(struct weston_compositor *compositor,
			  const struct weston_color_curve *curve,
			  const struct weston_vec3f *in,
			  struct weston_vec3f *out,
			  size_t len)
{
	struct weston_color_curve_parametric parametric;
	bool ret;

	switch(curve->type) {
	case WESTON_COLOR_CURVE_TYPE_ENUM:
		/**
		 * If the TF of the enum curve is implemented, sample from that.
		 * Otherwise, fallback to a parametric curve and we'll handle
		 * that below.
		 */
		switch (curve->u.enumerated.tf.info->tf) {
		case WESTON_TF_ST2084_PQ:
			sample_pq(curve->u.enumerated.tf_direction, in, out, len);
			return;
		default:
			ret = weston_color_curve_enum_get_parametric(compositor,
								     &curve->u.enumerated,
								     &parametric);
			weston_assert_true(compositor, ret);
			sample_parametric(compositor, &parametric, in, out, len);
			return;
		}
	case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
		sample_parametric(compositor, &curve->u.parametric, in, out, len);
		return;
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		weston_assert_not_reached(compositor,
					  "no need to sample identity");
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		weston_assert_not_reached(compositor,
					  "function does not handle LUT 3x1D");
	}

	weston_assert_not_reached(compositor, "unknown color curve");
}

/** Reduce a color transform into a curve
 *
 * @param xform The color transform to inspect.
 * @return A pointer to a curve step in the given color transform,
 * or NULL if the whole color transform cannot be reduced into one curve step.
 */
WL_EXPORT const struct weston_color_curve *
weston_color_transform_as_single_curve(const struct weston_color_transform *xform)
{
	const struct weston_color_curve *curve = NULL;

	if (!xform->steps_valid)
		return NULL;

	if (xform->pre_curve.type != WESTON_COLOR_CURVE_TYPE_IDENTITY)
		curve = &xform->pre_curve;

	if (xform->mapping.type != WESTON_COLOR_MAPPING_TYPE_IDENTITY)
		return NULL;

	if (curve) {
		if (xform->post_curve.type != WESTON_COLOR_CURVE_TYPE_IDENTITY)
			return NULL;
	} else {
		curve = &xform->post_curve;
	}

	return curve;
}

static bool
invert_linpow(union weston_color_curve_parametric_chan_data *inv,
	      const union weston_color_curve_parametric_chan_data *orig)
{
	/*
	 * LINPOW is defined as:
	 * y = (a * x + b) ^ g | x >= d
	 * y = c * x           | 0 <= x < d
	 *
	 * First, compute y for the cross-over point x=d, get
	 * y1 = c * d and y1 = (a * d + b) ^ g. If y1 and y2 are not equal,
	 * the curve is not continuous, and cannot be inverted. Since they must
	 * be equal, choose the cross-over point as y = c * d.
	 *
	 * Solve x from the first equation:
	 * x = (y ^ (1/g) - b) / a = 1/a * y ^ (1/g) - b/a
	 *
	 * Solve x from the second equation:
	 * x = y / c
	 *
	 * The result can be parametrized into POWLIN.
	 */

	/* Ensure the inequalities do not need reversing, and invertibility */
	if (orig->c < 1e-6f)
		return false;

	/* Invertibility conditions */
	if (fabsf(orig->a) < 1e-6f)
		return false;

	if (fabsf(orig->g) < 1e-6f)
		return false;

	/* Continuity condition */
	float y1 = orig->c * orig->d;
	float y2 = powf(orig->a * orig->d + orig->b, orig->g);
	if (!(fabsf(y1 - y2) < 1e-5))
		return false;

	/* inv is a POWLIN curve, orig is LINPOW */
	inv->a = 1.0f / orig->a;
	inv->b = -orig->b / orig->a;
	inv->c = 1.0f / orig->c;
	inv->d = orig->c * orig->d;
	inv->g = 1.0f / orig->g;

	return true;
}

static bool
invert_powlin(union weston_color_curve_parametric_chan_data *inv,
	      const union weston_color_curve_parametric_chan_data *orig)
{
	/*
	 * POWLIN is defined as:
	 * y = a * x ^ g + b | x >= d
	 * y = c * x         | 0 <= x < d
	 *
	 * First, compute y for the cross-over point x=d, get
	 * y1 = c * d and y1 = a * d ^ g + b. If y1 and y2 are not equal,
	 * the curve is not continuous, and cannot be inverted. Since they must
	 * be equal, choose the cross-over point as y = c * d.
	 *
	 * Solve x from the first equation:
	 * x = ((y - b) / a) ^ (1/g) = (1/a * y - b/a) ^ (1/g)
	 *
	 * Solve x from the second equation:
	 * x = y / c
	 *
	 * The result can be parametrized into LINPOW.
	 */

	/* Ensure the inequalities do not need reversing, and invertibility */
	if (orig->c < 1e-6f)
		return false;

	/* Invertibility conditions */
	if (fabsf(orig->a) < 1e-6f)
		return false;

	if (fabsf(orig->g) < 1e-6f)
		return false;

	/* Continuity condition */
	float y1 = orig->c * orig->d;
	float y2 = orig->a * powf(orig->d, orig->g) + orig->b;
	if (!(fabsf(y1 - y2) < 1e-5))
		return false;

	/* inv is a LINPOW curve, orig is POWLIN */
	inv->a = 1.0f / orig->a;
	inv->b = -orig->b / orig->a;
	inv->c = 1.0f / orig->c;
	inv->d = orig->c * orig->d;
	inv->g = 1.0f / orig->g;

	return true;
}

static bool
weston_color_curve_parametric_inverse(struct weston_color_curve_parametric *inv,
				      const struct weston_color_curve_parametric *orig)
{
	unsigned i;

	/*
	 * Just an assumption that for clamped curves the domain equals range.
	 * Would be difficult to express otherwise.
	 */
	inv->clamped_input = orig->clamped_input;

	switch (orig->type) {
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW:
		inv->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN;
		for (i = 0; i < ARRAY_LENGTH(inv->params.chan); i++) {
			if (!invert_linpow(&inv->params.chan[i], &orig->params.chan[i]))
				return false;
		}
		break;
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN:
		inv->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
		for (i = 0; i < ARRAY_LENGTH(inv->params.chan); i++) {
			if (!invert_powlin(&inv->params.chan[i], &orig->params.chan[i]))
				return false;
		}
		break;
	}

	return true;
}

/** Invert an enumerated or parametric curve
 *
 * @param curve The curve to be inverted, must be of type identity, enum or
 * parametric. LUT_3x1D will fail.
 * @return A newly malloc'd curve, or NULL on failure to invert the given curve.
 * The resulting curve is of the same type as the given curve. The caller is
 * responsible for freeing the returned pointer.
 */
WL_EXPORT struct weston_color_curve *
weston_color_curve_create_inverse(const struct weston_color_curve *curve)
{
	struct weston_color_curve *invcurve;

	invcurve = xzalloc(sizeof *invcurve);

	switch (curve->type) {
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		invcurve->type = WESTON_COLOR_CURVE_TYPE_IDENTITY;
		return invcurve;
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		/* These may not be possible to invert precisely enough */
		break;
	case WESTON_COLOR_CURVE_TYPE_ENUM:
		invcurve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		invcurve->u.enumerated.tf = curve->u.enumerated.tf;
		switch (curve->u.enumerated.tf_direction) {
		case WESTON_FORWARD_TF:
			invcurve->u.enumerated.tf_direction = WESTON_INVERSE_TF;
			return invcurve;
		case WESTON_INVERSE_TF:
			invcurve->u.enumerated.tf_direction = WESTON_FORWARD_TF;
			return invcurve;
		}
		break;
	case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
		invcurve->type = WESTON_COLOR_CURVE_TYPE_PARAMETRIC;
		if (weston_color_curve_parametric_inverse(&invcurve->u.parametric,
							  &curve->u.parametric))
			return invcurve;
		break;
	}

	free(invcurve);
	return NULL;
}

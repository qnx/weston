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

#include "color.h"
#include "color-properties.h"
#include "color-operations.h"

#include "shared/helpers.h"
#include "shared/weston-assert.h"

/**
 * Clamp value to [0.0, 1.0], except pass NaN through.
 *
 * This function is not intended for hiding NaN.
 */
static float
ensure_unorm(float v)
{
	if (v <= 0.0f)
		return 0.0f;
	if (v > 1.0f)
		return 1.0f;
	return v;
}

static float
linpow(float x, const union weston_color_curve_parametric_chan_data *p)
{
	/* See WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW for details about LINPOW. */

	if (x >= p->d)
		return pow((p->a * x) + p->b, p->g);

	return p->c * x;
}

static void
sample_linpow(const union weston_color_curve_parametric_chan_data *p,
	      uint32_t len, bool clamp_input, float *in, float *out)
{
	float x;
	unsigned int i;

	for (i = 0; i < len; i++) {
		x = in[i];
		if (clamp_input)
			x = ensure_unorm(x);

		/* LINPOW uses mirroring for negative input values. */
		if (x < 0.0)
			out[i] = -linpow(-x, p);
		else
			out[i] = linpow(x, p);
	}
}

static float
powlin(float x, const union weston_color_curve_parametric_chan_data *p)
{
	/* See WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN for details about POWLIN. */

	if (x >= p->d)
		return p->a * pow(x, p->g) + p->b;

	return p->c * x;
}

static void
sample_powlin(const union weston_color_curve_parametric_chan_data *p,
	      uint32_t len, bool clamp_input, float *in, float *out)
{
	float x;
	unsigned int i;

	for (i = 0; i < len; i++) {
		x = in[i];
		if (clamp_input)
			x = ensure_unorm(x);

		/* POWLIN uses mirroring for negative input values. */
		if (x < 0.0)
			out[i] = -powlin(-x, p);
		else
			out[i] = powlin(x, p);
	}
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
sample_pq(enum weston_tf_direction tf_direction, uint32_t ch, uint32_t len,
	  float *in, float *out)
{
	unsigned int i;
	float x;

	for (i = 0; i < len; i++) {
		/**
		 * PQ and inverse PQ are always clamped, undefined for values
		 * out of [0, 1] range.
		 */
		x = ensure_unorm(in[i]);

		if (tf_direction == WESTON_FORWARD_TF)
			out[i] = perceptual_quantizer(x);
		else
			out[i] = perceptual_quantizer_inverse(x);
	}
}

/**
* Given a color curve and a channel, sample an input.
*
* This handles the parametric curves (LINPOW, POWLIN, etc) and enumerated color
* curves. Others should result in failure.
*
* @param compositor The Weston compositor
* @param curve The color curve to be used to sample
* @param ch The curve color channel to sample from
* @param len The in and out arrays length
* @param in The input array to sample
* @param out The resulting array from sampling
* @returns True on success, false otherwise
*/
bool
weston_color_curve_sample(struct weston_compositor *compositor,
			  struct weston_color_curve *curve,
			  uint32_t ch, uint32_t len, float *in, float *out)
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
		switch(curve->u.enumerated.tf->tf) {
		case WESTON_TF_ST2084_PQ:
			sample_pq(curve->u.enumerated.tf_direction, ch, len, in, out);
			return true;
		default:
			ret = weston_color_curve_enum_get_parametric(compositor,
								     &curve->u.enumerated,
								     &parametric);
			if (!ret)
				return false;
			goto param;
		}
	case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
		/* Parametric curve, let's copy it and we'll handle that below. */
		parametric = curve->u.parametric;
		goto param;
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		weston_assert_not_reached(compositor,
					  "no need to sample identity");
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		weston_assert_not_reached(compositor,
					  "function does not handle LUT 3x1D");
	}

	weston_assert_not_reached(compositor, "unknown color curve");

param:
	/* Sample from parametric curves. */
	switch (parametric.type) {
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW:
		sample_linpow(&parametric.params.chan[ch],
			      len, parametric.clamped_input, in, out);
		return true;
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN:
		sample_powlin(&parametric.params.chan[ch],
			      len, parametric.clamped_input, in, out);
		return true;
	}

	weston_assert_not_reached(compositor, "unknown parametric color curve");
}

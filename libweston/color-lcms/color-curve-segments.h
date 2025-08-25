/*
 * Copyright 2023 Collabora, Ltd.
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

#ifndef COLOR_CURVE_SEGMENTS_H
#define COLOR_CURVE_SEGMENTS_H

#include <lcms2_plugin.h>

#include "color-lcms.h"

#if HAVE_CMS_GET_TONE_CURVE_SEGMENT

bool
get_parametric_curveset_params(struct weston_compositor *compositor,
			       _cmsStageToneCurvesData *trc_data,
			       cmsInt32Number *type,
			       float curveset_params[3][10],
			       bool *clamped_input);

void
curveset_print(cmsStage *stage, struct weston_log_scope *scope);

bool
are_curvesets_inverse(cmsStage *set_A, cmsStage *set_B);

bool
are_curves_equal(cmsToneCurve *curve_A, cmsToneCurve *curve_B);

cmsStage *
join_powerlaw_curvesets(cmsContext context_id,
			cmsToneCurve **set_A, cmsToneCurve **set_B);

# else /* HAVE_CMS_GET_TONE_CURVE_SEGMENT */

static inline bool
get_parametric_curveset_params(struct weston_compositor *compositor,
			       _cmsStageToneCurvesData *trc_data,
			       cmsInt32Number *type,
			       float curveset_params[3][10],
			       bool *clamped_input)
{
	return false;
}

static inline void
curveset_print(cmsStage *stage, struct weston_log_scope *scope)
{
	weston_log_scope_printf(scope, "%*scmsGetToneCurveSegment() symbol not " \
				       "found, so can't print curve set\n", 6, "");
}

static inline bool
are_curvesets_inverse(cmsStage *set_A, cmsStage *set_B)
{
	return false;
}

static inline bool
are_curves_equal(cmsToneCurve *curve_A, cmsToneCurve *curve_B)
{
	return false;
}

static inline cmsStage *
join_powerlaw_curvesets(cmsContext context_id,
			cmsToneCurve **set_A, cmsToneCurve **set_B)
{
	return NULL;
}

#endif /* HAVE_CMS_GET_TONE_CURVE_SEGMENT */

#endif /* COLOR_CURVE_SEGMENTS_H */

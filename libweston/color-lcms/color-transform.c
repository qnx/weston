/*
 * Copyright 2021-2025 Collabora, Ltd.
 * Copyright 2021-2022 Advanced Micro Devices, Inc.
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

#include <assert.h>
#include <libweston/libweston.h>
#include <libweston/linalg.h>
#include <lcms2_plugin.h>

#include "color.h"
#include "color-curve-segments.h"
#include "color-lcms.h"
#include "color-properties.h"
#include "shared/helpers.h"
#include "shared/string-helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"

/**
 * LCMS compares this parameter with the actual version of the LCMS and enforces
 * the minimum version is plug-in. If the actual LCMS version is lower than the
 * plug-in requirement the function cmsCreateContext is failed with plug-in as
 * parameter.
 */
#define REQUIRED_LCMS_VERSION 2120

/** Precision for detecting identity matrix */
#define MATRIX_PRECISION_BITS 12

/**
 * The method is used in linearization of an arbitrary color profile
 * when EOTF is retrieved we want to know a generic way to decide the number
 * of points
 */
unsigned int
cmlcms_reasonable_1D_points(void)
{
	return 1024;
}

static void
fill_in_curves(cmsToneCurve *curves[3], float *values, unsigned len)
{
	float *R_lut = values;
	float *G_lut = R_lut + len;
	float *B_lut = G_lut + len;
	unsigned i;
	cmsFloat32Number x;

	assert(len > 1);
	for (i = 0; i < 3; i++)
		assert(curves[i]);

	for (i = 0; i < len; i++) {
		x = (double)i / (len - 1);
		R_lut[i] = cmsEvalToneCurveFloat(curves[0], x);
		G_lut[i] = cmsEvalToneCurveFloat(curves[1], x);
		B_lut[i] = cmsEvalToneCurveFloat(curves[2], x);
	}
}

static void
cmlcms_fill_in_pre_curve(struct weston_color_transform *xform_base,
			 float *values, unsigned len)
{
	struct cmlcms_color_transform *xform = to_cmlcms_xform(xform_base);

	fill_in_curves(xform->pre_curve, values, len);
}

static void
cmlcms_fill_in_post_curve(struct weston_color_transform *xform_base,
			 float *values, unsigned len)
{
	struct cmlcms_color_transform *xform = to_cmlcms_xform(xform_base);

	fill_in_curves(xform->post_curve, values, len);
}

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

void
cmlcms_color_transform_destroy(struct cmlcms_color_transform *xform)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(xform->base.cm);

	wl_list_remove(&xform->link);

	cmsFreeToneCurveTriple(xform->pre_curve);

	cmsDeleteTransform(xform->cmap_3dlut);

	cmsFreeToneCurveTriple(xform->post_curve);

	if (xform->lcms_ctx)
		cmsDeleteContext(xform->lcms_ctx);

	unref_cprof(xform->search_key.input_profile);
	unref_cprof(xform->search_key.output_profile);

	weston_log_scope_printf(cm->transforms_scope,
				"Destroyed color transformation t%u.\n", xform->base.id);

	free(xform);
}

/*
 * The method of testing for identity matrix is from
 * https://gitlab.freedesktop.org/pq/fourbyfour/-/blob/master/README.d/precision_testing.md#inversion-error
 */
static bool
matrix_is_identity(struct weston_mat4f M, int bits_precision)
{
	M = weston_m4f_sub_m4f(M, WESTON_MAT4F_IDENTITY);
	return -log2(weston_m4f_inf_norm(M)) >= bits_precision;
}

static struct weston_mat4f
stage_matrix_get_mat4(const _cmsStageMatrixData *smd)
{
	const double *p = smd->Offset;
	const double *d = smd->Double;
	struct weston_vec3f t = WESTON_VEC3F_ZERO;
	struct weston_mat3f A = WESTON_MAT3F(
		/* smd is row-major. */
		d[0], d[1], d[2],
		d[3], d[4], d[5],
		d[6], d[7], d[8]
	);

	if (p)
		t = WESTON_VEC3F(p[0], p[1], p[2]);

	return weston_m4f_from_m3f_v3f(A, t);
}

static bool
is_matrix_stage(const cmsStage *stage)
{
	if (!stage || cmsStageType(stage) != cmsSigMatrixElemType)
		return false;

	return true;
}

static bool
is_identity_matrix_stage(const cmsStage *stage)
{
	const _cmsStageMatrixData *data;
	struct weston_mat4f M;

	if (!is_matrix_stage(stage))
		return false;

	data = cmsStageData(stage);
	M = stage_matrix_get_mat4(data);

	return matrix_is_identity(M, MATRIX_PRECISION_BITS);
}

/* Returns the matrix (next * prev). */
static cmsStage *
multiply_matrix_stages(cmsContext context_id, cmsStage *next, cmsStage *prev)
{
	struct weston_mat4f M_prev = stage_matrix_get_mat4(cmsStageData(prev));
	struct weston_mat4f M_next = stage_matrix_get_mat4(cmsStageData(next));
	struct weston_mat4f R = weston_m4f_mul_m4f(M_next, M_prev);
	double A[9] = {
		/* row-major */
		R.col[0].el[0], R.col[1].el[0], R.col[2].el[0],
		R.col[0].el[1], R.col[1].el[1], R.col[2].el[1],
		R.col[0].el[2], R.col[1].el[2], R.col[2].el[2]
	};
	double t[3] = {
		R.col[3].el[0], R.col[3].el[1], R.col[3].el[2]
	};
	cmsStage *ret;

	ret = cmsStageAllocMatrix(context_id, 3, 3, A, t);
	abort_oom_if_null(ret);
	return ret;
}

/** Merge consecutive matrices into a single matrix, and drop identity matrices
 *
 * If we have a pipeline { M1, M2, M3 } of matrices only, then the total
 * operation is the matrix M = M3 * M2 * M1 because the pipeline first applies
 * M1, then M2, and finally M3.
 */
static bool
merge_matrices(cmsPipeline **lut, cmsContext context_id)
{
	cmsPipeline *pipe;
	cmsStage *elem;
	cmsStage *prev = NULL;
	cmsStage *freeme = NULL;
	bool modified = false;

	pipe = cmsPipelineAlloc(context_id, 3, 3);
	abort_oom_if_null(pipe);

	elem = cmsPipelineGetPtrToFirstStage(*lut);
	do {
		if (is_matrix_stage(prev) && is_matrix_stage(elem)) {
			/* replace the two matrices with a merged one */
			prev = multiply_matrix_stages(context_id, elem, prev);
			if (freeme)
				cmsStageFree(freeme);
			freeme = prev;
			modified = true;
		} else {
			if (prev) {
				if (is_identity_matrix_stage(prev)) {
					/* skip inserting it */
					modified = true;
				} else {
					cmsPipelineInsertStage(pipe, cmsAT_END,
							       cmsStageDup(prev));
				}
			}
			prev = elem;
		}

		if (elem)
			elem = cmsStageNext(elem);
	} while (prev);

	if (freeme)
		cmsStageFree(freeme);

	cmsPipelineFree(*lut);
	*lut = pipe;

	return modified;
}

/*
 * XXX: Joining curve sets pair by pair might cause precision problems,
 * especially as we convert even analytical curve types into tabulated.
 * It might be preferable to convert a whole chain of curve sets at once
 * instead.
 */
static cmsStage *
join_curvesets(cmsContext context_id, const cmsStage *prev,
	       const cmsStage *next, unsigned int num_samples)
{
	_cmsStageToneCurvesData *prev_, *next_;
	cmsToneCurve *arr[3];
	cmsUInt32Number i;
	cmsStage *ret = NULL;

	prev_ = cmsStageData(prev);
	next_ = cmsStageData(next);

	assert(prev_->nCurves == ARRAY_LENGTH(arr));
	assert(next_->nCurves == ARRAY_LENGTH(arr));

	/* If the CurveSet's are parametric powerlaw curves that we know how to
	 * merge (preserving them as parametric powerlaw curves), we do that. We
	 * want to avoid transforming parametric curves into sampled curves. */
	ret = join_powerlaw_curvesets(context_id,
				      prev_->TheCurves, next_->TheCurves);
	if (ret)
		return ret;

	/* Transform both CurveSet's into a single sampled one. */
	for (i = 0; i < ARRAY_LENGTH(arr); i++) {
		arr[i] = lcmsJoinToneCurve(context_id, prev_->TheCurves[i],
					   next_->TheCurves[i], num_samples);
		abort_oom_if_null(arr[i]);
	}

	ret = cmsStageAllocToneCurves(context_id, ARRAY_LENGTH(arr), arr);
	abort_oom_if_null(ret);
	cmsFreeToneCurveTriple(arr);
	return ret;
}

static bool
is_identity_curve_stage(const cmsStage *stage)
{
	const _cmsStageToneCurvesData *data;
	unsigned int i;
	bool is_identity = true;

	assert(stage);

	if (cmsStageType(stage) != cmsSigCurveSetElemType)
		return false;

	data = cmsStageData(stage);
	for (i = 0; i < data->nCurves; i++)
		is_identity &= cmsIsToneCurveLinear(data->TheCurves[i]);

	return is_identity;
}

static bool
merge_curvesets(cmsPipeline **lut, cmsContext context_id)
{
	cmsPipeline *pipe;
	cmsStage *elem;
	cmsStage *prev = NULL;
	cmsStage *freeme = NULL;
	bool modified = false;

	pipe = cmsPipelineAlloc(context_id, 3, 3);
	abort_oom_if_null(pipe);

	elem = cmsPipelineGetPtrToFirstStage(*lut);
	do {
		if (prev && cmsStageType(prev) == cmsSigCurveSetElemType &&
		    elem && cmsStageType(elem) == cmsSigCurveSetElemType) {
			/* If the curvesets are inverse, joining them results in
			 * the identity. So we can drop both and continue. */
			if (are_curvesets_inverse(prev, elem)) {
				prev = cmsStageNext(elem);
				if (prev)
					elem = cmsStageNext(prev);
				else
					elem = NULL;
				modified = true;
				continue;
			}
			/* Replace two curve set elements with a merged one. */
			prev = join_curvesets(context_id, prev, elem,
					      cmlcms_reasonable_1D_points());
			if (freeme)
				cmsStageFree(freeme);
			freeme = prev;
			modified = true;
		} else {
			if (prev) {
				if (is_identity_curve_stage(prev)) {
					/* skip inserting it */
					modified = true;
				} else {
					cmsPipelineInsertStage(pipe, cmsAT_END,
							cmsStageDup(prev));
				}
			}
			prev = elem;
		}

		if (elem)
			elem = cmsStageNext(elem);
	} while (prev);

	if (freeme)
		cmsStageFree(freeme);

	cmsPipelineFree(*lut);
	*lut = pipe;

	return modified;
}

static const struct weston_color_tf_info *
lcms_curve_matches_any_tf(struct weston_compositor *compositor,
			  uint32_t lcms_curve_type, bool clamped_input,
			  const float lcms_curve_params[3][MAX_PARAMS_LCMS_PARAM_CURVE])
{
	struct weston_color_curve_parametric curve = { 0 };
	unsigned int i, j;
	uint32_t n_lcms_curve_params;

	curve.clamped_input = clamped_input;

	switch(lcms_curve_type) {
	case 1:
		/**
		 * LittleCMS type 1 is the pure power-law curve, which is a
		 * special case of LINPOW. See init_curve_from_type_1().
		 */
		n_lcms_curve_params = 1;
		curve.type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
		break;
	case 4:
		/**
		 * LittleCMS type 4 is almost exactly the same as LINPOW. See
		 * init_curve_from_type_4().
		 */
		n_lcms_curve_params = 5;
		curve.type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
		break;
	default:
		return NULL;
	}

	weston_assert_uint32_lt_or_eq(compositor,
				      n_lcms_curve_params, MAX_PARAMS_LCMS_PARAM_CURVE);

	for (i = 0; i < 3; i++)
		for (j = 0; j < n_lcms_curve_params; j++)
			curve.params.chan[i].data[j] = lcms_curve_params[i][j];

	return weston_color_tf_info_from_parametric_curve(&curve);
}


static bool
init_curve_from_type_1(struct weston_compositor *compositor,
		       struct weston_color_curve *curve,
		       const float type_1_params[3][MAX_PARAMS_LCMS_PARAM_CURVE],
		       bool clamped_input)
{
	struct weston_color_curve_enum *enumerated = &curve->u.enumerated;
	struct weston_color_curve_parametric *parametric = &curve->u.parametric;
	const struct weston_color_tf_info *tf_info;
	unsigned int i;

	/* Check if LittleCMS curve matches any TF (except the parametric TF's). */
	tf_info = lcms_curve_matches_any_tf(compositor, 1, clamped_input, type_1_params);
	if (tf_info) {
		curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		enumerated->tf = tf_info;
		enumerated->tf_direction = WESTON_FORWARD_TF;
		return true;
	}

	/* This is a pure power-law with custom exp. If clamped_input == false,
	 * this matches WESTON_TF_POWER (parametric TF that is not clamped). */
	if (!clamped_input) {
		curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		enumerated->tf = weston_color_tf_info_from(compositor,
							   WESTON_TF_POWER);
		enumerated->tf_direction = WESTON_FORWARD_TF;
		for (i = 0; i < 3; i++)
			enumerated->params[i][0] = type_1_params[i][0];
		return true;
	}

	/* Pure power-law with custom exp and clamped_input. We don't have any
	 * TF that matches this, so let's use a parametric curve. */
	curve->type = WESTON_COLOR_CURVE_TYPE_PARAMETRIC;

	/* LittleCMS type 1 is the pure power-law curve, which is a special case
	 * of LINPOW.
	 *
	 * LINPOW is defined as:
	 *
	 * y = (a * x + b) ^ g | x >= d
	 * y = c * x           | 0 <= x < d
	 *
	 * So for a = 1, b = 0, c = 1 and d = 0, we have:
	 *
	 * y = x ^ g | x >= 0
	 *
	 * As the pure power-law is only defined for values x >= 0 (because
	 * negative values raised to fractional exponents results in complex
	 * numbers), this is exactly the pure power-law curve.
	 */
	parametric->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
	parametric->clamped_input = clamped_input;

	for (i = 0; i < 3; i++) {
		parametric->params.chan[i].g = type_1_params[i][0];
		parametric->params.chan[i].a = 1.0f;
		parametric->params.chan[i].b = 0.0f;
		parametric->params.chan[i].c = 1.0f;
		parametric->params.chan[i].d = 0.0f;
	}

	return true;
}

static bool
init_curve_from_type_1_inverse(struct weston_compositor *compositor,
			       struct weston_color_curve *curve,
			       const float type_1_params[3][MAX_PARAMS_LCMS_PARAM_CURVE],
			       bool clamped_input)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(compositor->color_manager);
	struct weston_color_curve_enum *enumerated = &curve->u.enumerated;
	struct weston_color_curve_parametric *parametric = &curve->u.parametric;
	const struct weston_color_tf_info *tf_info;
	float g;
	const char *err_msg;
	unsigned int i;

	/* Check if LittleCMS curve matches any TF (except the parametric TF's). */
	tf_info = lcms_curve_matches_any_tf(compositor, 1, clamped_input, type_1_params);
	if (tf_info) {
		curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		enumerated->tf = tf_info;
		enumerated->tf_direction = WESTON_INVERSE_TF;
		return true;
	}

	/* This is the inverse of a pure power-law with custom exp. If
	 * clamped_input == false, this matches WESTON_TF_POWER (parametric TF
	 * that is not clamped). */
	if (!clamped_input) {
		curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		enumerated->tf = weston_color_tf_info_from(compositor,
							   WESTON_TF_POWER);
		enumerated->tf_direction = WESTON_INVERSE_TF;
		for (i = 0; i < 3; i++) {
			g = type_1_params[i][0];
			if (g == 0.0f) {
				err_msg = "WARNING: xform has a LittleCMS type -1 curve " \
					  "(inverse of pure power-law) with exponent 1 " \
					  "divided by 0, which is invalid";
				goto err;
			}
			enumerated->params[i][0] = g;
		}
	}

	/* Inverse of pure power-law with custom exp and clamped_input. We don't
	 * have any TF that matches this, so let's use a parametric curve. */
	curve->type = WESTON_COLOR_CURVE_TYPE_PARAMETRIC;

	/* LittleCMS type -1 (inverse of type 1) is the inverse of the pure
	 * power-law curve, which is a special case of LINPOW.
	 *
	 * The type 1 is defined as:
	 *
	 * y = x ^ g | x >= 0
	 *
	 * Computing its inverse, we have:
	 *
	 * y = x ^ (1 / g) | x >= 0
	 *
	 * LINPOW is defined as:
	 *
	 * y = (a * x + b) ^ g | x >= d
	 * y = c * x           | 0 <= x < d
	 *
	 * So for a = 1, b = 0, c = 1 and d = 0, we have:
	 *
	 * y = x ^ g | x >= 0
	 *
	 * If we take the param g from type -1 and invert it, we can fit type -1
	 * into the curve above.
	 */
	parametric->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
	parametric->clamped_input = clamped_input;

	for (i = 0; i < 3; i++) {
		g = type_1_params[i][0];
		if (g == 0.0f) {
			err_msg = "WARNING: xform has a LittleCMS type -1 curve " \
				  "(inverse of pure power-law) with exponent 1 " \
				  "divided by 0, which is invalid";
			goto err;
		}
		parametric->params.chan[i].g = 1.0f / g;
		parametric->params.chan[i].a = 1.0f;
		parametric->params.chan[i].b = 0.0f;
		parametric->params.chan[i].c = 1.0f;
		parametric->params.chan[i].d = 0.0f;
	}

	return true;

err:
	weston_log_scope_printf(cm->transforms_scope, "%s\n", err_msg);
	return false;
}

static bool
init_curve_from_type_4(struct weston_compositor *compositor,
		       struct weston_color_curve *curve,
		       const float type_4_params[3][MAX_PARAMS_LCMS_PARAM_CURVE],
		       bool clamped_input)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(compositor->color_manager);
	struct weston_color_curve_enum *enumerated = &curve->u.enumerated;
	struct weston_color_curve_parametric *parametric = &curve->u.parametric;
	const struct weston_color_tf_info *tf_info;
	float g, a, b, c, d;
	const char *err_msg;
	unsigned int i;

	/* Check if LittleCMS curve matches any TF (except the parametric TF's). */
	tf_info = lcms_curve_matches_any_tf(compositor, 4, clamped_input, type_4_params);
	if (tf_info) {
		curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		enumerated->tf = tf_info;
		enumerated->tf_direction = WESTON_FORWARD_TF;
		return true;
	}

	/* No TF's matches this curve, so let's put it in a parametric curve. */
	curve->type = WESTON_COLOR_CURVE_TYPE_PARAMETRIC;

	/* LittleCMS type 4 is almost exactly the same as LINPOW. So simply copy
	 * the params. No need to adjust anything.
	 *
	 * The only difference is that type 4 evaluates negative input values as
	 * is, and LINPOW handles negative input values using mirroring (i.e.
	 * for LINPOW being f(x) we'll compute -f(-x)).
	 *
	 * LINPOW is defined as:
	 *
	 * y = (a * x + b) ^ g | x >= d
	 * y = c * x           | 0 <= x < d
	 */
	parametric->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
	parametric->clamped_input = clamped_input;

	for (i = 0; i < 3; i++) {
		g = type_4_params[i][0];
		a = type_4_params[i][1];
		b = type_4_params[i][2];
		c = type_4_params[i][3];
		d = type_4_params[i][4];

		if (a < 0.0f) {
			err_msg = "WARNING: xform has a LittleCMS type 4 curve " \
				  "with a < 0, which is unexpected";
			goto err;
		}

		if (d < 0.0f) {
			err_msg = "WARNING: xform has a LittleCMS type 4 curve " \
				  "with d < 0, which is unexpected";
			goto err;
		}

		if (a * d + b < 0) {
			err_msg = "WARNING: xform has a LittleCMS type 4 curve " \
				  "with a * d + b < 0, which is invalid";
			goto err;
		}

		parametric->params.chan[i].g = g;
		parametric->params.chan[i].a = a;
		parametric->params.chan[i].b = b;
		parametric->params.chan[i].c = c;
		parametric->params.chan[i].d = d;
	}

	return true;

err:
	weston_log_scope_printf(cm->transforms_scope, "%s\n", err_msg);
	return false;
}

static bool
init_curve_from_type_4_inverse(struct weston_compositor *compositor,
			       struct weston_color_curve *curve,
			       const float type_4_params[3][MAX_PARAMS_LCMS_PARAM_CURVE],
			       bool clamped_input)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(compositor->color_manager);
	struct weston_color_curve_enum *enumerated = &curve->u.enumerated;
	struct weston_color_curve_parametric *parametric = &curve->u.parametric;
	const struct weston_color_tf_info *tf_info;
	float g, a, b, c, d;
	const char *err_msg;
	unsigned int i;

	/* Check if LittleCMS curve matches any TF (except the parametric ones). */
	tf_info = lcms_curve_matches_any_tf(compositor, 4, clamped_input, type_4_params);
	if (tf_info) {
		curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
		enumerated->tf = tf_info;
		enumerated->tf_direction = WESTON_INVERSE_TF;
		return true;
	}

	/* No TF's matches this curve, so let's put it in a parametric curve. */
	curve->type = WESTON_COLOR_CURVE_TYPE_PARAMETRIC;

	/* LittleCMS type -4 (inverse of type 4) fits into POWLIN. We need to
	 * adjust the params that LittleCMS gives us, like below. Do not forget
	 * that LittleCMS gives the params of the type 4 curve whose inverse
	 * is the one it wants to represent.
	 *
	 * Also, type -4 evaluates negative input values as is, and POWLIN
	 * handles negative input values using mirroring (i.e. for POWLIN being
	 * f(x) we'll compute -f(-x)). We do that to avoid negative values being
	 * raised to fractional exponents, what would result in complex numbers.
	 *
	 * The type 4 is defined as:
	 *
	 * y = (a * x + b) ^ g | x >= d
	 * y = c * x           | else
	 *
	 * Computing its inverse, we have:
	 *
	 * y = ((x ^ (1 / g)) / a) - (b / a) | x >= c * d or (a * d + b) ^ g
	 * y = x / c			     | else
	 *
	 * POWLIN is defined as:
	 *
	 * y = (a * (x ^ g)) + b | x >= d
	 * y = c * x             | 0 <= x < d
	 *
	 * So we need to take the params from LittleCMS and adjust:
	 *
	 * g ←  1 / g
	 * a ←  1 / a
	 * b ← -b / a
	 * c ←  1 / c
	 * d ←  c * d
	 *
	 * Also, notice that c * d should be equal to (a * d + b) ^ g. But
	 * because of precision problems or a deliberate discontinuity in the
	 * function, that may not be true. So we may have a range of input
	 * values for POWLIN such that c * d <= x <= (a * d + b) ^ g. For these
	 * values, when evaluating POWLIN we need to decide with what segment
	 * we're going to evaluate the input. For the majority of POWLIN color
	 * curves created from type -4 we are expecting c * d ≈ (a * d + b) ^ g,
	 * so the different output produced by the two discontinuous segments
	 * would be so close that this wouldn't matter. But mathematically
	 * there's nothing that guarantees that the two discontinuous segments
	 * are close, and in this case the outputs would vary significantly.
	 * There's nothing we can do regarding that, so we'll arbitrarily choose
	 * one of the segments to compute the output.
	 */
	parametric->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN;
	parametric->clamped_input = clamped_input;

	for (i = 0; i < 3; i++) {
		g = type_4_params[i][0];
		a = type_4_params[i][1];
		b = type_4_params[i][2];
		c = type_4_params[i][3];
		d = type_4_params[i][4];

		if (g == 0.0f) {
			err_msg = "WARNING: xform has a LittleCMS type -4 curve " \
				  "but the param g of the original type 4 curve " \
				  "is zero, so the inverse is invalid";
			goto err;
		}

		if (a == 0.0f) {
			err_msg = "WARNING: xform has a LittleCMS type -4 curve " \
				  "but the param a of the original type 4 curve " \
				  "is zero, so the inverse is invalid";
			goto err;
		}

		if (c == 0.0f) {
			err_msg = "WARNING: xform has a LittleCMS type -4 curve " \
				  "but the param c of the original type 4 curve " \
				  "is zero, so the inverse is invalid";
			goto err;
		}

		parametric->params.chan[i].g = 1.0f / g;
		parametric->params.chan[i].a = 1.0f / a;
		parametric->params.chan[i].b = -b / a;
		parametric->params.chan[i].c = 1.0f / c;
		parametric->params.chan[i].d = c * d;
	}

	return true;

err:
	weston_log_scope_printf(cm->transforms_scope, "%s\n", err_msg);
	return false;
}

static bool
translate_curve_element_parametric(struct cmlcms_color_transform *xform,
				   _cmsStageToneCurvesData *trc_data,
				   enum color_transform_step step)
{
	struct weston_compositor *compositor = xform->base.cm->compositor;
	struct weston_color_curve *curve;
	cmsInt32Number type;
	float lcms_curveset_params[3][MAX_PARAMS_LCMS_PARAM_CURVE];
	bool clamped_input;
	bool ret;

	switch (step) {
	case STEP_PRE_CURVE:
		curve = &xform->base.pre_curve;
		break;
	case STEP_POST_CURVE:
		curve = &xform->base.post_curve;
		break;
	default:
		weston_assert_not_reached(compositor,
					  "curve should be a pre or post curve");
	}

	/* The curveset may not be a parametric one, in such case we have a
	 * fallback path. But if it is a parametric curve, we get the params for
	 * each color channel and also the parametric curve type (defined by
	 * LittleCMS). */
	if (!get_parametric_curveset_params(compositor, trc_data, &type,
					    lcms_curveset_params, &clamped_input))
		return false;

	switch (type) {
	case 1:
		ret = init_curve_from_type_1(compositor, curve,
					     lcms_curveset_params, clamped_input);
		break;
	case -1:
		ret = init_curve_from_type_1_inverse(compositor, curve,
						     lcms_curveset_params, clamped_input);
		break;
	case 4:
		ret = init_curve_from_type_4(compositor, curve,
					     lcms_curveset_params, clamped_input);
		break;
	case -4:
		ret = init_curve_from_type_4_inverse(compositor, curve,
						     lcms_curveset_params, clamped_input);
		break;
	default:
		/* We don't implement the curve. */
		ret = false;
	}

	return ret;
}

static bool
translate_curve_element_LUT(struct cmlcms_color_transform *xform,
			    _cmsStageToneCurvesData *trc_data,
			    enum color_transform_step step)
{
	struct weston_compositor *compositor = xform->base.cm->compositor;
	struct weston_color_curve *curve;
	cmsToneCurve **stash;
	unsigned i;

	switch (step) {
	case STEP_PRE_CURVE:
		curve = &xform->base.pre_curve;
		curve->u.lut_3x1d.fill_in = cmlcms_fill_in_pre_curve;
		stash = xform->pre_curve;
		break;
	case STEP_POST_CURVE:
		curve = &xform->base.post_curve;
		curve->u.lut_3x1d.fill_in = cmlcms_fill_in_post_curve;
		stash = xform->post_curve;
		break;
	default:
		weston_assert_not_reached(compositor,
					  "curve should be a pre or post curve");
	}

	curve->type = WESTON_COLOR_CURVE_TYPE_LUT_3x1D;
	curve->u.lut_3x1d.optimal_len = cmlcms_reasonable_1D_points();

	weston_assert_uint32_eq(compositor, trc_data->nCurves, 3);
	for (i = 0; i < 3; i++) {
		stash[i] = cmsDupToneCurve(trc_data->TheCurves[i]);
		abort_oom_if_null(stash[i]);
	}

	return true;
}

static bool
translate_curve_element(struct cmlcms_color_transform *xform,
			cmsStage *elem, enum color_transform_step step)
{
	struct weston_compositor *compositor = xform->base.cm->compositor;
	_cmsStageToneCurvesData *trc_data;

	weston_assert_uint64_eq(compositor, cmsStageType(elem),
				cmsSigCurveSetElemType);

	trc_data = cmsStageData(elem);
	if (trc_data->nCurves != 3)
		return false;

	/* First try to translate the curve to a parametric one. */
	if (translate_curve_element_parametric(xform, trc_data, step))
		return true;

	/* Curve does not fit any of the parametric curves that we implement, so
	 * fallback to LUT. */
	return translate_curve_element_LUT(xform, trc_data, step);
}

static bool
translate_matrix_element(struct weston_color_mapping *map, cmsStage *elem)
{
	_cmsStageMatrixData *data = cmsStageData(elem);
	int c, r;

	if (cmsStageInputChannels(elem) != 3 ||
	    cmsStageOutputChannels(elem) != 3)
		return false;

	map->type = WESTON_COLOR_MAPPING_TYPE_MATRIX;

	/*
	 * map->u.mat.matrix is column-major, while
	 * data->Double is row-major.
	 */
	for (c = 0; c < 3; c++)
		for (r = 0; r < 3; r++)
			map->u.mat.matrix.col[c].el[r] = data->Double[r * 3 + c];

	if (data->Offset) {
		for (r = 0; r < 3; r++)
			map->u.mat.offset.el[r] = data->Offset[r];
	}

	return true;
}

static bool
translate_pipeline(struct cmlcms_color_transform *xform, const cmsPipeline *lut)
{
	cmsStage *elem;

	xform->base.pre_curve.type = WESTON_COLOR_CURVE_TYPE_IDENTITY;
	xform->base.mapping.type = WESTON_COLOR_MAPPING_TYPE_IDENTITY;
	xform->base.post_curve.type = WESTON_COLOR_CURVE_TYPE_IDENTITY;

	elem = cmsPipelineGetPtrToFirstStage(lut);

	if (!elem)
		return true;

	if (cmsStageType(elem) == cmsSigCurveSetElemType &&
	    (xform->allowed.steps & STEP_PRE_CURVE)) {
		if (!translate_curve_element(xform, elem, STEP_PRE_CURVE))
			return false;

		elem = cmsStageNext(elem);
	}

	if (!elem)
		return true;

	if (cmsStageType(elem) == cmsSigMatrixElemType &&
	    (xform->allowed.steps & STEP_MAPPING)) {
		if (!translate_matrix_element(&xform->base.mapping, elem))
			return false;

		elem = cmsStageNext(elem);
	}

	if (!elem)
		return true;

	if (cmsStageType(elem) == cmsSigCurveSetElemType &&
	    (xform->allowed.steps & STEP_POST_CURVE)) {
		if (!translate_curve_element(xform, elem, STEP_POST_CURVE))
			return false;

		elem = cmsStageNext(elem);
	}

	if (!elem)
		return true;

	return false;
}

WESTON_EXPORT_FOR_TESTS void
lcms_optimize_pipeline(cmsPipeline **lut, cmsContext context_id)
{
	bool cont_opt;

	/**
	 * This optimization loop will delete identity stages. Deleting
	 * identity matrix stages is harmless, but deleting identity
	 * curve set stages also removes the implicit clamping they do
	 * on their input values.
	 */
	do {
		cont_opt = merge_matrices(lut, context_id);
		cont_opt |= merge_curvesets(lut, context_id);
	} while (cont_opt);
}

static void
optimize_float_pipeline(cmsPipeline **lut, cmsContext context_id,
			struct cmlcms_color_transform *xform)
{
	lcms_optimize_pipeline(lut, context_id);

	xform->base.steps_valid = translate_pipeline(xform, *lut);
}

static const char *
cmlcms_stage_type_to_str(cmsStage *stage)
{
	/* This table is based on cmsStageSignature enum type from the
	 * LittleCMS API. */
	switch (cmsStageType(stage))
	{
	case cmsSigCurveSetElemType:
		return "CurveSet";
	case cmsSigMatrixElemType:
		return "Matrix";
	case cmsSigCLutElemType:
		return "CLut";
	case cmsSigBAcsElemType:
		return "BAcs";
	case cmsSigEAcsElemType:
		return "EAcs";
	case cmsSigXYZ2LabElemType:
		return "XYZ2Lab";
	case cmsSigLab2XYZElemType:
		return "Lab2XYz";
	case cmsSigNamedColorElemType:
		return "NamedColor";
	case cmsSigLabV2toV4:
		return "LabV2toV4";
	case cmsSigLabV4toV2:
		return "LabV4toV2";
	case cmsSigIdentityElemType:
		return "Identity";
	case cmsSigLab2FloatPCS:
		return "Lab2FloatPCS";
	case cmsSigFloatPCS2Lab:
		return "FloatPCS2Lab";
	case cmsSigXYZ2FloatPCS:
		return "XYZ2FloatPCS";
	case cmsSigFloatPCS2XYZ:
		return "FloatPCS2XYZ";
	case cmsSigClipNegativesElemType:
		return "ClipNegatives";
	}

	return NULL;
}

static void
matrix_print(cmsStage *stage, struct weston_log_scope *scope)
{
	const _cmsStageMatrixData *data;
	const unsigned int SIZE = 3;
	unsigned int row, col;
	double elem;
	const char *sep;

	assert(cmsStageType(stage) == cmsSigMatrixElemType);
	data = cmsStageData(stage);

	for (row = 0; row < SIZE; row++) {
		weston_log_scope_printf(scope, "      ");

		for (col = 0, sep = ""; col < SIZE; col++) {
			elem = data->Double[row * SIZE + col];
			weston_log_scope_printf(scope, "%s% .4f", sep, elem);
			sep = " ";
		}

		/* We print offset after the last column of the matrix. */
		if (data->Offset)
			weston_log_scope_printf(scope, " % .4f", data->Offset[row]);

		weston_log_scope_printf(scope, "\n");
	}
}

static void
pipeline_print(cmsPipeline **lut, cmsContext context_id,
	       struct weston_log_scope *scope)
{
	cmsStage *stage = cmsPipelineGetPtrToFirstStage(*lut);
	const char *type_str;

	if (!weston_log_scope_is_enabled(scope))
		return;

	if (!stage) {
		weston_log_scope_printf(scope, "    no elements\n");
		return;
	}

	while (stage != NULL) {
		type_str = cmlcms_stage_type_to_str(stage);
		/* Unknown type, just print the hex */
		if (!type_str)
			weston_log_scope_printf(scope, "    unknown type 0x%x\n",
						cmsStageType(stage));
		else
			weston_log_scope_printf(scope, "    %s\n", type_str);

		switch(cmsStageType(stage)) {
		case cmsSigMatrixElemType:
			matrix_print(stage, scope);
			break;
		case cmsSigCurveSetElemType:
			curveset_print(stage, scope);
			break;
		default:
			break;
		}

		stage = cmsStageNext(stage);
	}
}

/** LittleCMS transform plugin entry point
 *
 * This function is called by LittleCMS when it is creating a new
 * cmsHTRANSFORM. We have the opportunity to inspect and override everything.
 * The initial cmsPipeline resulting from e.g.
 * cmsCreateMultiprofileTransformTHR() is handed to us for inspection before
 * the said function call returns.
 *
 * During this call we try to optimize the pipeline and translate it into an
 * optimized weston_color_transform. If the translation fails, or some renderer
 * or backend cannot use the translation, we depend on LittleCMS' own float
 * transformation machinery for evaluating the pipeline.
 *
 * \param xform_fn If we handle the given transformation, we should assign our
 * own transformation function here. We do not do that, because we depend on
 * LittleCMS' transformation machinery (i.e. an useful cmsHTRANSFORM).
 *
 * \param user_data We could store a void pointer to custom user data
 * through this pointer to be carried with the cmsHTRANSFORM.
 * Here none is needed.
 *
 * \param free_private_data_fn We could store a function pointer for freeing
 * our user data when the cmsHTRANSFORM is destroyed. None needed.
 *
 * \param lut The LittleCMS pipeline that describes this transformation.
 * We can create our own and replace the original completely in
 * optimize_float_pipeline().
 *
 * \param input_format Pointer to the format used as input for this
 * transformation. I suppose we could override it if we wanted to, but
 * no need.
 *
 * \param output_format Similar to input format.
 *
 * \param flags Some flags we could also override? See cmsFLAGS_* defines.
 *
 * \return We always return FALSE, because we always depend on LittleCMS being
 * able to handle the transformation itself (i.e. returning an useful
 * cmsHTRANSFORM).
 */
static cmsBool
transform_factory(_cmsTransform2Fn *xform_fn,
		  void **user_data,
		  _cmsFreeUserDataFn *free_private_data_fn,
		  cmsPipeline **lut,
		  cmsUInt32Number *input_format,
		  cmsUInt32Number *output_format,
		  cmsUInt32Number *flags)
{
	struct weston_color_manager_lcms *cm;
	struct cmlcms_color_transform *xform;
	cmsContext context_id;

	if (T_CHANNELS(*input_format) != 3) {
		weston_log("color-lcms debug: input format is not 3-channel.");
		return FALSE;
	}
	if (T_CHANNELS(*output_format) != 3) {
		weston_log("color-lcms debug: output format is not 3-channel.");
		return FALSE;
	}
	if (!T_FLOAT(*input_format)) {
		weston_log("color-lcms debug: input format is not float.");
		return FALSE;
	}
	if (!T_FLOAT(*output_format)) {
		weston_log("color-lcms debug: output format is not float.");
		return FALSE;
	}
	context_id = cmsGetPipelineContextID(*lut);
	assert(context_id);
	xform = cmsGetContextUserData(context_id);
	assert(xform);

	cm = to_cmlcms(xform->base.cm);

	/* Print pipeline before optimization */
	weston_log_scope_printf(cm->optimizer_scope,
				"  ICC-to-ICC transform pipeline before optimization:\n");
	pipeline_print(lut, context_id, cm->optimizer_scope);

	/* Optimize pipeline */
	optimize_float_pipeline(lut, context_id, xform);

	/* Print pipeline after optimization */
	weston_log_scope_printf(cm->optimizer_scope,
				"  ICC-to-ICC transform pipeline after optimization:\n");
	pipeline_print(lut, context_id, cm->optimizer_scope);

	return FALSE;
}

static cmsPluginTransform transform_plugin = {
	.base = {
		.Magic = cmsPluginMagicNumber,
		.ExpectedVersion = REQUIRED_LCMS_VERSION,
		.Type = cmsPluginTransformSig,
		.Next = NULL
	},
	.factories.xform = transform_factory,
};

static void
lcms_xform_error_logger(cmsContext context_id,
			cmsUInt32Number error_code,
			const char *text)
{
	struct cmlcms_color_transform *xform;
	struct cmlcms_color_profile *in;
	struct cmlcms_color_profile *out;

	xform = cmsGetContextUserData(context_id);
	in = xform->search_key.input_profile;
	out = xform->search_key.output_profile;

	weston_log("LittleCMS error with color transformation t%u from "
		   "'%s' (p%u) to '%s' (p%u), %s: %s\n",
		   xform->base.id,
		   in ? in->base.description : "(none)",
		   in ? in->base.id : 0,
		   out ? out->base.description : "(none)",
		   out ? out->base.id : 0,
		   cmlcms_category_name(xform->search_key.category),
		   text);
}

static cmsHTRANSFORM
xform_realize_icc_chain(struct cmlcms_color_transform *xform,
			struct lcmsProfilePtr *chain,
			size_t chain_len,
			const struct weston_render_intent_info *render_intent,
			struct color_transform_steps_mask allowed)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(xform->base.cm);
	cmsHTRANSFORM icc_chain;
	cmsUInt32Number dwFlags;

	weston_assert_ptr_not_null(cm->base.compositor, render_intent);

	/**
	 * Binding to our LittleCMS plug-in occurs here.
	 * If you want to disable the plug-in while debugging,
	 * replace &transform_plugin with NULL.
	 */
	xform->allowed = allowed;
	weston_assert_ptr_null(cm->base.compositor, xform->lcms_ctx);
	xform->lcms_ctx = cmsCreateContext(&transform_plugin, xform);
	abort_oom_if_null(xform->lcms_ctx);
	cmsSetLogErrorHandlerTHR(xform->lcms_ctx, lcms_xform_error_logger);

	/* transform_factory() is invoked by this call. */
	dwFlags = render_intent->bps ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0;
	icc_chain = cmsCreateMultiprofileTransformTHR(xform->lcms_ctx,
						      from_lcmsProfilePtr_array(chain),
						      chain_len,
						      TYPE_RGB_FLT,
						      TYPE_RGB_FLT,
						      render_intent->lcms_intent,
						      dwFlags);
	if (!icc_chain) {
		cmsDeleteContext(xform->lcms_ctx);
		xform->lcms_ctx = NULL;

		return NULL;
	}

	/* Blend-to-output should always have valid steps. */
	if (xform->search_key.category == CMLCMS_CATEGORY_BLEND_TO_OUTPUT)
		weston_assert_true(cm->base.compositor, xform->base.steps_valid);

	return icc_chain;
}

static bool
init_icc_to_icc_chain(struct cmlcms_color_transform *xform)
{
	struct weston_color_manager_lcms *cm = to_cmlcms(xform->base.cm);
	struct cmlcms_color_profile *in_prof = xform->search_key.input_profile;
	struct cmlcms_color_profile *out_prof = xform->search_key.output_profile;
	const struct weston_render_intent_info *render_intent;
	struct color_transform_steps_mask allowed = {
		STEP_PRE_CURVE | STEP_MAPPING | STEP_POST_CURVE
	};
	struct lcmsProfilePtr chain[5];
	unsigned chain_len = 0;

	weston_assert_uint32_eq(cm->base.compositor, out_prof->type, CMLCMS_PROFILE_TYPE_ICC);
	if (in_prof) {
		weston_assert_uint32_eq(cm->base.compositor, in_prof->type,
					CMLCMS_PROFILE_TYPE_ICC);
	}

	render_intent = xform->search_key.render_intent;

	/*
	 * Our blending space is chosen to be the optical output color space.
	 * From input space, we always go to electrical output space, then
	 * come to optical space for blending, and finally go back to
	 * electrical output space. Before the image is sent to display,
	 * we must also apply VCGT if given, since nothing else would do that.
	 *
	 * INPUT_TO_BLEND + BLEND_TO_OUTPUT = INPUT_TO_OUTPUT
	 */

	switch (xform->search_key.category) {
	case CMLCMS_CATEGORY_INPUT_TO_BLEND:
		chain[chain_len++] = in_prof->icc.profile;
		chain[chain_len++] = out_prof->icc.profile;
		chain[chain_len++] = out_prof->extract.eotf;
		break;
	case CMLCMS_CATEGORY_BLEND_TO_OUTPUT:
		chain[chain_len++] = out_prof->extract.inv_eotf;
		if (out_prof->extract.vcgt.p)
			chain[chain_len++] = out_prof->extract.vcgt;

		/* Render intent does not apply here, but need to set something. */
		render_intent = weston_render_intent_info_from(cm->base.compositor,
							       WESTON_RENDER_INTENT_ABSOLUTE);
		break;
	case CMLCMS_CATEGORY_INPUT_TO_OUTPUT:
		chain[chain_len++] = in_prof->icc.profile;
		chain[chain_len++] = out_prof->icc.profile;
		if (out_prof->extract.vcgt.p)
			chain[chain_len++] = out_prof->extract.vcgt;
		break;
	}

	assert(chain_len <= ARRAY_LENGTH(chain));

	weston_assert_ptr_null(cm->base.compositor, xform->cmap_3dlut);
	xform->cmap_3dlut = xform_realize_icc_chain(xform, chain, chain_len,
						    render_intent, allowed);

	return !!xform->cmap_3dlut;
}

static void
weston_color_curve_set_from_params(struct weston_color_curve *curve,
				   const struct weston_color_profile_params *p,
				   enum weston_tf_direction dir)
{
	unsigned i;

	curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;
	curve->u.enumerated.tf = p->tf_info;
	curve->u.enumerated.tf_direction = dir;

	for (i = 0; i < 3; i++)
		ARRAY_COPY(curve->u.enumerated.params[i], p->tf_params);
}

static void
weston_color_mapping_set_from_m4f(struct weston_color_mapping *mapping,
				  struct weston_mat4f mat)
{
	mapping->type = WESTON_COLOR_MAPPING_TYPE_MATRIX;
	mapping->u.mat.matrix = weston_m3f_from_m4f_xyz(mat);
	mapping->u.mat.offset = weston_v3f_from_v4f_xyz(mat.col[3]);
}

static bool
init_blend_to_parametric(struct cmlcms_color_transform *xform)
{
	struct weston_color_profile_params *out = xform->search_key.output_profile->params;

	weston_assert_uint32_eq(xform->base.cm->compositor,
				xform->search_key.output_profile->type,
				CMLCMS_PROFILE_TYPE_PARAMS);

	/*
	 * For blend-to-output with a parametric output profile, all we need
	 * is to electrically encode for the output with the inverse TF.
	 *
	 * In the input to the TF RGB 0,0,0 corresponds to min_luminance and
	 * RGB 1,1,1 corresponds to max_luminance.
	 */

	weston_color_curve_set_from_params(&xform->base.pre_curve, out, WESTON_INVERSE_TF);
	xform->base.mapping.type = WESTON_COLOR_MAPPING_TYPE_IDENTITY;
	xform->base.post_curve.type = WESTON_COLOR_CURVE_TYPE_IDENTITY;
	xform->base.steps_valid = true;

	return true;
}

struct rendering_intent_flags {
	bool black_point_compensation;
	bool white_point_adaptation;
	bool perceptual;
	bool saturate;
};

static struct rendering_intent_flags
rendering_intent_to_flags(enum weston_render_intent intent)
{
	struct rendering_intent_flags flags = {0};

	switch (intent) {
	case WESTON_RENDER_INTENT_ABSOLUTE:
		break;
	case WESTON_RENDER_INTENT_RELATIVE:
		flags.white_point_adaptation = true;
		break;
	case WESTON_RENDER_INTENT_RELATIVE_BPC:
		flags.white_point_adaptation = true;
		flags.black_point_compensation = true;
		break;
	case WESTON_RENDER_INTENT_PERCEPTUAL:
		flags.white_point_adaptation = true;
		flags.black_point_compensation = true;
		flags.perceptual = true;
		break;
	case WESTON_RENDER_INTENT_SATURATION:
		flags.white_point_adaptation = true;
		flags.black_point_compensation = true;
		flags.saturate = true;
		break;
	}

	return flags;
}

static bool
rgb_to_rgb_matrix(struct weston_mat4f *mat,
		  const struct weston_color_profile_params *in,
		  const struct weston_color_profile_params *out,
		  enum weston_render_intent intent,
		  char **errmsg)
{
	struct rendering_intent_flags flags = rendering_intent_to_flags(intent);

	/**
	 * The matrix input is optical where RGB 0,0,0 corresponds to
	 * min_luminance and RGB 1,1,1 corresponds to max_luminance.
	 *
	 * The matrix output shall be the same except with output
	 * min_luminance and max_luminance.
	 */
	struct weston_mat4f M;

	struct weston_mat3f npm_in;
	struct weston_mat3f npm_out_inv;
	struct weston_mat3f p2p;
	float v;

	/* Convert input [0, 1] scale to cd/m² */
	v = in->max_luminance - in->min_luminance;
	M = weston_m4f_scaling(v, v, v);
	v = in->min_luminance;
	M = weston_m4f_mul_m4f(weston_m4f_translation(v, v, v), M);

	if (flags.black_point_compensation) {
		/* With BPC, map input [target_min, reference] to [0, 1]. */
		v = -in->target_min_luminance;
		M = weston_m4f_mul_m4f(weston_m4f_translation(v, v, v), M);
		v = 1.0f / (in->reference_white_luminance - in->target_min_luminance);
		M = weston_m4f_mul_m4f(weston_m4f_scaling(v, v, v), M);
	} else {
		/* Without BPC, map [0, input reference] to [0, 1]. */
		v = 1.0f / in->reference_white_luminance;
		M = weston_m4f_mul_m4f(weston_m4f_scaling(v, v, v), M);
	}

	/* Color space conversion */
	if (!weston_normalized_primary_matrix_init(&npm_in,
						   &in->primaries, WESTON_NPM_FORWARD)) {
		str_printf(errmsg, "Could not compute NPM from input primaries. "
				   "The primaries or white point may be invalid.");
		return false;
	}
	if (!weston_normalized_primary_matrix_init(&npm_out_inv,
						   &out->primaries, WESTON_NPM_INVERSE)) {
		str_printf(errmsg, "Could not compute inverse NPM from output primaries. "
				   "The primaries or white point may be invalid.");
		return false;
	}

	p2p = npm_in;
	if (flags.white_point_adaptation) {
		struct weston_mat3f chad =
			weston_bradford_adaptation(in->primaries.white_point,
						   out->primaries.white_point);
		p2p = weston_m3f_mul_m3f(chad, p2p);
	}
	p2p = weston_m3f_mul_m3f(npm_out_inv, p2p);
	M = weston_m4f_mul_m4f(weston_m4f_from_m3f_v3f(p2p, WESTON_VEC3F_ZERO), M);

	if (flags.perceptual) {
		/* TODO: Dynamic range adjustment */
		/* TODO: target color volume */
	}

	/* TODO: flags.saturation */

	/*
	 * The input reference white luminance is RGB 1,1,1. To map input
	 * reference to output reference, we reinterpret RGB 1,1,1 as the output
	 * reference white luminance. RGB 0,0,0 is 0 cd/m² at this point.
	 * The reinterpretation is a significant semantic action, but it
	 * requires no code to implement.
	 */

	if (flags.black_point_compensation) {
		/* With BPC, map [0, 1] to output [target_min, reference]. */
		v = out->reference_white_luminance - out->target_min_luminance;
		M = weston_m4f_mul_m4f(weston_m4f_scaling(v, v, v), M);
		v = out->target_min_luminance;
		M = weston_m4f_mul_m4f(weston_m4f_translation(v, v, v), M);
	} else {
		/* Without BPC, map [0, 1] to [0, output reference]. */
		v = out->reference_white_luminance;
		M = weston_m4f_mul_m4f(weston_m4f_scaling(v, v, v), M);
	}

	/* Convert cd/m² to output [0, 1] scale. */
	v = -out->min_luminance;
	M = weston_m4f_mul_m4f(weston_m4f_translation(v, v, v), M);
	v = 1.0f / (out->max_luminance - out->min_luminance);
	M = weston_m4f_mul_m4f(weston_m4f_scaling(v, v, v), M);

	*mat = M;
	return true;
}

static bool
init_parametric_to_parametric(struct cmlcms_color_transform *xform)
{
	const struct cmlcms_color_transform_recipe *recipe = &xform->search_key;
	struct weston_color_manager_lcms *cm = to_cmlcms(xform->base.cm);
	struct weston_mat4f mat;
	char *errmsg = NULL;

	weston_assert_uint32_eq(cm->base.compositor,
				recipe->input_profile->type,
				CMLCMS_PROFILE_TYPE_PARAMS);
	weston_assert_uint32_eq(cm->base.compositor,
				recipe->output_profile->type,
				CMLCMS_PROFILE_TYPE_PARAMS);

	/*
	 * Decode input TF
	 *
	 * The TF is assumed to map pixel values to [0, 1] range where
	 * RGB 0,0,0 corresponds to min_luminance and
	 * RGB 1,1,1 corresponds to max_luminance.
	 */
	weston_color_curve_set_from_params(&xform->base.pre_curve,
					   recipe->input_profile->params, WESTON_FORWARD_TF);

	if (!rgb_to_rgb_matrix(&mat,
			       recipe->input_profile->params,
			       recipe->output_profile->params,
			       recipe->render_intent->intent,
			       &errmsg)) {
		weston_log_scope_printf(cm->transforms_scope, "%s\n", errmsg);
		free(errmsg);
		return false;
	}

	weston_color_mapping_set_from_m4f(&xform->base.mapping, mat);

	/* TODO: Use HLG OOTF for gamma correction? */
	/* TODO: try https://gitlab.freedesktop.org/pq/color-and-hdr/-/issues/45 */

	switch (recipe->category) {
	case CMLCMS_CATEGORY_INPUT_TO_BLEND:
		xform->base.post_curve.type = WESTON_COLOR_CURVE_TYPE_IDENTITY;
		break;
	case CMLCMS_CATEGORY_INPUT_TO_OUTPUT:
		weston_color_curve_set_from_params(&xform->base.post_curve,
						   recipe->output_profile->params,
						   WESTON_INVERSE_TF);
		break;
	case CMLCMS_CATEGORY_BLEND_TO_OUTPUT:
		weston_assert_not_reached(xform->base.cm->compositor,
					  "blend-to-output handled elsewhere");
	}

	xform->base.steps_valid = true;

	return true;
}

enum cmlcms_color_transform_type {
	CMLCMS_BLEND_TO_ICC   = 0x0,
	CMLCMS_BLEND_TO_PARAM = 0x1,
	CMLCMS_ICC_TO_ICC     = 0x2,
	CMLCMS_ICC_TO_PARAM   = 0x3,
	CMLCMS_PARAM_TO_ICC   = 0x4,
	CMLCMS_PARAM_TO_PARAM = 0x5
};

static enum cmlcms_color_transform_type
get_transform_type(const struct cmlcms_color_transform *xform)
{
	const struct cmlcms_color_transform_recipe *recipe = &xform->search_key;
	unsigned t = 0x0;

	switch (recipe->output_profile->type) {
	case CMLCMS_PROFILE_TYPE_ICC:
		t += 0x0;
		break;
	case CMLCMS_PROFILE_TYPE_PARAMS:
		t += 0x1;
		break;
	}

	if (recipe->input_profile) {
		switch (recipe->input_profile->type) {
		case CMLCMS_PROFILE_TYPE_ICC:
			t += 0x2;
			break;
		case CMLCMS_PROFILE_TYPE_PARAMS:
			t += 0x4;
			break;
		}
	}

	return (enum cmlcms_color_transform_type)t;
}

char *
cmlcms_color_transform_recipe_string(const struct cmlcms_color_transform_recipe *recipe)
{
	const char *input_prof_desc = "none";
	unsigned input_prof_id = 0;
	const char *output_prof_desc = "none";
	unsigned output_prof_id = 0;
	const char *intent_desc = "none";
	char *str;

	if (recipe->input_profile) {
		input_prof_desc = recipe->input_profile->base.description;
		input_prof_id = recipe->input_profile->base.id;
	}

	if (recipe->output_profile) {
		output_prof_desc = recipe->output_profile->base.description;
		output_prof_id = recipe->output_profile->base.id;
	}

	if (recipe->render_intent)
		intent_desc = recipe->render_intent->desc;

	str_printf(&str, "  category: %s\n" \
			 "  input profile p%u: %s\n" \
			 "  output profile p%u: %s\n" \
			 "  render intent: %s\n",
			 cmlcms_category_name(recipe->category),
			 input_prof_id, input_prof_desc,
			 output_prof_id, output_prof_desc,
			 intent_desc);

	abort_oom_if_null(str);

	return str;
}

static bool
build_3d_lut(struct weston_compositor *compositor, cmsHTRANSFORM cmap_3dlut,
	     unsigned int len_shaper, float *shaper,
	     unsigned int len_lut3d, float *lut3d)
{
	float divider = len_lut3d - 1;
	float rgb_in[3], rgb_out[3];
	uint32_t index, index_r, index_g, index_b;
	float *curves[3];

	curves[0] = &shaper[0];
	curves[1] = &shaper[len_shaper];
	curves[2] = &shaper[2 * len_shaper];

	for (index_b = 0; index_b < len_lut3d; index_b++) {
		for (index_g = 0; index_g < len_lut3d; index_g++) {
			for (index_r = 0; index_r < len_lut3d; index_r++) {
				/**
				 * For each channel, use the shaper to compute
				 * the value x such that y(x) = index / divider.
				 * As the shapper is a LUT, we find the closest
				 * neighbors of such point (x, y) and then use
				 * linear interpolation to estimate x.
				 */
				rgb_in[0] = weston_inverse_evaluate_lut1d(compositor,len_shaper,
									  curves[0],
									  (float)index_r / divider);
				rgb_in[1] = weston_inverse_evaluate_lut1d(compositor, len_shaper,
									  curves[1],
									  (float)index_g / divider);
				rgb_in[2] = weston_inverse_evaluate_lut1d(compositor, len_shaper,
									  curves[2],
									  (float)index_b / divider);

				cmsDoTransform(cmap_3dlut, rgb_in, rgb_out, 1);

				index = 3 * (index_r + len_lut3d * (index_g + len_lut3d * index_b));
				lut3d[index    ] = rgb_out[0];
				lut3d[index + 1] = rgb_out[1];
				lut3d[index + 2] = rgb_out[2];
			}
		}
	}

	return true;
}

static int
sign_of_difference(float a, float b)
{
	float d = a - b;

	if (d < -1e-9)
		return -1;
	if (d > 1e-9)
		return 1;

	return 0;
}

static bool
is_monotonic(const float *lut, unsigned len)
{
	unsigned i;
	int sign = sign_of_difference(lut[0], lut[len - 1]);

	if (sign == 0)
		return false;

	for (i = 1; i < len; i++) {
		if (sign != sign_of_difference(lut[i - 1], lut[i]))
			return false;
	}

	return true;
}

static bool
build_shaper(cmsContext lcms_ctx, cmsHTRANSFORM cmap_3dlut,
	     unsigned int len_shaper, float *shaper)
{
	float *curves[3];
	float divider = len_shaper - 1;
	float rgb_in[3], rgb_out[3];
	cmsToneCurve *tc[3] = { NULL };
	unsigned int ch, i;
	float smoothing_param;
	bool ret = true;

	/**
	 * We use cmsSmoothToneCurve() for:
	 *
	 * a) trying to ensure monotonicity;
	 * b) getting rid of abrupt changes;
	 *
	 * A lambda between 0.0 and 1.0 is usually enough. 1.0 means moderate to
	 * high smooth. We just want a mild smoothing, so we arbitrarily
	 * hardcoded this value.
	 */
	smoothing_param = 0.3f;

	curves[0] = &shaper[0];
	curves[1] = &shaper[len_shaper];
	curves[2] = &shaper[2 * len_shaper];

	for (i = 0; i < len_shaper; i++) {
		rgb_in[0] = rgb_in[1] = rgb_in[2] = (float)i / divider;
		cmsDoTransform(cmap_3dlut, rgb_in, rgb_out, 1);
		for (ch = 0; ch < 3; ch++)
			curves[ch][i] = ensure_unorm(rgb_out[ch]);
	}

	for (ch = 0; ch < 3; ch++) {
		/*
		 * If the curve is already monotonic, don't risk running the
		 * smoothing. It may break monotonicity for nearly zero gradient.
		 */
		if (is_monotonic(curves[ch], len_shaper))
			continue;

		tc[ch] = cmsBuildTabulatedToneCurveFloat(lcms_ctx, len_shaper,
							 curves[ch]);
		if (!tc[ch]) {
			ret = false;
			goto out;
		}

		/**
		 * TODO: that should fail if the produced curves are not
		 * monotonic. Try to smooth harder if that happens.
		 */
		ret = cmsSmoothToneCurve(tc[ch], smoothing_param);
		if (!ret)
			goto out;

		for (i = 0; i < len_shaper; i++)
			curves[ch][i] = cmsEvalToneCurveFloat(tc[ch],
							      (float)i / divider);
	}

out:
	cmsFreeToneCurveTriple(tc);
	return ret;
}

/**
 * Based on [1]. We get cmsHTRANSFORM cmap_3dlut and decompose into a shaper
 * (3x1D LUT) + 3D LUT. With that, we can reduce the 3D LUT dimension size
 * without loosing precision. 3D LUT dimension size is problematic because it
 * demands n³ memory. In this function we construct such shaper.
 *
 * [1] https://www.littlecms.com/ASICprelinerization_CGIV08.pdf
 */
static bool
xform_to_shaper_plus_3dlut(struct weston_color_transform *xform_base,
			   uint32_t len_shaper, float *shaper,
			   uint32_t len_lut3d, float *lut3d)
{
	struct cmlcms_color_transform *xform = to_cmlcms_xform(xform_base);
	struct weston_compositor *compositor = xform_base->cm->compositor;
	bool ret;

	ret = build_shaper(xform->lcms_ctx, xform->cmap_3dlut,
			   len_shaper, shaper);
	if (!ret)
		return false;

	ret = build_3d_lut(compositor, xform->cmap_3dlut,
			   len_shaper, shaper, len_lut3d, lut3d);
	if (!ret)
		return false;

	return true;
}

static void
cmlcms_color_transform_recipe_copy(struct cmlcms_color_transform_recipe *dst,
				   const struct cmlcms_color_transform_recipe *src)
{
	dst->category = src->category;
	dst->input_profile = ref_cprof(src->input_profile);
	dst->output_profile = ref_cprof(src->output_profile);
	dst->render_intent = src->render_intent;
}

static struct cmlcms_color_transform *
cmlcms_color_transform_create(struct weston_color_manager_lcms *cm,
			      const struct cmlcms_color_transform_recipe *recipe)
{
	struct cmlcms_color_transform *xform;
	const char *err_msg = NULL;
	bool ret = false;
	char *str;

	xform = xzalloc(sizeof *xform);
	weston_color_transform_init(&xform->base, &cm->base);
	wl_list_init(&xform->link);
	xform->base.to_shaper_plus_3dlut = xform_to_shaper_plus_3dlut;
	cmlcms_color_transform_recipe_copy(&xform->search_key, recipe);

	weston_log_scope_printf(cm->transforms_scope,
				"New color transformation: t%u\n", xform->base.id);
	str = cmlcms_color_transform_recipe_string(&xform->search_key);
	weston_log_scope_printf(cm->transforms_scope, "%s", str);
	free(str);

	if (!ensure_output_profile_extract(recipe->output_profile, cm->lcms_ctx,
					   cmlcms_reasonable_1D_points(), &err_msg))
		goto error;

	switch (get_transform_type(xform)) {
	case CMLCMS_BLEND_TO_ICC:
		ret = init_icc_to_icc_chain(xform);
		break;
	case CMLCMS_BLEND_TO_PARAM:
		ret = init_blend_to_parametric(xform);
		break;
	case CMLCMS_ICC_TO_ICC:
		ret = init_icc_to_icc_chain(xform);
		break;
	case CMLCMS_ICC_TO_PARAM:
		break;
	case CMLCMS_PARAM_TO_ICC:
		break;
	case CMLCMS_PARAM_TO_PARAM:
		ret = init_parametric_to_parametric(xform);
		break;
	}

	if (!ret) {
		err_msg = "creating color transformation failed";
		goto error;
	}

	wl_list_insert(&cm->color_transform_list, &xform->link);

	str = weston_color_transform_string(&xform->base);
	weston_log_scope_printf(cm->transforms_scope, "  %s", str);
	free(str);

	str = weston_color_transform_details_string(4, &xform->base);
	if (str) {
		weston_log_scope_printf(cm->transforms_scope, "%s", str);
		free(str);
	}

	return xform;

error:
	weston_log_scope_printf(cm->transforms_scope,
				"	%s\n", err_msg);
	cmlcms_color_transform_destroy(xform);
	return NULL;
}

static void
cmclms_adjust_recipe(struct cmlcms_color_transform_recipe *adjusted,
		     const struct cmlcms_color_transform_recipe *recipe,
		     struct weston_color_manager_lcms *cm)
{
	const struct cmlcms_color_profile *in_prof = recipe->input_profile;
	struct weston_color_profile_params tmp;
	struct weston_color_profile *replacement;
	char *errmsg;
	bool ret;

	cmlcms_color_transform_recipe_copy(adjusted, recipe);

	if (!in_prof)
		return;

	/*
	 * The standard sRGB display uses a power-2.2 EOTF. Anything that claims
	 * to be targeting a display with the sRGB two-piece TF is likely mistaken.
	 */
	if (in_prof->type == CMLCMS_PROFILE_TYPE_PARAMS &&
	    in_prof->params->tf_info->tf == WESTON_TF_SRGB) {
		tmp = *in_prof->params;
		tmp.tf_info = weston_color_tf_info_from(cm->base.compositor, WESTON_TF_GAMMA22);
		ret = cmlcms_get_color_profile_from_params(&cm->base,
							   &tmp, "override sRGB EOTF",
							   &replacement, &errmsg);
		if (ret) {
			weston_log_scope_printf(cm->transforms_scope,
						"Replacing profile p%u (%s) with profile p%u (%s)"
						"for color transformation.\n",
						in_prof->base.id,
						in_prof->params->tf_info->desc,
						replacement->id, tmp.tf_info->desc);
			unref_cprof(adjusted->input_profile);
			adjusted->input_profile = to_cmlcms_cprof(replacement);
		} else {
			weston_log("Warning: overriding sRGB two-piece TF with power-2.2 failed: %s\n", errmsg);
			free(errmsg);
		}
	}
}

static bool
transform_matches_recipe(const struct cmlcms_color_transform *xform,
			 const struct cmlcms_color_transform_recipe *recipe)
{
	if (xform->search_key.category != recipe->category)
		return false;

	if (xform->search_key.render_intent != recipe->render_intent ||
	    xform->search_key.output_profile != recipe->output_profile ||
	    xform->search_key.input_profile != recipe->input_profile)
		return false;

	return true;
}

struct cmlcms_color_transform *
cmlcms_color_transform_get(struct weston_color_manager_lcms *cm,
			   const struct cmlcms_color_transform_recipe *recipe)
{
	struct cmlcms_color_transform_recipe adjusted;
	struct cmlcms_color_transform *xform = NULL;
	struct cmlcms_color_transform *it;

	weston_assert_ptr_not_null(cm->base.compositor, recipe->output_profile);
	switch (recipe->category) {
	case CMLCMS_CATEGORY_BLEND_TO_OUTPUT:
		weston_assert_ptr_null(cm->base.compositor, recipe->render_intent);
		weston_assert_ptr_null(cm->base.compositor, recipe->input_profile);
		break;
	case CMLCMS_CATEGORY_INPUT_TO_OUTPUT:
	case CMLCMS_CATEGORY_INPUT_TO_BLEND:
		weston_assert_ptr_not_null(cm->base.compositor, recipe->render_intent);
		weston_assert_ptr_not_null(cm->base.compositor, recipe->input_profile);
		break;
	}

	cmclms_adjust_recipe(&adjusted, recipe, cm);

	wl_list_for_each(it, &cm->color_transform_list, link) {
		if (transform_matches_recipe(it, &adjusted)) {
			weston_color_transform_ref(&it->base);
			xform = it;
		}
	}

	if (!xform)
		xform = cmlcms_color_transform_create(cm, &adjusted);

	if (!xform)
		weston_log("color-lcms error: failed to create a color transformation.\n");

	unref_cprof(adjusted.input_profile);
	unref_cprof(adjusted.output_profile);

	return xform;
}

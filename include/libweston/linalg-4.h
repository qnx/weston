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

#pragma once

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <math.h>

#include <libweston/linalg-types.h>

/* ================= 4-vectors and 4x4 matrices ============== */

/** Construct a column vector from elements */
#define WESTON_VEC4F(x, y, z, w) ((struct weston_vec4f){ .el = { (x), (y), (z), (w) }})

/** Construct the [0, 0, 0, 0]^T vector */
#define WESTON_VEC4F_ZERO ((struct weston_vec4f){ .el = {}})

/** Construct matrix from elements a{row}{column} */
#define WESTON_MAT4F(a00, a01, a02, a03,			\
		a10, a11, a12, a13,				\
		a20, a21, a22, a23,				\
		a30, a31, a32, a33)				\
	((struct weston_mat4f){ .colmaj = {			\
		a00, a10, a20, a30,				\
		a01, a11, a21, a31,				\
		a02, a12, a22, a32,				\
		a03, a13, a23, a33,				\
	}})

/** Construct the identity 4x4 matrix */
#define WESTON_MAT4F_IDENTITY					\
	((struct weston_mat4f){ .colmaj = {			\
		1.0f, 0.0f, 0.0f, 0.0f,				\
		0.0f, 1.0f, 0.0f, 0.0f,				\
		0.0f, 0.0f, 1.0f, 0.0f,				\
		0.0f, 0.0f, 0.0f, 1.0f,				\
	}})

/** Construct a translation matrix */
static inline struct weston_mat4f
weston_m4f_translation(float tx, float ty, float tz)
{
	return WESTON_MAT4F(
		1.0f, 0.0f, 0.0f,  tx,
		0.0f, 1.0f, 0.0f,  ty,
		0.0f, 0.0f, 1.0f,  tz,
		0.0f, 0.0f, 0.0f, 1.0f);
}

/** Construct a scaling matrix */
static inline struct weston_mat4f
weston_m4f_scaling(float sx, float sy, float sz)
{
	return WESTON_MAT4F(
		  sx, 0.0f, 0.0f, 0.0f,
		0.0f,   sy, 0.0f, 0.0f,
		0.0f, 0.0f,   sz, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}

/** Construct a 2D x-y rotation matrix
 *
 * \param cos_th Cosine of the counter-clockwise angle.
 * \param sin_th Sine of the counter-clockwise angle.
 */
static inline struct weston_mat4f
weston_m4f_rotation_xy(float cos_th, float sin_th)
{
	return WESTON_MAT4F(
		cos_th, -sin_th, 0.0f, 0.0f,
		sin_th,  cos_th, 0.0f, 0.0f,
		  0.0f,    0.0f, 1.0f, 0.0f,
		  0.0f,    0.0f, 0.0f, 1.0f);
}

static inline struct weston_mat4f
weston_m4f_from_m3f_v3f(struct weston_mat3f R, struct weston_vec3f t)
{
	return WESTON_MAT4F(
		R.col[0].el[0], R.col[1].el[0], R.col[2].el[0], t.el[0],
		R.col[0].el[1], R.col[1].el[1], R.col[2].el[1], t.el[1],
		R.col[0].el[2], R.col[1].el[2], R.col[2].el[2], t.el[2],
		          0.0f,           0.0f,           0.0f,    1.0f
	);
}

/** 4-vector dot product */
static inline float
weston_v4f_dot_v4f(struct weston_vec4f a, struct weston_vec4f b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/**
 * Matrix infinity-norm
 *
 * http://www.netlib.org/lapack/lug/node75.html
 */
static inline float
weston_m4f_inf_norm(struct weston_mat4f M)
{
	unsigned row;
	double infnorm = -1.0;

	for (row = 0; row < 4; row++) {
		unsigned col;
		double sum = 0.0;

		for (col = 0; col < 4; col++)
			sum += fabsf(M.col[col].el[row]);

		if (infnorm < sum)
			infnorm = sum;
	}

	return infnorm;
}

/** Transpose 4x4 matrix */
static inline struct weston_mat4f
weston_m4f_transpose(struct weston_mat4f M)
{
	struct weston_mat4f R;
	unsigned i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			R.col[j].el[i] = M.col[i].el[j];

	return R;
}

/** Matrix-vector multiplication A * b */
static inline struct weston_vec4f
weston_m4f_mul_v4f(struct weston_mat4f A, struct weston_vec4f b)
{
	struct weston_vec4f result;
	unsigned r;

	for (r = 0; r < 4; r++) {
		struct weston_vec4f row =
			WESTON_VEC4F(A.col[0].el[r], A.col[1].el[r], A.col[2].el[r], A.col[3].el[r]);
		result.el[r] = weston_v4f_dot_v4f(row, b);
	}
	return result;
}

/** Matrix multiplication A * B */
static inline struct weston_mat4f
weston_m4f_mul_m4f(struct weston_mat4f A, struct weston_mat4f B)
{
	struct weston_mat4f result;
	unsigned c;

	for (c = 0; c < 4; c++)
		result.col[c] = weston_m4f_mul_v4f(A, B.col[c]);

	return result;
}

/** Element-wise matrix subtraction A - B */
static inline struct weston_mat4f
weston_m4f_sub_m4f(struct weston_mat4f A, struct weston_mat4f B)
{
	struct weston_mat4f R;
	unsigned i;

	for (i = 0; i < 4 * 4; i++)
		R.colmaj[i] = A.colmaj[i] - B.colmaj[i];

	return R;
}

bool
weston_m4f_invert(struct weston_mat4f *out, struct weston_mat4f M);

#ifdef  __cplusplus
}
#endif

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

/* ================= 3-vectors and 3x3 matrices ============== */

/** Construct a column vector from elements */
#define WESTON_VEC3F(x, y, z) ((struct weston_vec3f){ .el = { (x), (y), (z) }})

/** Construct the [0, 0, 0]^T vector */
#define WESTON_VEC3F_ZERO ((struct weston_vec3f){ .el = {}})

/** Construct matrix from elements a{row}{column} */
#define WESTON_MAT3F(a00, a01, a02,					\
		a10, a11, a12,					\
		a20, a21, a22)					\
	((struct weston_mat3f){ .colmaj = {				\
		a00, a10, a20,					\
		a01, a11, a21,					\
		a02, a12, a22,					\
	}})

/** Construct the identity 3x3 matrix */
#define WESTON_MAT3F_IDENTITY					\
	((struct weston_mat3f){ .colmaj = {				\
		1.0f, 0.0f, 0.0f,					\
		0.0f, 1.0f, 0.0f,					\
		0.0f, 0.0f, 1.0f,					\
	}})

/** Construct a diagonal matrix */
static inline struct weston_mat3f
weston_m3f_diag(struct weston_vec3f d)
{
	return WESTON_MAT3F(
		 d.x, 0.0f, 0.0f,
		0.0f,  d.y, 0.0f,
		0.0f, 0.0f,  d.z);
}

/** Copy the top-left 3x3 from 4x4 */
static inline struct weston_mat3f
weston_m3f_from_m4f_xyz(struct weston_mat4f M)
{
	return WESTON_MAT3F(
		M.col[0].el[0], M.col[1].el[0], M.col[2].el[0],
		M.col[0].el[1], M.col[1].el[1], M.col[2].el[1],
		M.col[0].el[2], M.col[1].el[2], M.col[2].el[2]
	);
}

/** Drop w from vec4f */
static inline struct weston_vec3f
weston_v3f_from_v4f_xyz(struct weston_vec4f v)
{
	return WESTON_VEC3F(v.x, v.y, v.z);
}

/** 3-vector dot product */
static inline float
weston_v3f_dot_v3f(struct weston_vec3f a, struct weston_vec3f b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
 * Matrix infinity-norm
 *
 * http://www.netlib.org/lapack/lug/node75.html
 */
static inline float
weston_m3f_inf_norm(struct weston_mat3f M)
{
	unsigned row;
	double infnorm = -1.0;

	for (row = 0; row < 3; row++) {
		unsigned col;
		double sum = 0.0;

		for (col = 0; col < 3; col++)
			sum += fabsf(M.col[col].el[row]);

		if (infnorm < sum)
			infnorm = sum;
	}

	return infnorm;
}

/** Transpose 3x3 matrix */
static inline struct weston_mat3f
weston_m3f_transpose(struct weston_mat3f M)
{
	struct weston_mat3f R;
	unsigned i, j;

	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			R.col[j].el[i] = M.col[i].el[j];

	return R;
}

/** Matrix-vector multiplication A * b */
static inline struct weston_vec3f
weston_m3f_mul_v3f(struct weston_mat3f A, struct weston_vec3f b)
{
	struct weston_vec3f result;
	unsigned r;

	for (r = 0; r < 3; r++) {
		struct weston_vec3f row =
			WESTON_VEC3F(A.col[0].el[r], A.col[1].el[r], A.col[2].el[r]);
		result.el[r] = weston_v3f_dot_v3f(row, b);
	}
	return result;
}

/** Matrix multiplication A * B */
static inline struct weston_mat3f
weston_m3f_mul_m3f(struct weston_mat3f A, struct weston_mat3f B)
{
	struct weston_mat3f result;
	unsigned c;

	for (c = 0; c < 3; c++)
		result.col[c] = weston_m3f_mul_v3f(A, B.col[c]);

	return result;
}

/** Element-wise matrix subtraction A - B */
static inline struct weston_mat3f
weston_m3f_sub_m3f(struct weston_mat3f A, struct weston_mat3f B)
{
	struct weston_mat3f R;
	unsigned i;

	for (i = 0; i < 3 * 3; i++)
		R.colmaj[i] = A.colmaj[i] - B.colmaj[i];

	return R;
}

bool
weston_m3f_invert(struct weston_mat3f *out, struct weston_mat3f M);

#ifdef  __cplusplus
}
#endif

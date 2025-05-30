/*
 * Copyright © 2011 Intel Corporation
 * Copyright © 2012 Collabora, Ltd.
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
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <wayland-server.h>
#include <libweston/matrix.h>
#include <libweston/linalg-4.h>
#include <libweston/linalg-3.h>

/*
 * Matrices are stored in column-major order, that is the array indices are:
 *  0  4  8 12
 *  1  5  9 13
 *  2  6 10 14
 *  3  7 11 15
 */

WL_EXPORT void
weston_matrix_init(struct weston_matrix *matrix)
{
	matrix->M = WESTON_MAT4F_IDENTITY;
	matrix->type = 0;
}

/* m <- n * m, that is, m is multiplied on the LEFT. */
WL_EXPORT void
weston_matrix_multiply(struct weston_matrix *m, const struct weston_matrix *n)
{
	m->M = weston_m4f_mul_m4f(n->M, m->M);
	m->type |= n->type;
}

WL_EXPORT void
weston_matrix_translate(struct weston_matrix *matrix, float x, float y, float z)
{
	matrix->M = weston_m4f_mul_m4f(weston_m4f_translation(x, y, z), matrix->M);
	matrix->type |= WESTON_MATRIX_TRANSFORM_TRANSLATE;
}

WL_EXPORT void
weston_matrix_scale(struct weston_matrix *matrix, float x, float y,float z)
{
	matrix->M = weston_m4f_mul_m4f(weston_m4f_scaling(x, y, z), matrix->M);
	matrix->type |= WESTON_MATRIX_TRANSFORM_SCALE;
}

WL_EXPORT void
weston_matrix_rotate_xy(struct weston_matrix *matrix, float cos, float sin)
{
	matrix->M = weston_m4f_mul_m4f(weston_m4f_rotation_xy(cos, sin), matrix->M);
	matrix->type |= WESTON_MATRIX_TRANSFORM_ROTATE;
}

/* v <- m * v */
WL_EXPORT void
weston_matrix_transform(const struct weston_matrix *matrix,
			struct weston_vector *v)
{
	v->v = weston_m4f_mul_v4f(matrix->M, v->v);
}

WL_EXPORT struct weston_coord
weston_matrix_transform_coord(const struct weston_matrix *matrix,
			      struct weston_coord c)
{
	struct weston_coord out;
	struct weston_vector t = { .v.el = { c.x, c.y, 0.0, 1.0 } };

	weston_matrix_transform(matrix, &t);

	assert(fabsf(t.v.el[3]) > 1e-6);

	out.x = t.v.el[0] / t.v.el[3];
	out.y = t.v.el[1] / t.v.el[3];
	return out;
}

WL_EXPORT int
weston_matrix_invert(struct weston_matrix *inverse,
		     const struct weston_matrix *matrix)
{
	if (weston_m4f_invert(&inverse->M, matrix->M)) {
		inverse->type = matrix->type;
		return 0;
	}

	return -1;
}

static inline void
swap_rows(double *a, double *b)
{
	unsigned k;
	double tmp;

	for (k = 0; k < 13; k += 4) {
		tmp = a[k];
		a[k] = b[k];
		b[k] = tmp;
	}
}

static inline void
swap_unsigned(unsigned *a, unsigned *b)
{
	unsigned tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static inline unsigned
find_pivot(double *column, unsigned k)
{
	unsigned p = k;
	for (++k; k < 4; ++k)
		if (fabs(column[p]) < fabs(column[k]))
			p = k;

	return p;
}

static bool
m4f_LU_decompose(double *restrict LU, unsigned *restrict p, struct weston_mat4f M)
{
	unsigned i, j, k;
	unsigned pivot;
	double pv;

	for (i = 0; i < 4; ++i)
		p[i] = i;
	for (i = 16; i--; )
		LU[i] = M.colmaj[i];

	/* LU decomposition with partial pivoting */
	for (k = 0; k < 4; ++k) {
		pivot = find_pivot(&LU[k * 4], k);
		if (pivot != k) {
			swap_unsigned(&p[k], &p[pivot]);
			swap_rows(&LU[k], &LU[pivot]);
		}

		pv = LU[k * 4 + k];
		if (fabs(pv) < 1e-9)
			return false; /* zero pivot, error */

		for (i = k + 1; i < 4; ++i) {
			LU[i + k * 4] /= pv;

			for (j = k + 1; j < 4; ++j)
				LU[i + j * 4] -= LU[i + k * 4] * LU[k + j * 4];
		}
	}

	return true;
}

static inline void
m4f_LU_inverse_transform(const double *restrict A,
			 const unsigned *restrict p,
			 struct weston_vec4f *restrict v)
{
	/* Solve A * x = v, when we have P * A = L * U.
	 * P * A * x = P * v  =>  L * U * x = P * v
	 * Let U * x = b, then L * b = P * v.
	 */
	double b[4];
	unsigned j;

	/* Forward substitution, column version, solves L * b = P * v */
	/* The diagonal of L is all ones, and not explicitly stored. */
	b[0] = v->el[p[0]];
	b[1] = v->el[p[1]] - b[0] * A[1 + 0 * 4];
	b[2] = v->el[p[2]] - b[0] * A[2 + 0 * 4] - b[1] * A[2 + 1 * 4];
	b[3] = v->el[p[3]] - b[0] * A[3 + 0 * 4] - b[1] * A[3 + 1 * 4] - b[2] * A[3 + 2 * 4];

	/* backward substitution, column version, solves U * y = b */
	for (j = 3; j > 0; --j) {
		unsigned k;
		b[j] /= A[j + j * 4];
		for (k = 0; k < j; ++k)
			b[k] -= b[j] * A[k + j * 4];
	}

	b[0] /= A[0 + 0 * 4];

	/* the result */
	for (j = 0; j < 4; ++j)
		v->el[j] = b[j];
}

/** Invert 4x4 matrix
 *
 * reference: Gene H. Golub and Charles F. van Loan. Matrix computations.
 * 3rd ed. The Johns Hopkins University Press. 1996.
 * LU decomposition, forward and back substitution: Chapter 3.
 *
 * \param[out] out Destination to save the inverted matrix.
 * \param M The matrix to invert.
 * \return True for success, false for failure. On failure,
 * \c *out remains unchanged.
 */
WL_EXPORT bool
weston_m4f_invert(struct weston_mat4f *out, struct weston_mat4f M)
{
	double LU[16];		/* column-major */
	unsigned perm[4];	/* permutation */
	unsigned c;

	if (!m4f_LU_decompose(LU, perm, M))
		return false;

	*out = WESTON_MAT4F_IDENTITY;
	for (c = 0; c < 4; ++c)
		m4f_LU_inverse_transform(LU, perm, &out->col[c]);

	return true;
}

static inline void
swap_rows3(double *restrict a, double *restrict b)
{
	unsigned k;
	double tmp;

	for (k = 0; k < 7; k += 3) {
		tmp = a[k];
		a[k] = b[k];
		b[k] = tmp;
	}
}

static inline unsigned
find_pivot3(double *column, unsigned k)
{
	unsigned p = k;
	for (++k; k < 3; ++k)
		if (fabs(column[p]) < fabs(column[k]))
			p = k;

	return p;
}

static inline bool
m3f_LU_decompose(double *restrict LU, unsigned *restrict p, struct weston_mat3f M)
{
	unsigned i, j, k;
	unsigned pivot;
	double pv;

	for (i = 0; i < 3; ++i)
		p[i] = i;
	for (i = 9; i--; )
		LU[i] = M.colmaj[i];

	/* LU decomposition with partial pivoting */
	for (k = 0; k < 3; ++k) {
		pivot = find_pivot3(&LU[k * 3], k);
		if (pivot != k) {
			swap_unsigned(&p[k], &p[pivot]);
			swap_rows3(&LU[k], &LU[pivot]);
		}

		pv = LU[k * 3 + k];
		if (fabs(pv) < 1e-9)
			return false; /* zero pivot, error */

		for (i = k + 1; i < 3; ++i) {
			LU[i + k * 3] /= pv;

			for (j = k + 1; j < 3; ++j)
				LU[i + j * 3] -= LU[i + k * 3] * LU[k + j * 3];
		}
	}

	return true;
}

static inline void
m3f_LU_inverse_transform(const double *restrict A,
			const unsigned *restrict p,
			struct weston_vec3f *restrict v)
{
	/* Solve A * x = v, when we have P * A = L * U.
	 * P * A * x = P * v  =>  L * U * x = P * v
	 * Let U * x = b, then L * b = P * v.
	 */
	double b[3];
	unsigned j;

	/* Forward substitution, column version, solves L * b = P * v */
	/* The diagonal of L is all ones, and not explicitly stored. */
	b[0] = v->el[p[0]];
	b[1] = v->el[p[1]] - b[0] * A[1 + 0 * 3];
	b[2] = v->el[p[2]] - b[0] * A[2 + 0 * 3] - b[1] * A[2 + 1 * 3];

	/* backward substitution, column version, solves U * y = b */
	for (j = 2; j > 0; --j) {
		unsigned k;
		b[j] /= A[j + j * 3];
		for (k = 0; k < j; ++k)
			b[k] -= b[j] * A[k + j * 3];
	}

	b[0] /= A[0 + 0 * 3];

	/* the result */
	for (j = 0; j < 3; ++j)
		v->el[j] = b[j];
}

/** Invert 3x3 matrix
 *
 * reference: Gene H. Golub and Charles F. van Loan. Matrix computations.
 * 3rd ed. The Johns Hopkins University Press. 1996.
 * LU decomposition, forward and back substitution: Chapter 3.
 *
 * \param[out] out Destination to save the inverted matrix.
 * \param M The matrix to invert.
 * \return True for success, false for failure. On failure,
 * \c *out remains unchanged.
 */
WL_EXPORT bool
weston_m3f_invert(struct weston_mat3f *out, struct weston_mat3f M)
{
	double LU[9];		/* column-major */
	unsigned perm[3];	/* permutation */
	unsigned c;

	if (!m3f_LU_decompose(LU, perm, M))
		return false;

	*out = WESTON_MAT3F_IDENTITY;
	for (c = 0; c < 3; ++c)
		m3f_LU_inverse_transform(LU, perm, &out->col[c]);

	return true;
}

static bool
near_zero(float a)
{
	if (fabs(a) > 0.00001)
		return false;

	return true;
}

static float
get_el(const struct weston_matrix *matrix, int row, int col)
{
	assert(row >= 0 && row <= 3);
	assert(col >= 0 && col <= 3);

	return matrix->M.col[col].el[row];
}

static bool
near_zero_at(const struct weston_matrix *matrix, int row, int col)
{
	return near_zero(get_el(matrix, row, col));
}

static bool
near_one_at(const struct weston_matrix *matrix, int row, int col)
{
	return near_zero(get_el(matrix, row, col) - 1.0);
}

static bool
near_pm_one_at(const struct weston_matrix *matrix, int row, int col)
{
	return near_zero(fabs(get_el(matrix, row, col)) - 1.0);
}

static bool
near_int_at(const struct weston_matrix *matrix, int row, int col)
{
	float el = get_el(matrix, row, col);

	return near_zero(roundf(el) - el);
}

/* Lazy decompose the matrix to figure out whether its operations will
 * cause an image to look ugly without some kind of filtering.
 *
 * while this is a 3D transformation matrix, we only concern ourselves
 * with 2D for this test. We do use some small rounding to try to catch
 * sequences of operations that lead back to a matrix that doesn't
 * require filters.
 *
 * We assume the matrix won't be used to transform a vector with w != 1.0
 *
 * Filtering will be necessary when:
 *  a non-integral translation is applied
 *  non-affine (perspective) translation is in use
 *  any scaling (other than -1) is in use
 *  a rotation that isn't a multiple of 90 degrees about Z is present
 */
WL_EXPORT bool
weston_matrix_needs_filtering(const struct weston_matrix *matrix)
{
	/* check for non-integral X/Y translation - ignore Z */
	if (!near_int_at(matrix, 0, 3) ||
	    !near_int_at(matrix, 1, 3))
		return true;

	/* Any transform matrix that matches this will be non-affine. */
	if (!near_zero_at(matrix, 3, 0) ||
	    !near_zero_at(matrix, 3, 1) ||
	    !near_zero_at(matrix, 3, 2) ||
	    !near_pm_one_at(matrix, 3, 3))
		return true;

	/* Check for anything that could come from a rotation that isn't
	 * around the Z axis:
	 * [  ?   ?  0  ? ]
	 * [  ?   ?  0  ? ]
	 * [  0   0 ±1  ? ]
	 * [  ?   ?  ?  1 ]
	 * It's not clear that we'd realistically see a -1 in [2][2], but
	 * it wouldn't require filtering if we did, so allow it.
	 */
	if (!near_zero_at(matrix, 0, 2) ||
	    !near_zero_at(matrix, 1, 2) ||
	    !near_zero_at(matrix, 2, 0) ||
	    !near_zero_at(matrix, 2, 1) ||
	    !near_pm_one_at(matrix, 2, 2))
		return true;

	/* We've culled the low hanging fruit, now let's match the only
	 * matrices left we don't have to filter, before defaulting to
	 * filtering.
	 *
	 * These are a combination of testing rotation and scaling at once: */
	if (near_pm_one_at(matrix, 0, 0)) {
		/* This could be a multiple of 90 degree rotation about Z,
		 * possibly with a flip, if the matrix is of the form:
		 * [  ±1  0  0  ? ]
		 * [  0  ±1  0  ? ]
		 * [  0   0  1  ? ]
		 * [  0   0  0  1 ]
		 * Forcing ±1 excludes non-unity scale.
		 */
		if (near_zero_at(matrix, 1, 0) &&
		    near_zero_at(matrix, 0, 1) &&
		    near_pm_one_at(matrix, 1, 1))
			return false;
	}
	if (near_zero_at(matrix, 0, 0)) {
		/* This could be a multiple of 90 degree rotation about Z,
		 * possibly with a flip, if the matrix is of the form:
		 * [  0  ±1  0  ? ]
		 * [  ±1  0  0  ? ]
		 * [  0   0  1  ? ]
		 * [  0   0  0  1 ]
		 * Forcing ±1 excludes non-unity scale.
		 */
		if (near_zero_at(matrix, 1, 1) &&
		    near_pm_one_at(matrix, 1, 0) &&
		    near_pm_one_at(matrix, 0, 1))
			return false;
	}

	/* The matrix wasn't "simple" enough to classify with dumb
	 * heuristics, so recommend filtering */
	return true;
}

/** Examine a matrix to see if it applies a standard output transform.
 *
 * \param mat matrix to examine
 * \param[out] transform the transform, if applicable
 * \return true if a standard transform is present

 * Note that the check only considers rotations and flips.
 * If any other scale or translation is present, those may have to
 * be dealt with by the caller in some way.
 */
WL_EXPORT bool
weston_matrix_to_transform(const struct weston_matrix *mat,
			   enum wl_output_transform *transform)
{
	/* As a first pass we can eliminate any matrix that doesn't have
	 * zeroes in these positions:
	 * [  ?   ?  0  ? ]
	 * [  ?   ?  0  ? ]
	 * [  0   0  ?  ? ]
	 * [  0   0  0  ? ]
	 * As they will be non-affine, or rotations about axes
	 * other than Z.
	 */
	if (!near_zero_at(mat, 2, 0) ||
	    !near_zero_at(mat, 3, 0) ||
	    !near_zero_at(mat, 2, 1) ||
	    !near_zero_at(mat, 3, 1) ||
	    !near_zero_at(mat, 0, 2) ||
	    !near_zero_at(mat, 1, 2) ||
	    !near_zero_at(mat, 3, 2))
		return false;

	/* Enforce the form:
	 * [  ?   ?  0  ? ]
	 * [  ?   ?  0  ? ]
	 * [  0   0  ?  ? ]
	 * [  0   0  0  1 ]
	 * While we could scale all the elements by a constant to make
	 * 3,3 == 1, we choose to be lazy and not bother. A matrix
	 * that doesn't fit this form seems likely to be too complicated
	 * to pass the other checks.
	 */
	if (!near_one_at(mat, 3, 3))
		return false;

	if (near_zero_at(mat, 0, 0)) {
		if (!near_zero_at(mat, 1, 1))
			return false;

		/* We now have a matrix like:
		 * [  0   A  0  ? ]
		 * [  B   0  0  ? ]
		 * [  0   0  ?  ? ]
		 * [  0   0  0  1 ]
		 * When transforming a vector with a matrix of this form, the X
		 * and Y coordinates are effectively exchanged, so we have a
		 * 90 or 270 degree rotation (not 0 or 180), and could have
		 * a flip depending on the signs of A and B.
		 *
		 * We don't require A and B to have the same absolute value,
		 * so there may be independent scales in the X or Y dimensions.
		 */
		if (get_el(mat, 0, 1) > 0) {
			/*  A is positive */

			if (get_el(mat, 1, 0) > 0)
				*transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			else
				*transform = WL_OUTPUT_TRANSFORM_90;
		} else {
			/* A is negative */

			if (get_el(mat, 1, 0) > 0)
				*transform = WL_OUTPUT_TRANSFORM_270;
			else
				*transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
		}
	} else if (near_zero_at(mat, 1, 0)) {
		if (!near_zero_at(mat, 0, 1))
			return false;

		/* We now have a matrix like:
		 * [  A   0  0  ? ]
		 * [  0   B  0  ? ]
		 * [  0   0  ?  ? ]
		 * [  0   0  0  1 ]
		 * This case won't exchange the X and Y inputs, so the
		 * transform is 0 or 180 degrees. We could have a flip
		 * depending on the signs of A and B.
		 *
		 * We don't require A and B to have the same absolute value,
		 * so there may be independent scales in the X or Y dimensions.
		 */
		if (get_el(mat, 0, 0) > 0) {
			/* A is positive */

			if (get_el(mat, 1, 1) > 0)
				*transform = WL_OUTPUT_TRANSFORM_NORMAL;
			else
				*transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
		} else {
			/* A is negative */

			if (get_el(mat, 1, 1) > 0)
				*transform = WL_OUTPUT_TRANSFORM_FLIPPED;
			else
				*transform = WL_OUTPUT_TRANSFORM_180;
		}
	} else {
		return false;
	}

	return true;
}

WL_EXPORT void
weston_matrix_init_transform(struct weston_matrix *matrix,
			     enum wl_output_transform transform,
			     int x, int y, int width, int height,
			     int scale)
{
	weston_matrix_init(matrix);

	weston_matrix_translate(matrix, -x, -y, 0);

	switch (transform) {
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		weston_matrix_scale(matrix, -1, 1, 1);
		weston_matrix_translate(matrix, width, 0, 0);
		break;
	default:
		break;
	}

	switch (transform) {
	default:
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		break;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		weston_matrix_rotate_xy(matrix, 0, -1);
		weston_matrix_translate(matrix, 0, width, 0);
		break;
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		weston_matrix_rotate_xy(matrix, -1, 0);
		weston_matrix_translate(matrix,
					width, height, 0);
		break;
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		weston_matrix_rotate_xy(matrix, 0, 1);
		weston_matrix_translate(matrix, height, 0, 0);
		break;
	}

	weston_matrix_scale(matrix, scale, scale, 1);
}

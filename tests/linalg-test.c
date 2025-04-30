/*
 * Copyright 2022, 2025 Collabora, Ltd.
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
#include <libweston/linalg.h>
#include "weston-test-client-helper.h"
#include "weston-test-assert.h"

static void
print_mat3(struct weston_mat3f M)
{
	unsigned r, c;

	for (r = 0; r < 3; ++r) {
		for (c = 0; c < 3; ++c)
			testlog(" %14.6e", M.col[c].el[r]);
		testlog("\n");
	}
}

/*
 * Test various ways of accessing the vector elements,
 * make sure they are consistent.
 */
TEST(vec3_layout)
{
	struct weston_vec3f v;
	unsigned i;

	static_assert(sizeof(v) == 3 * sizeof(float), "vec3 storage");

	v = WESTON_VEC3F(1, 2, 3);
	test_assert_f64_eq(v.x, 1);
	test_assert_f64_eq(v.y, 2);
	test_assert_f64_eq(v.z, 3);

	for (i = 0; i < 3; i++)
		test_assert_f64_eq(v.el[i], i + 1);

	return RESULT_OK;
}

/*
 * Test various ways of accessing the matrix elements,
 * make sure they are consistent.
 */
TEST(mat3_layout)
{
	struct weston_mat3f M;
	unsigned row, col, i;

	static_assert(sizeof(M.col) == sizeof(M.colmaj), "mat3 storage");

	M = WESTON_MAT3F(
		1, 2, 3,
		4, 5, 6,
		7, 8, 9
	);

	for (row = 0; row < 3; row++)
		for (col = 0; col < 3; col++)
			test_assert_f64_eq(M.col[col].el[row], 1 + col + 3 * row);

	M = weston_m3f_transpose(M);

	for (i = 0; i < 9; i++)
		test_assert_f64_eq(M.colmaj[i], i + 1);

	return RESULT_OK;
}

TEST(mat3_inf_norm)
{
	struct weston_mat3f M = WESTON_MAT3F(
		1, 2, 3,
		13, 14, 15, /* <- sum */
		5, 6, 7
	);

	test_assert_f64_eq(weston_m3f_inf_norm(M), 42.0);

	return RESULT_OK;
}

struct test_matrix3 {
	/* the matrix to test */
	struct weston_mat3f M;

	/*
	 * Residual error limit; inf norm(M * inv(M) - I) < err_limit
	 * The residual error as calculated here represents the relative
	 * error added by transforming a vector with inv(M).
	 */
	double err_limit;
};

static const struct test_matrix3 matrices3[] = {
	/* A very trivial case. */
	{
		.M = WESTON_MAT3F(
			1, 0, 0,
		        0, 2, 0,
		        0, 0, 3),
		.err_limit = 0.0,
	},

	/* See the description in matrices4[] */
	{
		.M = WESTON_MAT3F(
			1, 0, 1980,
		        0, 1, 1080,
		        0, 0, 1),
		.err_limit = 0.0,
	},

	/*
	 * If you want to verify the matrices in Octave, type this:
	 * M = [ <paste the series of numbers> ]
	 * mat = reshape(M, 3, 3)
	 * det(mat)
	 * cond(mat)
	 */

	/* cond = 1e3, abs det = 1 */
	{
		.M = WESTON_MAT3F(
			-3.85619916,  -7.33213522, -17.39592142,
			3.68083576,   6.9908134,   16.69315075,
			2.24593119,   6.73273163,  15.43687958
		),
		.err_limit = 1e-4,
	},

	/* cond = 1e3, abs det = 15 */
	{
		.M = WESTON_MAT3F(
			-24.17876224,  31.41542335,  29.67758047,
 			27.80376451, -37.71058091, -35.15458289,
			4.70529412, -10.23486155,  -8.8383264
		),
		.err_limit = 1e-4,
	},

	/* cond = 700, abs det = 1e-6, invertible regardless of det */
	{
		.M = WESTON_MAT3F(
			-0.1494663,   0.15094259, -0.0227504,
			-0.03434422,  0.03261981,  0.00269234,
			-0.10630476,  0.10418501, -0.00725791
		),
		.err_limit = 1e-4,
	},

	/* cond = 1e6, abs det = 1, this is a little more challenging */
	{
		.M = WESTON_MAT3F(
			-4.76473003, -247.24422465,  181.83067879,
			-8.99040059, -502.78411442,  370.79353696,
			11.30800122,  578.40401799, -425.14300652
		),
		.err_limit = 0.02,
	},

	/* cond = 15, abs det = 1e-9, should be well invertible */
	{
		.M = WESTON_MAT3F(
			-0.00114829, -0.00051657,  0.00126965,
			-0.00181574,  0.00044979,  0.00049775,
			-0.00234378,  0.00010053,  0.00190233
		),
		.err_limit = 1e-6,
	},
};

TEST_P(mat3_inversion_precision, matrices3)
{
	const struct test_matrix3 *tm = data;
	struct weston_mat3f rr;
	double err;

	/* Compute rr = M * inv(M) */
	test_assert_true(weston_m3f_invert(&rr, tm->M));
	rr = weston_m3f_mul_m3f(tm->M, rr);

	/* Residual: subtract identity matrix (expected result) */
	rr = weston_m3f_sub_m3f(rr, WESTON_MAT3F_IDENTITY);

	/*
	 * Infinity norm of the residual is our measure.
	 * See https://gitlab.freedesktop.org/pq/fourbyfour/-/blob/master/README.d/precision_testing.md
	 */
	err = weston_m3f_inf_norm(rr);
	testlog("Residual error %g (%.1f bits precision), limit %g.\n",
		err, -log2(err), tm->err_limit);

	if (err > tm->err_limit) {
		testlog("Error is too high for matrix\n");
		print_mat3(tm->M);
		test_assert_true(false);
	}

	return RESULT_OK;
}

static void
print_mat4(struct weston_mat4f M)
{
	unsigned r, c;

	for (r = 0; r < 4; ++r) {
		for (c = 0; c < 4; ++c)
			testlog(" %14.6e", M.col[c].el[r]);
		testlog("\n");
	}
}

/*
 * Test various ways of accessing the vector elements,
 * make sure they are consistent.
 */
TEST(vec4_layout)
{
	struct weston_vec4f v;
	unsigned i;

	static_assert(sizeof(v) == 4 * sizeof(float), "vec4 storage");

	v = WESTON_VEC4F(1, 2, 3, 4);
	test_assert_f32_eq(v.x, 1);
	test_assert_f32_eq(v.y, 2);
	test_assert_f32_eq(v.z, 3);
	test_assert_f32_eq(v.w, 4);

	for (i = 0; i < 4; i++)
		test_assert_f32_eq(v.el[i], i + 1);

	return RESULT_OK;
}

/*
 * Test various ways of accessing the matrix elements,
 * make sure they are consistent.
 */
TEST(mat4_layout)
{
	struct weston_mat4f M;
	unsigned row, col, i;

	static_assert(sizeof(M.col) == sizeof(M.colmaj), "mat4 storage");

	M = WESTON_MAT4F(
		1, 2, 3, 4,
		5, 6, 7, 8,
		9, 10, 11, 12,
		13, 14, 15, 16
	);

	for (row = 0; row < 4; row++)
		for (col = 0; col < 4; col++)
			test_assert_f32_eq(M.col[col].el[row], 1 + col + 4 * row);

	M = weston_m4f_transpose(M);

	for (i = 0; i < 16; i++)
		test_assert_f32_eq(M.colmaj[i], i + 1);

	return RESULT_OK;
}

TEST(mat4_inf_norm)
{
	struct weston_mat4f M = WESTON_MAT4F(
		1, 2, 3, 4,
		13, 14, 15, 16, /* <- sum */
		5, 6, 7, 8,
		9, 10, 11, 12);

	test_assert_f32_eq(weston_m4f_inf_norm(M), 58.0);

	return RESULT_OK;
}

struct test_matrix4 {
	/* the matrix to test */
	struct weston_mat4f M;

	/*
	 * Residual error limit; inf norm(M * inv(M) - I) < err_limit
	 * The residual error as calculated here represents the relative
	 * error added by transforming a vector with inv(M).
	 */
	double err_limit;
};

static const struct test_matrix4 matrices4[] = {
	/* A very trivial case. */
	{
		.M = WESTON_MAT4F(
			1, 0, 0, 0,
		        0, 2, 0, 0,
		        0, 0, 3, 0,
		        0, 0, 0, 4),
		.err_limit = 0.0,
	},

	/*
	 * A very likely case in a compositor, being a matrix applying
	 * just a translation. Surprisingly, fourbyfour-analyze says:
	 *
	 * -------------------------------------------------------------------
	 * $ ./fourbyfour-analyse 1 0 0 1980 0 1 0 1080
	 * Your input matrix A is
	 *               1            0            0         1980
	 *               0            1            0         1080
	 *               0            0            1            0
	 *               0            0            0            1
	 *
	 * The singular values of A are: 2255.39, 1, 1, 0.000443382
	 * The condition number according to 2-norm of A is 5.087e+06.
	 *
	 * This means that if you were to solve the linear system Ax=b for vector x,
	 * in the worst case you would lose 6.7 digits (22.3 bits) of precision.
	 * The condition number is how much errors in vector b would be amplified
	 * when solving x even with infinite computational precision.
	 *
	 * Compare this to the precision of vectors b and x:
	 *
	 * - Single precision floating point has 7.2 digits (24 bits) of precision,
	 * leaving your result with no correct digits.
	 * Single precision, matrix A has rank 3 which means that the solution space
	 * for x has 1 dimension and therefore has many solutions.
	 *
	 * - Double precision floating point has 16.0 digits (53 bits) of precision,
	 * leaving your result with 9.2 correct digits (30 correct bits).
	 * Double precision, matrix A has full rank which means the solution x is
	 * unique.
	 *
	 * NOTE! The above gives you only an upper limit on errors.
	 * If the upper limit is low, you can be confident of your computations. But,
	 * if the upper limit is high, it does not necessarily imply that your
	 * computations will be doomed.
	 * -------------------------------------------------------------------
	 *
	 * This is one example where the condition number is highly pessimistic,
	 * while the actual inversion results in no error at all.
	 *
	 * https://gitlab.freedesktop.org/pq/fourbyfour
	 */
	{
		.M = WESTON_MAT4F(
			1, 0, 0, 1980,
		        0, 1, 0, 1080,
		        0, 0, 1, 0,
		        0, 0, 0, 1),
		.err_limit = 0.0,
	},

	/*
	 * The following matrices have been generated with
	 * fourbyfour-generate using parameters out of a hat as listed below.
	 *
	 * If you want to verify the matrices in Octave, type this:
	 * M = [ <paste the series of numbers> ]
	 * mat = reshape(M, 4, 4)
	 * det(mat)
	 * cond(mat)
	 */

	/* cond = 1e3 */
	{
		.M = WESTON_MAT4F(
			-4.12798022231678357619e-02, -7.93301899046665176529e-02, 2.49367040174418935772e-01, -2.22400462135059429070e-01,
		         2.02416121867255743849e-01, -2.25754422240346010187e-02, -2.91283152417864787953e-01, 1.49354988316431153139e-01,
		         6.18473094065821293874e-01, 5.81511312950217934548e-02, -1.18363610818063924590e+00, 8.00087538947595322547e-01,
		         1.25723127083294305972e-01, 7.72723720984487272290e-02, -3.76023220287807879991e-01, 2.82473279931768073148e-01),
		.err_limit = 1e-5,
	},

	/* cond = 1e3, abs det = 15 */
	{
		.M = WESTON_MAT4F(
			 6.84154939885726509630e+00, -6.87241565273813304060e+00, -2.56772939909334070308e+01, -2.52185055099662420730e+01,
		         2.04511561406330022450e+00, -3.67551043874248994925e+00, -1.96421641406619129633e+00, -2.40644091603848320204e+00,
		         5.83631095663641819016e+00, -9.31051765621826277197e+00, -1.80402129629135217215e+01, -1.78475057662460052654e+01,
		        -9.88588496379959025262e+00, 1.49790516545410774540e+01, 2.64975800675967363418e+01, 2.65795891678410747261e+01),
		.err_limit = 1e-4,
	},

	/* cond = 700, abs det = 1e-6, invertible regardless of det */
	{
		.M = WESTON_MAT4F(
			 1.32125189257677579449e-03, -1.67411409720826992453e-01, 1.07940907587735196449e-01, -1.22163309792902186057e-01,
		        -5.42113793774764013422e-02, 5.30455105336593901733e-01, -2.59607412684229155175e-01, 4.36480803188117993940e-01,
		         2.88175168292948129939e-03, -1.85262537685181277736e-01, 1.46265858042118279680e-01, -9.41398969709369287662e-02,
		        -2.88900393087768159184e-03, 1.57987202530630227448e-01, -1.20781192010860280450e-01, 8.95194304475115387731e-02),
		.err_limit = 1e-4,
	},

	/* cond = 1e6, this is a little more challenging */
	{
		.M = WESTON_MAT4F(
			-4.41851445093878913983e-01, -5.16386185043831491548e-01, 2.86186055948129847160e-01, -5.79440137716940473211e-01,
		         2.49798696238173301154e-01, 2.84965614532234345901e-01, -1.65729639683955931595e-01, 3.12568045963485974248e-01,
		         3.15253213984537428161e-01, 3.71270066781250074328e-01, -2.02675623845341434937e-01, 4.19969870491003371971e-01,
		         5.60818677658178832424e-01, 6.45373659426444201692e-01, -3.68902466471524526082e-01, 7.13785795079988516498e-01),
		.err_limit = 0.02,
	},

	/* cond = 15, abs det = 1e-9, should be well invertible */
	{
		.M = WESTON_MAT4F(
			-5.37536200142514660589e-05, 7.92552373388843642288e-03, -3.90554524958281433500e-03, 2.68892064500873568395e-03,
		        -9.72329428437283989350e-03, 8.32075145342783470404e-03, 6.52648485926096092596e-03, 1.06707947887298994737e-03,
		         1.04453728969657322345e-02, -1.03627268579679666927e-02, -3.56835980207569763989e-03, -3.95935925157862422114e-03,
		         5.37160838929722633805e-03, 6.13466744624343262009e-05, -1.23695935407398946090e-04, 8.21231194921675112380e-04),
		.err_limit = 1e-6,
	},
};

TEST_P(mat4_inversion_precision, matrices4)
{
	const struct test_matrix4 *tm = data;
	struct weston_mat4f rr;
	float err;

	/* Compute rr = M * inv(M) */
	test_assert_true(weston_m4f_invert(&rr, tm->M));
	rr = weston_m4f_mul_m4f(tm->M, rr);

	/* Residual: subtract identity matrix (expected result) */
	rr = weston_m4f_sub_m4f(rr, WESTON_MAT4F_IDENTITY);

	/*
	 * Infinity norm of the residual is our measure.
	 * See https://gitlab.freedesktop.org/pq/fourbyfour/-/blob/master/README.d/precision_testing.md
	 */
	err = weston_m4f_inf_norm(rr);
	testlog("Residual error %g (%.1f bits precision), limit %g.\n",
		err, -log2f(err), tm->err_limit);

	if (err > tm->err_limit) {
		testlog("Error is too high for matrix\n");
		print_mat4(tm->M);
		test_assert_true(false);
	}

	return RESULT_OK;
}

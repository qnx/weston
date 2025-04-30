/*
 * Copyright © 2016 Collabora, Ltd.
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

#include "shared/timespec-util.h"
#include "weston-test-client-helper.h"
#include "weston-test-assert.h"

TEST(test_timespec_sub)
{
	struct timespec a, b, r;

	a.tv_sec = 1;
	a.tv_nsec = 1;
	b.tv_sec = 0;
	b.tv_nsec = 2;
	timespec_sub(&r, &a, &b);
	test_assert_u64_eq(r.tv_sec, 0);
	test_assert_u64_eq(r.tv_nsec, NSEC_PER_SEC - 1);

	return RESULT_OK;
}

TEST(test_timespec_to_nsec)
{
	struct timespec a;

	a.tv_sec = 4;
	a.tv_nsec = 4;
	test_assert_u64_eq(timespec_to_nsec(&a), (NSEC_PER_SEC * 4ULL) + 4);

	return RESULT_OK;
}

TEST(test_timespec_to_usec)
{
	struct timespec a;

	a.tv_sec = 4;
	a.tv_nsec = 4000;
	test_assert_u64_eq(timespec_to_usec(&a), (4000000ULL) + 4);

	return RESULT_OK;
}

TEST(test_timespec_to_msec)
{
	struct timespec a;

	a.tv_sec = 4;
	a.tv_nsec = 4000000;
	test_assert_u64_eq(timespec_to_msec(&a), (4000ULL) + 4);

	return RESULT_OK;
}

TEST(test_timespec_to_proto)
{
	struct timespec a;
	uint32_t tv_sec_hi;
	uint32_t tv_sec_lo;
	uint32_t tv_nsec;

	a.tv_sec = 0;
	a.tv_nsec = 0;
	timespec_to_proto(&a, &tv_sec_hi, &tv_sec_lo, &tv_nsec);
	test_assert_u64_eq(0, tv_sec_hi);
	test_assert_u64_eq(0, tv_sec_lo);
	test_assert_u64_eq(0, tv_nsec);

	a.tv_sec = 1234;
	a.tv_nsec = NSEC_PER_SEC - 1;
	timespec_to_proto(&a, &tv_sec_hi, &tv_sec_lo, &tv_nsec);
	test_assert_u64_eq(0, tv_sec_hi);
	test_assert_u64_eq(1234, tv_sec_lo);
	test_assert_u64_eq(NSEC_PER_SEC - 1, tv_nsec);

	a.tv_sec = (time_t)0x7000123470005678LL;
	a.tv_nsec = 1;
	timespec_to_proto(&a, &tv_sec_hi, &tv_sec_lo, &tv_nsec);
	test_assert_u64_eq((uint64_t)a.tv_sec >> 32, tv_sec_hi);
	test_assert_u64_eq(0x70005678, tv_sec_lo);
	test_assert_u64_eq(1, tv_nsec);

	return RESULT_OK;
}

TEST(test_millihz_to_nsec)
{
	test_assert_u64_eq(millihz_to_nsec(60000), 16666666);

	return RESULT_OK;
}

TEST(test_timespec_add_nsec)
{
	struct timespec a, r;

	a.tv_sec = 0;
	a.tv_nsec = NSEC_PER_SEC - 1;
	timespec_add_nsec(&r, &a, 1);
	test_assert_u64_eq(1, r.tv_sec);
	test_assert_u64_eq(0, r.tv_nsec);

	timespec_add_nsec(&r, &a, 2);
	test_assert_u64_eq(1, r.tv_sec);
	test_assert_u64_eq(1, r.tv_nsec);

	timespec_add_nsec(&r, &a, (NSEC_PER_SEC * 2ULL));
	test_assert_u64_eq(2, r.tv_sec);
	test_assert_u64_eq(NSEC_PER_SEC - 1, r.tv_nsec);

	timespec_add_nsec(&r, &a, (NSEC_PER_SEC * 2ULL) + 2);
	test_assert_u64_eq(r.tv_sec, 3);
	test_assert_u64_eq(r.tv_nsec, 1);

	a.tv_sec = 1;
	a.tv_nsec = 1;
	timespec_add_nsec(&r, &a, -2);
	test_assert_u64_eq(r.tv_sec, 0);
	test_assert_u64_eq(r.tv_nsec, NSEC_PER_SEC - 1);

	a.tv_nsec = 0;
	timespec_add_nsec(&r, &a, -NSEC_PER_SEC);
	test_assert_u64_eq(0, r.tv_sec);
	test_assert_u64_eq(0, r.tv_nsec);

	a.tv_nsec = 0;
	timespec_add_nsec(&r, &a, -NSEC_PER_SEC + 1);
	test_assert_u64_eq(0, r.tv_sec);
	test_assert_u64_eq(1, r.tv_nsec);

	a.tv_nsec = 50;
	timespec_add_nsec(&r, &a, (-NSEC_PER_SEC * 10ULL));
	test_assert_u64_eq(-9, r.tv_sec);
	test_assert_u64_eq(50, r.tv_nsec);

	r.tv_sec = 4;
	r.tv_nsec = 0;
	timespec_add_nsec(&r, &r, NSEC_PER_SEC + 10ULL);
	test_assert_u64_eq(5, r.tv_sec);
	test_assert_u64_eq(10, r.tv_nsec);

	timespec_add_nsec(&r, &r, (NSEC_PER_SEC * 3ULL) - 9ULL);
	test_assert_u64_eq(8, r.tv_sec);
	test_assert_u64_eq(1, r.tv_nsec);

	timespec_add_nsec(&r, &r, (NSEC_PER_SEC * 7ULL) + (NSEC_PER_SEC - 1ULL));
	test_assert_u64_eq(16, r.tv_sec);
	test_assert_u64_eq(0, r.tv_nsec);

	return RESULT_OK;
}

TEST(test_timespec_add_msec)
{
	struct timespec a, r;

	a.tv_sec = 1000;
	a.tv_nsec = 1;
	timespec_add_msec(&r, &a, 2002);
	test_assert_u64_eq(1002, r.tv_sec);
	test_assert_u64_eq(2000001, r.tv_nsec);

	return RESULT_OK;
}

TEST(test_timespec_sub_to_nsec)
{
	struct timespec a, b;

	a.tv_sec = 1000;
	a.tv_nsec = 1;
	b.tv_sec = 1;
	b.tv_nsec = 2;
	test_assert_u64_eq((999LL * NSEC_PER_SEC) - 1, timespec_sub_to_nsec(&a, &b));

	return RESULT_OK;
}

TEST(test_timespec_sub_to_msec)
{
	struct timespec a, b;

	a.tv_sec = 1000;
	a.tv_nsec = 2000000L;
	b.tv_sec = 2;
	b.tv_nsec = 1000000L;
	test_assert_u64_eq((998 * 1000) + 1, timespec_sub_to_msec(&a, &b));

	return RESULT_OK;
}

TEST(test_timespec_from_nsec)
{
	struct timespec a;

	timespec_from_nsec(&a, 0);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_nsec(&a, NSEC_PER_SEC - 1);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(NSEC_PER_SEC - 1, a.tv_nsec);

	timespec_from_nsec(&a, NSEC_PER_SEC);
	test_assert_u64_eq(1, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_nsec(&a, (5LL * NSEC_PER_SEC) + 1);
	test_assert_u64_eq(5, a.tv_sec);
	test_assert_u64_eq(1, a.tv_nsec);

	return RESULT_OK;
}

TEST(test_timespec_from_usec)
{
	struct timespec a;

	timespec_from_usec(&a, 0);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_usec(&a, 999999);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(999999 * 1000, a.tv_nsec);

	timespec_from_usec(&a, 1000000);
	test_assert_u64_eq(1, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_usec(&a, 5000001);
	test_assert_u64_eq(5, a.tv_sec);
	test_assert_u64_eq(1000, a.tv_nsec);

	return RESULT_OK;
}

TEST(test_timespec_from_msec)
{
	struct timespec a;

	timespec_from_msec(&a, 0);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_msec(&a, 999);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(999 * 1000000, a.tv_nsec);

	timespec_from_msec(&a, 1000);
	test_assert_u64_eq(1, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_msec(&a, 5001);
	test_assert_u64_eq(5, a.tv_sec);
	test_assert_u64_eq(1000000, a.tv_nsec);

	return RESULT_OK;
}

TEST(test_timespec_from_proto)
{
	struct timespec a;

	timespec_from_proto(&a, 0, 0, 0);
	test_assert_u64_eq(0, a.tv_sec);
	test_assert_u64_eq(0, a.tv_nsec);

	timespec_from_proto(&a, 0, 1234, 9999);
	test_assert_u64_eq(1234, a.tv_sec);
	test_assert_u64_eq(9999, a.tv_nsec);

	timespec_from_proto(&a, 0x1234, 0x5678, 1);
	test_assert_u64_eq((time_t)0x0000123400005678LL, a.tv_sec);
	test_assert_u64_eq(1, a.tv_nsec);

	return RESULT_OK;
}

TEST(test_timespec_is_zero)
{
	struct timespec zero = { 0 };
	struct timespec non_zero_sec = { .tv_sec = 1, .tv_nsec = 0 };
	struct timespec non_zero_nsec = { .tv_sec = 0, .tv_nsec = 1 };

	test_assert_true(timespec_is_zero(&zero));
	test_assert_false(timespec_is_zero(&non_zero_nsec));
	test_assert_false(timespec_is_zero(&non_zero_sec));

	return RESULT_OK;
}

TEST(test_timespec_eq)
{
	struct timespec a = { .tv_sec = 2, .tv_nsec = 1 };
	struct timespec b = { .tv_sec = -1, .tv_nsec = 2 };

	test_assert_true(timespec_eq(&a, &a));
	test_assert_true(timespec_eq(&b, &b));

	test_assert_false(timespec_eq(&a, &b));
	test_assert_false(timespec_eq(&b, &a));

	return RESULT_OK;
}

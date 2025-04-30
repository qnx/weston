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

#include "id-number-allocator.h"
#include "weston-test-client-helper.h"
#include "weston-test-assert.h"

/*
 * Allocating IDs without ever releasing any in between must produce a
 * consecutive sequence starting from 1. 0 is not a valid id.
 * Tests reallocation of the bucket array.
 */
TEST(test_sequential_ids)
{
	struct weston_idalloc *ida;
	unsigned i;

	ida = weston_idalloc_create(NULL);

	for (i = 1; i < 10000; i++)
		test_assert_u32_eq(weston_idalloc_get_id(ida), i);

	/* Additional testing of lowest_free_bucket manipulation. */
	weston_idalloc_put_id(ida, 99);
	test_assert_u32_eq(weston_idalloc_get_id(ida), 99);
	test_assert_u32_eq(weston_idalloc_get_id(ida), 10000);

	weston_idalloc_destroy(ida);

	return RESULT_OK;
}

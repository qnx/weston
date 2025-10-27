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

#include "weston-test-client-helper.h"

struct setup_args {
	struct fixture_metadata meta;
	enum weston_renderer_type renderer;
	enum client_buffer_type buffer_type;
	bool gl_force_import_yuv_fallback;
};

struct color_state {
	bool create_color_representation_surface;
	enum wp_color_representation_surface_v1_coefficients coefficients;
	enum wp_color_representation_surface_v1_range range;
};

static const struct color_state color_state_cases[] = {
#define CRS(x) WP_COLOR_REPRESENTATION_SURFACE_V1_ ##x
	{ false, 0, 0 },
	{ true, 0, 0 },
	{ true, CRS(COEFFICIENTS_BT601), CRS(RANGE_LIMITED) },
	{ true, CRS(COEFFICIENTS_BT601), CRS(RANGE_FULL) },
	{ true, CRS(COEFFICIENTS_BT709), CRS(RANGE_LIMITED) },
	{ true, CRS(COEFFICIENTS_BT709), CRS(RANGE_FULL) },
	{ true, CRS(COEFFICIENTS_BT2020), CRS(RANGE_LIMITED) },
	{ true, CRS(COEFFICIENTS_BT2020), CRS(RANGE_FULL) },
#undef CRS
};

enum feedback_result {
	FB_PENDING = 0,
	FB_PRESENTED,
	FB_PRESENTED_ZERO_COPY,
	FB_DISCARDED
};

enum test_result_code
test_color_representation(const struct color_state *color_state,
			  enum client_buffer_type buffer_type,
			  enum feedback_result expected_result);

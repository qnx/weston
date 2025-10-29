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

#include "pixel-formats.h"
#include "shared/weston-drm-fourcc.h"
#include "shared/xalloc.h"
#include "weston-test-assert.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_GL;
	setup.test_quirks.required_capabilities = WESTON_CAP_COLOR_REP;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

/*
 * Test that the SURFACE_EXISTS error is send by the compositor.
 */
TEST(color_presentation_protocol_surface_exists)
{
	struct wp_color_representation_surface_v1 *color_representation_surface;
	struct wp_color_representation_surface_v1 *color_representation_surface_2;
	struct client *client;
	struct wl_surface *surface;

	client = create_client();
	client->surface = create_test_surface(client);
	surface = client->surface->wl_surface;

	color_representation_surface =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation, surface);
	color_representation_surface_2 =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation, surface);

	expect_protocol_error(client,
		&wp_color_representation_manager_v1_interface,
		WP_COLOR_REPRESENTATION_MANAGER_V1_ERROR_SURFACE_EXISTS);

	wp_color_representation_surface_v1_destroy(color_representation_surface);
	wp_color_representation_surface_v1_destroy(color_representation_surface_2);
	client_destroy(client);

	return RESULT_OK;
}

/*
 * Test that a color representation can successfully be recreated after
 * destruction without e.g. triggering a SURFACE_EXISTS error.
 */
TEST(color_presentation_protocol_surface_recreate)
{
	struct wp_color_representation_surface_v1 *color_representation_surface;
	struct wp_color_representation_surface_v1 *color_representation_surface_2;
	struct client *client;
	struct wl_surface *surface;

	client = create_client();
	client->surface = create_test_surface(client);
	surface = client->surface->wl_surface;

	color_representation_surface =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation, surface);
	wp_color_representation_surface_v1_destroy(color_representation_surface);
	color_representation_surface_2 =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation, surface);

	client_roundtrip(client);

	wp_color_representation_surface_v1_destroy(color_representation_surface_2);
	client_destroy(client);

	return RESULT_OK;
}

struct coefficients_case {
	uint32_t drm_format;
	enum wp_color_representation_surface_v1_coefficients coefficients;
	enum wp_color_representation_surface_v1_range range;
	enum wp_color_representation_surface_v1_error error_code;
};

#define VALID_CASE(format, coefs, range) { \
	DRM_FORMAT_ ## format, \
	WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_ ## coefs, \
	WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_ ## range, \
	0 \
}

#define INVALID_CASE(format, coefs, range, err) { \
	DRM_FORMAT_ ## format, \
	WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_ ## coefs, \
	WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_ ## range, \
	WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ ## err \
}

static const struct coefficients_case coefficients_cases[] = {
	VALID_CASE(ARGB8888, IDENTITY, FULL),
	INVALID_CASE(ARGB8888, IDENTITY, LIMITED, COEFFICIENTS),
	INVALID_CASE(ARGB8888, BT601, LIMITED, PIXEL_FORMAT),
	INVALID_CASE(ARGB8888, BT601, FULL, PIXEL_FORMAT),
	INVALID_CASE(ARGB8888, BT709, LIMITED, PIXEL_FORMAT),
	INVALID_CASE(ARGB8888, BT709, FULL, PIXEL_FORMAT),
	INVALID_CASE(ARGB8888, BT2020, LIMITED, PIXEL_FORMAT),
	INVALID_CASE(ARGB8888, BT2020, FULL, PIXEL_FORMAT),
	INVALID_CASE(ARGB8888, FCC, LIMITED, COEFFICIENTS),
	INVALID_CASE(ARGB8888, FCC, FULL, COEFFICIENTS),
	{DRM_FORMAT_ARGB8888, 0, 0, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS},
	VALID_CASE(NV12, BT601, LIMITED),
	VALID_CASE(NV12, BT601, FULL),
	VALID_CASE(NV12, BT709, LIMITED),
	VALID_CASE(NV12, BT709, FULL),
	VALID_CASE(NV12, BT2020, LIMITED),
	VALID_CASE(NV12, BT2020, FULL),
	INVALID_CASE(NV12, IDENTITY, LIMITED, COEFFICIENTS),
	INVALID_CASE(NV12, IDENTITY, FULL, PIXEL_FORMAT),
	INVALID_CASE(NV12, FCC, LIMITED, COEFFICIENTS),
	INVALID_CASE(NV12, FCC, FULL, COEFFICIENTS),
	{DRM_FORMAT_NV12, 0, 0, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS},
};

/*
 * Test that various protocol errors regarding invalid combinations of DRM
 * format, matrix coefficients and quantization range are send by the compositor
 * as required by the protocol.
 */
TEST_P(color_presentation_protocol_valid_coefficients, coefficients_cases)
{
	const struct coefficients_case *coefficients_case = data;
	struct wp_color_representation_surface_v1 *color_representation_surface;
	struct client *client;
	struct wl_surface *surface;

	client = create_client();
	client->surface = create_test_surface(client);
	surface = client->surface->wl_surface;
	client->surface->buffer = create_shm_buffer(client, 8, 8,
						    coefficients_case->drm_format);

	wl_surface_attach(surface, client->surface->buffer->proxy, 0, 0);
	wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);

	color_representation_surface =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation,
			surface);
	wp_color_representation_surface_v1_set_coefficients_and_range(
		color_representation_surface,
		coefficients_case->coefficients,
		coefficients_case->range);
	wl_surface_commit(surface);

	if (coefficients_case->error_code)
		expect_protocol_error(client,
			&wp_color_representation_surface_v1_interface,
			coefficients_case->error_code);
	else
		client_roundtrip(client);

	wp_color_representation_surface_v1_destroy(color_representation_surface);
	client_destroy(client);

	return RESULT_OK;
}

struct alpha_mode_case {
	enum wp_color_representation_surface_v1_alpha_mode alpha_mode;
	enum wp_color_representation_surface_v1_error error_code;
};

static const struct alpha_mode_case alpha_mode_cases[] = {
	{WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL, 0},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_OPTICAL, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ALPHA_MODE},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ALPHA_MODE},
	{-1, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ALPHA_MODE},
};

/*
 * Test that PREMULTIPLIED_ELECTRICAL is the only alpha mode currently supported.
 */
TEST_P(color_presentation_protocol_alpha_mode, alpha_mode_cases)
{
	const struct alpha_mode_case *alpha_mode_case = data;
	struct wp_color_representation_surface_v1 *color_representation_surface;
	struct client *client;

	client = create_client();
	client->surface = create_test_surface(client);

	color_representation_surface =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation,
			client->surface->wl_surface);
	wp_color_representation_surface_v1_set_alpha_mode(color_representation_surface,
		alpha_mode_case->alpha_mode);

	if (alpha_mode_case->error_code)
		expect_protocol_error(client,
			&wp_color_representation_surface_v1_interface,
			alpha_mode_case->error_code);
	else
		client_roundtrip(client);

	wp_color_representation_surface_v1_destroy(color_representation_surface);
	client_destroy(client);

	return RESULT_OK;
}

struct chroma_location_case {
	enum wp_color_representation_surface_v1_chroma_location chroma_location;
	enum wp_color_representation_surface_v1_error error_code;
};

static const struct chroma_location_case chroma_location_cases[] = {
	{WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_0, 0},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_1, 0},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_2, 0},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_3, 0},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_4, 0},
	{WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_5, 0},
	{0, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_CHROMA_LOCATION},
};

/*
 * Test that all chroma location values are accepted, but not invalid values.
 */
TEST_P(color_presentation_protocol_chroma_location, chroma_location_cases)
{
	const struct chroma_location_case *chroma_location_case = data;
	struct wp_color_representation_surface_v1 *color_representation_surface;
	struct client *client;

	client = create_client();
	client->surface = create_test_surface(client);

	color_representation_surface =
		wp_color_representation_manager_v1_get_surface(
			client->color_representation,
			client->surface->wl_surface);
	wp_color_representation_surface_v1_set_chroma_location(
		color_representation_surface,
		chroma_location_case->chroma_location);

	if (chroma_location_case->error_code)
		expect_protocol_error(client,
			&wp_color_representation_surface_v1_interface,
			chroma_location_case->error_code);
	else
		client_roundtrip(client);

	wp_color_representation_surface_v1_destroy(color_representation_surface);
	client_destroy(client);

	return RESULT_OK;
}

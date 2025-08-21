/*
 * Copyright (C) 2025 Amazon.com, Inc. or its affiliates
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

#include "weston-test-client-helper.h"
#include "image-iter.h"

static const int WINDOW_WIDTH  = 320;
static const int WINDOW_HEIGHT = 240;

enum effect_type {
	EFFECT_TYPE_NONE = 0,
	EFFECT_TYPE_INVERSION,
	EFFECT_TYPE_DEUTERANOPIA,
	EFFECT_TYPE_PROTANOPIA,
	EFFECT_TYPE_TRITANOPIA,
};

struct setup_args {
	struct fixture_metadata meta;
	enum effect_type type;
	const char *ref_image_prefix;
};

static const struct setup_args my_setup_args[] = {
	{
		.meta.name = "normal-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_NONE,
	},
	{
		.meta.name = "inversion-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_INVERSION,
	},
	{
		.meta.name = "deuteranopia-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_DEUTERANOPIA,
	},
	{
		.meta.name = "protanopia-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_PROTANOPIA,
	},
	{
		.meta.name = "tritanopia-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_TRITANOPIA,
	},
};

static const char *
get_effect_type_str(enum effect_type type)
{
	switch (type) {
	case EFFECT_TYPE_DEUTERANOPIA:
		return "deuteranopia";
	case EFFECT_TYPE_PROTANOPIA:
		return "protanopia";
	case EFFECT_TYPE_TRITANOPIA:
		return "tritanopia";
	case EFFECT_TYPE_INVERSION:
		return "inversion";
	case EFFECT_TYPE_NONE:
		return NULL;
	};
	test_assert_not_reached("unknown color effect");
}

static enum test_result_code
fixture_setup(struct weston_test_harness *harness, const struct setup_args *arg)
{
	struct compositor_setup setup;
	const char *effect = get_effect_type_str(arg->type);

	compositor_setup_defaults(&setup);
	setup.shell = SHELL_TEST_DESKTOP;
	setup.renderer = WESTON_RENDERER_GL;
	setup.refresh = HIGHEST_OUTPUT_REFRESH;
	setup.width = WINDOW_WIDTH;
	setup.height = WINDOW_HEIGHT;

	weston_ini_setup(&setup,
			 cfgln("[output]"),
			 cfgln("name=headless"),
			 effect ? cfgln("color-effect=%s", effect) : cfgln(""));

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

TEST(color_effects)
{
	int seq_no = get_test_fixture_index();
	const struct setup_args *arg = &my_setup_args[seq_no];
	struct client *client;
	struct buffer *buffer;
	struct buffer *screenshot;
	struct wl_surface *surface;
	const uint32_t width = WINDOW_WIDTH;
	const uint32_t height = WINDOW_HEIGHT;
	struct rectangle clip;
	int frame;

	client = create_client_and_test_surface(0, 0, width, height);
	test_assert_ptr_not_null(client);
	surface = client->surface->wl_surface;

	/* move pointer away so it does not interfere */
	weston_test_move_pointer(client->test->weston_test, 0, 1, 0, 0, 0);

	/* buffer with cat image */
	buffer = client_buffer_from_image_file(client, "colorful-cat", 1);
	test_assert_ptr_not_null(buffer);

	/* commit buffer */
	wl_surface_attach(surface, buffer->proxy, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	frame_callback_set(surface, &frame);
	wl_surface_commit(surface);
	frame_callback_wait(client, &frame);

	/* take screenshot */
	screenshot = capture_screenshot_of_output(client, NULL);
	test_assert_ptr_not_null(screenshot);

	/* compare to reference image, ignoring background (which also changes
	 * with color effects, but let's make the test run faster) */
	clip.x = 0;
	clip.y = 0;
	clip.width = pixman_image_get_width(buffer->image);
	clip.height = pixman_image_get_height(buffer->image);
	verify_image(screenshot->image, arg->ref_image_prefix, seq_no, &clip, seq_no);

	buffer_destroy(screenshot);
	buffer_destroy(buffer);
	client_destroy(client);

	return RESULT_OK;
}

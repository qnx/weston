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

static const int CAT_WIDTH = 220;
static const int CAT_HEIGHT = 220;

static const int SOLID_BUFFER_WIDTH = 20;
static const int SOLID_BUFFER_HEIGHT = 15;

static const struct solid_buffer_color {
	uint32_t r;
	uint32_t g;
	uint32_t b;
	uint32_t a;
} SOLID_BUFFER_COLOR = {
	.r = 0xcfffffff,
	.g = 0x8fffffff,
	.b = 0x4fffffff,
	.a = 0xffffffff,
};

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
	uint32_t object_width;
	uint32_t object_height;
	bool solid_color;
};

static const struct setup_args my_setup_args[] = {
	{
		.meta.name = "normal-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_NONE,
		.solid_color = false,
		.object_width = CAT_WIDTH,
		.object_height = CAT_HEIGHT,
	},
	{
		.meta.name = "inversion-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_INVERSION,
		.solid_color = false,
		.object_width = CAT_WIDTH,
		.object_height = CAT_HEIGHT,
	},
	{
		.meta.name = "deuteranopia-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_DEUTERANOPIA,
		.solid_color = false,
		.object_width = CAT_WIDTH,
		.object_height = CAT_HEIGHT,
	},
	{
		.meta.name = "protanopia-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_PROTANOPIA,
		.solid_color = false,
		.object_width = CAT_WIDTH,
		.object_height = CAT_HEIGHT,
	},
	{
		.meta.name = "tritanopia-cat",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_TRITANOPIA,
		.solid_color = false,
		.object_width = CAT_WIDTH,
		.object_height = CAT_HEIGHT,
	},
	{
		.meta.name = "normal-solid-color",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_NONE,
		.solid_color = true,
		.object_width = SOLID_BUFFER_WIDTH,
		.object_height = SOLID_BUFFER_HEIGHT,
	},
	{
		.meta.name = "inversion-solid-color",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_INVERSION,
		.solid_color = true,
		.object_width = SOLID_BUFFER_WIDTH,
		.object_height = SOLID_BUFFER_HEIGHT,
	},
	{
		.meta.name = "deuteranopia-solid-color",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_DEUTERANOPIA,
		.solid_color = true,
		.object_width = SOLID_BUFFER_WIDTH,
		.object_height = SOLID_BUFFER_HEIGHT,
	},
	{
		.meta.name = "protanopia-solid-color",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_PROTANOPIA,
		.solid_color = true,
		.object_width = SOLID_BUFFER_WIDTH,
		.object_height = SOLID_BUFFER_HEIGHT,
	},
	{
		.meta.name = "tritanopia-solid-color",
		.ref_image_prefix = "color-effects",
		.type = EFFECT_TYPE_TRITANOPIA,
		.solid_color = true,
		.object_width = SOLID_BUFFER_WIDTH,
		.object_height = SOLID_BUFFER_HEIGHT,
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
	struct surface *surface;
	struct rectangle clip;
	struct wl_buffer *buf;
	struct buffer *buffer = NULL;
	struct wp_viewport *viewport;
	int frame;
	bool res;

	client = create_client_and_test_surface(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	test_assert_ptr_not_null(client);
	surface = client->surface;

	viewport = client_create_viewport(client);

	if (!arg->solid_color) {
		buffer = client_buffer_from_image_file(client, "colorful-cat", 1);
		test_assert_ptr_not_null(buffer);
		buf = buffer->proxy;
	} else {
		buf = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(client->single_pixel_manager,
									       SOLID_BUFFER_COLOR.r,
									       SOLID_BUFFER_COLOR.g,
									       SOLID_BUFFER_COLOR.b,
									       SOLID_BUFFER_COLOR.a);
		wp_viewport_set_source(viewport, wl_fixed_from_int(0), wl_fixed_from_int(0),
				       wl_fixed_from_int(1), wl_fixed_from_int(1));
		wp_viewport_set_destination(viewport, arg->object_width, arg->object_height);
	}

	/* move pointer away so it does not interfere */
	weston_test_move_pointer(client->test->weston_test, 0, 1, 0, 0, 0);

	/* commit buffer */
	wl_surface_attach(surface->wl_surface, buf, 0, 0);
	wl_surface_damage_buffer(surface->wl_surface, 0, 0, arg->object_width, arg->object_height);
	frame_callback_set(surface->wl_surface, &frame);
	wl_surface_commit(surface->wl_surface);
	frame_callback_wait(client, &frame);

	/* take screenshot and compare to reference image; ignore background */
	clip = (struct rectangle) {
		.x = 0,
		.y = 0,
		.width = arg->object_width,
		.height = arg->object_height,
	};
	res = verify_screen_content(client, arg->ref_image_prefix, seq_no, &clip,
				    seq_no, NULL, NO_DECORATIONS);

	if (buffer) {
		buffer_destroy(buffer);
	} else {
		wl_buffer_destroy(buf);
	}
	wp_viewport_destroy(viewport);
	client_destroy(client);

	return res ? RESULT_OK : RESULT_FAIL;
}

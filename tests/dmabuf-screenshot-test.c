/*
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates
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

struct setup_args {
	struct fixture_metadata meta;
	enum weston_compositor_backend backend;
};

/**
 * It is important to use different backends because they have outputs with
 * different Y origin (bottom-left in the case of DRM-backend, top-left in the
 * case of headless), and GL-renderer needs to make the screenshot work for both
 * cases.
 */
static const struct setup_args my_setup_args[] = {
	{
		.meta.name = "headless",
		.backend = WESTON_BACKEND_HEADLESS,
	},
	{
		.meta.name = "DRM",
		.backend = WESTON_BACKEND_DRM,
	},
};

static enum test_result_code
fixture_setup(struct weston_test_harness *harness, const struct setup_args *arg)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
        setup.backend = arg->backend;
	setup.renderer = WESTON_RENDERER_GL;
	setup.width = 200;
	setup.height = 200;
	setup.shell = SHELL_TEST_DESKTOP;
        setup.refresh = HIGHEST_OUTPUT_REFRESH;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

static struct buffer *
surface_commit_color(struct client *client, struct wl_surface *surface,
		     pixman_color_t *color, int width, int height)
{
	struct buffer *buf;

	buf = create_shm_buffer_a8r8g8b8(client, width, height);
	fill_image_with_color(buf->image, color);
	wl_surface_attach(surface, buf->proxy, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, width, height);
	wl_surface_commit(surface);

	return buf;
}

TEST(screenshot)
{
        struct client *client;
        struct wl_subcompositor *subco;
	struct wl_surface *surf;
	struct wl_subsurface *subsurf;
        struct buffer *buf_main, *buf_sub;
        struct buffer *buf_screenshot;
        struct rectangle clip;
        char *fname;
        bool match;
        pixman_image_t *reference;
        pixman_image_t *diffimg;
        pixman_color_t red, green;

	if (!client_buffer_util_is_dmabuf_supported()) {
		testlog("%s: Skipped: udmabuf not supported\n", get_test_name());
		return RESULT_SKIP;
	}

	color_rgb888(&red, 255, 0, 0);
	color_rgb888(&green, 0, 255, 0);

        client = create_client_and_test_surface(0, 0, 100, 100);
        subco = client_get_subcompositor(client);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(client->test->weston_test, 0, 1, 0, 2, 30);

        surf = wl_compositor_create_surface(client->wl_compositor);
	subsurf = wl_subcompositor_get_subsurface(subco, surf,
						  client->surface->wl_surface);

        buf_main = surface_commit_color(client, client->surface->wl_surface,
                                        &red, 100, 100);

        wl_subsurface_set_position(subsurf, 100, 100);
        buf_sub = surface_commit_color(client, surf,
                                       &green, 100, 100);

        wl_surface_commit(client->surface->wl_surface);

        buf_screenshot = client_capture_output(client, client->output,
                                               WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER,
                                               CLIENT_BUFFER_TYPE_DMABUF);

        /* load reference image */
        fname = screenshot_reference_filename("dmabuf-screenshot", 0);
        reference = load_image_from_png(fname);
        free(fname);

        /* only the colored squares area matters */
	clip.x = 0;
	clip.y = 0;
	clip.width = 200;
	clip.height = 200;

	client_buffer_util_maybe_sync_dmabuf_start(buf_screenshot->buf);
        match = check_images_match(buf_screenshot->image, reference, &clip, NULL);
	client_buffer_util_maybe_sync_dmabuf_end(buf_screenshot->buf);

	testlog("Screenshot %s reference image\n", match? "equal to" : "different from");
	if (!match) {
		diffimg = visualize_image_difference(buf_screenshot->image, reference, &clip, NULL);
		fname = output_filename_for_test_case("error", 0, "png");
		write_image_as_png(diffimg, fname);
		pixman_image_unref(diffimg);
		free(fname);
	}

        pixman_image_unref(reference);
	buffer_destroy(buf_sub);
	buffer_destroy(buf_main);
	buffer_destroy(buf_screenshot);
	wl_subcompositor_destroy(subco);
	wl_subsurface_destroy(subsurf);
	wl_surface_destroy(surf);
        client_destroy(client);

        return match ? RESULT_OK : RESULT_FAIL;
}

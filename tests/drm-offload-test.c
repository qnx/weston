/*
 * Copyright © 2025 Collabora, Ltd.
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
#include "weston-test-client-helper.h"
#include "weston-test-assert.h"
#include "xdg-client-helper.h"

struct setup_args {
	struct fixture_metadata meta;
	enum weston_renderer_type renderer;
};

static const struct setup_args my_setup_args[] = {
	{
		.meta.name = "GL",
		.renderer = WESTON_RENDERER_GL,
	},
	{
		.meta.name = "Vulkan",
		.renderer = WESTON_RENDERER_VULKAN,
	},
};

static enum test_result_code
fixture_setup(struct weston_test_harness *harness, const struct setup_args *arg)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.backend = WESTON_BACKEND_DRM;
	setup.renderer = arg->renderer;
	setup.logging_scopes = "log,drm-backend";
	setup.width = 1024;
	setup.height = 768;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

static void
buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct client_buffer *buffer = data;

	client_buffer_util_destroy_buffer(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

enum feedback_result {
	FB_PENDING = 0,
	FB_PRESENTED,
	FB_PRESENTED_ZERO_COPY,
	FB_DISCARDED
};

static void
presentation_feedback_handle_sync_output(void *data,
					 struct wp_presentation_feedback *feedback,
					 struct wl_output *output)
{
}

static void
presentation_feedback_handle_presented(void *data,
				       struct wp_presentation_feedback *feedback,
				       uint32_t tv_sec_hi,
				       uint32_t tv_sec_lo,
				       uint32_t tv_nsec,
				       uint32_t refresh,
				       uint32_t seq_hi,
				       uint32_t seq_lo,
				       uint32_t flags)
{
	enum feedback_result *result = data;
	bool zero_copy = flags & WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;

	if (zero_copy)
		*result = FB_PRESENTED_ZERO_COPY;
	else
		*result = FB_PRESENTED;

	wp_presentation_feedback_destroy(feedback);
}

static void
presentation_feedback_handle_discarded(void *data,
				       struct wp_presentation_feedback *feedback)
{
	enum feedback_result *result = data;

	*result = FB_DISCARDED;
	wp_presentation_feedback_destroy(feedback);
}

static const struct wp_presentation_feedback_listener presentation_feedback_listener = {
	.sync_output = presentation_feedback_handle_sync_output,
	.presented = presentation_feedback_handle_presented,
	.discarded = presentation_feedback_handle_discarded,
};

static void
presentation_wait_nofail(struct client *client, enum feedback_result *result)
{
	while (*result == FB_PENDING) {
		if (wl_display_dispatch(client->wl_display) < 0)
			test_assert_not_reached("Connection error");
	}
}

static void
overlay_buffer_release(void *data, struct wl_buffer *buffer)
{
	wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener overlay_buffer_listener = {
	overlay_buffer_release
};

/*
 * All following tests assume the vkms default configuration of a single
 * 1024x768 pixel output with a primary plane and one cursor plane (limited to
 * 512x512 pixels).
 */

/*
 * Test that a fullscreen client with fullscreen-sized buffer is presented via
 * direct-scanout.
 */
TEST(drm_offload_fullscreen) {
	struct xdg_client *xdg_client;
	struct xdg_surface_data *xdg_surface;
	struct client *client;
	struct client_buffer *buffer;
	struct wl_surface *surface;
	const struct pixel_format_info *fmt_info;
	struct wp_presentation_feedback *presentation_feedback;
	enum feedback_result result;

	fmt_info = pixel_format_get_info(DRM_FORMAT_XRGB8888);

	xdg_client = create_xdg_client();
	client = xdg_client->client;
	xdg_surface = create_xdg_surface(xdg_client);
	surface = xdg_surface->surface->wl_surface;

	xdg_surface_make_toplevel(xdg_surface, "weston.test.drm-offload", "one");
	xdg_toplevel_set_fullscreen(xdg_surface->xdg_toplevel, NULL);
	xdg_surface_wait_configure(xdg_surface);

	test_assert_true(xdg_surface->configure.fullscreen);
	test_assert_int_gt(xdg_surface->configure.width, 0);
	test_assert_int_gt(xdg_surface->configure.height, 0);

	buffer = client_buffer_util_create_dmabuf_buffer(client->wl_display,
							 client->dmabuf,
							 fmt_info,
							 xdg_surface->configure.width,
							 xdg_surface->configure.height);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_surface_attach(surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);
	xdg_surface_maybe_ack_configure(xdg_surface);

	result = FB_PENDING;
	presentation_feedback = wp_presentation_feedback(client->presentation,
							 surface);
	wp_presentation_feedback_add_listener(presentation_feedback,
					      &presentation_feedback_listener,
					      &result);
	wl_surface_commit(surface);
	presentation_wait_nofail(client, &result);
	test_assert_enum(result, FB_PRESENTED_ZERO_COPY);

	client_buffer_util_destroy_buffer(buffer);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

/*
 * Test that a fullscreen client with fullscreen-sized buffer and a fully
 * transparent overlay surface is presented via direct-scanout.
 */
TEST(drm_offload_fullscreen_transparent_overlay) {
	struct xdg_client *xdg_client;
	struct xdg_surface_data *xdg_surface;
	struct client *client;
	struct client_buffer *buffer;
	struct wl_surface *surface;
	struct wl_surface *overlay_surface;
	struct wl_subsurface *overlay_subsurface;
	struct wl_buffer *overlay_buffer;
	struct wp_viewport *overlay_viewport;
	const struct pixel_format_info *fmt_info;
	struct wp_presentation_feedback *presentation_feedback;
	enum feedback_result result;

	fmt_info = pixel_format_get_info(DRM_FORMAT_XRGB8888);

	xdg_client = create_xdg_client();
	client = xdg_client->client;
	xdg_surface = create_xdg_surface(xdg_client);
	surface = xdg_surface->surface->wl_surface;

	xdg_surface_make_toplevel(xdg_surface, "weston.test.drm-offload", "one");
	xdg_toplevel_set_fullscreen(xdg_surface->xdg_toplevel, NULL);
	xdg_surface_wait_configure(xdg_surface);

	test_assert_true(xdg_surface->configure.fullscreen);
	test_assert_int_gt(xdg_surface->configure.width, 0);
	test_assert_int_gt(xdg_surface->configure.height, 0);

	buffer = client_buffer_util_create_dmabuf_buffer(client->wl_display,
							 client->dmabuf,
							 fmt_info,
							 xdg_surface->configure.width,
							 xdg_surface->configure.height);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_surface_attach(surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);
	xdg_surface_maybe_ack_configure(xdg_surface);

	overlay_surface = wl_compositor_create_surface(client->wl_compositor);
	overlay_subsurface =
		wl_subcompositor_get_subsurface(client->wl_subcompositor,
						overlay_surface,
						surface);
	overlay_viewport = wp_viewporter_get_viewport(client->viewporter,
						      overlay_surface);
	wp_viewport_set_destination(overlay_viewport, 100, 100);
	overlay_buffer =
		wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(client->single_pixel_manager,
									 0x0, 0x0, 0x0, 0x0);
	wl_surface_attach(overlay_surface, overlay_buffer, 0, 0);
	wl_buffer_add_listener(overlay_buffer, &overlay_buffer_listener, NULL);
	wl_surface_damage_buffer(overlay_surface, 0, 0, 1, 1);
	wl_surface_commit(overlay_surface);

	result = FB_PENDING;
	presentation_feedback = wp_presentation_feedback(client->presentation,
							 surface);
	wp_presentation_feedback_add_listener(presentation_feedback,
					      &presentation_feedback_listener,
					      &result);
	wl_surface_commit(surface);
	presentation_wait_nofail(client, &result);
	test_assert_enum(result, FB_PRESENTED_ZERO_COPY);

	wp_viewport_destroy(overlay_viewport);
	wl_subsurface_destroy(overlay_subsurface);
	wl_surface_destroy(overlay_surface);

	client_buffer_util_destroy_buffer(buffer);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

/*
 * Test that a fullscreen client with smaller-than-fullscreen-sized buffer is
 * *not* presented via direct-scanout.
 *
 * This should be optimized in the future.
 */
TEST(drm_offload_fullscreen_black_background) {
	struct xdg_client *xdg_client;
	struct xdg_surface_data *xdg_surface;
	struct client *client;
	struct client_buffer *buffer;
	struct wl_surface *surface;
	const struct pixel_format_info *fmt_info;
	struct wp_presentation_feedback *presentation_feedback;
	enum feedback_result result;

	fmt_info = pixel_format_get_info(DRM_FORMAT_XRGB8888);

	xdg_client = create_xdg_client();
	client = xdg_client->client;
	xdg_surface = create_xdg_surface(xdg_client);
	surface = xdg_surface->surface->wl_surface;

	xdg_surface_make_toplevel(xdg_surface, "weston.test.drm-offload", "one");
	xdg_toplevel_set_fullscreen(xdg_surface->xdg_toplevel, NULL);
	xdg_surface_wait_configure(xdg_surface);

	test_assert_true(xdg_surface->configure.fullscreen);
	test_assert_int_gt(xdg_surface->configure.width, 0);
	test_assert_int_gt(xdg_surface->configure.height, 0);

	buffer = client_buffer_util_create_dmabuf_buffer(client->wl_display,
							 client->dmabuf,
							 fmt_info,
							 xdg_surface->configure.width - 100,
							 xdg_surface->configure.height - 100);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_surface_attach(surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	xdg_surface_maybe_ack_configure(xdg_surface);

	result = FB_PENDING;
	presentation_feedback = wp_presentation_feedback(client->presentation,
							 surface);
	wp_presentation_feedback_add_listener(presentation_feedback,
					      &presentation_feedback_listener,
					      &result);
	wl_surface_commit(surface);
	presentation_wait_nofail(client, &result);
	test_assert_enum(result, FB_PRESENTED);

	client_buffer_util_destroy_buffer(buffer);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

/*
 * Test that a windowed / not-fullscreen client on top of a solid background is
 * *not* presented via direct-scanout.
 *
 * This should be optimized in the future.
 */
TEST(drm_offload_windowed) {
	struct xdg_client *xdg_client;
	struct xdg_surface_data *xdg_surface;
	struct client *client;
	struct client_buffer *buffer;
	struct wl_surface *surface;
	const struct pixel_format_info *fmt_info;
	struct wp_presentation_feedback *presentation_feedback;
	enum feedback_result result;

	fmt_info = pixel_format_get_info(DRM_FORMAT_XRGB8888);

	xdg_client = create_xdg_client();
	client = xdg_client->client;
	xdg_surface = create_xdg_surface(xdg_client);
	surface = xdg_surface->surface->wl_surface;

	xdg_surface_make_toplevel(xdg_surface, "weston.test.drm-offload", "one");
	xdg_surface_wait_configure(xdg_surface);

	test_assert_false(xdg_surface->configure.fullscreen);
	test_assert_int_eq(xdg_surface->configure.width, 0);
	test_assert_int_eq(xdg_surface->configure.height, 0);

	buffer = client_buffer_util_create_dmabuf_buffer(client->wl_display,
							 client->dmabuf,
							 fmt_info,
							 100,
							 100);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_surface_attach(surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	xdg_surface_maybe_ack_configure(xdg_surface);

	result = FB_PENDING;
	presentation_feedback = wp_presentation_feedback(client->presentation,
							 surface);
	wp_presentation_feedback_add_listener(presentation_feedback,
					      &presentation_feedback_listener,
					      &result);
	wl_surface_commit(surface);
	presentation_wait_nofail(client, &result);
	test_assert_enum(result, FB_PRESENTED);

	client_buffer_util_destroy_buffer(buffer);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

/*
 * Test that a windowed / not-fullscreen client with a wl_shm buffer is *not*
 * presented via direct-scanout. This is mainly a sanity check for the tests
 * above.
 */
TEST(drm_offload_windowed_shm) {
	struct xdg_client *xdg_client;
	struct xdg_surface_data *xdg_surface;
	struct client *client;
	struct client_buffer *buffer;
	struct wl_surface *surface;
	const struct pixel_format_info *fmt_info;
	struct wp_presentation_feedback *presentation_feedback;
	enum feedback_result result;

	fmt_info = pixel_format_get_info(DRM_FORMAT_XRGB8888);

	xdg_client = create_xdg_client();
	client = xdg_client->client;
	xdg_surface = create_xdg_surface(xdg_client);
	surface = xdg_surface->surface->wl_surface;

	xdg_surface_make_toplevel(xdg_surface, "weston.test.drm-offload", "one");
	xdg_surface_wait_configure(xdg_surface);

	test_assert_false(xdg_surface->configure.fullscreen);
	test_assert_int_eq(xdg_surface->configure.width, 0);
	test_assert_int_eq(xdg_surface->configure.height, 0);

	buffer = client_buffer_util_create_shm_buffer(client->wl_shm,
						      fmt_info,
						      100,
						      100);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_surface_attach(surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	xdg_surface_maybe_ack_configure(xdg_surface);

	result = FB_PENDING;
	presentation_feedback = wp_presentation_feedback(client->presentation,
							 surface);
	wp_presentation_feedback_add_listener(presentation_feedback,
					      &presentation_feedback_listener,
					      &result);
	wl_surface_commit(surface);
	presentation_wait_nofail(client, &result);
	test_assert_enum(result, FB_PRESENTED);

	client_buffer_util_destroy_buffer(buffer);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

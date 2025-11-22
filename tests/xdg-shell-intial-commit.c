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

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "libweston-internal.h"
#include "libweston/desktop.h"
#include "shared/xalloc.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"
#include "tests/test-config.h"
#include "xdg-client-helper.h"


static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_PIXMAN;
	setup.width = 320;
	setup.height = 240;
	setup.shell = SHELL_DESKTOP;
	setup.logging_scopes = "proto,log,test-harness-plugin";
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

TEST(initial_commit_without_a_buffer)
{
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	test_assert_ptr_not_null(xdg_client);

	xdg_surface_make_toplevel(xdg_surface, "weston.test", "one");
	xdg_surface_wait_configure(xdg_surface);

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

TEST(initial_commit_with_a_buffer)
{
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	test_assert_ptr_not_null(xdg_client);

	xdg_surface_make_toplevel(xdg_surface, "weston.test", "one");
	xdg_surface_commit_solid(xdg_surface, 255, 0, 0);

	/* we should be expecting a protocol error */
	expect_protocol_error(xdg_client->client, &xdg_surface_interface,
			      XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER);

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

TEST(initial_commit_with_fullscreen_state)
{
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	test_assert_ptr_not_null(xdg_client);

	xdg_surface_make_toplevel(xdg_surface, "weston.test", "one");
	xdg_surface_set_fullscreen(xdg_surface);
	client_roundtrip(xdg_client->client);

	/* we shouldn't be getting a configure event */
	test_assert_u32_eq(xdg_surface->configure.serial, 0);

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

TEST(initial_commit_with_max_state)
{
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	test_assert_ptr_not_null(xdg_client);

	xdg_surface_make_toplevel(xdg_surface, "weston.test", "one");
	xdg_surface_set_maximized(xdg_surface);
	client_roundtrip(xdg_client->client);

	/* we shouldn't be getting a configure event */
	test_assert_u32_eq(xdg_surface->configure.serial, 0);

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

TEST(initial_commit_without_a_buffer_subsurface)
{
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);
	struct wl_subcompositor *subco;
	struct wl_subsurface *sub;
	struct wl_surface *parent;
	struct wl_surface *new_surf;
	pixman_color_t color;
	struct buffer *buf;
	int width, height;

	test_assert_ptr_not_null(xdg_client);
	xdg_surface_make_toplevel(xdg_surface, "weston.test", "one");
	xdg_surface_set_fullscreen(xdg_surface);

	subco = client_get_subcompositor(xdg_client->client);
	/* create a new surface and use the client as the parent when creating
	 * a subsurface */
	new_surf = wl_compositor_create_surface(xdg_client->client->wl_compositor);
	parent = xdg_surface->surface->wl_surface;
	sub = wl_subcompositor_get_subsurface(subco, new_surf, parent);

	width = DEFAULT_WINDOW_SIZE;
	height = DEFAULT_WINDOW_SIZE;

	color_rgb888(&color, 255, 0, 0);
	buf = create_shm_buffer_solid(xdg_surface->surface->client,
				      width, height, &color);
	test_assert_ptr_not_null(buf);

	wl_surface_attach(new_surf, buf->proxy, 0, 0);
	wl_surface_damage_buffer(xdg_surface->surface->wl_surface,
				 0, 0, width, height);

	wl_surface_commit(new_surf);

	client_roundtrip(xdg_client->client);
	/* this used to incorrectly trigger/schedule an configure event */
	test_assert_u32_eq(xdg_surface->configure.serial, 0);

	buffer_destroy(buf);
	wl_subsurface_destroy(sub);
	wl_surface_destroy(new_surf);
	wl_subcompositor_destroy(subco);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

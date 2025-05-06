/*
 * Copyright © 2016-2023 Collabora, Ltd.
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

#include <libweston/desktop.h>
#include "shared/xalloc.h"
#include "weston-test-client-helper.h"
#include "weston-test-assert.h"
#include "xdg-client-helper.h"
#include "xdg-shell-client-protocol.h"

static void
handle_xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
	struct xdg_surface_data *surface = data;
	uint32_t *state;

	surface->configure.width = width;
	surface->configure.height = height;
	surface->configure.fullscreen = false;
	surface->configure.maximized = false;
	surface->configure.resizing = false;
	surface->configure.activated = false;

	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN)
			surface->configure.fullscreen = true;
		else if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
			surface->configure.maximized = true;
		else if (*state == XDG_TOPLEVEL_STATE_RESIZING)
			surface->configure.resizing = true;
		else if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)
			surface->configure.activated = true;
	}
}

static void
handle_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
}

static void
handle_xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
				     int32_t width, int32_t height)
{
}

static void
handle_xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
				    struct wl_array *capabilities)
{
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_xdg_toplevel_configure,
	handle_xdg_toplevel_close,
	handle_xdg_toplevel_configure_bounds,
	handle_xdg_toplevel_wm_capabilities,
};

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *wm_surface,
			     uint32_t serial)
{
	struct xdg_surface_data *surface = data;

	surface->configure.serial = serial;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_xdg_surface_configure,
};

static void
handle_xdg_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	struct xdg_client *xdg_client = data;

	xdg_wm_base_pong(wm_base, serial);
	wl_display_flush(xdg_client->client->wl_display);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	handle_xdg_ping,
};

struct xdg_surface_data *
create_xdg_surface(struct xdg_client *xdg_client)
{
	struct xdg_surface_data *xdg_surface = xzalloc(sizeof(*xdg_surface));

	test_assert_ptr_not_null(xdg_surface);
	xdg_surface->surface = create_test_surface(xdg_client->client);
	test_assert_ptr_not_null(xdg_surface->surface);

	xdg_surface->xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_client->xdg_wm_base,
					    xdg_surface->surface->wl_surface);
	xdg_surface_add_listener(xdg_surface->xdg_surface,
				 &xdg_surface_listener, xdg_surface);

	return xdg_surface;
}

void
destroy_xdg_surface(struct xdg_surface_data *xdg_surface)
{
	if (xdg_surface->xdg_toplevel)
		xdg_toplevel_destroy(xdg_surface->xdg_toplevel);
	xdg_surface_destroy(xdg_surface->xdg_surface);
	surface_destroy(xdg_surface->surface);
	free(xdg_surface);
}

void
xdg_surface_make_toplevel(struct xdg_surface_data *xdg_surface,
			  const char *app_id, const char *title)
{
	xdg_surface->xdg_toplevel =
		xdg_surface_get_toplevel(xdg_surface->xdg_surface);
	test_assert_ptr_not_null(xdg_surface->xdg_toplevel);
	xdg_toplevel_add_listener(xdg_surface->xdg_toplevel,
				  &xdg_toplevel_listener, xdg_surface);
	xdg_toplevel_set_app_id(xdg_surface->xdg_toplevel, app_id);
	xdg_toplevel_set_title(xdg_surface->xdg_toplevel, title);
}

void
xdg_surface_wait_configure(struct xdg_surface_data *xdg_surface)
{
	wl_surface_commit(xdg_surface->surface->wl_surface);
	wl_display_roundtrip(xdg_surface->surface->client->wl_display);
	test_assert_u32_gt(xdg_surface->configure.serial, 0);
}

void
xdg_surface_commit_solid(struct xdg_surface_data *xdg_surface,
			 uint8_t r, uint8_t g, uint8_t b)
{
	pixman_color_t color;
	struct buffer *buf;
	int width = 0;
	int height = 0;

	if (xdg_surface->configure.width == 0 &&
	    xdg_surface->configure.height == 0) {
		xdg_surface->configure.width =
		xdg_surface->configure.height = DEFAULT_WINDOW_SIZE;
	}

	width = xdg_surface->configure.width;
	height = xdg_surface->configure.height;

	buf = create_shm_buffer_a8r8g8b8(xdg_surface->surface->client,
					 width, height);
	test_assert_ptr_not_null(buf);
	xdg_surface->surface->buffer = buf;

	color_rgb888(&color, r, g, b);
	fill_image_with_color(buf->image, &color);

	wl_surface_attach(xdg_surface->surface->wl_surface, buf->proxy, 0, 0);
	wl_surface_damage_buffer(xdg_surface->surface->wl_surface,
				 0, 0, width, height);

	if (xdg_surface->configure.serial > 0) {
		xdg_surface_ack_configure(xdg_surface->xdg_surface,
					  xdg_surface->configure.serial);
		xdg_surface->configure.serial = 0;
	}

	xdg_surface->surface->width = width;
	xdg_surface->surface->height = height;

	wl_surface_commit(xdg_surface->surface->wl_surface);
}

struct xdg_client *
create_xdg_client(void)
{
	struct xdg_client *xdg_client = xzalloc(sizeof(*xdg_client));

	test_assert_ptr_not_null(xdg_client);
	xdg_client->client = create_client();
	test_assert_ptr_not_null(xdg_client->client);

	xdg_client->xdg_wm_base = bind_to_singleton_global(xdg_client->client,
							   &xdg_wm_base_interface,
							   5);
	test_assert_ptr_not_null(xdg_client->xdg_wm_base);
	xdg_wm_base_add_listener(xdg_client->xdg_wm_base, &xdg_wm_base_listener,
				 xdg_client);

	return xdg_client;
}

void
xdg_client_destroy(struct xdg_client *xdg_client)
{
	xdg_wm_base_destroy(xdg_client->xdg_wm_base);
	client_destroy(xdg_client->client);
	free(xdg_client);
}

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

#ifndef __HAVE_XDG_CLIENT_HELPER_H
#define __HAVE_XDG_CLIENT_HELPER_H

#define DEFAULT_WINDOW_SIZE	120

struct xdg_client {
	struct client *client;
	struct xdg_wm_base *xdg_wm_base;
};

struct xdg_surface_data {
	struct xdg_client *xdg_client;
	struct surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct xdg_popup *xdg_popup;

	struct xdg_surface *xdg_parent;
	struct wl_list parent_link;
	struct wl_list child_list;

	struct {
		int width;
		int height;
		bool fullscreen;
		bool maximized;
		bool resizing;
		bool activated;
		uint32_t serial; /* != 0 if configure pending */
	} configure;

	struct {
		bool fullscreen;
		bool maximized;
	} target;
};

struct xdg_client *
create_xdg_client(void);

void
xdg_client_destroy(struct xdg_client *xdg_client);

void
xdg_surface_make_toplevel(struct xdg_surface_data *xdg_surface,
			  const char *app_id, const char *title);

void
xdg_surface_wait_configure(struct xdg_surface_data *xdg_surface);

struct xdg_surface_data *
create_xdg_surface(struct xdg_client *xdg_client);

void
xdg_surface_commit_solid(struct xdg_surface_data *xdg_surface,
			 uint8_t r, uint8_t g, uint8_t b);

void
destroy_xdg_surface(struct xdg_surface_data *xdg_surface);

#endif

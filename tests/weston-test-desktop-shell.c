/*
 * Copyright © 2010-2012 Intel Corporation
 * Copyright © 2011-2012 Collabora, Ltd.
 * Copyright © 2013 Raspberry Pi Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>
#include <signal.h>
#include <math.h>
#include <sys/types.h>

#include "frontend/weston.h"
#include <libweston/config-parser.h>
#include "shared/helpers.h"
#include "shared/xalloc.h"
#include <libweston/shell-utils.h>
#include <libweston/desktop.h>

struct desktest_shell {
	struct wl_listener compositor_destroy_listener;
	struct weston_compositor *compositor;
	struct weston_desktop *desktop;
	struct weston_layer background_layer;
	struct weston_curtain *background;
	struct weston_layer layer;
	struct weston_layer fullscreen_layer;
};

struct desktest_surface {
	struct weston_desktop_surface *desktop_surface;
	struct weston_view *view;
	struct weston_curtain *fullscreen_black_curtain;
};

static void
desktop_surface_added(struct weston_desktop_surface *desktop_surface,
		      void *shell)
{
	struct desktest_surface *dtsurface;

	dtsurface = xzalloc(sizeof *dtsurface);
	dtsurface->view = weston_desktop_surface_create_view(desktop_surface);
	weston_desktop_surface_set_user_data(desktop_surface, dtsurface);
}

static void
desktop_surface_removed(struct weston_desktop_surface *desktop_surface,
			void *shell)
{
	struct desktest_surface *dtsurface =
		weston_desktop_surface_get_user_data(desktop_surface);

	if (dtsurface->fullscreen_black_curtain)
		weston_shell_utils_curtain_destroy(dtsurface->fullscreen_black_curtain);

	weston_desktop_surface_set_user_data(desktop_surface, NULL);
	weston_desktop_surface_unlink_view(dtsurface->view);
	weston_view_destroy(dtsurface->view);
	free(dtsurface);
}

static void
black_surface_committed(struct weston_surface *es,
			struct weston_coord_surface new_origin)
{
}

static int
black_surface_get_label(struct weston_surface *surface, char *buf, size_t len)
{
	return snprintf(buf, len, "fullscreen black background surface");
}

static void
desktop_surface_committed(struct weston_desktop_surface *desktop_surface,
			  struct weston_coord_surface unused, void *shell)
{
	struct desktest_shell *dts = shell;
	struct desktest_surface *dtsurface =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct weston_geometry geometry =
		weston_desktop_surface_get_geometry(desktop_surface);
	struct weston_coord_global pos;
	struct weston_coord_surface offset;
	struct weston_output *output;
	bool fullscreen;

	assert(dtsurface->view);

	/*
	 * TODO: For now desktest_shell does not properly handle various changes
	 * of the surface state once mapped. Tests will thus be more reliable
	 * if they recreate surfaces/toplevels for every tested state.
	 */
	if (weston_surface_is_mapped(surface))
		return;

	weston_surface_map(surface);
	pos.c = weston_coord(0, 0);
	offset = weston_coord_surface(geometry.x, geometry.y,
				      dtsurface->view->surface);
	offset = weston_coord_surface_invert(offset);
	weston_view_set_position_with_offset(dtsurface->view, pos, offset);

	fullscreen = weston_desktop_surface_get_fullscreen(desktop_surface);
	if (!fullscreen) {
		weston_view_move_to_layer(dtsurface->view, &dts->layer.view_list);
		return;
	}

	output = weston_shell_utils_get_default_output(dts->compositor);
	struct weston_curtain_params curtain_params = {
		.r = 0.0, .g = 0.0, .b = 0.0, .a = 1.0,
		.pos = output->pos,
		.width = output->width, .height = output->height,
		.surface_committed = black_surface_committed,
		.get_label = black_surface_get_label,
		.surface_private = dtsurface->view,
		.capture_input = true,
	};

	weston_view_move_to_layer(dtsurface->view,
				  &dts->fullscreen_layer.view_list);
	weston_shell_utils_center_on_output(dtsurface->view, output);

	assert(!dtsurface->fullscreen_black_curtain);
	dtsurface->fullscreen_black_curtain =
		weston_shell_utils_curtain_create(dts->compositor, &curtain_params);
	weston_view_move_to_layer(dtsurface->fullscreen_black_curtain->view,
				  &dtsurface->view->layer_link);
}

static void
desktop_surface_move(struct weston_desktop_surface *desktop_surface,
		     struct weston_seat *seat, uint32_t serial, void *shell)
{
}

static void
desktop_surface_resize(struct weston_desktop_surface *desktop_surface,
		       struct weston_seat *seat, uint32_t serial,
		       enum weston_desktop_surface_edge edges, void *shell)
{
}

static void
desktop_surface_fullscreen_requested(struct weston_desktop_surface *desktop_surface,
				     bool fullscreen,
				     struct weston_output *output, void *shell)
{
	struct desktest_shell *dts = shell;
	int width = 0; int height = 0;

	weston_desktop_surface_set_fullscreen(desktop_surface, fullscreen);
	if (fullscreen) {
		struct weston_output *output =
			weston_shell_utils_get_default_output(dts->compositor);

		width = output->width;
		height = output->height;
	}
	weston_desktop_surface_set_size(desktop_surface, width, height);
}

static void
desktop_surface_maximized_requested(struct weston_desktop_surface *desktop_surface,
				    bool maximized, void *shell)
{
}

static void
desktop_surface_minimized_requested(struct weston_desktop_surface *desktop_surface,
				    void *shell)
{
}

static void
desktop_surface_ping_timeout(struct weston_desktop_client *desktop_client,
			     void *shell)
{
}

static void
desktop_surface_pong(struct weston_desktop_client *desktop_client,
		     void *shell)
{
}

static const struct weston_desktop_api shell_desktop_api = {
	.struct_size = sizeof(struct weston_desktop_api),
	.surface_added = desktop_surface_added,
	.surface_removed = desktop_surface_removed,
	.committed = desktop_surface_committed,
	.move = desktop_surface_move,
	.resize = desktop_surface_resize,
	.fullscreen_requested = desktop_surface_fullscreen_requested,
	.maximized_requested = desktop_surface_maximized_requested,
	.minimized_requested = desktop_surface_minimized_requested,
	.ping_timeout = desktop_surface_ping_timeout,
	.pong = desktop_surface_pong,
};

static int
background_get_label(struct weston_surface *surface, char *buf, size_t len)
{
	return snprintf(buf, len, "test desktop shell background");
}

static void
shell_destroy(struct wl_listener *listener, void *data)
{
	struct desktest_shell *dts;

	dts = container_of(listener, struct desktest_shell,
			   compositor_destroy_listener);

	wl_list_remove(&dts->compositor_destroy_listener.link);

	weston_desktop_destroy(dts->desktop);
	weston_shell_utils_curtain_destroy(dts->background);

	weston_layer_fini(&dts->layer);
	weston_layer_fini(&dts->background_layer);
	weston_layer_fini(&dts->fullscreen_layer);

	free(dts);
}

static void
desktest_shell_click_to_activate_binding(struct weston_pointer *pointer,
					 const struct timespec *time,
					 uint32_t button, void *data)
{
	if (pointer->grab != &pointer->default_grab)
		return;
	if (pointer->focus == NULL)
		return;

	weston_view_activate_input(pointer->focus, pointer->seat,
				   WESTON_ACTIVATE_FLAG_CLICKED);
}

static void
desktest_shell_add_bindings(struct desktest_shell *dts)
{
	weston_compositor_add_button_binding(dts->compositor, BTN_LEFT, 0,
					     desktest_shell_click_to_activate_binding,
					     dts);
}

WL_EXPORT int
wet_shell_init(struct weston_compositor *ec,
	       int *argc, char *argv[])
{
	struct desktest_shell *dts;
	struct weston_output *output;
	struct weston_coord_global pos;

	dts = xzalloc(sizeof *dts);
	if (!dts)
		return -1;

	if (!weston_compositor_add_destroy_listener_once(ec,
							 &dts->compositor_destroy_listener,
							 shell_destroy)) {
		free(dts);
		return 0;
	}

	dts->compositor = ec;

	weston_layer_init(&dts->layer, ec);
	weston_layer_init(&dts->background_layer, ec);
	weston_layer_init(&dts->fullscreen_layer, ec);

	weston_layer_set_position(&dts->layer,
				  WESTON_LAYER_POSITION_NORMAL);
	weston_layer_set_position(&dts->background_layer,
				  WESTON_LAYER_POSITION_BACKGROUND);
	weston_layer_set_position(&dts->fullscreen_layer,
				  WESTON_LAYER_POSITION_FULLSCREEN);

	output = weston_shell_utils_get_default_output(dts->compositor);
	struct weston_curtain_params background_params = {
		.r = 0.16, .g = 0.32, .b = 0.48, .a = 1.0,
		.pos = output->pos,
		.width = output->width, .height = output->height,
		.capture_input = true,
		.surface_committed = NULL,
		.get_label = background_get_label,
		.surface_private = NULL,
	};

	dts->background = weston_shell_utils_curtain_create(ec, &background_params);
	if (dts->background == NULL)
		goto out_free;
	weston_surface_set_role(dts->background->view->surface,
				"test-desktop background", NULL, 0);

	pos.c = weston_coord(0, 0);
	weston_view_set_position(dts->background->view, pos);
	weston_view_move_to_layer(dts->background->view,
				  &dts->background_layer.view_list);

	dts->desktop = weston_desktop_create(ec, &shell_desktop_api, dts);
	if (dts->desktop == NULL)
		goto out_view;

	screenshooter_create(ec);

	desktest_shell_add_bindings(dts);

	return 0;

out_view:
	weston_shell_utils_curtain_destroy(dts->background);

out_free:
	wl_list_remove(&dts->compositor_destroy_listener.link);
	free(dts);

	return -1;
}

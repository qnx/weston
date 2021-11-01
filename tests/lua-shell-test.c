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
#include <sys/mman.h>

#include "libweston-internal.h"
#include "libweston/desktop.h"
#include "shared/xalloc.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"
#include "tests/test-config.h"
#include "xdg-client-helper.h"

#define NR_XDG_SURFACES	4

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_PIXMAN;
	setup.width = 320;
	setup.height = 240;
	setup.shell = SHELL_LUA;
	setup.logging_scopes = "log,test-harness-plugin";
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	weston_ini_setup(&setup,
			 cfgln("[shell]"),
			 cfgln("lua-script=%s", WESTON_LUA_SHELL_DIR "/shell.lua"));

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

#define DECLARE_LIST_ITERATOR(name, parent, list, child, link)			\
static child *									\
next_##name(parent *from, child *pos)						\
{										\
	struct wl_list *entry = pos ? &pos->link : &from->list;			\
	child *ret = wl_container_of(entry->next, ret, link);			\
	return (&ret->link == &from->list) ? NULL : ret;			\
}

DECLARE_LIST_ITERATOR(pnode_from_z, struct weston_output, paint_node_z_order_list,
		      struct weston_paint_node, z_order_link);

TEST(four_apps_in_a_square)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct app {
		const char *title_id;
		int width, height;
		float x, y;
	} apps[NR_XDG_SURFACES] = {
		{ "one", 	320, 240, 0.0,   0.0   },
		{ "two", 	160, 120, 160.0, 0.0   },
		{ "three", 	80,  60,  0.0,   120.0 },
		{ "four", 	40,  30,  160.0, 120.0 },
	};
	int i = NR_XDG_SURFACES - 1;

	struct xdg_client *xdg_client = create_xdg_client();

	struct xdg_surface_data *xdg_surface1 = create_xdg_surface(xdg_client);
	struct xdg_surface_data *xdg_surface2 = create_xdg_surface(xdg_client);
	struct xdg_surface_data *xdg_surface3 = create_xdg_surface(xdg_client);
	struct xdg_surface_data *xdg_surface4 = create_xdg_surface(xdg_client);

	test_assert_ptr_not_null(xdg_client);
	test_assert_ptr_not_null(xdg_surface1);
	test_assert_ptr_not_null(xdg_surface2);
	test_assert_ptr_not_null(xdg_surface3);
	test_assert_ptr_not_null(xdg_surface4);

	xdg_surface_make_toplevel(xdg_surface1, "weston.test.lua.one", "one");
	xdg_surface_wait_configure(xdg_surface1);

	xdg_surface_make_toplevel(xdg_surface2, "weston.test.lua.two", "two");
	xdg_surface_wait_configure(xdg_surface2);

	xdg_surface_make_toplevel(xdg_surface3, "weston.test.lua.three", "three");
	xdg_surface_wait_configure(xdg_surface3);

	xdg_surface_make_toplevel(xdg_surface4, "weston.test.lua.four", "four");
	xdg_surface_wait_configure(xdg_surface4);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface1, 255, 0, 0);
	xdg_surface_commit_solid(xdg_surface2, 255, 0, 0);
	xdg_surface_commit_solid(xdg_surface3, 255, 0, 0);
	xdg_surface_commit_solid(xdg_surface4, 255, 0, 0);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode = NULL;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);

		while ((pnode = next_pnode_from_z(output, pnode)) != NULL && i > -1) {
			struct weston_view *view = pnode->view;
			struct weston_surface *surface = view->surface;
			struct weston_buffer *buffer = surface->buffer_ref.buffer;
			struct weston_desktop_surface *wds =
				weston_surface_get_desktop_surface(surface);
			const char *wds_title =
				weston_desktop_surface_get_title(wds);
			struct weston_geometry geom =
				weston_desktop_surface_get_geometry(wds);
			struct weston_coord_global pos =
				weston_view_get_pos_offset_global(view);
			struct app app = apps[i--];

			test_assert_ptr_not_null(pnode);
			test_assert_ptr_not_null(surface);
			test_assert_ptr_not_null(view);
			test_assert_ptr_not_null(buffer);

			test_assert_true(weston_view_is_mapped(view));
			test_assert_true(weston_surface_is_mapped(surface));

			test_assert_str_eq(wds_title, app.title_id);

			test_assert_int_eq(geom.width, app.width);
			test_assert_int_eq(geom.height, app.height);

			test_assert_f32_eq(pos.c.x, app.x);
			test_assert_f32_eq(pos.c.y, app.y);
		}
	}

	destroy_xdg_surface(xdg_surface1);
	destroy_xdg_surface(xdg_surface2);
	destroy_xdg_surface(xdg_surface3);
	destroy_xdg_surface(xdg_surface4);

	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

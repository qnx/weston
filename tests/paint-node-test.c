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
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_PIXMAN;
	setup.width = 320;
	setup.height = 240;
	setup.shell = SHELL_TEST_DESKTOP;
	setup.logging_scopes = "log,test-harness-plugin";
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

static struct buffer *
surface_commit_color(struct client *client, struct wl_surface *surface,
		     pixman_color_t *color, int width, int height)
{
	struct buffer *buf;

	buf = create_shm_buffer_solid(client, width, height, color);
	wl_surface_attach(surface, buf->proxy, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, width, height);
	wl_surface_commit(surface);

	return buf;
}

#define DECLARE_LIST_ITERATOR(name, parent, list, child, link)			\
static child *									\
next_##name(parent *from, child *pos)						\
{										\
	struct wl_list *entry = pos ? &pos->link : &from->list;			\
	if (entry->next == &from->list)						\
		return NULL;							\
	return container_of(entry->next, child, link);				\
}

DECLARE_LIST_ITERATOR(output, struct weston_compositor, output_list,
		      struct weston_output, link);
DECLARE_LIST_ITERATOR(pnode_from_z, struct weston_output, paint_node_z_order_list,
		      struct weston_paint_node, z_order_link);

static enum weston_paint_node_status
get_paint_node_status(struct client *client,
		      struct wet_testsuite_data *suite_data)
{
	enum weston_paint_node_status changes = WESTON_PAINT_NODE_CLEAN;

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_compositor *compositor;
		struct weston_output *output;
		struct weston_head *head;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);
		compositor = breakpoint->compositor;
		head = breakpoint->resource;
		output = next_output(compositor, NULL);
		test_assert_ptr_eq(output, head->output);
		test_assert_str_eq(output->name, "headless");
		test_assert_ptr_null(next_output(compositor, output));
		changes = output->paint_node_changes;
	}

	return changes;
}


TEST(paint_node_status_on_repaint)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct client *client;
	struct buffer *buf1, *buf2, *buf3;
	enum weston_paint_node_status changes;
	struct surface *new_surf;
	struct rectangle opaque = { .x = 0, .y = 0, .width = 100, .height = 100 };
	pixman_color_t red;

	color_rgb888(&red, 255, 0, 0);

	client = create_client();
	test_assert_ptr_not_null(client);

	client->surface = create_test_surface(client);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);
	weston_test_move_surface(client->test->weston_test, client->surface->wl_surface,
				 50, 50);
	buf1 = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);
	changes = get_paint_node_status(client, suite_data);
	test_assert_enum(changes, WESTON_PAINT_NODE_ALL_DIRTY);

	/* move the surface */
	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);
	weston_test_move_surface(client->test->weston_test, client->surface->wl_surface,
				 80, 80);
	wl_surface_attach(client->surface->wl_surface, buf1->proxy, 0, 0);
	wl_surface_damage_buffer(client->surface->wl_surface, 0, 0, 200, 200);
	wl_surface_commit(client->surface->wl_surface);
	changes = get_paint_node_status(client, suite_data);
	test_assert_enum(changes,
			 (WESTON_PAINT_NODE_BUFFER_DIRTY |
			  WESTON_PAINT_NODE_VIEW_DIRTY |
			  WESTON_PAINT_NODE_VISIBILITY_DIRTY));

	/* a new buffer */
	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);
	buf2 = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);
	changes = get_paint_node_status(client, suite_data);
	test_assert_enum(changes, WESTON_PAINT_NODE_BUFFER_DIRTY);

	/* a buffer with updated dimensions */
	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);
	buf3 = surface_commit_color(client, client->surface->wl_surface, &red, 200, 200);
	changes = get_paint_node_status(client, suite_data);
	test_assert_enum(changes,
			 (WESTON_PAINT_NODE_BUFFER_DIRTY |
			  WESTON_PAINT_NODE_VIEW_DIRTY |
			  WESTON_PAINT_NODE_VISIBILITY_DIRTY));

	/* an opaque buffer moving will change visibility */
	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);
	surface_set_opaque_rect(client->surface, &opaque);
	weston_test_move_surface(client->test->weston_test, client->surface->wl_surface,
				 100, 100);
	wl_surface_attach(client->surface->wl_surface, buf3->proxy, 0, 0);
	wl_surface_damage_buffer(client->surface->wl_surface, 0, 0, 200, 200);
	wl_surface_commit(client->surface->wl_surface);
	changes = get_paint_node_status(client, suite_data);
	test_assert_enum(changes,
			 (WESTON_PAINT_NODE_BUFFER_DIRTY |
			  WESTON_PAINT_NODE_VIEW_DIRTY |
			  WESTON_PAINT_NODE_VISIBILITY_DIRTY));

	/* a new surface rebuilds the view list */
	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);
	new_surf = create_test_surface(client);
	weston_test_move_surface(client->test->weston_test,
				 new_surf->wl_surface,
				 5, 5);
	wl_surface_attach(new_surf->wl_surface, buf1->proxy, 0, 0);
	wl_surface_damage_buffer(new_surf->wl_surface, 0, 0, 200, 200);
	wl_surface_commit(new_surf->wl_surface);
	changes = get_paint_node_status(client, suite_data);
	test_assert_enum(changes, WESTON_PAINT_NODE_ALL_DIRTY);

	buffer_destroy(buf1);
	buffer_destroy(buf2);
	buffer_destroy(buf3);
	surface_destroy(new_surf);
	client_destroy(client);

	return RESULT_OK;
}


TEST(top_surface_present_in_output_repaint)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct client *client;
	struct buffer *buf;
	pixman_color_t red;

	color_rgb888(&red, 255, 0, 0);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(client->test->weston_test, 0, 1, 0, 2, 30);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);

	buf = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_compositor *compositor;
		struct weston_output *output;
		struct weston_head *head;
		struct weston_paint_node *pnode;
		struct weston_view *view;
		struct weston_surface *surface;
		struct weston_buffer *buffer;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);
		compositor = breakpoint->compositor;
		head = breakpoint->resource;
		output = next_output(compositor, NULL);
		test_assert_ptr_eq(output, head->output);
		test_assert_str_eq(output->name, "headless");
		test_assert_ptr_null(next_output(compositor, output));

		/* check that our surface is top of the paint node list */
		pnode = next_pnode_from_z(output, NULL);
		test_assert_ptr_not_null(pnode);
		view = pnode->view;
		surface = view->surface;
		buffer = surface->buffer_ref.buffer;
		test_assert_ptr_not_null(surface->resource);
		test_assert_ptr_eq(wl_resource_get_client(surface->resource),
				   suite_data->wl_client);
		test_assert_true(weston_view_is_mapped(view));
		test_assert_true(weston_surface_is_mapped(surface));
		test_assert_s32_eq(surface->width, 100);
		test_assert_s32_eq(surface->height, 100);
		test_assert_s32_eq(buffer->width, surface->width);
		test_assert_s32_eq(buffer->height, surface->height);
		test_assert_enum(buffer->type, WESTON_BUFFER_SHM);
	}

	buffer_destroy(buf);
	client_destroy(client);

	return RESULT_OK;
}

TEST(test_surface_unmaps_on_null)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct client *client;
	struct buffer *buf;
	pixman_color_t red;

	color_rgb888(&red, 255, 0, 0);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(client->test->weston_test, 0, 1, 0, 2, 30);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) client->output->wl_output);

	buf = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_compositor *compositor;
		struct weston_output *output;
		struct weston_head *head;
		struct weston_paint_node *pnode;
		struct weston_view *view;
		struct weston_surface *surface;
		struct weston_buffer *buffer;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);
		compositor = breakpoint->compositor;
		head = breakpoint->resource;
		output = next_output(compositor, NULL);
		test_assert_ptr_eq(output, head->output);
		test_assert_str_eq(output->name, "headless");
		test_assert_ptr_null(next_output(compositor, output));

		/* check that our surface is top of the paint node list */
		pnode = next_pnode_from_z(output, NULL);
		test_assert_ptr_not_null(pnode);
		view = pnode->view;
		surface = view->surface;
		buffer = surface->buffer_ref.buffer;
		test_assert_ptr_eq(wl_resource_get_client(surface->resource),
				   suite_data->wl_client);
		test_assert_true(weston_view_is_mapped(view));
		test_assert_true(weston_surface_is_mapped(surface));
		test_assert_s32_eq(surface->width, 100);
		test_assert_s32_eq(surface->height, 100);
		test_assert_s32_eq(buffer->width, surface->width);
		test_assert_s32_eq(buffer->height, surface->height);
		test_assert_enum(buffer->type, WESTON_BUFFER_SHM);

		REARM_BREAKPOINT(breakpoint);
	}

	wl_surface_attach(client->surface->wl_surface, NULL, 0, 0);
	wl_surface_commit(client->surface->wl_surface);

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_compositor *compositor;
		struct weston_output *output;
		struct weston_head *head;
		struct weston_paint_node *pnode;
		struct weston_view *view;
		struct weston_surface *surface;
		struct weston_buffer *buffer;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);
		compositor = breakpoint->compositor;
		head = breakpoint->resource;
		output = next_output(compositor, NULL);
		test_assert_ptr_eq(output, head->output);
		test_assert_str_eq(output->name, "headless");
		test_assert_ptr_null(next_output(compositor, output));

		/* check that our NULL-buffer commit removed the surface from
		 * view */
		pnode = next_pnode_from_z(output, NULL);
		test_assert_ptr_not_null(pnode);
		view = pnode->view;
		surface = view->surface;
		buffer = surface->buffer_ref.buffer;
		test_assert_ptr_null(surface->resource);
		test_assert_enum(buffer->type, WESTON_BUFFER_SOLID);
	}

	buffer_destroy(buf);
	client_destroy(client);

	return RESULT_OK;
}

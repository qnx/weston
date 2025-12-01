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
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"
#include "presentation-time-client-protocol.h"
#include "shared/xalloc.h"

static int feedback_count;

struct feedback {
	struct client *client;
	struct wp_presentation_feedback *obj;
	bool expect_present;
};

enum fifo_barrier_status {
	FIFO_BARRIER_INACTIVE = 0,
	FIFO_BARRIER_ACTIVE,
};

enum rearm_breakpoint {
	REARM_BREAKPOINT_NO = 0,
	REARM_BREAKPOINT_YES,
};

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

	buf = create_shm_buffer_a8r8g8b8(client, width, height);
	fill_image_with_color(buf->image, color);
	wl_surface_attach(surface, buf->proxy, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, width, height);
	wl_surface_commit(surface);

	return buf;
}

/* Ensure we can only have one fifo object for a surface */
TEST(get_two_fifos)
{
	struct client *client;
	struct wp_fifo_v1 *fifo1, *fifo2;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	fifo1 = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	fifo2 = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	expect_protocol_error(client, &wp_fifo_manager_v1_interface,
			      WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS);
	wp_fifo_v1_destroy(fifo2);
	wp_fifo_v1_destroy(fifo1);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure we can get a second fifo for a surface if we destroy the first. */
TEST(get_two_fifos_safely)
{
	struct client *client;
	struct wp_fifo_v1 *fifo;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	wp_fifo_v1_destroy(fifo);
	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	wp_fifo_v1_destroy(fifo);
	client_roundtrip(client);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure the appropriate error occurs for using a fifo object associated
 * with a destroyed surface.
 */
TEST(use_fifo_on_destroyed_surface)
{
	struct client *client;
	struct wp_fifo_v1 *fifo;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	surface_destroy(client->surface);
	client->surface = NULL;
	wp_fifo_v1_set_barrier(fifo);
	expect_protocol_error(client, &wp_fifo_v1_interface,
			      WP_FIFO_V1_ERROR_SURFACE_DESTROYED);
	wp_fifo_v1_destroy(fifo);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure the compositor doesn't explode if we delete a surface with
 * active barriers
 */
TEST(fifo_delete_surface_with_barriers)
{
	struct client *client;
	struct buffer *buf;
	struct wp_fifo_v1 *fifo;
	pixman_color_t red;
	int i;

	color_rgb888(&red, 255, 0, 0);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	wp_fifo_v1_set_barrier(fifo);
	buf = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);

	/* Load up some future transactions */
	for (i = 0; i < 10; i++) {
		wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		wl_surface_commit(client->surface->wl_surface);
	}

	/* Steal and destroy the surface */
	wl_surface_destroy(client->surface->wl_surface);
	client->surface->wl_surface = NULL;

	client_roundtrip(client);

	wp_fifo_v1_destroy(fifo);
	buffer_destroy(buf);
	client_destroy(client);

	return RESULT_OK;
}

static void
check_fifo_status(struct client *client,
		  struct wet_testsuite_data *suite_data,
		  enum fifo_barrier_status expected,
		  enum rearm_breakpoint rearm)
{
	bool expected_fifo_barrier = expected == FIFO_BARRIER_ACTIVE;

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_surface *surface;
		struct wl_resource *surface_res;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_LATCH);
		surface_res = wl_client_get_object(suite_data->wl_client,
						   wl_proxy_get_id((struct wl_proxy *)client->surface->wl_surface));
		surface = wl_resource_get_user_data(surface_res);

		test_assert_int_eq(surface->fifo_barrier, expected_fifo_barrier);
		if (rearm == REARM_BREAKPOINT_YES)
			REARM_BREAKPOINT(breakpoint);
	}
}

/* Make sure N barriers provokes N redraws */
TEST(fifo_many_barriers)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct client *client;
	struct buffer *buf, *buf2;
	struct wp_fifo_v1 *fifo;
	pixman_color_t red;
	int i;

	color_rgb888(&red, 255, 0, 0);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_LATCH,
			       (struct wl_proxy *) client->output->wl_output);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	wp_fifo_v1_set_barrier(fifo);
	buf = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);
	/* Check that a string of commits with fifo set result in that
	 * number of repaints.
	 */
	for (i = 0; i < 10; i++) {
		wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		wl_surface_commit(client->surface->wl_surface);
	}
	client_roundtrip(client);

	for (i = 0; i < 11; i++)
		check_fifo_status(client, suite_data, FIFO_BARRIER_ACTIVE, REARM_BREAKPOINT_YES);

	/* A new commit with a visible change will cause a repaint now, and we can
	 * check for clear fifo status after.
	 */
	buf2 = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);
	wl_surface_commit(client->surface->wl_surface);
	client_roundtrip(client);
	check_fifo_status(client, suite_data, FIFO_BARRIER_INACTIVE, REARM_BREAKPOINT_NO);

	wp_fifo_v1_destroy(fifo);
	buffer_destroy(buf2);
	buffer_destroy(buf);
	client_destroy(client);

	return RESULT_OK;
}

static void
feedback_sync_output(void *data,
		     struct wp_presentation_feedback *presentation_feedback,
		     struct wl_output *output)
{
	/* do nothing */
}

static void
feedback_presented(void *data,
		   struct wp_presentation_feedback *presentation_feedback,
		   uint32_t tv_sec_hi,
		   uint32_t tv_sec_lo,
		   uint32_t tv_nsec,
		   uint32_t refresh_nsec,
		   uint32_t seq_hi,
		   uint32_t seq_lo,
		   uint32_t flags)
{
	struct feedback *fb = data;

	test_assert_true(fb->expect_present);

	wp_presentation_feedback_destroy(fb->obj);
	free(fb);
	feedback_count--;
}

static void
feedback_discarded(void *data,
		   struct wp_presentation_feedback *presentation_feedback)
{
	struct feedback *fb = data;

	test_assert_false(fb->expect_present);

	wp_presentation_feedback_destroy(fb->obj);
	free(fb);
	feedback_count--;
}

static const struct wp_presentation_feedback_listener feedback_listener = {
	feedback_sync_output,
	feedback_presented,
	feedback_discarded
};

static void
feedback_create(struct client *client,
		struct wl_surface *surface,
		struct wp_presentation *pres,
		bool expect_present)
{
	struct feedback *fb;

	fb = xzalloc(sizeof *fb);
	fb->client = client;
	fb->obj = wp_presentation_feedback(pres, surface);
	wp_presentation_feedback_add_listener(fb->obj, &feedback_listener, fb);
	fb->expect_present = expect_present;
	feedback_count++;
}

/* Make sure fifo is ignored on occluded surfaces.
 * This is a "may" in the spec, so this isn't necessarily rigorous,
 * but a strong effort.
 */
TEST(fifo_on_occluded_surface)
{
	struct wl_subcompositor *subco;
	struct wl_surface *oc_surf;
	struct wl_region *opaque_region;
	struct wl_subsurface *oc_subsurf;
	struct client *client;
	struct buffer *buf_main, *buf_main_small, *buf_sub;
	struct wp_fifo_v1 *fifo;
	struct wp_presentation *pres;
	pixman_color_t red, green, blue;
	int i;
	bool match;

	color_rgb888(&red, 255, 0, 0);
	color_rgb888(&green, 0, 255, 0);
	color_rgb888(&blue, 0, 0, 255);

	client = create_client_and_test_surface(10, 10, 100, 100);
	test_assert_ptr_not_null(client);

	pres = client_get_presentation(client);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(client->test->weston_test, 0, 1, 0, 2, 30);

	subco = client_get_subcompositor(client);
	oc_surf = wl_compositor_create_surface(client->wl_compositor);
	oc_subsurf = wl_subcompositor_get_subsurface(subco, oc_surf,
						     client->surface->wl_surface);
	buf_main = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);
	buf_sub = surface_commit_color(client, oc_surf, &green, 50, 50);
	wl_subsurface_set_position(oc_subsurf, 0, 0);
	wl_subsurface_place_above(oc_subsurf, client->surface->wl_surface);
	wl_subsurface_set_desync(oc_subsurf);

	/* Tell the compositor our subsurface is opaque so it knows it should
	 * occlude the parent later
	 */
	opaque_region = wl_compositor_create_region(client->wl_compositor);
	wl_region_add(opaque_region, 0, 0, 50, 50);
	wl_surface_set_opaque_region(oc_surf, opaque_region);
	wl_region_destroy(opaque_region);
	wl_surface_commit(oc_surf);

	/* Let's take a shot to make sure the smaller red parent surface is above the
	 * large green subsurface at this point.
	 */
	match = verify_screen_content(client, "fifo_occlude_start", 0, NULL,
				      0, NULL, NO_DECORATIONS);
	test_assert_true(match);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, client->surface->wl_surface);
	wp_fifo_v1_set_barrier(fifo);
	wl_surface_commit(client->surface->wl_surface);

	feedback_count = 0;
	for (i = 0; i < 10; i++) {
		wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		feedback_create(client, client->surface->wl_surface, pres, true);
		wl_surface_commit(client->surface->wl_surface);
	}

	/* Commit a buffer on the main surface that is smaller than the opaque subsurface
	 * that is above it. This will cause the main surface to become occluded.
	 */
	wp_fifo_v1_wait_barrier(fifo);
	buf_main_small = surface_commit_color(client, client->surface->wl_surface, &red, 25, 25);
	wl_surface_commit(client->surface->wl_surface);

	/* These waits shouldn't happen, so all the feedback should be discarded */
	for (i = 0; i < 10; i++) {
		wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		feedback_create(client, client->surface->wl_surface, pres, false);
		wl_surface_commit(client->surface->wl_surface);
	}

	/* Kick that last feedback out as discarded */
	wp_fifo_v1_wait_barrier(fifo);
	wl_surface_commit(client->surface->wl_surface);

	/* Destroy the fifo early so we can be sure destroying a fifo proxy
	 * doesn't change existing content updates.
	 */
	wp_fifo_v1_destroy(fifo);
	client_roundtrip(client);

	while (feedback_count)
		test_assert_int_ge(wl_display_dispatch(client->wl_display), 0);

	/* And let's make sure what we're seeing is just the subsurface */
	match = verify_screen_content(client, "fifo_occlude_restack", 0, NULL,
				      0, NULL, NO_DECORATIONS);
	test_assert_true(match);

	wp_presentation_destroy(pres);
	wl_subcompositor_destroy(subco);
	wl_subsurface_destroy(oc_subsurf);
	wl_surface_destroy(oc_surf);
	buffer_destroy(buf_main);
	buffer_destroy(buf_main_small);
	buffer_destroy(buf_sub);
	client_destroy(client);

	return RESULT_OK;
}

static int
count_barriers(struct client *client,
	       struct wl_surface *wlsurface,
	       struct wet_testsuite_data *suite_data)
{
	int barrier_count = 0;
	bool barrier;

	do {
		RUN_INSIDE_BREAKPOINT(client, suite_data) {
			struct weston_surface *surface;
			struct wl_resource *surface_res;

			test_assert_enum(breakpoint->template_->breakpoint,
					 WESTON_TEST_BREAKPOINT_POST_LATCH);
			surface_res = wl_client_get_object(suite_data->wl_client,
							   wl_proxy_get_id((struct wl_proxy *)wlsurface));
			surface = wl_resource_get_user_data(surface_res);

			barrier = surface->fifo_barrier;
			if (barrier) {
				barrier_count++;
				REARM_BREAKPOINT(breakpoint);
			}
		}
	} while (barrier);
	return barrier_count;
}

static int
get_surface_width(struct client *client,
		  struct wl_surface *wlsurface,
		  struct wet_testsuite_data *suite_data,
		  bool rearm)
{
	int width;

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_surface *surface;
		struct wl_resource *surface_res;

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_LATCH);
		surface_res = wl_client_get_object(suite_data->wl_client,
						   wl_proxy_get_id((struct wl_proxy *)wlsurface));
		surface = wl_resource_get_user_data(surface_res);

		width = surface->width;
		if (rearm)
			REARM_BREAKPOINT(breakpoint);
	}
	return width;
}

/* Make sure fifo is ignored on synchronous subsurfaces, but works on desync */
TEST(fifo_on_subsurface)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct wl_subcompositor *subco;
	struct wl_surface *surf;
	struct wl_subsurface *subsurf;
	struct client *client;
	struct buffer *buf_main, *buf_sub, *buf_sub_2;
	struct buffer *buf[10];
	struct wp_fifo_v1 *fifo;
	pixman_color_t red, green, blue;
	bool match;
	int i;

	color_rgb888(&red, 255, 0, 0);
	color_rgb888(&green, 0, 255, 0);
	color_rgb888(&blue, 0, 0, 255);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	subco = client_get_subcompositor(client);
	surf = wl_compositor_create_surface(client->wl_compositor);

	subsurf = wl_subcompositor_get_subsurface(subco, surf,
						  client->surface->wl_surface);
	buf_main = surface_commit_color(client, client->surface->wl_surface, &red, 150, 150);
	weston_test_move_surface(client->test->weston_test, client->surface->wl_surface, 50, 50);
	buf_sub = surface_commit_color(client, surf, &green, 200, 200);
	wl_subsurface_set_position(subsurf, -25, -25);
	wl_subsurface_place_below(subsurf, client->surface->wl_surface);

	/* surf is implicitly in synchronized mode */
	wl_surface_commit(surf);
	wl_surface_commit(client->surface->wl_surface);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager, surf);

	match = verify_screen_content(client, "fifo_subsurface_start", 0,
				      NULL, 0, NULL, NO_DECORATIONS);
	test_assert_true(match);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_LATCH,
			       (struct wl_proxy *) client->output->wl_output);

	/* Since the surface is synchronized, weston will push all of these
	 * into the suburface cache. And also because it's synchronized,
	 * the fifo_wait won't wait.
	 */
	for (i = 0; i < 20; i++) {
		if (i < 19)
			wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		wl_surface_commit(surf);
	}
	wl_surface_commit(client->surface->wl_surface);

	/* Change the surface width so we have something to look for. */
	buf_sub_2 = surface_commit_color(client, surf, &green, 201, 201);
	wl_surface_commit(surf);
	wl_surface_commit(client->surface->wl_surface);

	/* This effectively serializes with the compositor, breaking at the first
	 * latch. If our width is updated at the first latch, then the sync
	 * subsurface commits were properly consumed.
	 */
	test_assert_int_eq(get_surface_width(client, surf, suite_data, false), 201);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_LATCH,
			       (struct wl_proxy *) client->output->wl_output);

	/* Let's make sure desynchronized surfaces work properly too */
	wl_subsurface_set_desync(subsurf);
	for (i = 0; i < 10; i++) {
		/* Skip the last barrier so we're assured a redraw with no
		 * barrier set to give count_barriers a terminal case */
		if (i < 9)
			wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		/* Commit a new buffer so there's scene damage. */
		buf[i] = surface_commit_color(client, surf, &red, 100, 100);
	}
	test_assert_int_eq(count_barriers(client, surf, suite_data), 9);

	wp_fifo_v1_destroy(fifo);
	wl_subcompositor_destroy(subco);
	wl_subsurface_destroy(subsurf);
	wl_surface_destroy(surf);
	buffer_destroy(buf_main);
	buffer_destroy(buf_sub_2);
	buffer_destroy(buf_sub);
	for (i = 0; i < 10; i++)
		buffer_destroy(buf[i]);
	client_destroy(client);

	return RESULT_OK;
}

/* Make sure that surface state changes that can change occlusion status are
 * properly noticed before a redraw.
 */
TEST(fifo_when_occlusion_changes)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct wl_subcompositor *subco;
	struct wl_surface *surf;
	struct wl_subsurface *subsurf;
	struct wl_region *opaque_region;
	struct client *client;
	struct buffer *buf_main[3], *buf_sub;
	struct wp_fifo_v1 *fifo;
	pixman_color_t red, green, blue;
	int i, width;
	bool match;

	color_rgb888(&red, 255, 0, 0);
	color_rgb888(&green, 0, 255, 0);
	color_rgb888(&blue, 0, 0, 255);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	subco = client_get_subcompositor(client);
	surf = wl_compositor_create_surface(client->wl_compositor);

	subsurf = wl_subcompositor_get_subsurface(subco, surf,
						  client->surface->wl_surface);
	buf_main[0] = surface_commit_color(client, client->surface->wl_surface, &red, 150, 150);
	weston_test_move_surface(client->test->weston_test, client->surface->wl_surface, 50, 50);
	buf_sub = surface_commit_color(client, surf, &green, 200, 200);
	wl_subsurface_set_position(subsurf, -25, -25);
	/* Make the subsurface opaque and above the parent */
	opaque_region = wl_compositor_create_region(client->wl_compositor);
	wl_region_add(opaque_region, 0, 0, 200, 200);
	wl_surface_set_opaque_region(surf, opaque_region);
	wl_region_destroy(opaque_region);
	wl_subsurface_place_above(subsurf, client->surface->wl_surface);

	/* surf is implicitly in synchronized mode */
	wl_surface_commit(surf);
	wl_surface_commit(client->surface->wl_surface);

	fifo = wp_fifo_manager_v1_get_fifo(client->fifo_manager,
					   client->surface->wl_surface);

	/* Wait for a render before we start queuing up fifo requests */
	match = verify_screen_content(client, "occlusion_change_start", 0,
				      NULL, 0, NULL, NO_DECORATIONS);
	test_assert_true(match);

	client_push_breakpoint(client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_LATCH,
			       (struct wl_proxy *) client->output->wl_output);

	/* fifo operations should do nothing, as the surface is occluded. */
	for (i = 0; i < 30; i++) {
		wp_fifo_v1_set_barrier(fifo);
		wp_fifo_v1_wait_barrier(fifo);
		wl_surface_commit(client->surface->wl_surface);
	}

	/* Bigger buffer, the surface will no longer be fully occluded */
	wp_fifo_v1_set_barrier(fifo);
	wp_fifo_v1_wait_barrier(fifo);
	buf_main[1] = surface_commit_color(client, client->surface->wl_surface, &red, 200, 200);

	/* Another buffer - if visibility is improperly tracked, we'll only
	 * see this one and not the previous.
	 */
	wp_fifo_v1_set_barrier(fifo);
	wp_fifo_v1_wait_barrier(fifo);
	buf_main[2] = surface_commit_color(client, client->surface->wl_surface, &red, 210, 210);
	client_roundtrip(client);

	width = get_surface_width(client, client->surface->wl_surface,
				  suite_data, true);
	test_assert_int_eq(width, 200);

	width = get_surface_width(client, client->surface->wl_surface,
				  suite_data, false);
	test_assert_int_eq(width, 210);

	wp_fifo_v1_destroy(fifo);
	wl_subcompositor_destroy(subco);
	wl_subsurface_destroy(subsurf);
	wl_surface_destroy(surf);
	for (i = 0; i < 3; i++)
		buffer_destroy(buf_main[i]);
	buffer_destroy(buf_sub);
	client_destroy(client);

	return RESULT_OK;
}

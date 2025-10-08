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
#include "shared/timespec-util.h"
#include "shared/xalloc.h"

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

/* Ensure we can only have one commit-timer object for a surface */
TEST(get_two_timers)
{
	struct client *client;
	struct wp_commit_timer_v1 *timer1, *timer2;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	timer1 = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);
	timer2 = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);
	expect_protocol_error(client, &wp_commit_timing_manager_v1_interface,
			      WP_COMMIT_TIMING_MANAGER_V1_ERROR_COMMIT_TIMER_EXISTS);
	wp_commit_timer_v1_destroy(timer2);
	wp_commit_timer_v1_destroy(timer1);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure we can get a second timer for a surface if we destroy the first. */
TEST(get_two_timers_safely)
{
	struct client *client;
	struct wp_commit_timer_v1 *timer;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);
	wp_commit_timer_v1_destroy(timer);
	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);
	wp_commit_timer_v1_destroy(timer);
	client_roundtrip(client);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure the appropriate error occurs for using a timer object associated
 * with a destroyed surface.
 */
TEST(use_timer_on_destroyed_surface)
{
	struct client *client;
	struct wp_commit_timer_v1 *timer;
	struct wp_presentation *pres;
	struct timespec now;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	pres = client_get_presentation(client);

	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);
	surface_destroy(client->surface);
	client->surface = NULL;

	clock_gettime(client_get_presentation_clock(client), &now);
	wp_commit_timer_v1_set_timestamp(timer,
					 (uint64_t)now.tv_sec >> 32,
					 now.tv_sec,
					 now.tv_nsec);
	expect_protocol_error(client, &wp_commit_timer_v1_interface,
			      WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED);

	wp_presentation_destroy(pres);
	wp_commit_timer_v1_destroy(timer);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure an error occurs for invalid tv_nsec. */
TEST(invalid_timestamp)
{
	struct client *client;
	struct wp_commit_timer_v1 *timer;
	struct wp_presentation *pres;
	struct timespec now;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	pres = client_get_presentation(client);

	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);

	clock_gettime(client_get_presentation_clock(client), &now);
	wp_commit_timer_v1_set_timestamp(timer,
					 (uint64_t)now.tv_sec >> 32,
					 now.tv_sec,
					 1000000000);
	expect_protocol_error(client, &wp_commit_timer_v1_interface,
			      WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP);

	wp_presentation_destroy(pres);
	wp_commit_timer_v1_destroy(timer);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure an error occurs when a second timestamp is set before a
 * wl_surface.commit
 */
TEST(too_many_timestamps)
{
	struct client *client;
	struct wp_commit_timer_v1 *timer;
	struct wp_presentation *pres;
	struct timespec now;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	pres = client_get_presentation(client);

	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);

	clock_gettime(client_get_presentation_clock(client), &now);
	wp_commit_timer_v1_set_timestamp(timer,
					 (uint64_t)now.tv_sec >> 32,
					 now.tv_sec,
					 now.tv_nsec);
	wp_commit_timer_v1_set_timestamp(timer,
					 (uint64_t)now.tv_sec >> 32,
					 now.tv_sec,
					 now.tv_nsec);
	expect_protocol_error(client, &wp_commit_timer_v1_interface,
			      WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS);

	wp_presentation_destroy(pres);
	wp_commit_timer_v1_destroy(timer);
	client_destroy(client);

	return RESULT_OK;
}


/* Ensure the compositor doesn't explode if we delete a surface with
 * timestamped content updates
 */
TEST(commit_timing_delete_surface_with_timestamps)
{
	struct client *client;
	struct buffer *buf;
	struct wp_commit_timer_v1 *timer;
	struct timespec target;
	struct wp_presentation *pres;
	pixman_color_t red;
	int i;

	color_rgb888(&red, 255, 0, 0);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	pres = client_get_presentation(client);
	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);
	buf = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);

	clock_gettime(client_get_presentation_clock(client), &target);
	/* Load up some future transactions */
	for (i = 0; i < 10; i++) {
		timespec_add_nsec(&target, &target, (NSEC_PER_SEC * 60ULL));
		wp_commit_timer_v1_set_timestamp(timer,
						 (uint64_t)target.tv_sec >> 32,
						 target.tv_sec,
						 target.tv_nsec);

		wl_surface_commit(client->surface->wl_surface);
	}

	/* Steal and destroy the surface */
	wl_surface_destroy(client->surface->wl_surface);
	client->surface->wl_surface = NULL;

	client_roundtrip(client);

	wp_commit_timer_v1_destroy(timer);
	wp_presentation_destroy(pres);
	buffer_destroy(buf);
	client_destroy(client);

	return RESULT_OK;
}

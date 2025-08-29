/*
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
 * Copyright © 2025 Collabora, Ltd.
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include <linux/input.h>

#include <wayland-client.h>
#include "shared/os-compatibility.h"
#include "shared/timespec-util.h"
#include "shared/xalloc.h"
#include <libweston/zalloc.h>
#include "xdg-shell-client-protocol.h"

#include "commit-timing-v1-client-protocol.h"
#include "fifo-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define MAX_BUFFER_ALLOC	1000

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_shm *shm;
	struct wp_commit_timing_manager_v1 *commit_timing_manager;
	struct wp_fifo_manager_v1 *fifo_manager;
	struct wp_presentation *presentation;
	bool have_clock_id;
	clockid_t presentation_clock_id;
	int64_t first_frame_time;
	int64_t refresh_nsec;
};

struct buffer {
	struct window *window;
	struct wl_buffer *buffer;
	void *shm_data;
	int busy;
	int width, height;
	size_t size;	/* width * 4 * height */
	struct wl_list buffer_link; /** window::buffer_list */
};

struct window {
	struct display *display;
	int width, height;
	int init_width, init_height;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_list buffer_list;
	struct wl_callback *callback;
	struct wp_fifo_v1 *fifo;
	struct wp_commit_timer_v1 *commit_timer;
	bool wait_for_configure;
	bool maximized;
	bool fullscreen;
	bool needs_update_buffer;
};

struct feedback {
	struct wp_presentation_feedback *fb;
	struct window *window;
	int64_t target_time;
	bool final;
};

static int running = 1;

static void
draw_for_time(void *data, int64_t time, bool wait_fifo);

static void
finish_run(struct window *window);

static struct buffer *
alloc_buffer(struct window *window, int width, int height)
{
	struct buffer *buffer = calloc(1, sizeof(*buffer));

	buffer->width = width;
	buffer->height = height;
	wl_list_insert(&window->buffer_list, &buffer->buffer_link);

	return buffer;
}

static void
destroy_buffer(struct buffer *buffer)
{
	if (buffer->buffer)
		wl_buffer_destroy(buffer->buffer);

	munmap(buffer->shm_data, buffer->size);
	wl_list_remove(&buffer->buffer_link);
	free(buffer);
}

static struct buffer *
pick_free_buffer(struct window *window)
{
	struct buffer *b;
	struct buffer *buffer = NULL;

	wl_list_for_each(b, &window->buffer_list, buffer_link) {
		if (!b->busy) {
			buffer = b;
			break;
		}
	}

	return buffer;
}

static void
prune_old_released_buffers(struct window *window)
{
	struct buffer *b, *b_next;

	wl_list_for_each_safe(b, b_next,
			      &window->buffer_list, buffer_link) {
		if (!b->busy && (b->width != window->width ||
		    b->height != window->height))
			destroy_buffer(b);
	}
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	struct buffer *mybuf = data;

	mybuf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static int
create_shm_buffer(struct window *window, struct buffer *buffer)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;
	int width, height;
	struct display *display;

	width = window->width;
	height = window->height;
	stride = width * 4;
	size = stride * height;
	display = window->display;

	fd = os_create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n",
			size, strerror(errno));
		return -1;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	pool = wl_shm_create_pool(display->shm, fd, size);
	buffer->buffer = wl_shm_pool_create_buffer(pool, 0,
						   width, height,
						   stride,
						   WL_SHM_FORMAT_XRGB8888);
	wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->size = size;
	buffer->shm_data = data;

	return 0;
}

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
		       uint32_t format, int fd, uint32_t size)
{
	/* Just so we don’t leak the keymap fd */
	close(fd);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface,
		      struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		    uint32_t serial, uint32_t time, uint32_t key,
		    uint32_t state)
{
	if (key == KEY_ESC && state)
		running = 0;
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
			 enum wl_seat_capability caps)
{
	struct display *d = data;

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
		d->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && d->keyboard) {
		wl_keyboard_destroy(d->keyboard);
		d->keyboard = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(surface, serial);

	if (window->wait_for_configure) {
		draw_for_time(window, 0, false);
		window->wait_for_configure = false;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_xdg_surface_configure,
};

static void
handle_xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
	struct window *window = data;
	uint32_t *p;

	window->fullscreen = false;
	window->maximized = false;

	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			window->fullscreen = true;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			window->maximized = true;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!window->fullscreen && !window->maximized) {
			window->init_width = width;
			window->init_height = height;
		}
		window->width = width;
		window->height = height;
	} else if (!window->fullscreen && !window->maximized) {
		window->width = window->init_width;
		window->height = window->init_height;
	}

	window->needs_update_buffer = true;
}

static void
handle_xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_xdg_toplevel_configure,
	handle_xdg_toplevel_close,
};

static struct window *
create_window(struct display *display, int width, int height)
{
	struct window *window;
	int i;

	window = zalloc(sizeof *window);
	if (!window)
		return NULL;

	window->callback = NULL;
	window->display = display;
	window->width = width;
	window->height = height;
	window->init_width = width;
	window->init_height = height;
	window->surface = wl_compositor_create_surface(display->compositor);
	window->fifo = wp_fifo_manager_v1_get_fifo(display->fifo_manager,
						   window->surface);
	window->commit_timer = wp_commit_timing_manager_v1_get_timer(display->commit_timing_manager,
								     window->surface);
	window->needs_update_buffer = false;
	wl_list_init(&window->buffer_list);

	assert(display->wm_base);

	window->xdg_surface =
		xdg_wm_base_get_xdg_surface(display->wm_base,
					    window->surface);
	assert(window->xdg_surface);
	xdg_surface_add_listener(window->xdg_surface,
				 &xdg_surface_listener, window);

	window->xdg_toplevel =
		xdg_surface_get_toplevel(window->xdg_surface);
	assert(window->xdg_toplevel);
	xdg_toplevel_add_listener(window->xdg_toplevel,
				  &xdg_toplevel_listener, window);

	xdg_toplevel_set_title(window->xdg_toplevel, "simple-shm");
	xdg_toplevel_set_app_id(window->xdg_toplevel,
			"org.freedesktop.weston.simple-shm");

	wl_surface_commit(window->surface);
	window->wait_for_configure = true;

	for (i = 0; i < MAX_BUFFER_ALLOC; i++)
		alloc_buffer(window, window->width, window->height);

	return window;
}

static void
destroy_window(struct window *window)
{
	struct buffer *buffer, *buffer_next;

	if (window->callback)
		wl_callback_destroy(window->callback);

	wl_list_for_each_safe(buffer, buffer_next,
			      &window->buffer_list, buffer_link)
		destroy_buffer(buffer);

	if (window->xdg_toplevel)
		xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		xdg_surface_destroy(window->xdg_surface);
	wl_surface_destroy(window->surface);

	if (window->fifo)
		wp_fifo_v1_destroy(window->fifo);

	if (window->commit_timer)
		wp_commit_timer_v1_destroy(window->commit_timer);

	free(window);
}

static struct buffer *
window_next_buffer(struct window *window)
{
	struct buffer *buffer = NULL;
	int ret = 0;

	if (window->needs_update_buffer) {
		int i;

		for (i = 0; i < MAX_BUFFER_ALLOC; i++)
			alloc_buffer(window, window->width, window->height);

		window->needs_update_buffer = false;
	}

	buffer = pick_free_buffer(window);

	if (!buffer)
		return NULL;

	if (!buffer->buffer) {
		ret = create_shm_buffer(window, buffer);

		if (ret < 0)
			return NULL;

		/* paint the padding */
		memset(buffer->shm_data, 0xff,
		       window->width * window->height * 4);
	}

	return buffer;
}

static void
paint_pixels(void *image, int width, int height, uint32_t time)
{
	const int halfh = height / 2;
	const int halfw = width / 2;
	int ir, or;
	uint32_t *pixel = image;
	int y;

	/* squared radii thresholds */
	or = (halfw < halfh ? halfw : halfh) - 8;
	ir = or - 32;
	or *= or;
	ir *= ir;

	for (y = 0; y < height; y++) {
		int x;
		int y2 = (y - halfh) * (y - halfh);

		for (x = 0; x < width; x++) {
			uint32_t v;

			/* squared distance from center */
			int r2 = (x - halfw) * (x - halfw) + y2;

			if (r2 < ir)
				v = (r2 / 32 + time / 64) * 0x0080401;
			else if (r2 < or)
				v = (y + time / 32) * 0x0080401;
			else
				v = (x + time / 16) * 0x0080401;
			v &= 0x00ffffff;

			/* cross if compositor uses X from XRGB as alpha */
			if (abs(x - y) > 6 && abs(x + y - height) > 6)
				v |= 0xff000000;

			*pixel++ = v;
		}
	}
}

static void
queue_some_frames(struct window *window)
{
	struct display *display = window->display;
	int64_t target_nsec;
	int i;

	assert(display->first_frame_time);

	/* Round off error will cause us problems if we don't
	 * reduce this a bit, because we could end up rounding
	 * to either side of a refresh.
	 */
	target_nsec = display->first_frame_time - 100000;

	for (i = 0; i < 60; i++) {
		target_nsec += display->refresh_nsec * 2;
		draw_for_time(window, target_nsec, false);
	}

	for (i = 0; i < 30; i++) {
		target_nsec += display->refresh_nsec * 4;
		draw_for_time(window, target_nsec, false);
	}

	for (i = 0; i < 10; i++) {
		target_nsec += display->refresh_nsec * 10;
		draw_for_time(window, target_nsec, false);
	}

	for (i = 0; i < 10; i++) {
		target_nsec += display->refresh_nsec * 100;
		draw_for_time(window, target_nsec, false);
	}

	finish_run(window);
}

static void
feedback_sync_output(void *data,
		     struct wp_presentation_feedback *presentation_feedback,
		     struct wl_output *output)
{
	/* Just don't care */
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
	struct feedback *feedback = data;
	struct window *window = feedback->window;
	struct display *display = window->display;
	struct timespec pres_ts = {
		.tv_sec = ((int64_t)tv_sec_hi << 32) + tv_sec_lo,
		.tv_nsec = tv_nsec,
	};
	int64_t ntime = timespec_to_nsec(&pres_ts);
	double delay;

	if (feedback->final) {
		running = 0;
		goto out;
	}

	if (!feedback->target_time) {
		display->first_frame_time = ntime;
		display->refresh_nsec = refresh_nsec;
		queue_some_frames(window);
		goto out;
	}

	delay = (ntime - feedback->target_time) / 1000000.0;

	printf("%fms away from intended time\n", delay);
	if (fabs(delay) > display->refresh_nsec / 1000000)
		printf("Warning: we missed the intended target display cycle.\n");

out:
	wp_presentation_feedback_destroy(feedback->fb);
	free(feedback);
}

static void
feedback_discarded(void *data,
		   struct wp_presentation_feedback *presentation_feedback)
{
	struct feedback *feedback = data;

	printf("Warning: a frame was discarded\n");

	if (feedback->final)
		running = 0;

	wp_presentation_feedback_destroy(feedback->fb);
	free(feedback);
}

static const struct wp_presentation_feedback_listener feedback_listener = {
	feedback_sync_output,
	feedback_presented,
	feedback_discarded,
};

static void
finish_run(struct window *window)
{
	struct display *display = window->display;
	struct feedback *feedback;
	struct buffer *buffer;

	feedback = xzalloc(sizeof *feedback);
	feedback->window = window;
	feedback->final = true;
	feedback->target_time = 0;

	buffer = window_next_buffer(window);
	assert(buffer);

	paint_pixels(buffer->shm_data, window->width,
		     window->height, 1);

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	feedback->fb = wp_presentation_feedback(display->presentation,
						window->surface);
	wp_presentation_feedback_add_listener(feedback->fb,
					      &feedback_listener, feedback);

	wp_fifo_v1_wait_barrier(window->fifo);
	wl_surface_commit(window->surface);
}

static void
draw_for_time(void *data, int64_t time, bool wait_fifo)
{
	struct window *window = data;
	struct display *display = window->display;
	struct buffer *buffer;
	struct feedback *feedback;

	assert(display->have_clock_id);

	prune_old_released_buffers(window);

	buffer = window_next_buffer(window);
	assert(buffer);

	paint_pixels(buffer->shm_data, window->width,
		     window->height, time / 1000000);

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	feedback = xzalloc(sizeof *feedback);
	feedback->window = window;

	feedback->fb = wp_presentation_feedback(display->presentation,
						window->surface);
	wp_presentation_feedback_add_listener(feedback->fb,
					      &feedback_listener, feedback);

	feedback->target_time = time;
	if (time) {
		struct timespec target;

		timespec_from_nsec(&target, time);
		wp_commit_timer_v1_set_timestamp(window->commit_timer,
						 (int64_t)target.tv_sec >> 32,
						 target.tv_sec,
						 target.tv_nsec);
	}
	wp_fifo_v1_set_barrier(window->fifo);
	wl_surface_commit(window->surface);
	buffer->busy = 1;
}

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
};

static void
presentation_handle_clock_id(void *data,
			     struct wp_presentation *wp_presentation,
			     uint32_t clock_id)
{
	struct display *display = data;

	display->presentation_clock_id = clock_id;
	display->have_clock_id = true;
}

static const struct wp_presentation_listener presentation_listener = {
	presentation_handle_clock_id,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t id, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry,
					 id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		d->wm_base = wl_registry_bind(registry,
					      id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(d->wm_base, &xdg_wm_base_listener, d);
	} else if (strcmp(interface, "wl_seat") == 0) {
		d->seat = wl_registry_bind(registry, id,
					   &wl_seat_interface, 1);
		wl_seat_add_listener(d->seat, &seat_listener, d);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry,
					  id, &wl_shm_interface, 1);
	} else if (strcmp(interface, wp_commit_timing_manager_v1_interface.name) == 0) {
		d->commit_timing_manager = wl_registry_bind(registry, id,
							    &wp_commit_timing_manager_v1_interface,
							    1);
	} else if (strcmp(interface, wp_fifo_manager_v1_interface.name) == 0) {
		d->fifo_manager = wl_registry_bind(registry, id,
						   &wp_fifo_manager_v1_interface,
						   1);
	} else if (strcmp(interface, wp_presentation_interface.name) == 0) {
		d->presentation = wl_registry_bind(registry, id,
						   &wp_presentation_interface,
						   2);
		wp_presentation_add_listener(d->presentation,
					     &presentation_listener, d);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static struct display *
create_display(void)
{
	struct display *display;

	display = xzalloc(sizeof *display);

	display->display = wl_display_connect(NULL);
	assert(display->display);

	display->registry = wl_display_get_registry(display->display);
	wl_registry_add_listener(display->registry,
				 &registry_listener, display);
	wl_display_roundtrip(display->display);
	if (display->shm == NULL) {
		fprintf(stderr, "No wl_shm global\n");
		exit(1);
	}

	wl_display_roundtrip(display->display);

	return display;
}

static void
destroy_display(struct display *display)
{
	if (display->shm)
		wl_shm_destroy(display->shm);

	if (display->wm_base)
		xdg_wm_base_destroy(display->wm_base);

	if (display->compositor)
		wl_compositor_destroy(display->compositor);

	if (display->presentation)
		wp_presentation_destroy(display->presentation);

	if (display->fifo_manager)
		wp_fifo_manager_v1_destroy(display->fifo_manager);

	if (display->commit_timing_manager)
		wp_commit_timing_manager_v1_destroy(display->commit_timing_manager);

	if (display->keyboard)
		wl_keyboard_destroy(display->keyboard);

	if (display->seat)
		wl_seat_destroy(display->seat);

	wl_registry_destroy(display->registry);
	wl_display_flush(display->display);
	wl_display_disconnect(display->display);
	free(display);
}

static void
signal_int(int signum)
{
	running = 0;
}

static void
usage(const char *program)
{
	fprintf(stdout,
		"Usage: %s [OPTIONS]\n"
		"\n"
		"Schedule frames in the future with commit-timing\n"
		"\n"
		"Options:\n"
		"  -h, --help             Show this help\n"
		"\n",
		program);
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display *display;
	struct window *window;
	int ret = 0, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") ||
		    !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "Invalid argument: '%s'\n", argv[i - 1]);
			return 1;
		}
	}

	display = create_display();
	window = create_window(display, 256, 256);
	if (!window)
		return 1;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	while (running && ret != -1)
		ret = wl_display_dispatch(display->display);

	fprintf(stderr, "simple-timing exiting\n");

	destroy_window(window);
	destroy_display(display);

	return 0;
}

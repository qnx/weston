/*
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
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
#include <libweston/zalloc.h>
#include "xdg-shell-client-protocol.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#define FMT(fmt, bpp, r, g, b, a) { WL_SHM_FORMAT_ ## fmt, #fmt, bpp, { r, g, b, a } }

#define MAX_BUFFER_ALLOC	2

struct format {
	uint32_t code;
	const char *string;
	int bpp;
	uint64_t color[4];
};

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_shm *shm;
	const struct format *format;
	bool paint_format;
	bool has_format;
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
	struct buffer *prev_buffer;
	struct wl_callback *callback;
	bool wait_for_configure;
	bool maximized;
	bool fullscreen;
	bool needs_update_buffer;
};

static const struct format shm_formats[] = {
	/* 8 bpp formats */
	FMT(R8,             8, 0x00, 0x55, 0xaa, 0xff ),

	/* 16 bpp formats */
	FMT(R16,           16, 0x0000, 0x5555, 0xaaaa, 0xffff ),
	FMT(GR88,          16, 0x00ff, 0xff00, 0x0000, 0xffff ),
	FMT(RG88,          16, 0xff00, 0x00ff, 0x0000, 0xffff ),
	FMT(RGB565,        16, 0xf800, 0x07e0, 0x001f, 0xffff ),
	FMT(BGR565,        16, 0x001f, 0x07e0, 0xf800, 0xffff ),
	FMT(XRGB4444,      16, 0xff00, 0xf0f0, 0xf00f, 0x7777 ),
	FMT(ARGB4444,      16, 0xff00, 0xf0f0, 0xf00f, 0x7777 ),
	FMT(XBGR4444,      16, 0xf00f, 0xf0f0, 0xff00, 0x7777 ),
	FMT(ABGR4444,      16, 0xf00f, 0xf0f0, 0xff00, 0x7777 ),
	FMT(RGBX4444,      16, 0xf00f, 0x0f0f, 0x00ff, 0x7777 ),
	FMT(RGBA4444,      16, 0xf00f, 0x0f0f, 0x00ff, 0x7777 ),
	FMT(BGRX4444,      16, 0x00ff, 0x0f0f, 0xf00f, 0x7777 ),
	FMT(BGRA4444,      16, 0x00ff, 0x0f0f, 0xf00f, 0x7777 ),
	FMT(XRGB1555,      16, 0xfc00, 0x83e1, 0x801f, 0x0000 ),
	FMT(ARGB1555,      16, 0xfc00, 0x83e1, 0x801f, 0x0000 ),
	FMT(XBGR1555,      16, 0x801f, 0x83e1, 0xfc00, 0x0000 ),
	FMT(ABGR1555,      16, 0x801f, 0x83e1, 0xfc00, 0x0000 ),
	FMT(RGBX5551,      16, 0xf801, 0x07c1, 0x003f, 0x0000 ),
	FMT(RGBA5551,      16, 0xf801, 0x07c1, 0x003f, 0x0000 ),
	FMT(BGRX5551,      16, 0x003f, 0x07c1, 0xf801, 0x0000 ),
	FMT(BGRA5551,      16, 0x003f, 0x07c1, 0xf801, 0x0000 ),

	/* 24 bpp formats */
	FMT(RGB888,        24, 0xff0000, 0x00ff00, 0x0000ff, 0xffffff ),
	FMT(BGR888,        24, 0x0000ff, 0x00ff00, 0xff0000, 0xffffff ),

	/* 32 bpp formats */
	FMT(GR1616,        32, 0x0000ffff, 0xffff0000, 0x00000000, 0xffffffff ),
	FMT(RG1616,        32, 0xffff0000, 0x0000ffff, 0x00000000, 0xffffffff ),
	FMT(XRGB8888,      32, 0xffff0000, 0xff00ff00, 0xff0000ff, 0x7f7f7f7f ),
	FMT(ARGB8888,      32, 0xffff0000, 0xff00ff00, 0xff0000ff, 0x7f7f7f7f ),
	FMT(XBGR8888,      32, 0xff0000ff, 0xff00ff00, 0xffff0000, 0x7f7f7f7f ),
	FMT(ABGR8888,      32, 0xff0000ff, 0xff00ff00, 0xffff0000, 0x7f7f7f7f ),
	FMT(RGBX8888,      32, 0xff0000ff, 0x00ff00ff, 0x0000ffff, 0x7f7f7f7f ),
	FMT(RGBA8888,      32, 0xff0000ff, 0x00ff00ff, 0x0000ffff, 0x7f7f7f7f ),
	FMT(BGRX8888,      32, 0x0000ffff, 0x00ff00ff, 0xff0000ff, 0x7f7f7f7f ),
	FMT(BGRA8888,      32, 0x0000ffff, 0x00ff00ff, 0xff0000ff, 0x7f7f7f7f ),
	FMT(XRGB2101010,   32, 0xfff00000, 0xc00ffc00, 0xc00003ff, 0x5ff7fdff ),
	FMT(ARGB2101010,   32, 0xfff00000, 0xc00ffc00, 0xc00003ff, 0x5ff7fdff ),
	FMT(XBGR2101010,   32, 0xc00003ff, 0xc00ffc00, 0xfff00000, 0x5ff7fdff ),
	FMT(ABGR2101010,   32, 0xc00003ff, 0xc00ffc00, 0xfff00000, 0x5ff7fdff ),
	FMT(RGBX1010102,   32, 0xffc00003, 0x003ff003, 0x00000fff, 0x7fdff7fd ),
	FMT(RGBA1010102,   32, 0xffc00003, 0x003ff003, 0x00000fff, 0x7fdff7fd ),
	FMT(BGRX1010102,   32, 0x00000fff, 0x003ff003, 0xffc00003, 0x7fdff7fd ),
	FMT(BGRA1010102,   32, 0x00000fff, 0x003ff003, 0xffc00003, 0x7fdff7fd ),

	/* 64 bpp formats */
	FMT(XRGB16161616,  64, 0xffffffff00000000, 0xffff0000ffff0000, 0xffff00000000ffff, 0x7fff7fff7fff7fff ),
	FMT(ARGB16161616,  64, 0xffffffff00000000, 0xffff0000ffff0000, 0xffff00000000ffff, 0x7fff7fff7fff7fff ),
	FMT(XBGR16161616,  64, 0xffff00000000ffff, 0xffff0000ffff0000, 0xffffffff00000000, 0x7fff7fff7fff7fff ),
	FMT(ABGR16161616,  64, 0xffff00000000ffff, 0xffff0000ffff0000, 0xffffffff00000000, 0x7fff7fff7fff7fff ),
	FMT(XRGB16161616F, 64, 0x3c003c0000000000, 0x3c0000003c000000, 0x3c00000000003c00, 0x3800380038003800 ),
	FMT(ARGB16161616F, 64, 0x3c003c0000000000, 0x3c0000003c000000, 0x3c00000000003c00, 0x3800380038003800 ),
	FMT(XBGR16161616F, 64, 0x3c00000000003c00, 0x3c0000003c000000, 0x3c003c0000000000, 0x3800380038003800 ),
	FMT(ABGR16161616F, 64, 0x3c00000000003c00, 0x3c0000003c000000, 0x3c003c0000000000, 0x3800380038003800 ),
};

static int running = 1;

static void
redraw(void *data, struct wl_callback *callback, uint32_t time);

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
create_shm_buffer(struct window *window, struct buffer *buffer,
		  const struct format *format)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;
	int width, height;
	struct display *display;

	width = window->width;
	height = window->height;
	stride = width * (format->bpp / 8);
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
						   stride, format->code);
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
		redraw(window, NULL, 0);
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
	window->needs_update_buffer = false;
	wl_list_init(&window->buffer_list);

	if (display->wm_base) {
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
	} else {
		assert(0);
	}

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
		ret = create_shm_buffer(window, buffer,
					window->display->format);

		if (ret < 0)
			return NULL;

		/* paint the padding */
		memset(buffer->shm_data, 0xff,
		       window->width * window->height *
		       (window->display->format->bpp / 8));
	}

	return buffer;
}

static void
paint_pixels(void *image, int padding, int width, int height, uint32_t time)
{
	const int halfh = padding + (height - padding * 2) / 2;
	const int halfw = padding + (width  - padding * 2) / 2;
	int ir, or;
	uint32_t *pixel = image;
	int y;

	/* squared radii thresholds */
	or = (halfw < halfh ? halfw : halfh) - 8;
	ir = or - 32;
	or *= or;
	ir *= ir;

	pixel += padding * width;
	for (y = padding; y < height - padding; y++) {
		int x;
		int y2 = (y - halfh) * (y - halfh);

		pixel += padding;
		for (x = padding; x < width - padding; x++) {
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

		pixel += padding;
	}
}

static void
paint_format(void *image, const struct format *format, int width, int height)
{
	uint64_t *img64 = (uint64_t*) image;
	uint32_t *img32 = (uint32_t*) image;
	uint16_t *img16 = (uint16_t*) image;
	uint8_t *img8 = (uint8_t*) image;
	uint64_t color;
	int i, j;

#define GET_COLOR(y) \
	(y < (1 * (height / 4))) ? format->color[0] : \
	(y < (2 * (height / 4))) ? format->color[1] : \
	(y < (3 * (height / 4))) ? format->color[2] : \
	                           format->color[3]

	switch (format->code) {
	case WL_SHM_FORMAT_R8:
		for (i = 0; i < height; i++) {
			color = GET_COLOR(i);
			for (j = 0; j < width; j++)
				img8[i * width + j] = color;
		}
		break;

	case WL_SHM_FORMAT_R16:
	case WL_SHM_FORMAT_GR88:
	case WL_SHM_FORMAT_RG88:
	case WL_SHM_FORMAT_RGB565:
	case WL_SHM_FORMAT_BGR565:
	case WL_SHM_FORMAT_XRGB4444:
	case WL_SHM_FORMAT_ARGB4444:
	case WL_SHM_FORMAT_XBGR4444:
	case WL_SHM_FORMAT_ABGR4444:
	case WL_SHM_FORMAT_RGBX4444:
	case WL_SHM_FORMAT_RGBA4444:
	case WL_SHM_FORMAT_BGRX4444:
	case WL_SHM_FORMAT_BGRA4444:
	case WL_SHM_FORMAT_XRGB1555:
	case WL_SHM_FORMAT_ARGB1555:
	case WL_SHM_FORMAT_XBGR1555:
	case WL_SHM_FORMAT_ABGR1555:
	case WL_SHM_FORMAT_RGBX5551:
	case WL_SHM_FORMAT_RGBA5551:
	case WL_SHM_FORMAT_BGRX5551:
	case WL_SHM_FORMAT_BGRA5551:
		for (i = 0; i < height; i++) {
			color = GET_COLOR(i);
			for (j = 0; j < width; j++)
				img16[i * width + j] = color;
		}
		break;

	case WL_SHM_FORMAT_RGB888:
	case WL_SHM_FORMAT_BGR888:
		for (i = 0; i < height; i++) {
			color = GET_COLOR(i);
			for (j = 0; j < width; j++) {
				img8[(i * width + j) * 3 + 0] =
					(color >> 16) & 0xff;
				img8[(i * width + j) * 3 + 1] =
					(color >>  8) & 0xff;
				img8[(i * width + j) * 3 + 2] =
					(color >>  0) & 0xff;
			}
		}
		break;

	case WL_SHM_FORMAT_GR1616:
	case WL_SHM_FORMAT_RG1616:
	case WL_SHM_FORMAT_XRGB8888:
	case WL_SHM_FORMAT_ARGB8888:
	case WL_SHM_FORMAT_XBGR8888:
	case WL_SHM_FORMAT_ABGR8888:
	case WL_SHM_FORMAT_RGBX8888:
	case WL_SHM_FORMAT_RGBA8888:
	case WL_SHM_FORMAT_BGRX8888:
	case WL_SHM_FORMAT_BGRA8888:
	case WL_SHM_FORMAT_XRGB2101010:
	case WL_SHM_FORMAT_ARGB2101010:
	case WL_SHM_FORMAT_XBGR2101010:
	case WL_SHM_FORMAT_ABGR2101010:
	case WL_SHM_FORMAT_RGBX1010102:
	case WL_SHM_FORMAT_RGBA1010102:
	case WL_SHM_FORMAT_BGRX1010102:
	case WL_SHM_FORMAT_BGRA1010102:
		for (i = 0; i < height; i++) {
			color = GET_COLOR(i);
			for (j = 0; j < width; j++)
				img32[i * width + j] = color;
		}
		break;

	case WL_SHM_FORMAT_XRGB16161616:
	case WL_SHM_FORMAT_ARGB16161616:
	case WL_SHM_FORMAT_XBGR16161616:
	case WL_SHM_FORMAT_ABGR16161616:
	case WL_SHM_FORMAT_XRGB16161616F:
	case WL_SHM_FORMAT_ARGB16161616F:
	case WL_SHM_FORMAT_XBGR16161616F:
	case WL_SHM_FORMAT_ABGR16161616F:
		for (i = 0; i < height; i++) {
			color = GET_COLOR(i);
			for (j = 0; j < width; j++)
				img64[i * width + j] = color;
		}
		break;

	default:
		assert(0);
		break;
	};

#undef GET_COLOR
}

static const struct wl_callback_listener frame_listener;

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct buffer *buffer;

	prune_old_released_buffers(window);

	buffer = window_next_buffer(window);
	if (!buffer) {
		fprintf(stderr,
			!callback ? "Failed to create the first buffer.\n" :
			"Both buffers busy at redraw(). Server bug?\n");
		abort();
	}

	if (window->display->paint_format)
		paint_format(buffer->shm_data, window->display->format,
			     window->width, window->height);
	else
		paint_pixels(buffer->shm_data, 20, window->width,
			     window->height, time);

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface,
			  20, 20, window->width - 40, window->height - 40);

	if (callback)
		wl_callback_destroy(callback);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);
	wl_surface_commit(window->surface);
	buffer->busy = 1;
}

static const struct wl_callback_listener frame_listener = {
	redraw
};

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	struct display *d = data;

	if (format == d->format->code)
		d->has_format = true;
}

struct wl_shm_listener shm_listener = {
	shm_format
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
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
		wl_shm_add_listener(d->shm, &shm_listener, d);
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
create_display(const struct format *format, bool paint_format)
{
	struct display *display;

	display = zalloc(sizeof *display);
	if (display == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	display->display = wl_display_connect(NULL);
	assert(display->display);

	display->format = format;
	display->paint_format = paint_format;
	display->has_format= false;
	display->registry = wl_display_get_registry(display->display);
	wl_registry_add_listener(display->registry,
				 &registry_listener, display);
	wl_display_roundtrip(display->display);
	if (display->shm == NULL) {
		fprintf(stderr, "No wl_shm global\n");
		exit(1);
	}

	wl_display_roundtrip(display->display);

	/*
	 * Why do we need two roundtrips here?
	 *
	 * wl_display_get_registry() sends a request to the server, to which
	 * the server replies by emitting the wl_registry.global events.
	 * The first wl_display_roundtrip() sends wl_display.sync. The server
	 * first processes the wl_display.get_registry which includes sending
	 * the global events, and then processes the sync. Therefore when the
	 * sync (roundtrip) returns, we are guaranteed to have received and
	 * processed all the global events.
	 *
	 * While we are inside the first wl_display_roundtrip(), incoming
	 * events are dispatched, which causes registry_handle_global() to
	 * be called for each global. One of these globals is wl_shm.
	 * registry_handle_global() sends wl_registry.bind request for the
	 * wl_shm global. However, wl_registry.bind request is sent after
	 * the first wl_display.sync, so the reply to the sync comes before
	 * the initial events of the wl_shm object.
	 *
	 * The initial events that get sent as a reply to binding to wl_shm
	 * include wl_shm.format. These tell us which pixel formats are
	 * supported, and we need them before we can create buffers. They
	 * don't change at runtime, so we receive them as part of init.
	 *
	 * When the reply to the first sync comes, the server may or may not
	 * have sent the initial wl_shm events. Therefore we need the second
	 * wl_display_roundtrip() call here.
	 *
	 * The server processes the wl_registry.bind for wl_shm first, and
	 * the second wl_display.sync next. During our second call to
	 * wl_display_roundtrip() the initial wl_shm events are received and
	 * processed. Finally, when the reply to the second wl_display.sync
	 * arrives, it guarantees we have processed all wl_shm initial events.
	 *
	 * This sequence contains two examples on how wl_display_roundtrip()
	 * can be used to guarantee, that all reply events to a request
	 * have been received and processed. This is a general Wayland
	 * technique.
	 */

	if (!display->has_format) {
		fprintf(stderr, "Format '%s' not supported by compositor.\n",
			format->string);
		exit(1);
	}

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
		"Draw pixels into shared memory buffers using wl_shm\n"
		"\n"
		"Options:\n"
		"  -h, --help             Show this help\n"
		"  -F, --format <format>  Test format (see list below)\n"
		"\n"
		"RGB formats:\n"
		"  -  8 bpp: r8.\n"
		"\n"
		"  - 16 bpp: r16, gr88, rg88, rgb565, bgr565, xrgb4444, argb4444, xbgr4444,\n"
		"            abgr4444, rgbx4444, rgba4444, bgrx4444, bgra4444, xrgb1555,\n"
		"            argb1555, xbgr1555, abgr1555, rgbx5551, rgba5551, bgrx5551,\n"
		"            bgra5551.\n"
		"\n"
		"  - 24 bpp: rgb888, bgr888.\n"
		"\n"
		"  - 32 bpp: gr1616, rg1616, xrgb8888, argb8888, xbgr8888, abgr8888, rgbx8888,\n"
		"            rgba8888, bgrx8888, bgra8888, xrgb2101010, argb2101010, xbgr2101010,\n"
		"            abgr2101010, rgbx1010102, rgba1010102, bgrx1010102, bgra1010102.\n"
		"\n"
		"  - 64 bpp: xrgb16161616, argb16161616, xbgr16161616, abgr16161616,\n"
		"            xrgb16161616f, argb16161616f, xbgr16161616f, abgr16161616f.\n",
		program);
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display *display;
	struct window *window;
	const struct format *format = NULL;
	bool paint_format = false;
	const char *value;
	int ret = 0, i, j;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") ||
		    !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (!strcmp(argv[i], "-F") ||
			   !strcmp(argv[i], "--format")) {
			value = ++i == argc ? "" : argv[i];
			for (j = 0; j < (int) ARRAY_SIZE(shm_formats); j++) {
				if (!strcasecmp(shm_formats[j].string, value)) {
					format = &shm_formats[j];
					paint_format = true;
					break;
				}
			}
			if (!format) {
				fprintf(stderr, "Format '%s' not supported by "
					"client.\n", value);
				return 1;
			}
		} else {
			fprintf(stderr, "Invalid argument: '%s'\n", argv[i - 1]);
			return 1;
		}
	}

	if (!format)
		for (i = 0; i < (int) ARRAY_SIZE(shm_formats); i++)
			if (shm_formats[i].code == WL_SHM_FORMAT_XRGB8888)
				format = &shm_formats[i];
	assert(format);

	display = create_display(format, paint_format);
	window = create_window(display, 256, 256);
	if (!window)
		return 1;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	/* Initialise damage to full surface, so the padding gets painted */
	wl_surface_damage(window->surface, 0, 0,
			  window->width, window->height);

	if (!window->wait_for_configure)
		redraw(window, NULL, 0);

	while (running && ret != -1)
		ret = wl_display_dispatch(display->display);

	fprintf(stderr, "simple-shm exiting\n");

	destroy_window(window);
	destroy_display(display);

	return 0;
}

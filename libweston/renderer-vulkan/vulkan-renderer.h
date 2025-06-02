/*
 * Copyright © 2025 Erico Nunes
 *
 * based on gl-renderer:
 * Copyright © 2012 John Kåre Alsaker
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

#pragma once

#include "config.h"

#include <stdint.h>

#include <libweston/libweston.h>
#include "backend.h"
#include "libweston-internal.h"

#ifdef HAVE_XCB_XKB
#include <xcb/xcb.h>
#else
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;
#endif  /* HAVE_XCB_XKB */

/**
 * Options passed to the \c display_create method of the vulkan renderer interface.
 *
 * \see struct vulkan_renderer_interface
 */
struct vulkan_renderer_display_options {
	struct weston_renderer_options base;
	void *gbm_device;
	const struct pixel_format_info **formats;
	unsigned formats_count;
};

#define NUM_GBM_BOS 2

struct vulkan_renderer_output_options {
	struct gbm_bo *gbm_bos[NUM_GBM_BOS];
	unsigned int num_gbm_bos;
	struct weston_size fb_size;
	struct weston_geometry area;
	const struct pixel_format_info **formats;
	unsigned formats_count;

	// xcb backend options
	void *xcb_connection;
	xcb_visualid_t xcb_visualid;
	xcb_window_t xcb_window;

	// wayland backend options
	void *wayland_display;
	void *wayland_surface;
};

struct vulkan_renderer_fbo_options {
	/** Size of the framebuffer in pixels, including borders */
	struct weston_size fb_size;
	/** Area inside the framebuffer in pixels for composited content */
	struct weston_geometry area;
};

struct vulkan_renderer_interface {
	int (*display_create)(struct weston_compositor *ec,
			      const struct vulkan_renderer_display_options *options);

	int (*output_window_create)(struct weston_output *output,
				    const struct vulkan_renderer_output_options *options);

	int (*output_fbo_create)(struct weston_output *output,
				 const struct vulkan_renderer_fbo_options *options);

	void (*output_destroy)(struct weston_output *output);

	int (*create_fence_fd)(struct weston_output *output);
};

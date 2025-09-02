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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct pixel_format_info;
struct wl_buffer;
struct wl_shm;
struct wl_display;
struct zwp_linux_dmabuf_v1;

#define MAX_DMABUF_PLANES 4

struct client_buffer {
	const struct pixel_format_info *fmt;
	struct wl_buffer *wl_buffer;
	void *data;
	size_t bytes;
	int dmabuf_fd;
	int width;
	int height;
	size_t bytes_per_line[MAX_DMABUF_PLANES];
	size_t strides[MAX_DMABUF_PLANES];
	size_t offsets[MAX_DMABUF_PLANES];
};

bool
client_buffer_util_is_dmabuf_supported(void);

void
client_buffer_util_destroy_buffer(struct client_buffer *buf);

struct client_buffer *
client_buffer_util_create_shm_buffer(struct wl_shm *shm,
				     const struct pixel_format_info *fmt,
				     int width,
				     int height);

struct client_buffer *
client_buffer_util_create_dmabuf_buffer(struct wl_display *display,
					struct zwp_linux_dmabuf_v1 *dmabuf,
					const struct pixel_format_info *fmt,
					int width,
					int height);

void
client_buffer_util_maybe_sync_dmabuf_start(struct client_buffer *buf);

void
client_buffer_util_maybe_sync_dmabuf_end(struct client_buffer *buf);

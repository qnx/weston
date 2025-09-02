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

#include "client-buffer-util.h"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <wayland-client.h>

#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "pixel-formats.h"
#include "shared/os-compatibility.h"
#include "shared/weston-drm-fourcc.h"
#include "shared/xalloc.h"

#define UDMABUF_PATH "/dev/udmabuf"

/* Align buffers to 256 bytes - required by e.g. AMD GPUs */
#define STRIDE_ALIGN_MASK 255

static size_t
get_aligned_stride(size_t width_bytes, bool align_for_gpu)
{
	if (align_for_gpu)
		return (width_bytes + STRIDE_ALIGN_MASK) & ~STRIDE_ALIGN_MASK;
	else
		return (width_bytes + 3) & ~3u;
}

static bool
client_buffer_util_fill_buffer_args(struct client_buffer *buf,
				    bool align_for_gpu)
{
	buf->dmabuf_fd = -1;

	switch (buf->fmt->format) {
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		buf->bytes_per_line[0] = buf->width * 2;
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->offsets[0] = 0;
		buf->bytes = buf->strides[0] * buf->height;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		buf->bytes_per_line[0] = buf->width * 3;
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->offsets[0] = 0;
		buf->bytes = buf->strides[0] * buf->height;
		break;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XYUV8888:
		buf->bytes_per_line[0] = buf->width * 4;
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->offsets[0] = 0;
		buf->bytes = buf->strides[0] * buf->height;
		break;
	case DRM_FORMAT_XRGB16161616:
	case DRM_FORMAT_ARGB16161616:
	case DRM_FORMAT_XBGR16161616:
	case DRM_FORMAT_ABGR16161616:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		buf->bytes_per_line[0] = buf->width * 8;
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->offsets[0] = 0;
		buf->bytes = buf->strides[0] * buf->height;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		buf->bytes_per_line[0] = buf->width;
		buf->bytes_per_line[1] =
			buf->width / pixel_format_hsub(buf->fmt, 1) * sizeof(uint16_t);
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->strides[1] = get_aligned_stride(buf->bytes_per_line[1],
						     align_for_gpu);
		buf->offsets[0] = 0;
		buf->offsets[1] = buf->strides[0] * buf->height;
		buf->bytes =
			buf->strides[0] * buf->height +
			buf->strides[1] * (buf->height / pixel_format_vsub(buf->fmt, 1));
		break;
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		buf->bytes_per_line[0] = buf->width * sizeof(uint16_t);
		buf->bytes_per_line[1] =
			buf->width / pixel_format_hsub(buf->fmt, 1) * sizeof(uint32_t);
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->strides[1] = get_aligned_stride(buf->bytes_per_line[1],
						     align_for_gpu);
		buf->offsets[0] = 0;
		buf->offsets[1] = buf->strides[0] * buf->height;
		buf->bytes =
			buf->strides[0] * buf->height +
			buf->strides[1] * (buf->height / pixel_format_vsub(buf->fmt, 1));
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		buf->bytes_per_line[0] = buf->width;
		buf->bytes_per_line[1] = buf->bytes_per_line[2] = buf->width / pixel_format_hsub(buf->fmt, 1);
		buf->strides[0] = get_aligned_stride(buf->bytes_per_line[0],
						     align_for_gpu);
		buf->strides[1] = buf->strides[2] = get_aligned_stride(buf->bytes_per_line[1],
								       align_for_gpu);
		buf->offsets[0] = 0;
		buf->offsets[1] = buf->strides[0] * buf->height;
		buf->offsets[2] = buf->offsets[1] + buf->strides[1] * (buf->height / pixel_format_vsub(buf->fmt, 1));
		buf->bytes =
			buf->strides[0] * buf->height +
			buf->strides[1] * (buf->height / pixel_format_vsub(buf->fmt, 1)) * 2;
		break;
	default:
		fprintf(stderr, "Format not handled\n");
		return false;
	}

	if (align_for_gpu) {
		int pagesizemask = getpagesize() - 1;

		buf->bytes = (buf->bytes + pagesizemask) & ~pagesizemask;
	}

	return true;
}

bool
client_buffer_util_is_dmabuf_supported(void)
{
	int udmabuf_fd;

	udmabuf_fd = open (UDMABUF_PATH, O_RDWR | O_CLOEXEC, 0);
	if (udmabuf_fd == -1)
		return false;

	close(udmabuf_fd);
	return true;
}

void
client_buffer_util_destroy_buffer(struct client_buffer *buf)
{
	if (buf->wl_buffer)
		wl_buffer_destroy(buf->wl_buffer);
	if (buf->data)
		munmap(buf->data, buf->bytes);
	if (buf->dmabuf_fd > -1)
		close(buf->dmabuf_fd);
	free(buf);
}

struct client_buffer *
client_buffer_util_create_shm_buffer(struct wl_shm *shm,
				     const struct pixel_format_info *fmt,
				     int width,
				     int height)
{
	struct client_buffer *buf;
	struct wl_shm_pool *pool;
	int fd = -1;

	buf = xzalloc(sizeof *buf);
	buf->fmt = fmt;
	buf->width = width;
	buf->height = height;

	if (!client_buffer_util_fill_buffer_args(buf, false))
		goto error;

	fd = os_create_anonymous_file(buf->bytes);
	if (fd == -1) {
		fprintf(stderr, "os_create_anonymous_file() failed: %s\n",
			strerror (errno));
		goto error;
	}

	buf->data = mmap(NULL, buf->bytes,
			 PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf->data == MAP_FAILED) {
		fprintf(stderr, "mmap() failed: %s\n", strerror (errno));
		goto error;
	}

	pool = wl_shm_create_pool(shm, fd, buf->bytes);
	buf->wl_buffer = wl_shm_pool_create_buffer(pool, 0, buf->width,
						   buf->height, buf->strides[0],
						   pixel_format_get_shm_format(fmt));
	wl_shm_pool_destroy(pool);
	close(fd);

	if (!buf->wl_buffer) {
		fprintf(stderr, "wl_shm_pool_create_buffer() failed\n");
		goto error;
	}

	return buf;

error:
	if (fd != -1)
		close(fd);
	client_buffer_util_destroy_buffer(buf);
	return NULL;
}

struct buffer_create_data {
	struct client_buffer *buf;
	bool failed;
};

static void
create_succeeded(void *data,
		 struct zwp_linux_buffer_params_v1 *params,
		 struct wl_buffer *new_buffer)
{
	struct buffer_create_data *create_data = data;

	create_data->buf->wl_buffer = new_buffer;
	wl_proxy_set_queue ((struct wl_proxy *) create_data->buf->wl_buffer, NULL);

	zwp_linux_buffer_params_v1_destroy(params);
}

static void
create_failed(void *data, struct zwp_linux_buffer_params_v1 *params)
{
	struct buffer_create_data *create_data = data;

	create_data->failed = true;
	zwp_linux_buffer_params_v1_destroy(params);
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
	create_succeeded,
	create_failed
};

struct client_buffer *
client_buffer_util_create_dmabuf_buffer(struct wl_display *display,
					struct zwp_linux_dmabuf_v1 *dmabuf,
					const struct pixel_format_info *fmt,
					int width,
					int height)
{
	struct client_buffer *buf;
	struct buffer_create_data create_data = { 0 };
	struct zwp_linux_buffer_params_v1 *params;
	struct wl_event_queue *event_queue;
	struct udmabuf_create create;
	int udmabuf_fd = -1;
	int mem_fd = -1;

	buf = xzalloc(sizeof *buf);
	buf->fmt = fmt;
	buf->width = width;
	buf->height = height;

	if (!client_buffer_util_fill_buffer_args(buf, true))
		goto error;

	udmabuf_fd = open (UDMABUF_PATH, O_RDWR | O_CLOEXEC, 0);
	if (udmabuf_fd == -1) {
		fprintf(stderr, "udmabuf not supported\n");
		goto error;
	}

	mem_fd = memfd_create ("udmabuf", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (mem_fd == -1) {
		fprintf(stderr, "memfd_create() failed: %s\n", strerror (errno));
		goto error;
	}

	if (ftruncate (mem_fd, buf->bytes) < 0) {
		fprintf(stderr, "ftruncate() failed: %s\n", strerror (errno));
		close (mem_fd);
		goto error;
	}

	if (fcntl (mem_fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
		fprintf(stderr, "adding seals failed: %s\n", strerror (errno));
		close (mem_fd);
		goto error;
	}

	create.memfd = mem_fd;
	create.flags = UDMABUF_FLAGS_CLOEXEC;
	create.offset = 0;
	create.size = buf->bytes;

	buf->dmabuf_fd = ioctl (udmabuf_fd, UDMABUF_CREATE, &create);
	if (buf->dmabuf_fd == -1) {
		fprintf(stderr, "creating udmabuf failed: %s\n", strerror (errno));
		goto error;
	}
	/* The underlying memfd is kept as as a reference in the kernel. */
	close (mem_fd);
	mem_fd = -1;
	close (udmabuf_fd);
	udmabuf_fd = -1;

	buf->data = mmap(NULL, buf->bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
			 buf->dmabuf_fd, 0);
	if (buf->data == MAP_FAILED) {
		fprintf(stderr, "mmap() failed: %s\n", strerror (errno));
		goto error;
	}

	params = zwp_linux_dmabuf_v1_create_params(dmabuf);
	event_queue = wl_display_create_queue (display);
	wl_proxy_set_queue ((struct wl_proxy *) params, event_queue);

	for (unsigned int i = 0; i < pixel_format_get_plane_count(buf->fmt); i++) {
		zwp_linux_buffer_params_v1_add(params,
					       buf->dmabuf_fd,
					       i /* plane id */,
					       buf->offsets[i],
					       buf->strides[i],
					       DRM_FORMAT_MOD_LINEAR >> 32,
		                               DRM_FORMAT_MOD_LINEAR & 0xffffffff);
	}

	create_data.buf = buf;
	zwp_linux_buffer_params_v1_add_listener(params,
						&params_listener,
						&create_data);

	zwp_linux_buffer_params_v1_create(params,
					  buf->width,
					  buf->height,
					  fmt->format,
					  0 /* flags */);

	while (!buf->wl_buffer && !create_data.failed) {
		if (wl_display_dispatch_queue(display, event_queue) == -1)
			break;
	}

	wl_event_queue_destroy(event_queue);

	if (!buf->wl_buffer) {
		fprintf(stderr, "zwp_linux_buffer_params_v1_create() failed\n");
		goto error;
	}

	return buf;

error:
	if (udmabuf_fd != -1)
		close(udmabuf_fd);
	if (mem_fd != -1)
		close(mem_fd);
	client_buffer_util_destroy_buffer(buf);
	return NULL;
}

void
client_buffer_util_maybe_sync_dmabuf_start(struct client_buffer *buf)
{
	struct dma_buf_sync sync = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
	int ret;

	if (buf->dmabuf_fd == -1)
		return;

	do {
		ret = ioctl(buf->dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
	} while (ret && (errno == EINTR || errno == EAGAIN));
}

void
client_buffer_util_maybe_sync_dmabuf_end(struct client_buffer *buf)
{
	struct dma_buf_sync sync = { DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE };
	int ret;

	if (buf->dmabuf_fd == -1)
		return;

	do {
		ret = ioctl(buf->dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
	} while (ret && (errno == EINTR || errno == EAGAIN));
}

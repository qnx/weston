/*
 * Copyright © 2020 Collabora, Ltd.
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

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "test-config.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "image-iter.h"
#include "pixel-formats.h"
#include "shared/client-buffer-util.h"
#include "shared/os-compatibility.h"
#include "shared/weston-drm-fourcc.h"
#include "shared/xalloc.h"

/* XXX For formats with more than 8 bit pre component, we should ideally load a
 * 16-bit (or 32-bit) per component image and store into a 16-bit (or 32-bit)
 * per component renderbuffer so that we can ensure the additional precision is
 * correctly handled. */

struct setup_args {
	struct fixture_metadata meta;
	enum weston_renderer_type renderer;
	const char *logging_scopes;

	/* Formats in these arrays can be defined per renderer and must be
	 * advertised and supported by the renderer.
	 * If undefined, all formats checked by the test are considered
	 * must pass. */
	const uint32_t *shm_format_must_pass;
	size_t shm_format_num;
	const uint32_t *dmabuf_format_must_pass;
	size_t dmabuf_format_num;

	bool gl_force_import_yuv_fallback;
};

/* Formats supported by llvmpipe as of Mesa 25.0.4 */
static const uint32_t gl_dmabuf_format_must_pass[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XBGR16161616,
	DRM_FORMAT_ABGR16161616,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_ABGR16161616F,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YVU444,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_XYUV8888,
	DRM_FORMAT_P010,
	DRM_FORMAT_P012,
	DRM_FORMAT_P016,
};

static const uint32_t vulkan_shm_format_must_pass[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888
};

static const uint32_t vulkan_dmabuf_format_must_pass[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888
};

static const struct setup_args my_setup_args[] = {
	{
		.meta.name = "GL",
		.renderer = WESTON_RENDERER_GL,
		.logging_scopes = "log,gl-shader-generator",
		.dmabuf_format_must_pass = gl_dmabuf_format_must_pass,
		.dmabuf_format_num = ARRAY_LENGTH(gl_dmabuf_format_must_pass),
		.gl_force_import_yuv_fallback = false,
	},
	{
		.meta.name = "GL force-import-yuv-fallback",
		.renderer = WESTON_RENDERER_GL,
		.logging_scopes = "log,gl-shader-generator",
		.dmabuf_format_must_pass = gl_dmabuf_format_must_pass,
		.dmabuf_format_num = ARRAY_LENGTH(gl_dmabuf_format_must_pass),
		.gl_force_import_yuv_fallback = true,
	},
	{
		.renderer = WESTON_RENDERER_VULKAN,
		.logging_scopes = "log",
		.meta.name = "Vulkan",
		.shm_format_must_pass = vulkan_shm_format_must_pass,
		.shm_format_num = ARRAY_LENGTH(vulkan_shm_format_must_pass),
		.dmabuf_format_must_pass = vulkan_dmabuf_format_must_pass,
		.dmabuf_format_num = ARRAY_LENGTH(vulkan_dmabuf_format_must_pass),
	},
};

static enum test_result_code
fixture_setup(struct weston_test_harness *harness, const struct setup_args *arg)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = arg->renderer;
	setup.width = 324;
	setup.height = 264;
	setup.shell = SHELL_TEST_DESKTOP;
	setup.logging_scopes = arg->logging_scopes;
	setup.refresh = HIGHEST_OUTPUT_REFRESH;
	setup.test_quirks.gl_force_import_yuv_fallback =
		arg->gl_force_import_yuv_fallback;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

enum buffer_type {
	BUFFER_TYPE_SHM = 1,
	BUFFER_TYPE_DMABUF,
};

struct client_buffer_case {
	uint32_t drm_format;
	const char *drm_format_name;
	int ref_seq_no;
	struct client_buffer *(*create_buffer)(struct client *client,
					       uint32_t drm_format,
					       enum buffer_type type,
					       pixman_image_t *rgb_image);
};

struct client_buffer_create_data {
	const struct pixel_format_info *fmt;
	enum buffer_type type;
	int width;
	int height;
};

static struct client_buffer_create_data
create_init(uint32_t drm_format, enum buffer_type type,
	    const struct image_header *ih)
{
	return (struct client_buffer_create_data){
		.fmt = pixel_format_get_info(drm_format),
		.type = type,
		.width = ih->width,
		.height = ih->height,
	};
}

static struct client_buffer *
client_buffer_create(struct client *client,
		     struct client_buffer_create_data *create_data)
{
	struct client_buffer *buf = NULL;

	switch (create_data->type) {
		case BUFFER_TYPE_SHM: {
			if (!support_shm_format(client,
						pixel_format_get_shm_format (create_data->fmt))) {
				testlog("%s: Skipped: format not supported by compositor for SHM\n",
					get_test_name());
				return NULL;
			}

			buf = client_buffer_util_create_shm_buffer(client->wl_shm,
								   create_data->fmt,
								   create_data->width,
								   create_data->height);
			break;
		}
		case BUFFER_TYPE_DMABUF: {
			if (!support_drm_format(client, create_data->fmt->format,
						DRM_FORMAT_MOD_LINEAR)) {
				testlog("%s: Skipped: format not supported by compositor for DMABUF\n",
					get_test_name());
				return NULL;
			}

			buf = client_buffer_util_create_dmabuf_buffer(client->wl_display,
								      client->dmabuf,
								      create_data->fmt,
								      create_data->width,
								      create_data->height);
			break;
		}
	}

	test_assert_ptr_not_null(buf);
	return buf;
}

/*
 * 16 bpp RGB
 *
 * RGBX4444: [15:0] R:G:B:x 4:4:4:4 little endian
 * RGBA4444: [15:0] R:G:B:A 4:4:4:4 little endian
 *
 * BGRX4444: [15:0] B:G:R:x 4:4:4:4 little endian
 * BGRA4444: [15:0] B:G:R:A 4:4:4:4 little endian
 *
 * XRGB4444: [15:0] x:R:G:B 4:4:4:4 little endian
 * ARGB4444: [15:0] A:R:G:B 4:4:4:4 little endian
 *
 * XBGR4444: [15:0] x:B:G:R 4:4:4:4 little endian
 * ABGR4444: [15:0] A:B:G:R 4:4:4:4 little endian
 */
static struct client_buffer *
rgba4444_create_buffer(struct client *client,
		       uint32_t drm_format,
		       enum buffer_type type,
		       pixman_image_t *rgb_image)
{
	static const int swizzles[][4] = {
		{ 3, 2, 1, 0 }, /* RGBX4444, RGBA4444 */
		{ 1, 2, 3, 0 }, /* BGRX4444, BGRA4444 */
		{ 2, 1, 0, 3 }, /* XRGB4444, ARGB4444 */
		{ 0, 1, 2, 3 }, /* XBGR4444, ABGR4444 */
	};

	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	bool is_opaque;
	int idx, x, y;
	uint16_t a;

	switch (drm_format) {
	case DRM_FORMAT_RGBX4444:
		is_opaque = true;
		idx = 0;
		break;
	case DRM_FORMAT_RGBA4444:
		is_opaque = false;
		idx = 0;
		break;
	case DRM_FORMAT_BGRX4444:
		is_opaque = true;
		idx = 1;
		break;
	case DRM_FORMAT_BGRA4444:
		is_opaque = false;
		idx = 1;
		break;
	case DRM_FORMAT_XRGB4444:
		is_opaque = true;
		idx = 2;
		break;
	case DRM_FORMAT_ARGB4444:
		is_opaque = false;
		idx = 2;
		break;
	case DRM_FORMAT_XBGR4444:
		is_opaque = true;
		idx = 3;
		break;
	case DRM_FORMAT_ABGR4444:
		is_opaque = false;
		idx = 3;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	/* Store alpha as 0x0 to ensure the compositor correctly replaces it
	 * with 0xf. */
	a = is_opaque ? 0x0 : 0xf;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint16_t *dst_row =
			(uint16_t*) buf->data + (buf->strides[0] / sizeof(uint16_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint16_t r = (src_row[x] >> 20) & 0xf;
			uint16_t g = (src_row[x] >> 12) & 0xf;
			uint16_t b = (src_row[x] >> 4) & 0xf;

			dst_row[x] =
				r << (swizzles[idx][0] * 4) |
				g << (swizzles[idx][1] * 4) |
				b << (swizzles[idx][2] * 4) |
				a << (swizzles[idx][3] * 4);
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 16 bpp RGB
 *
 * RGBX5551: [15:0] R:G:B:x 5:5:5:1 little endian
 * RGBA5551: [15:0] R:G:B:A 5:5:5:1 little endian
 *
 * BGRX5551: [15:0] B:G:R:x 5:5:5:1 little endian
 * BGRA5551: [15:0] B:G:R:A 5:5:5:1 little endian
 */
static struct client_buffer *
rgba5551_create_buffer(struct client *client,
		       uint32_t drm_format,
		       enum buffer_type type,
		       pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;

	int x, y;
	uint16_t a;

	test_assert_true(drm_format == DRM_FORMAT_RGBX5551 ||
			 drm_format == DRM_FORMAT_RGBA5551 ||
			 drm_format == DRM_FORMAT_BGRX5551 ||
			 drm_format == DRM_FORMAT_BGRA5551);

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	/* Store alpha as 0x0 to ensure the compositor correctly replaces it
	 * with 0x1. */
	a = drm_format == DRM_FORMAT_RGBX5551 ||
		drm_format == DRM_FORMAT_RGBX5551 ? 0x0 : 0x1;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint16_t *dst_row =
			(uint16_t*) buf->data + (buf->strides[0] / sizeof(uint16_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint16_t r = (src_row[x] >> 19) & 0x1f;
			uint16_t g = (src_row[x] >> 11) & 0x1f;
			uint16_t b = (src_row[x] >> 3) & 0x1f;

			if (drm_format == DRM_FORMAT_RGBX5551 ||
			    drm_format == DRM_FORMAT_RGBA5551)
				dst_row[x] = r << 11 | g << 6 | b << 1 | a;
			else
				dst_row[x] = b << 11 | g << 6 | r << 1 | a;
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 16 bpp RGB
 *
 * RGB565: [15:0] R:G:B 5:6:5 little endian
 * BGR565: [15:0] B:G:R 5:6:5 little endian
 */
static struct client_buffer *
rgb565_create_buffer(struct client *client,
		     uint32_t drm_format,
		     enum buffer_type type,
		     pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int x, y;

	test_assert_true(drm_format == DRM_FORMAT_RGB565 ||
			 drm_format == DRM_FORMAT_BGR565);

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint16_t *dst_row =
			(uint16_t*) buf->data + (buf->strides[0] / sizeof(uint16_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint16_t r = (src_row[x] >> 19) & 0x1f;
			uint16_t g = (src_row[x] >> 10) & 0x3f;
			uint16_t b = (src_row[x] >> 3) & 0x1f;

			if (drm_format == DRM_FORMAT_RGB565)
				dst_row[x] = r << 11 | g << 5 | b;
			else
				dst_row[x] = b << 11 | g << 5 | r;
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 24 bpp RGB
 *
 * RGB888: [23:0] R:G:B 8:8:8 little endian
 * BGR888: [23:0] B:G:R 8:8:8 little endian
 */
static struct client_buffer *
rgb888_create_buffer(struct client *client,
		     uint32_t drm_format,
		     enum buffer_type type,
		     pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int x, y;

	test_assert_true(drm_format == DRM_FORMAT_RGB888 ||
			 drm_format == DRM_FORMAT_BGR888);

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint8_t *dst_row = (uint8_t*) buf->data + src.width * 3 * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint8_t r = src_row[x] >> 16;
			uint8_t g = src_row[x] >> 8;
			uint8_t b = src_row[x];

			if (drm_format == DRM_FORMAT_RGB888) {
				dst_row[x * 3 + 2] = r;
				dst_row[x * 3 + 1] = g;
				dst_row[x * 3 + 0] = b;
			} else {
				dst_row[x * 3 + 2] = b;
				dst_row[x * 3 + 1] = g;
				dst_row[x * 3 + 0] = r;
			}
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 32 bpp RGB
 *
 * RGBX8888: [31:0] R:G:B:x 8:8:8:8 little endian
 * RGBA8888: [31:0] R:G:B:A 8:8:8:8 little endian
 *
 * BGRX8888: [31:0] B:G:R:x 8:8:8:8 little endian
 * BGRA8888: [31:0] B:G:R:A 8:8:8:8 little endian
 *
 * XRGB8888: [31:0] x:R:G:B 8:8:8:8 little endian
 * ARGB8888: [31:0] A:R:G:B 8:8:8:8 little endian
 *
 * XBGR8888: [31:0] x:B:G:R 8:8:8:8 little endian
 * ABGR8888: [31:0] A:B:G:R 8:8:8:8 little endian
 */
static struct client_buffer *
rgba8888_create_buffer(struct client *client,
		       uint32_t drm_format,
		       enum buffer_type type,
		       pixman_image_t *rgb_image)
{
	static const int swizzles[][4] = {
		{ 3, 2, 1, 0 }, /* RGBX8888, RGBA8888 */
		{ 1, 2, 3, 0 }, /* BGRX8888, BGRA8888 */
		{ 2, 1, 0, 3 }, /* XRGB8888, ARGB8888 */
		{ 0, 1, 2, 3 }, /* XBGR8888, ABGR8888 */
	};

	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	bool is_opaque;
	int idx, x, y;
	uint32_t a;

	switch (drm_format) {
	case DRM_FORMAT_RGBX8888:
		is_opaque = true;
		idx = 0;
		break;
	case DRM_FORMAT_RGBA8888:
		is_opaque = false;
		idx = 0;
		break;
	case DRM_FORMAT_BGRX8888:
		is_opaque = true;
		idx = 1;
		break;
	case DRM_FORMAT_BGRA8888:
		is_opaque = false;
		idx = 1;
		break;
	case DRM_FORMAT_XRGB8888:
		is_opaque = true;
		idx = 2;
		break;
	case DRM_FORMAT_ARGB8888:
		is_opaque = false;
		idx = 2;
		break;
	case DRM_FORMAT_XBGR8888:
		is_opaque = true;
		idx = 3;
		break;
	case DRM_FORMAT_ABGR8888:
		is_opaque = false;
		idx = 3;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	/* Store alpha as 0x00 to ensure the compositor correctly replaces it
	 * with 0xff. */
	a = is_opaque ? 0x00 : 0xff;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint32_t *dst_row =
			(uint32_t*) buf->data + (buf->strides[0] / sizeof(uint32_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint32_t r = (src_row[x] >> 16) & 0xff;
			uint32_t g = (src_row[x] >> 8) & 0xff;
			uint32_t b = (src_row[x] >> 0) & 0xff;

			dst_row[x] =
				r << (swizzles[idx][0] * 8) |
				g << (swizzles[idx][1] * 8) |
				b << (swizzles[idx][2] * 8) |
				a << (swizzles[idx][3] * 8);
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 32 bpp RGB
 *
 * XRGB2101010: [31:0] x:R:G:B 2:10:10:10 little endian
 * ARGB2101010: [31:0] A:R:G:B 2:10:10:10 little endian
 *
 * XBGR2101010: [31:0] x:B:G:R 2:10:10:10 little endian
 * ABGR2101010: [31:0] A:B:G:R 2:10:10:10 little endian
 */
static struct client_buffer *
rgba2101010_create_buffer(struct client *client,
			  uint32_t drm_format,
			  enum buffer_type type,
			  pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int x, y;
	uint32_t a;

	test_assert_true(drm_format == DRM_FORMAT_XRGB2101010 ||
			 drm_format == DRM_FORMAT_ARGB2101010 ||
			 drm_format == DRM_FORMAT_XBGR2101010 ||
			 drm_format == DRM_FORMAT_ABGR2101010);

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	/* Store alpha as 0x0 to ensure the compositor correctly replaces it
	 * with 0x3. */
	a = drm_format == DRM_FORMAT_XRGB2101010 ||
		drm_format == DRM_FORMAT_XRGB2101010 ? 0x0 : 0x3;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint32_t *dst_row =
			(uint32_t*) buf->data + (buf->strides[0] / sizeof(uint32_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint32_t r = ((src_row[x] >> 16) & 0xff) << 2;
			uint32_t g = ((src_row[x] >> 8) & 0xff) << 2;
			uint32_t b = ((src_row[x] >> 0) & 0xff) << 2;

			if (drm_format == DRM_FORMAT_XRGB2101010 ||
			    drm_format == DRM_FORMAT_ARGB2101010)
				dst_row[x] = a << 30 | r << 20 | g << 10 | b;
			else
				dst_row[x] = a << 30 | b << 20 | g << 10 | r;
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 64 bpp RGB
 *
 * XRGB16161616: [63:0] x:R:G:B 16:16:16:16 little endian
 * ARGB16161616: [63:0] A:R:G:B 16:16:16:16 little endian
 *
 * XBGR16161616: [63:0] x:B:G:R 16:16:16:16 little endian
 * ABGR16161616: [63:0] A:B:G:R 16:16:16:16 little endian
 */
static struct client_buffer *
rgba16161616_create_buffer(struct client *client,
			   uint32_t drm_format,
			   enum buffer_type type,
			   pixman_image_t *rgb_image)
{
	static const int swizzles[][4] = {
		{ 2, 1, 0, 3 }, /* XRGB16161616, ARGB16161616 */
		{ 0, 1, 2, 3 }, /* XBGR16161616, ABGR16161616 */
	};

	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	bool is_opaque;
	int idx, x, y;
	uint64_t a;

	switch (drm_format) {
	case DRM_FORMAT_XRGB16161616:
		is_opaque = true;
		idx = 0;
		break;
	case DRM_FORMAT_ARGB16161616:
		is_opaque = false;
		idx = 0;
		break;
	case DRM_FORMAT_XBGR16161616:
		is_opaque = true;
		idx = 1;
		break;
	case DRM_FORMAT_ABGR16161616:
		is_opaque = false;
		idx = 1;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	/* Store alpha as 0x0000 to ensure the compositor correctly replaces it
	 * with 0xffff. */
	a = is_opaque ? 0x0000 : 0xffff;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint64_t *dst_row =
			(uint64_t*) buf->data + (buf->strides[0] / sizeof(uint64_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint64_t r = ((src_row[x] >> 16) & 0xff) << 8;
			uint64_t g = ((src_row[x] >> 8) & 0xff) << 8;
			uint64_t b = ((src_row[x] >> 0) & 0xff) << 8;

			dst_row[x] =
				r << (swizzles[idx][0] * 16) |
				g << (swizzles[idx][1] * 16) |
				b << (swizzles[idx][2] * 16) |
				a << (swizzles[idx][3] * 16);
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/* Convert an IEEE 754-2008 binary32 value to binary16 bits. Doesn't bother
 * supporting Inf, Nan or subnormal numbers. Simply return signed 0 if there's
 * an underflow due to the loss of precision. */
static uint16_t
binary16_from_binary32(float binary32)
{
	uint32_t bits;
	uint16_t sign, significand, exponent;

	memcpy(&bits, &binary32, 4);

	sign = bits >> 31;
	exponent = (bits >> 23) & 0xff;
	significand = (bits >> 13) & 0x3ff;

	if (exponent >= 103)
		return sign << 15 | (exponent - 112) << 10 | significand;
	else
		return sign << 15;
}

/*
 * Floating point 64bpp RGB
 * IEEE 754-2008 binary16 half-precision float
 * [15:0] sign:exponent:mantissa 1:5:10
 *
 * XRGB16161616F: [63:0] x:R:G:B 16:16:16:16 little endian
 * ARGB16161616F: [63:0] A:R:G:B 16:16:16:16 little endian
 *
 * XBGR16161616F: [63:0] x:B:G:R 16:16:16:16 little endian
 * ABGR16161616F: [63:0] A:B:G:R 16:16:16:16 little endian
 */
static struct client_buffer *
rgba16161616f_create_buffer(struct client *client,
			    uint32_t drm_format,
			    enum buffer_type type,
			    pixman_image_t *rgb_image)
{
	static const int swizzles[][4] = {
		{ 2, 1, 0, 3 }, /* XRGB16161616F, ARGB16161616F */
		{ 0, 1, 2, 3 }, /* XBGR16161616F, ABGR16161616F */
	};

	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	bool is_opaque;
	int idx, x, y;
	uint64_t a;

	switch (drm_format) {
	case DRM_FORMAT_XRGB16161616F:
		is_opaque = true;
		idx = 0;
		break;
	case DRM_FORMAT_ARGB16161616F:
		is_opaque = false;
		idx = 0;
		break;
	case DRM_FORMAT_XBGR16161616F:
		is_opaque = true;
		idx = 1;
		break;
	case DRM_FORMAT_ABGR16161616F:
		is_opaque = false;
		idx = 1;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	/* Store alpha as 0.0 to ensure the compositor correctly replaces it
	 * with 1.0. */
	a = is_opaque ?
		binary16_from_binary32(0.0f) :
		binary16_from_binary32(1.0f);

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		uint64_t *dst_row =
			(uint64_t*) buf->data + (buf->strides[0] / sizeof(uint64_t)) * y;
		uint32_t *src_row = image_header_get_row_u32(&src, y);

		for (x = 0; x < src.width; x++) {
			uint64_t r = ((src_row[x] >> 16) & 0xff) << 8;
			uint64_t g = ((src_row[x] >> 8) & 0xff) << 8;
			uint64_t b = ((src_row[x] >> 0) & 0xff) << 8;
			r = binary16_from_binary32(r / 65535.0f);
			g = binary16_from_binary32(g / 65535.0f);
			b = binary16_from_binary32(b / 65535.0f);

			dst_row[x] =
				r << (swizzles[idx][0] * 16) |
				g << (swizzles[idx][1] * 16) |
				b << (swizzles[idx][2] * 16) |
				a << (swizzles[idx][3] * 16);
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * Based on Rec. ITU-R BT.709-6
 *
 * This is intended to be obvious and accurate, not fast.
 */
static void
x8r8g8b8_to_ycbcr8_bt709(uint32_t xrgb,
			 uint8_t *y_out, uint8_t *cb_out, uint8_t *cr_out)
{
	double y, cb, cr;
	double r = (xrgb >> 16) & 0xff;
	double g = (xrgb >> 8) & 0xff;
	double b = (xrgb >> 0) & 0xff;

	/* normalize to [0.0, 1.0] */
	r /= 255.0;
	g /= 255.0;
	b /= 255.0;

	/* Y normalized to [0.0, 1.0], Cb and Cr [-0.5, 0.5] */
	y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	cr = (r - y) / 1.5748;
	cb = (b - y) / 1.8556;

	/* limited range quantization to 8 bit */
	*y_out = round(219.0 * y + 16.0);
	if (cr_out)
		*cr_out = round(224.0 * cr + 128.0);
	if (cb_out)
		*cb_out = round(224.0 * cb + 128.0);
}

/*
 * Same as above but for conversion to 16-bit Y'CbCr formats. 'depth' can be set
 * to any value in the range [9, 16]. If 'depth' is less than 16, components are
 * aligned to the most significant bit with the least significant bits set to 0.
 */
static void
x8r8g8b8_to_ycbcr16_bt709(uint32_t xrgb, int depth,
			  uint16_t *y_out, uint16_t *cb_out, uint16_t *cr_out)
{
	uint16_t d;
	double y, cb, cr;
	double r = (xrgb >> 16) & 0xff;
	double g = (xrgb >> 8) & 0xff;
	double b = (xrgb >> 0) & 0xff;

	/* Rec. ITU-R BT.709-6 defines D as 1 or 4 for 8-bit or 10-bit
	 * quantization respectively. We extrapolate here to [9, 16]-bit depths
	 * by setting D to 2^(depth - 8). */
	test_assert_int_ge(depth, 9);
	test_assert_int_le(depth, 16);
	d = 1 << (depth - 8);

	/* normalize to [0.0, 1.0] */
	r /= 255.0;
	g /= 255.0;
	b /= 255.0;

	/* Y normalized to [0.0, 1.0], Cb and Cr [-0.5, 0.5] */
	y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	cr = (r - y) / 1.5748;
	cb = (b - y) / 1.8556;

	/* limited range quantization to [9, 16]-bit aligned to the MSB */
	*y_out = (uint16_t) round((219.0 * y + 16.0) * d) << (16 - depth);
	if (cr_out)
		*cr_out = (uint16_t)
			round((224.0 * cr + 128.0) * d) << (16 - depth);
	if (cb_out)
		*cb_out = (uint16_t)
			round((224.0 * cb + 128.0) * d) << (16 - depth);
}

/*
 * 3 plane YCbCr
 * plane 0: Y plane, [7:0] Y
 * plane 1: Cb plane, [7:0] Cb
 * plane 2: Cr plane, [7:0] Cr
 *
 * YUV420: 2x2 subsampled Cb (1) and Cr (2) planes
 *
 * YVU420: 2x2 subsampled Cr (1) and Cb (2) planes
 *
 * YUV444: no subsampling Cb (1) and Cr (2) planes

 * YVU444: no subsampling Cr (1) and Cb (2) planes
 */
static struct client_buffer *
y_u_v_create_buffer(struct client *client,
		    uint32_t drm_format,
		    enum buffer_type type,
		    pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int x, y;
	uint32_t *rgb_row;
	uint8_t *y_base;
	uint8_t *u_base;
	uint8_t *v_base;
	uint8_t *y_row;
	uint8_t *u_row;
	uint8_t *v_row;
	uint32_t argb;

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	y_base = buf->data + buf->offsets[0];
	switch (drm_format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV444:
		u_base = buf->data + buf->offsets[1];
		v_base = buf->data + buf->offsets[2];
		break;
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YVU444:
		v_base = buf->data + buf->offsets[1];
		u_base = buf->data + buf->offsets[2];
		break;
	}

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		y_row = y_base + y * buf->strides[0];

		switch (drm_format) {
		case DRM_FORMAT_YUV420:
		case DRM_FORMAT_YUV422:
		case DRM_FORMAT_YUV444:
			u_row = u_base + (y / pixel_format_vsub(buf->fmt, 1)) * buf->strides[1];
			v_row = v_base + (y / pixel_format_vsub(buf->fmt, 1)) * buf->strides[2];
			break;
		case DRM_FORMAT_YVU420:
		case DRM_FORMAT_YVU422:
		case DRM_FORMAT_YVU444:
			v_row = v_base + (y / pixel_format_vsub(buf->fmt, 1)) * buf->strides[1];
			u_row = u_base + (y / pixel_format_vsub(buf->fmt, 1)) * buf->strides[2];
			break;
		}

		for (x = 0; x < src.width; x++) {
			/*
			 * Sub-sample the source image instead, so that U and V
			 * sub-sampling does not require proper
			 * filtering/averaging/siting.
			 */
			argb = *(rgb_row + x / 2 * 2);

			/*
			 * A stupid way of "sub-sampling" chroma. This does not
			 * do the necessary filtering/averaging/siting or
			 * alternate Cb/Cr rows.
			 */
			if ((y & (pixel_format_vsub(buf->fmt, 1) - 1)) == 0 &&
			    (x & (pixel_format_hsub(buf->fmt, 1) - 1)) == 0) {
				x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
							 u_row + x / pixel_format_hsub(buf->fmt, 1),
							 v_row + x / pixel_format_hsub(buf->fmt, 1));
			} else {
				x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
							 NULL, NULL);
			}
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 2 plane YCbCr
 *
 * NV12: plane 0 = Y plane, [7:0] Y
 *       plane 1 = Cr:Cb plane, [15:0] Cr:Cb little endian
 *       2x2 subsampled Cr:Cb plane
 *
 * NV21: plane 0 = Y plane, [7:0] Y
 *       plane 1 = Cb:Cr plane, [15:0] Cb:Cr little endian
 *       2x2 subsampled Cb:Cr plane
 */
static struct client_buffer *
nv12_create_buffer(struct client *client,
		   uint32_t drm_format,
		   enum buffer_type type,
		   pixman_image_t *rgb_image)
{
	static const int swizzles[][2] = {
		{ 0, 1 }, /* NV12 */
		{ 1, 0 }  /* NV21 */
	};
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int idx, x, y;
	uint32_t *rgb_row;
	uint8_t *y_base;
	uint16_t *uv_base;
	uint8_t *y_row;
	uint16_t *uv_row;
	uint32_t argb;
	uint8_t cr;
	uint8_t cb;

	switch (drm_format) {
	case DRM_FORMAT_NV12:
		idx = 0;
		break;
	case DRM_FORMAT_NV21:
		idx = 1;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	y_base = buf->data + buf->offsets[0];
	uv_base = (uint16_t *)(buf->data + buf->offsets[1]);

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		y_row = y_base + y * buf->strides[0];
		uv_row = uv_base + (y / 2) * (buf->strides[1] / sizeof(uint16_t));

		for (x = 0; x < src.width; x++) {
			/*
			 * Sub-sample the source image instead, so that U and V
			 * sub-sampling does not require proper
			 * filtering/averaging/siting.
			 */
			argb = *(rgb_row + x / 2 * 2);

			/*
			 * A stupid way of "sub-sampling" chroma. This does not
			 * do the necessary filtering/averaging/siting.
			 */
			if ((y & 1) == 0 && (x & 1) == 0) {
				x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
							 &cb, &cr);
				*(uv_row + x / 2) =
					((uint16_t) cr << (swizzles[idx][1] * 8)) |
					((uint16_t) cb << (swizzles[idx][0] * 8));
			} else {
				x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
							 NULL, NULL);
			}
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 2 plane YCbCr
 *
 * NV16: plane 0 = Y plane, [7:0] Y
 *       plane 1 = Cr:Cb plane, [15:0] Cr:Cb little endian
 *       2x1 subsampled Cr:Cb plane
 *
 * NV61: plane 0 = Y plane, [7:0] Y
 *       plane 1 = Cb:Cr plane, [15:0] Cb:Cr little endian
 *       2x1 subsampled Cb:Cr plane
 */
static struct client_buffer *
nv16_create_buffer(struct client *client,
		   uint32_t drm_format,
		   enum buffer_type type,
		   pixman_image_t *rgb_image)
{
	static const int swizzles[][2] = {
		{ 0, 1 }, /* NV16 */
		{ 1, 0 }  /* NV61 */
	};
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int idx, x, y;
	uint32_t *rgb_row;
	uint8_t *y_base;
	uint16_t *uv_base;
	uint8_t *y_row;
	uint16_t *uv_row;
	uint32_t argb;
	uint8_t cr;
	uint8_t cb;

	switch (drm_format) {
	case DRM_FORMAT_NV16:
		idx = 0;
		break;
	case DRM_FORMAT_NV61:
		idx = 1;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	y_base = buf->data + buf->offsets[0];
	uv_base = (uint16_t *)(buf->data + buf->offsets[1]);

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		y_row = y_base + y * buf->strides[0];
		uv_row = uv_base + y * (buf->strides[1] / sizeof(uint16_t));

		for (x = 0; x < src.width; x++) {
			/*
			 * 2x2 sub-sample the source image to get the same
			 * result as the other YUV variants, so we can use the
			 * same reference image for checking.
			 */
			argb = *(rgb_row + x / 2 * 2);

			/*
			 * A stupid way of "sub-sampling" chroma. This does not
			 * do the necessary filtering/averaging/siting.
			 */
			if ((x & 1) == 0) {
				x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
							 &cb, &cr);
				*(uv_row + x / 2) =
					((uint16_t) cr << (swizzles[idx][1] * 8)) |
					((uint16_t) cb << (swizzles[idx][0] * 8));
			} else {
				x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
							 NULL, NULL);
			}
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 2 plane YCbCr
 *
 * NV24: plane 0 = Y plane, [7:0] Y
 *       plane 1 = Cr:Cb plane, [15:0] Cr:Cb little endian
 *       non-subsampled Cr:Cb plane
 *
 * NV42: plane 0 = Y plane, [7:0] Y
 *       plane 1 = Cb:Cr plane, [15:0] Cb:Cr little endian
 *       non-subsampled Cb:Cr plane
 */
static struct client_buffer *
nv24_create_buffer(struct client *client,
		   uint32_t drm_format,
		   enum buffer_type type,
		   pixman_image_t *rgb_image)
{
	static const int swizzles[][2] = {
		{ 0, 1 }, /* NV24 */
		{ 1, 0 }  /* NV42 */
	};
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int idx, x, y;
	uint32_t *rgb_row;
	uint8_t *y_base;
	uint16_t *uv_base;
	uint8_t *y_row;
	uint16_t *uv_row;
	uint32_t argb;
	uint8_t cr;
	uint8_t cb;

	switch (drm_format) {
	case DRM_FORMAT_NV24:
		idx = 0;
		break;
	case DRM_FORMAT_NV42:
		idx = 1;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	y_base = buf->data + buf->offsets[0];
	uv_base = (uint16_t *)(buf->data + buf->offsets[1]);

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		y_row = y_base + y * buf->strides[0];
		uv_row = uv_base + y * (buf->strides[1] / sizeof(uint16_t));

		for (x = 0; x < src.width; x++) {
			/*
			 * 2x2 sub-sample the source image to get the same
			 * result as the other YUV variants, so we can use the
			 * same reference image for checking.
			 */
			argb = *(rgb_row + x / 2 * 2);

			x8r8g8b8_to_ycbcr8_bt709(argb, y_row + x,
						 &cb, &cr);
			*(uv_row + x) =
				((uint16_t) cr << (swizzles[idx][1] * 8)) |
				((uint16_t) cb << (swizzles[idx][0] * 8));
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * Packed YCbCr
 *
 * YUYV: [31:0] Cr0:Y1:Cb0:Y0 8:8:8:8 little endian
 *       2x1 subsampled Cr:Cb plane
 *
 * YVYU: [31:0] Cb0:Y1:Cr0:Y0 8:8:8:8 little endian
 *       2x1 subsampled Cb:Cr plane
 *
 * UYVY: [31:0] Y1:Cr0:Y0:Cb0 8:8:8:8 little endian
 *       2x1 subsampled Cr:Cb plane
 *
 * VYUY: [31:0] Y1:Cb0:Y0:Cr0 8:8:8:8 little endian
 *       2x1 subsampled Cb:Cr plane
 */
static struct client_buffer *
yuyv_create_buffer(struct client *client,
		   uint32_t drm_format,
		   enum buffer_type type,
		   pixman_image_t *rgb_image)
{
	static const int swizzles[][4] = {
		{ 0, 1, 2, 3 }, /* YUYV */
		{ 0, 3, 2, 1 }, /* YVYU */
		{ 1, 0, 3, 2 }, /* UYVY */
		{ 1, 2, 3, 0 }  /* VYUY */
	};
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int idx, x, y;
	uint32_t *rgb_row;
	uint32_t *yuv_base;
	uint32_t *yuv_row;
	uint8_t cr;
	uint8_t cb;
	uint8_t y0;

	switch (drm_format) {
	case DRM_FORMAT_YUYV:
		idx = 0;
		break;
	case DRM_FORMAT_YVYU:
		idx = 1;
		break;
	case DRM_FORMAT_UYVY:
		idx = 2;
		break;
	case DRM_FORMAT_VYUY:
		idx = 3;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	yuv_base = buf->data;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		yuv_row = yuv_base + y * (buf->strides[0] / sizeof(uint32_t));

		for (x = 0; x < src.width; x += 2) {
			/*
			 * Sub-sample the source image instead, so that U and V
			 * sub-sampling does not require proper
			 * filtering/averaging/siting.
			 */
			x8r8g8b8_to_ycbcr8_bt709(*(rgb_row + x), &y0, &cb, &cr);
			*(yuv_row + x / 2) =
				((uint32_t)cr << (swizzles[idx][3] * 8)) |
				((uint32_t)y0 << (swizzles[idx][2] * 8)) |
				((uint32_t)cb << (swizzles[idx][1] * 8)) |
				((uint32_t)y0 << (swizzles[idx][0] * 8));
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * Packed YCbCr
 *
 * XYUV8888: [31:0] X:Y:Cb:Cr 8:8:8:8 little endian
 *           full resolution chroma
 */
static struct client_buffer *
xyuv8888_create_buffer(struct client *client,
		       uint32_t drm_format,
		       enum buffer_type type,
		       pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int x, y;
	uint32_t *rgb_row;
	uint32_t *yuv_base;
	uint32_t *yuv_row;
	uint8_t cr;
	uint8_t cb;
	uint8_t y0;

	test_assert_enum(drm_format, DRM_FORMAT_XYUV8888);

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	yuv_base = buf->data;

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		yuv_row = yuv_base + y * (buf->strides[0] / sizeof(uint32_t));

		for (x = 0; x < src.width; x++) {
			/*
			 * 2x2 sub-sample the source image to get the same
			 * result as the other YUV variants, so we can use the
			 * same reference image for checking.
			 */
			x8r8g8b8_to_ycbcr8_bt709(*(rgb_row + x / 2 * 2), &y0, &cb, &cr);
			/*
			 * The unused byte is intentionally set to "garbage"
			 * to catch any accidental use of it in the compositor.
			 */
			*(yuv_row + x) =
				((uint32_t)x << 24) |
				((uint32_t)y0 << 16) |
				((uint32_t)cb << 8) |
				((uint32_t)cr << 0);
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

/*
 * 2 plane YCbCr MSB aligned
 *
 * P016: index 0 = Y plane, [15:0] Y little endian
 *       index 1 = Cr:Cb plane, [31:0] Cr:Cb [16:16] little endian
 *       2x2 subsampled Cr:Cb plane 16 bits per channel
 *
 * P012: index 0 = Y plane, [15:0] Y:x [12:4] little endian
 *       index 1 = Cr:Cb plane, [31:0] Cr:x:Cb:x [12:4:12:4] little endian
 *       2x2 subsampled Cr:Cb plane 12 bits per channel
 *
 * P010: index 0 = Y plane, [15:0] Y:x [10:6] little endian
 *       index 1 = Cr:Cb plane, [31:0] Cr:x:Cb:x [10:6:10:6] little endian
 *       2x2 subsampled Cr:Cb plane 10 bits per channel
 */
static struct client_buffer *
p016_create_buffer(struct client *client,
		   uint32_t drm_format,
		   enum buffer_type type,
		   pixman_image_t *rgb_image)
{
	struct image_header src = image_header_from(rgb_image);
	struct client_buffer_create_data args = create_init(drm_format, type, &src);
	struct client_buffer *buf;
	int depth, x, y;
	uint32_t *rgb_row;
	uint16_t *y_base;
	uint32_t *uv_base;
	uint16_t *y_row;
	uint32_t *uv_row;
	uint32_t argb;
	uint16_t cr;
	uint16_t cb;

	switch (drm_format) {
	case DRM_FORMAT_P016:
		depth = 16;
		break;
	case DRM_FORMAT_P012:
		depth = 12;
		break;
	case DRM_FORMAT_P010:
		depth = 10;
		break;
	default:
		test_assert_not_reached("Invalid format!");
	};

	buf = client_buffer_create(client, &args);
	if (!buf)
		return NULL;

	y_base = (uint16_t *)(buf->data + buf->offsets[0]);
	uv_base = (uint32_t *)(buf->data + buf->offsets[1]);

	client_buffer_util_maybe_sync_dmabuf_start(buf);
	for (y = 0; y < src.height; y++) {
		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		y_row = y_base + y * (buf->strides[0] / sizeof(uint16_t));
		uv_row = uv_base + (y / 2) * (buf->strides[1] / sizeof(uint32_t));

		for (x = 0; x < src.width; x++) {
			/*
			 * Sub-sample the source image instead, so that U and V
			 * sub-sampling does not require proper
			 * filtering/averaging/siting.
			 */
			argb = *(rgb_row + x / 2 * 2);

			/*
			 * A stupid way of "sub-sampling" chroma. This does not
			 * do the necessary filtering/averaging/siting.
			 */
			if ((x & 1) == 0 && (y & 1) == 0) {
				x8r8g8b8_to_ycbcr16_bt709(argb, depth,
							  y_row + x,  &cb, &cr);
				*(uv_row + x / 2) =
					((uint32_t) cr << 16) |
					((uint32_t) cb << 0);
			} else {
				x8r8g8b8_to_ycbcr16_bt709(argb, depth,
							  y_row + x, NULL, NULL);
			}
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buf);

	return buf;
}

static void
show_window_with_client_buffer(struct client *client, struct client_buffer *buf)
{
	struct surface *surface = client->surface;
	int done;

	weston_test_move_surface(client->test->weston_test, surface->wl_surface,
				 4, 4);
	wl_surface_attach(surface->wl_surface, buf->wl_buffer, 0, 0);
	wl_surface_damage(surface->wl_surface, 0, 0, buf->width,
			  buf->height);
	frame_callback_set(surface->wl_surface, &done);
	wl_surface_commit(surface->wl_surface);
	frame_callback_wait(client, &done);
}

static const struct client_buffer_case client_buffer_cases[] = {
#define FMT(x) DRM_FORMAT_ ##x, #x
	/* RGB */
	{ FMT(RGBX4444), 0, rgba4444_create_buffer },
	{ FMT(RGBA4444), 0, rgba4444_create_buffer },
	{ FMT(BGRX4444), 0, rgba4444_create_buffer },
	{ FMT(BGRA4444), 0, rgba4444_create_buffer },
	{ FMT(XRGB4444), 0, rgba4444_create_buffer },
	{ FMT(ARGB4444), 0, rgba4444_create_buffer },
	{ FMT(XBGR4444), 0, rgba4444_create_buffer },
	{ FMT(ABGR4444), 0, rgba4444_create_buffer },
	{ FMT(RGBX5551), 1, rgba5551_create_buffer },
	{ FMT(RGBA5551), 1, rgba5551_create_buffer },
	{ FMT(BGRX5551), 1, rgba5551_create_buffer },
	{ FMT(BGRA5551), 1, rgba5551_create_buffer },
	{ FMT(RGB565), 2, rgb565_create_buffer },
	{ FMT(BGR565), 2, rgb565_create_buffer },
	{ FMT(RGB888), 3, rgb888_create_buffer },
	{ FMT(BGR888), 3, rgb888_create_buffer },
	{ FMT(RGBX8888), 3, rgba8888_create_buffer },
	{ FMT(RGBA8888), 3, rgba8888_create_buffer },
	{ FMT(BGRX8888), 3, rgba8888_create_buffer },
	{ FMT(BGRA8888), 3, rgba8888_create_buffer },
	{ FMT(XRGB8888), 3, rgba8888_create_buffer },
	{ FMT(ARGB8888), 3, rgba8888_create_buffer },
	{ FMT(XBGR8888), 3, rgba8888_create_buffer },
	{ FMT(ABGR8888), 3, rgba8888_create_buffer },
	{ FMT(XRGB2101010), 3, rgba2101010_create_buffer },
	{ FMT(ARGB2101010), 3, rgba2101010_create_buffer },
	{ FMT(XBGR2101010), 3, rgba2101010_create_buffer },
	{ FMT(ABGR2101010), 3, rgba2101010_create_buffer },
	{ FMT(XRGB16161616), 3, rgba16161616_create_buffer },
	{ FMT(ARGB16161616), 3, rgba16161616_create_buffer },
	{ FMT(XBGR16161616), 3, rgba16161616_create_buffer },
	{ FMT(ABGR16161616), 3, rgba16161616_create_buffer },
	{ FMT(XRGB16161616F), 3, rgba16161616f_create_buffer },
	{ FMT(ARGB16161616F), 3, rgba16161616f_create_buffer },
	{ FMT(XBGR16161616F), 3, rgba16161616f_create_buffer },
	{ FMT(ABGR16161616F), 3, rgba16161616f_create_buffer },
	/* YUV */
	{ FMT(YUV420), 4, y_u_v_create_buffer },
	{ FMT(YVU420), 4, y_u_v_create_buffer },
	{ FMT(YUV422), 4, y_u_v_create_buffer },
	{ FMT(YVU422), 4, y_u_v_create_buffer },
	{ FMT(YUV444), 4, y_u_v_create_buffer },
	{ FMT(YVU444), 4, y_u_v_create_buffer },
	{ FMT(NV12), 4, nv12_create_buffer },
	{ FMT(NV21), 4, nv12_create_buffer },
	{ FMT(NV16), 4, nv16_create_buffer },
	{ FMT(NV61), 4, nv16_create_buffer },
	{ FMT(NV24), 4, nv24_create_buffer },
	{ FMT(NV42), 4, nv24_create_buffer },
	{ FMT(YUYV), 4, yuyv_create_buffer },
	{ FMT(YVYU), 4, yuyv_create_buffer },
	{ FMT(UYVY), 4, yuyv_create_buffer },
	{ FMT(VYUY), 4, yuyv_create_buffer },
	{ FMT(XYUV8888), 4, xyuv8888_create_buffer },
	{ FMT(P010), 5, p016_create_buffer },
	{ FMT(P012), 5, p016_create_buffer },
	{ FMT(P016), 5, p016_create_buffer },
#undef FMT
};

static enum test_result_code
test_client_buffer(const struct client_buffer_case *cb_case,
		   enum buffer_type type)
{
	enum test_result_code res = RESULT_SKIP;
	char *fname;
	pixman_image_t *img;
	struct client *client;
	struct client_buffer *buf;
	bool match;

	/*
	 * Note for YUV formats:
	 *
	 * This test image is 256 x 256 pixels.
	 *
	 * Therefore this test does NOT exercise:
	 * - odd image dimensions
	 * - non-square image
	 * - row padding
	 * - unaligned row stride
	 * - different alignments or padding in sub-sampled planes
	 *
	 * The reason to not test these is that GL-renderer seems to be more
	 * or less broken.
	 *
	 * The source image is effectively further downscaled to 128 x 128
	 * before sampled and converted to 256 x 256 YUV, so that
	 * sub-sampling for U and V does not require proper algorithms.
	 * Therefore, this test also does not test:
	 * - chroma siting (chroma sample positioning)
	 */
	fname = image_filename("chocolate-cake");
	img = load_image_from_png(fname);
	free(fname);
	test_assert_ptr_not_null(img);

	client = create_client();
	client->surface = create_test_surface(client);

	buf = cb_case->create_buffer(client, cb_case->drm_format, type, img);
	if (buf) {
		show_window_with_client_buffer(client, buf);

		match = verify_screen_content(client, "client-buffer",
					      cb_case->ref_seq_no, NULL, 0,
					      NULL);
		res = match ? RESULT_OK : RESULT_FAIL;

		client_buffer_util_destroy_buffer(buf);
	}

	pixman_image_unref(img);
	client_destroy(client);

	return res;
}

/*
 * We do not require any format to pass, unless we run in CI where
 * WESTON_TEST_SKIP_IS_FAILURE is defined. In CI we require the hardcoded
 * list of formats to be supported and correct. Outside of CI we might running
 * with various graphics drivers with different supported formats, so we cannot
 * require a big list of formats to succeed.
 *
 * Even if a format is not required to pass, if the format is detected as
 * supported and the check with the format fails, the test will still fail.
 */
static bool
format_must_pass(uint32_t drm_format, const uint32_t *must_pass, const size_t num)
{
#if WESTON_TEST_SKIP_IS_FAILURE
	if (!must_pass || num == 0)
		return true;

	for (size_t i = 0; i < num; i++)
		if (must_pass[i] == drm_format)
			return true;
#endif

	return false;
}

static enum test_result_code
skip_is_just_fine(enum test_result_code c)
{
#if WESTON_TEST_SKIP_IS_FAILURE
	if (c == RESULT_SKIP)
		return RESULT_OK;
#endif
	return c;
}

static enum test_result_code
this_is_an_unwanted_case(void)
{
#if WESTON_TEST_SKIP_IS_FAILURE
	return RESULT_OK;
#else
	return RESULT_SKIP;
#endif
}

/*
 * Test that various SHM pixel formats result in correct coloring on screen.
 */
TEST_P(client_buffer_shm, client_buffer_cases)
{
	const struct client_buffer_case *cb_case = data;
	const struct setup_args *args = &my_setup_args[get_test_fixture_index()];
	enum test_result_code res;

	if (args->gl_force_import_yuv_fallback)
		return this_is_an_unwanted_case();

	testlog("%s: format %s\n", get_test_name(), cb_case->drm_format_name);

	res = test_client_buffer(cb_case, BUFFER_TYPE_SHM);
	if (res == RESULT_SKIP) {
		test_assert_false(format_must_pass(cb_case->drm_format,
						   args->shm_format_must_pass,
						   args->shm_format_num));
	}

	return skip_is_just_fine(res);
}

/*
 * Test that various DRM pixel formats result in correct coloring on screen.
 */
TEST_P(client_buffer_drm, client_buffer_cases)
{
	const struct client_buffer_case *cb_case = data;
	const struct setup_args *args = &my_setup_args[get_test_fixture_index()];
	enum test_result_code res;

	if (args->gl_force_import_yuv_fallback) {
		const struct pixel_format_info *info;

		info = pixel_format_get_info(cb_case->drm_format);
		if (info->color_model != COLOR_MODEL_YUV)
			return this_is_an_unwanted_case();
	}

	testlog("%s: format %s\n", get_test_name(), cb_case->drm_format_name);

	if (!client_buffer_util_is_dmabuf_supported()) {
		testlog("%s: Skipped: udmabuf not supported\n", get_test_name());
		return RESULT_SKIP;
	}

	res = test_client_buffer(cb_case, BUFFER_TYPE_DMABUF);
	if (res == RESULT_SKIP) {
		test_assert_false(format_must_pass(cb_case->drm_format,
						   args->dmabuf_format_must_pass,
						   args->dmabuf_format_num));
	}

	return skip_is_just_fine(res);
}

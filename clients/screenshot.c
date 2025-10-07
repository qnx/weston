/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright 2022 Collabora, Ltd.
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

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include <wayland-client.h>

#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "pixel-formats.h"
#include "shared/client-buffer-util.h"
#include "shared/file-util.h"
#include "shared/os-compatibility.h"
#include "shared/string-helpers.h"
#include "shared/xalloc.h"
#include "weston-output-capture-client-protocol.h"

struct screenshooter_app {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct zwp_linux_dmabuf_v1 *dmabuf;
	struct weston_capture_v1 *capture_factory;

	bool verbose;
	const struct pixel_format_info *requested_format;
	enum weston_capture_v1_source src_type;
	enum client_buffer_type buffer_type;

	struct wl_list output_list; /* struct screenshooter_output::link */

	bool retry;
	bool failed;
	int waitcount;
};

struct screenshooter_buffer {
	struct client_buffer *buf;
	pixman_image_t *image;
	enum weston_capture_v1_source src_type;
};

struct screenshooter_output {
	struct screenshooter_app *app;
	uint32_t name;
	struct wl_list link; /* struct screenshooter_app::output_list */

	struct wl_output *wl_output;
	int offset_x, offset_y;

	struct weston_capture_source_v1 *source;

	int buffer_width;
	int buffer_height;
	struct wl_array formats;
	bool formats_done;
	struct screenshooter_buffer *buffer;
};

struct buffer_size {
	int width, height;

	int min_x, min_y;
	int max_x, max_y;
};

static struct screenshooter_buffer *
screenshot_create_shm_buffer(struct screenshooter_app *app,
			     size_t width, size_t height,
			     const struct pixel_format_info *fmt)
{
	struct screenshooter_buffer *buffer;

	assert(width > 0);
	assert(height > 0);
	assert(fmt && fmt->bpp > 0);
	assert(fmt->pixman_format);

	buffer = xzalloc(sizeof *buffer);

	buffer->buf = client_buffer_util_create_shm_buffer(app->shm,
							   fmt,
							   width,
							   height);

	buffer->image = pixman_image_create_bits(fmt->pixman_format,
						 width, height,
						 buffer->buf->data,
						 buffer->buf->strides[0]);
	abort_oom_if_null(buffer->image);

	return buffer;
}

static struct screenshooter_buffer *
screenshot_create_udmabuf(struct screenshooter_app *app,
			  int width, int height,
			  const struct pixel_format_info *fmt)
{
	struct screenshooter_buffer* buffer = NULL;

	assert(width > 0);
	assert(height > 0);
	assert(fmt);

	buffer = xzalloc(sizeof *buffer);

	buffer->buf = client_buffer_util_create_dmabuf_buffer(app->display,
							      app->dmabuf,
							      fmt,
							      width,
							      height);

	if (fmt->pixman_format) {
		buffer->image = pixman_image_create_bits(fmt->pixman_format,
							 width, height,
							 buffer->buf->data,
							 buffer->buf->strides[0]);
		abort_oom_if_null(buffer->image);
	}

	return buffer;
}

static void
screenshooter_buffer_destroy(struct screenshooter_buffer *buffer)
{
	if (!buffer)
		return;

	if (buffer->image)
		pixman_image_unref(buffer->image);

	client_buffer_util_destroy_buffer(buffer->buf);
	free(buffer);
}

static void
capture_source_handle_format(void *data,
			     struct weston_capture_source_v1 *proxy,
			     uint32_t drm_format)
{
	struct screenshooter_output *output = data;
	uint32_t *fmt;

	assert(output->source == proxy);

	if (output->formats_done) {
		wl_array_release(&output->formats);
		wl_array_init(&output->formats);
		output->formats_done = false;
	}

	fmt = wl_array_add(&output->formats, sizeof(uint32_t));
	assert(fmt);
	*fmt = drm_format;

	if (output->app->verbose) {
		const struct pixel_format_info *fmt_info;

		fmt_info = pixel_format_get_info(drm_format);
		assert(fmt_info);
		printf("Got format %s / 0x%x\n", fmt_info->drm_format_name,
		       drm_format);
	}
}

static void
capture_source_handle_formats_done(void *data,
				   struct weston_capture_source_v1 *proxy)
{
	struct screenshooter_output *output = data;

	output->formats_done = true;
}

static void
capture_source_handle_size(void *data,
			   struct weston_capture_source_v1 *proxy,
			   int32_t width, int32_t height)
{
	struct screenshooter_output *output = data;

	assert(width > 0);
	assert(height > 0);

	output->buffer_width = width;
	output->buffer_height = height;

	if (output->app->verbose)
		printf("Got size %dx%d\n", width, height);
}

static void
capture_source_handle_complete(void *data,
			       struct weston_capture_source_v1 *proxy)
{
	struct screenshooter_output *output = data;

	output->app->waitcount--;
}

static void
capture_source_handle_retry(void *data,
			    struct weston_capture_source_v1 *proxy)
{
	struct screenshooter_output *output = data;

	output->app->waitcount--;
	output->app->retry = true;
}

static void
capture_source_handle_failed(void *data,
			     struct weston_capture_source_v1 *proxy,
			     const char *msg)
{
	struct screenshooter_output *output = data;

	output->app->waitcount--;
	/* We don't set app.failed here because there could be other
	 * outputs we still want to capture!
	 */

	if (msg)
		fprintf(stderr, "Output capture error: %s\n", msg);
}

static const struct weston_capture_source_v1_listener capture_source_handlers = {
	.format = capture_source_handle_format,
	.formats_done = capture_source_handle_formats_done,
	.size = capture_source_handle_size,
	.complete = capture_source_handle_complete,
	.retry = capture_source_handle_retry,
	.failed = capture_source_handle_failed,
};

static void
create_output(struct screenshooter_app *app, uint32_t output_name, uint32_t version)
{
	struct screenshooter_output *output;

	version = MIN(version, 4);
	output = xzalloc(sizeof *output);
	output->app = app;
	output->name = output_name;
	output->wl_output = wl_registry_bind(app->registry, output_name,
					     &wl_output_interface, version);
	abort_oom_if_null(output->wl_output);

	output->source = weston_capture_v1_create(app->capture_factory,
						  output->wl_output,
						  app->src_type);
	abort_oom_if_null(output->source);
	weston_capture_source_v1_add_listener(output->source,
					      &capture_source_handlers, output);

	wl_array_init(&output->formats);

	wl_list_insert(&app->output_list, &output->link);
}

static void
destroy_output(struct screenshooter_output *output)
{
	weston_capture_source_v1_destroy(output->source);

	wl_array_release(&output->formats);

	if (wl_output_get_version(output->wl_output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
		wl_output_release(output->wl_output);
	else
		wl_output_destroy(output->wl_output);

	screenshooter_buffer_destroy(output->buffer);
	wl_list_remove(&output->link);
	free(output);
}

static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t name, const char *interface, uint32_t version)
{
	struct screenshooter_app *app = data;

	if (strcmp(interface, wl_output_interface.name) == 0) {
		create_output(app, name, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		/*
		 * Not listening for format advertisements,
		 * weston_capture_source_v1.format event tells us what to use.
		 */
	} else if (strcmp(interface, weston_capture_v1_interface.name) == 0) {
		app->capture_factory = wl_registry_bind(registry, name,
							&weston_capture_v1_interface,
							2);
	} else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
		if (version < 3)
			return;
		app->dmabuf = wl_registry_bind(registry, name,
					       &zwp_linux_dmabuf_v1_interface,
					       3);
	}
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	/* Dynamic output removals will just fail the respective shot. */
}

static const struct wl_registry_listener registry_listener = {
	handle_global,
	handle_global_remove
};

static void
screenshooter_output_capture(struct screenshooter_output *output)
{
	const struct pixel_format_info *fmt_info = NULL;
	uint32_t *fmt;

	screenshooter_buffer_destroy(output->buffer);

	wl_array_for_each(fmt, &output->formats) {
		fmt_info = pixel_format_get_info(*fmt);
		assert(fmt_info);

		if (fmt_info == output->app->requested_format ||
		    output->app->requested_format == NULL)
			break;

		fmt_info = NULL;
	}
	if (!fmt_info) {
		fprintf(stderr, "No supported format found\n");
		exit(1);
	}

	if (output->app->verbose)
		printf("Creating buffer with format %s / 0x%x and size %ux%u\n",
		       fmt_info->drm_format_name, fmt_info->format,
		       output->buffer_width, output->buffer_height);

	if (output->app->buffer_type == CLIENT_BUFFER_TYPE_SHM) {
		output->buffer = screenshot_create_shm_buffer(output->app,
							      output->buffer_width,
							      output->buffer_height,
							      fmt_info);
	} else if (output->app->buffer_type == CLIENT_BUFFER_TYPE_DMABUF) {
		output->buffer = screenshot_create_udmabuf(output->app,
							   output->buffer_width,
							   output->buffer_height,
							   fmt_info);
	}
	abort_oom_if_null(output->buffer);

	weston_capture_source_v1_capture(output->source,
					 output->buffer->buf->wl_buffer);
	output->app->waitcount++;
}

static void
screenshot_write_png(const struct buffer_size *buff_size,
		     struct wl_list *output_list)
{
	pixman_image_t *shot;
	cairo_surface_t *surface;
	struct screenshooter_output *output;
	FILE *fp;
	char filepath[PATH_MAX];

	shot = pixman_image_create_bits(PIXMAN_a8r8g8b8,
					buff_size->width, buff_size->height,
					NULL, 0);
	abort_oom_if_null(shot);

	wl_list_for_each(output, output_list, link) {
		client_buffer_util_maybe_sync_dmabuf_start(output->buffer->buf);

		pixman_image_composite32(PIXMAN_OP_SRC,
					 output->buffer->image, /* src */
					 NULL, /* mask */
					 shot, /* dest */
					 0, 0, /* src x,y */
					 0, 0, /* mask x,y */
					 output->offset_x, output->offset_y, /* dst x,y */
					 output->buffer_width, output->buffer_height);

		client_buffer_util_maybe_sync_dmabuf_end(output->buffer->buf);
	}

	surface = cairo_image_surface_create_for_data((void *)pixman_image_get_data(shot),
						      CAIRO_FORMAT_ARGB32,
						      pixman_image_get_width(shot),
						      pixman_image_get_height(shot),
						      pixman_image_get_stride(shot));

	fp = file_create_dated(getenv("XDG_PICTURES_DIR"), "wayland-screenshot-",
			       ".png", filepath, sizeof(filepath));
	if (fp) {
		fclose (fp);
		cairo_surface_write_to_png(surface, filepath);
	}
	cairo_surface_destroy(surface);
	pixman_image_unref(shot);
}

static int
screenshot_set_buffer_size(struct buffer_size *buff_size,
			   struct wl_list *output_list)
{
	struct screenshooter_output *output;
	buff_size->min_x = buff_size->min_y = INT_MAX;
	buff_size->max_x = buff_size->max_y = INT_MIN;
	int position = 0;

	wl_list_for_each_reverse(output, output_list, link) {
		output->offset_x = position;
		position += output->buffer_width;
	}

	wl_list_for_each(output, output_list, link) {
		buff_size->min_x = MIN(buff_size->min_x, output->offset_x);
		buff_size->min_y = MIN(buff_size->min_y, output->offset_y);
		buff_size->max_x =
			MAX(buff_size->max_x, output->offset_x + output->buffer_width);
		buff_size->max_y =
			MAX(buff_size->max_y, output->offset_y + output->buffer_height);
	}

	if (buff_size->max_x <= buff_size->min_x ||
	    buff_size->max_y <= buff_size->min_y)
		return -1;

	buff_size->width = buff_size->max_x - buff_size->min_x;
	buff_size->height = buff_size->max_y - buff_size->min_y;

	return 0;
}

static bool
received_formats_for_all_outputs(struct screenshooter_app *app)
{
	struct screenshooter_output *output;

	wl_list_for_each(output, &app->output_list, link) {
		if (!output->formats_done)
			return false;
	}
	return true;
}

static void
print_usage_and_exit(void)
{
	printf("usage flags:\n"
	       "\t'-h,--help'"
	       "\n\t\tprint this help output\n"
	       "\t'-v,--verbose'"
	       "\n\t\tprint additional output\n"
	       "\t'-f,--format=<>'"
	       "\n\t\tthe DRM format name to use without the DRM_FORMAT_ prefix, e.g. RGBA8888 or NV12\n"
	       "\t'-s,--source-type=<>'"
	       "\n\t\tframebuffer to use framebuffer source (default), "
	       "\n\t\twriteback to use writeback source\n"
	       "\t'-b,--buffer-type=<>'"
	       "\n\t\tshm to use a SHM buffer (default), "
	       "\n\t\tdmabuf to use a DMA buffer\n");
	exit(0);
}

static const struct weston_enum_map source_types [] = {
	{ "framebuffer", WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER },
	{ "writeback", WESTON_CAPTURE_V1_SOURCE_WRITEBACK },
};

static const struct weston_enum_map buffer_types [] = {
	{ "shm", CLIENT_BUFFER_TYPE_SHM },
	{ "dmabuf", CLIENT_BUFFER_TYPE_DMABUF },
};

int
main(int argc, char *argv[])
{
	struct screenshooter_output *output;
	struct screenshooter_output *tmp_output;
	struct buffer_size buff_size = {};
	struct screenshooter_app app = {};
	int c, option_index;

	app.src_type = WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER;
	app.buffer_type = CLIENT_BUFFER_TYPE_SHM;

	static struct option long_options[] = {
		{"help",        no_argument,       NULL, 'h'},
		{"verbose",     no_argument,       NULL, 'v'},
		{"format",      required_argument, NULL, 'f'},
		{"source-type",	required_argument, NULL, 's'},
		{"buffer-type", required_argument, NULL, 'b'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "hvf:s:b:",
			long_options, &option_index)) != -1) {
		const struct weston_enum_map *entry;

		switch(c) {
		case 'v':
			app.verbose = true;
			break;
		case 'f':
			app.requested_format = pixel_format_get_info_by_drm_name(optarg);
			if (!app.requested_format) {
				fprintf(stderr, "Unknown format %s\n", optarg);
				return -1;
			}
			break;
		case 's':
			entry = weston_enum_map_find_name(source_types,
							  optarg);
			if (!entry)
				print_usage_and_exit();

			app.src_type = entry->value;
			break;
		case 'b':
			entry = weston_enum_map_find_name(buffer_types,
							  optarg);
			if (!entry)
				print_usage_and_exit();

			app.buffer_type = entry->value;
			break;
		default:
			print_usage_and_exit();
		}
	}

	wl_list_init(&app.output_list);

	app.display = wl_display_connect(NULL);
	if (app.display == NULL) {
		fprintf(stderr, "failed to create display: %s\n",
			strerror(errno));
		return -1;
	}

	app.registry = wl_display_get_registry(app.display);
	wl_registry_add_listener(app.registry, &registry_listener, &app);

	/* Process wl_registry advertisements */
	wl_display_roundtrip(app.display);

	if (!app.capture_factory) {
		fprintf(stderr, "Error: display does not support weston_capture_v1\n");
		return -1;
	}
	if (app.src_type == WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER &&
	    app.buffer_type != CLIENT_BUFFER_TYPE_SHM) {
		fprintf(stderr, "Error: Only support shm buffer with framebuffer source\n");
		return -1;
	}

	if(app.buffer_type == CLIENT_BUFFER_TYPE_SHM && !app.shm) {
		fprintf(stderr, "Error: display does not support wl_shm\n");
		return -1;
	}
	if (app.buffer_type == CLIENT_BUFFER_TYPE_DMABUF && !app.dmabuf) {
		fprintf(stderr, "Error: Compositor does not support zwp_linux_dmabuf_v1\n");
		return -1;
	}

	if (app.verbose) {
		printf("Taking screenshot with %s source %s buffer\n",
		       (app.src_type == WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER) ? "framebuffer" : "writeback",
		       (app.buffer_type == CLIENT_BUFFER_TYPE_SHM) ? "shm" : "dma");
	}

	/* Process initial events for wl_output and weston_capture_source_v1 */
	wl_display_roundtrip(app.display);

	while (!received_formats_for_all_outputs(&app)) {
		if (app.verbose)
			printf("Waiting for compositor to send capture source data\n");

		if (wl_display_dispatch(app.display) < 0) {
			fprintf(stderr, "Error: connection terminated\n");
			return -1;
		}
	}

	do {
		app.retry = false;

		wl_list_for_each(output, &app.output_list, link)
			screenshooter_output_capture(output);

		while (app.waitcount > 0 && !app.failed) {
			if (wl_display_dispatch(app.display) < 0)
				app.failed = true;
			assert(app.waitcount >= 0);
		}
	} while (app.retry && !app.failed);

	if (!app.failed) {
		if (screenshot_set_buffer_size(&buff_size, &app.output_list) < 0)
			return -1;
		screenshot_write_png(&buff_size, &app.output_list);
	} else {
		fprintf(stderr, "Error: screenshot or protocol failure\n");
	}

	wl_list_for_each_safe(output, tmp_output, &app.output_list, link)
		destroy_output(output);

	weston_capture_v1_destroy(app.capture_factory);
	wl_shm_destroy(app.shm);
	if (app.dmabuf)
		zwp_linux_dmabuf_v1_destroy(app.dmabuf);
	wl_registry_destroy(app.registry);
	wl_display_disconnect(app.display);

	return 0;
}

/*
 * Copyright 2025 Collabora, Ltd.
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

#include "color-representation-common.h"

#include "image-iter.h"
#include "pixel-formats.h"
#include "shared/weston-drm-fourcc.h"
#include "xdg-client-helper.h"

static void
presentation_feedback_handle_sync_output(void *data,
					 struct wp_presentation_feedback *feedback,
					 struct wl_output *output)
{
}

static void
presentation_feedback_handle_presented(void *data,
				       struct wp_presentation_feedback *feedback,
				       uint32_t tv_sec_hi,
				       uint32_t tv_sec_lo,
				       uint32_t tv_nsec,
				       uint32_t refresh,
				       uint32_t seq_hi,
				       uint32_t seq_lo,
				       uint32_t flags)
{
	enum feedback_result *result = data;
	bool zero_copy = flags & WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;

	if (zero_copy)
		*result = FB_PRESENTED_ZERO_COPY;
	else
		*result = FB_PRESENTED;

	wp_presentation_feedback_destroy(feedback);
}

static void
presentation_feedback_handle_discarded(void *data,
				       struct wp_presentation_feedback *feedback)
{
	enum feedback_result *result = data;

	*result = FB_DISCARDED;
	wp_presentation_feedback_destroy(feedback);
}

static const struct wp_presentation_feedback_listener presentation_feedback_listener = {
	.sync_output = presentation_feedback_handle_sync_output,
	.presented = presentation_feedback_handle_presented,
	.discarded = presentation_feedback_handle_discarded,
};

static void
presentation_wait_nofail(struct client *client, enum feedback_result *result)
{
	while (*result == FB_PENDING) {
		if (!test_assert_int_ge(wl_display_dispatch(client->wl_display), 0))
			break;
	}
	test_assert_u64_ne(*result, FB_PENDING);
}

static void
x8r8g8b8_to_ycbcr8(uint32_t xrgb,
		   const struct color_state *color_state,
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
	switch ((int)color_state->coefficients) {
	case 0:
	case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709:
		/* We choose BT709 as default */
		y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
		cr = (r - y) / 1.5748;
		cb = (b - y) / 1.8556;
		break;
	case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601:
		y = 0.299 * r + 0.587 * g + 0.114 * b;
		cr = (r - y) / 1.402;
		cb = (b - y) / 1.772;
		break;
	case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020:
		y = 0.2627 * r + 0.678 * g + 0.0593 * b;
		cr = (r - y) / 1.4746;
		cb = (b - y) / 1.8814;
		break;
	case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY:
	case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_FCC:
		/* For protocol error testing ensure we create invalid output */
		y = 0;
		cr = 0;
		cb = 0;
		break;
	default:
		test_assert_not_reached("Coefficients not handled");
	}

	switch ((int)color_state->range) {
	case 0:
	case WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED:
		/* We choose narrow range as default */
		*y_out = round(219.0 * y + 16.0);
		if (cr_out)
			*cr_out = round(224.0 * cr + 128.0);
		if (cb_out)
			*cb_out = round(224.0 * cb + 128.0);
		break;
	case WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL:
		*y_out = round(255.0 * y);
		if (cr_out)
			*cr_out = round(255.0 * cr + 128.0);
		if (cb_out)
			*cb_out = round(255.0 * cb + 128.0);
		break;
	default:
		test_assert_not_reached("Range not handled");
	}
}

static struct client_buffer*
create_and_fill_nv12_buffer_with_cake(struct client *client,
				      enum client_buffer_type buffer_type,
				      const struct color_state *color_state)
{
	const struct pixel_format_info *fmt_info;
	struct client_buffer *buffer;
	pixman_image_t *rgb_image;
	struct image_header src;
	uint8_t *y_base;
	uint16_t *uv_base;
	char *fname;
	int width, height;

	fmt_info = pixel_format_get_info(DRM_FORMAT_NV12);
	width = height = 256;

	switch (buffer_type) {
	case CLIENT_BUFFER_TYPE_SHM:
		buffer = client_buffer_util_create_shm_buffer(client->wl_shm,
			fmt_info,
			width,
			height);
		break;
	case CLIENT_BUFFER_TYPE_DMABUF:
		buffer = client_buffer_util_create_dmabuf_buffer(client->wl_display,
			client->dmabuf,
			fmt_info,
			width,
			height);
		break;
	default:
		test_assert_not_reached("Buffer type not handled");
		break;
	}

	fname = image_filename("chocolate-cake");
	rgb_image = load_image_from_png(fname);
	free(fname);
	test_assert_ptr_not_null(rgb_image);

	src = image_header_from(rgb_image);

	y_base = buffer->data + buffer->offsets[0];
	uv_base = (uint16_t *)(buffer->data + buffer->offsets[1]);

	client_buffer_util_maybe_sync_dmabuf_start(buffer);
	for (int y = 0; y < src.height; y++) {
		uint32_t *rgb_row;
		uint8_t *y_row;
		uint16_t *uv_row;

		rgb_row = image_header_get_row_u32(&src, y / 2 * 2);
		y_row = y_base + y * buffer->strides[0];
		uv_row = uv_base + (y / 2) * (buffer->strides[1] / sizeof(uint16_t));

		for (int x = 0; x < src.width; x++) {
			uint32_t argb;
			uint8_t cr;
			uint8_t cb;

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
				x8r8g8b8_to_ycbcr8(argb, color_state, y_row + x,
						   &cb, &cr);
				*(uv_row + x / 2) = ((uint16_t) cb) |
				                    ((uint16_t) cr << 8);
			} else {
				x8r8g8b8_to_ycbcr8(argb, color_state, y_row + x,
						   NULL, NULL);
			}
		}
	}
	client_buffer_util_maybe_sync_dmabuf_end(buffer);

	pixman_image_unref(rgb_image);

	return buffer;
}

/*
 * Test that a fullscreen client with smaller-than-fullscreen-sized NV12 buffer
 * is correctly rendered with various YCbCr matrix coefficients and range
 * combinations.
 */
enum test_result_code
test_color_representation(const struct color_state *color_state,
			  enum client_buffer_type buffer_type,
			  enum feedback_result expected_result)
{
	struct xdg_client *xdg_client;
	struct client *client;
	struct xdg_surface_data *xdg_surface;
	struct wl_surface *surface;
	struct client_buffer *buffer;
	struct wp_presentation_feedback *presentation_feedback;
	struct wp_color_representation_surface_v1 *color_representation_surface = NULL;
	enum feedback_result result;
	struct buffer *screenshot;
	bool match;

	if (buffer_type == CLIENT_BUFFER_TYPE_DMABUF &&
	    !client_buffer_util_is_dmabuf_supported()) {
		testlog("%s: Skipped: udmabuf not supported\n", get_test_name());
		return RESULT_SKIP;
	}

	xdg_client = create_xdg_client();
	client = xdg_client->client;
	xdg_surface = create_xdg_surface(xdg_client);
	surface = xdg_surface->surface->wl_surface;

	xdg_surface_make_toplevel(xdg_surface,
				  "weston.test.color-representation", "one");
	xdg_toplevel_set_fullscreen(xdg_surface->xdg_toplevel, NULL);
	xdg_surface_wait_configure(xdg_surface);

	buffer = create_and_fill_nv12_buffer_with_cake(client, buffer_type,
						       color_state);

	wl_surface_attach(surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);
	xdg_surface_maybe_ack_configure(xdg_surface);

	if (color_state->create_color_representation_surface) {
		color_representation_surface =
			wp_color_representation_manager_v1_get_surface(
				client->color_representation, surface);
		if (color_state->coefficients != 0)
			wp_color_representation_surface_v1_set_coefficients_and_range(
				color_representation_surface,
				color_state->coefficients, color_state->range);
	}

	result = FB_PENDING;
	presentation_feedback = wp_presentation_feedback(client->presentation,
							 surface);
	wp_presentation_feedback_add_listener(presentation_feedback,
					      &presentation_feedback_listener,
					      &result);
	wl_surface_commit(surface);
	presentation_wait_nofail(client, &result);

	test_assert_enum(result, expected_result);

	screenshot = client_capture_output(client, client->output,
					   WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER,
					   CLIENT_BUFFER_TYPE_SHM);
	test_assert_ptr_not_null(screenshot);

	client_buffer_util_maybe_sync_dmabuf_start(screenshot->buf);
	match = verify_image(screenshot->image, "color-representation", 0,
		NULL, 0);
	client_buffer_util_maybe_sync_dmabuf_end(screenshot->buf);

	buffer_destroy(screenshot);
	if (color_representation_surface)
		wp_color_representation_surface_v1_destroy(color_representation_surface);
	client_buffer_util_destroy_buffer(buffer);
	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	test_assert_true(match);

	return RESULT_OK;
}

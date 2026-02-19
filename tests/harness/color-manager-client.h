/*
 * Copyright 2023-2026 Collabora, Ltd.
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

#include <stdint.h>
#include <stdbool.h>

#include <libweston/colorimetry.h>

#include "color-management-v1-client-protocol.h"

struct client;
struct output;
struct surface;

struct color_manager_client {
	struct wp_color_manager_v1 *manager_proxy;

	/**
	 * Bit number protocol-enum-value is set to true for each compositor
	 * advertised item.
	 */
	uint32_t supported_rendering_intents;
	uint32_t supported_features;
	uint32_t supported_tf;
	uint32_t supported_primaries;
	bool init_done;
};

struct color_manager_client *
client_get_color_manager(struct client *client, unsigned version);

struct wp_image_description_creator_params_v1 *
color_manager_create_param(struct color_manager_client *cm);

void
param_creator_set_primaries(struct wp_image_description_creator_params_v1 *creator,
			    const struct weston_color_gamut *color_gamut);

void
param_creator_set_mastering_display_primaries(struct wp_image_description_creator_params_v1 *creator,
					      const struct weston_color_gamut *color_gamut);

enum image_description_status {
	CM_IMAGE_DESC_NOT_CREATED = 0,
	CM_IMAGE_DESC_READY,
	CM_IMAGE_DESC_FAILED,
};

struct image_description {
	struct wp_image_description_v1 *proxy;
	enum image_description_status status;

	/* For graceful failures. */
	int32_t failure_reason;
};

struct image_description *
image_description_from_param(struct wp_image_description_creator_params_v1 *creator);

struct image_description *
image_description_create_for_icc(struct color_manager_client *cm,
				 const char *icc_file_path);

struct image_description *
image_description_create_for_output(struct color_manager_client *cm,
				    struct output *output);

struct image_description *
image_description_create_for_preferred(struct color_manager_client *cm,
				       struct surface *surface);

void
image_description_wait_until_ready(struct client *client,
				   struct image_description *image_descr);

void
image_description_destroy(struct image_description *image_desc);

struct image_description_info {
	struct wp_image_description_info_v1 *wp_image_description_info;

	/* Bitfield that holds what events the compositor has sent us through
	 * the image_descr_info object. For each event image_descr_info_event v
	 * received, the bit v of this bitfield will be set to 1. */
	uint32_t events_received;
	bool done;

	/* For ICC-based image descriptions. */
	int32_t icc_fd;
	uint32_t icc_size;

	/* For parametric images descriptions. */
	enum wp_color_manager_v1_primaries primaries_named;
	struct weston_color_gamut primaries;
	enum wp_color_manager_v1_transfer_function tf_named;
	float tf_power;
	float min_lum, max_lum, ref_lum;
	struct weston_color_gamut target_primaries;
	float target_min_lum, target_max_lum;
	float target_max_cll;
	float target_max_fall;
};

struct image_description_info *
image_description_get_information(struct client *client,
				  struct image_description *image_descr);

void
image_description_info_destroy(struct image_description_info *info);

/** Private to the test harness */
void
color_manager_client_destroy(struct color_manager_client *cm);

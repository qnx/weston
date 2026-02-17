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

void
image_description_destroy(struct image_description *image_desc);

/** Private to the test harness */
void
color_manager_client_destroy(struct color_manager_client *cm);

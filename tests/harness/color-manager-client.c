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

#include "config.h"

#include "color-manager-client.h"
#include "weston-test-assert.h"
#include "weston-test-runner.h"
#include "weston-test-client-helper.h"

#include <wayland-client-protocol.h>

#include "shared/helpers.h"
#include "shared/xalloc.h"

static void
cm_supported_intent(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
		    uint32_t render_intent)
{
	struct color_manager_client *cm = data;

	cm->supported_rendering_intents |= bit(render_intent);
}

static void
cm_supported_feature(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
		     uint32_t feature)
{
	struct color_manager_client *cm = data;

	cm->supported_features |= bit(feature);
}

static void
cm_supported_tf_named(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
		      uint32_t tf)
{
	struct color_manager_client *cm = data;

	cm->supported_tf |= bit(tf);
}

static void
cm_supported_primaries_named(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
			     uint32_t primaries)
{
	struct color_manager_client *cm = data;

	cm->supported_primaries |= bit(primaries);
}

static void
cm_done(void *data, struct wp_color_manager_v1 *wp_color_manager_v1)
{
	struct color_manager_client *cm = data;

	cm->init_done = true;
}

static const struct wp_color_manager_v1_listener
cm_iface = {
	.supported_intent = cm_supported_intent,
	.supported_feature = cm_supported_feature,
	.supported_tf_named = cm_supported_tf_named,
	.supported_primaries_named = cm_supported_primaries_named,
	.done = cm_done,
};

struct color_manager_client *
client_get_color_manager(struct client *client, unsigned version)
{
	struct color_manager_client *cm = client->color_manager;

	if (cm) {
		if (wp_color_manager_v1_get_version(cm->manager_proxy) == version)
			return cm;

		color_manager_client_destroy(cm);
		client->color_manager = NULL;
	}

	cm = xzalloc(sizeof *cm);

	cm->manager_proxy = bind_to_singleton_global(client,
						     &wp_color_manager_v1_interface,
						     version);
	wp_color_manager_v1_add_listener(cm->manager_proxy, &cm_iface, cm);
	client->color_manager = cm;

	while (!cm->init_done)
		if (!test_assert_int_ge(wl_display_dispatch(client->wl_display), 0))
			break;
	test_assert_true(cm->init_done);

	return cm;
}

void
color_manager_client_destroy(struct color_manager_client *cm)
{
	if (!cm)
		return;

	wp_color_manager_v1_destroy(cm->manager_proxy);
	free(cm);
}

struct wp_image_description_creator_params_v1 *
color_manager_create_param(struct color_manager_client *cm)
{
	test_assert_bit_set(cm->supported_features, WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC);
	return wp_color_manager_v1_create_parametric_creator(cm->manager_proxy);
}

void
param_creator_set_primaries(struct wp_image_description_creator_params_v1 *creator,
			    const struct weston_color_gamut *color_gamut)
{
	wp_image_description_creator_params_v1_set_primaries(
		creator,
		color_gamut->primary[0].x * 1000000,
		color_gamut->primary[0].y * 1000000,
		color_gamut->primary[1].x * 1000000,
		color_gamut->primary[1].y * 1000000,
		color_gamut->primary[2].x * 1000000,
		color_gamut->primary[2].y * 1000000,
		color_gamut->white_point.x * 1000000,
		color_gamut->white_point.y * 1000000);
}

void
param_creator_set_mastering_display_primaries(struct wp_image_description_creator_params_v1 *creator,
					      const struct weston_color_gamut *color_gamut)
{
	wp_image_description_creator_params_v1_set_mastering_display_primaries(
		creator,
		color_gamut->primary[0].x * 1000000,
		color_gamut->primary[0].y * 1000000,
		color_gamut->primary[1].x * 1000000,
		color_gamut->primary[1].y * 1000000,
		color_gamut->primary[2].x * 1000000,
		color_gamut->primary[2].y * 1000000,
		color_gamut->white_point.x * 1000000,
		color_gamut->white_point.y * 1000000);
}

static void
image_desc_ready(void *data, struct wp_image_description_v1 *wp_image_description_v1,
		 uint32_t identity)
{
	struct image_description *image_desc = data;

	image_desc->status = CM_IMAGE_DESC_READY;
}

static void
image_desc_failed(void *data, struct wp_image_description_v1 *wp_image_description_v1,
		  uint32_t cause, const char *msg)
{
	struct image_description *image_desc = data;

	image_desc->status = CM_IMAGE_DESC_FAILED;
	image_desc->failure_reason = cause;

	testlog("Failed to create image description:\n" \
		"    cause: %u, msg: %s\n", cause, msg);
}

static const struct wp_image_description_v1_listener
image_desc_iface = {
	.ready = image_desc_ready,
	.failed = image_desc_failed,
};

struct image_description *
image_description_from_param(struct wp_image_description_creator_params_v1 *creator)
{
	struct image_description *image_desc = xzalloc(sizeof(*image_desc));

	image_desc->proxy = wp_image_description_creator_params_v1_create(creator);

	wp_image_description_v1_add_listener(image_desc->proxy,
					     &image_desc_iface, image_desc);

	return image_desc;
}

void
image_description_destroy(struct image_description *image_desc)
{
	wp_image_description_v1_destroy(image_desc->proxy);
	free(image_desc);
}

/*
 * Copyright 2024 Collabora, Ltd.
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

#include "weston-test-client-helper.h"
#include "weston-test-assert.h"
#include "shared/xalloc.h"

#include "color-management-v1-client-protocol.h"

#define NOT_SET -99
#define BAD_ENUM 99999

/**
 * This is used to know where to expect the error in the test.
 */
enum error_point {
	ERROR_POINT_NONE = 0,
	ERROR_POINT_PRIMARIES_NAMED,
	ERROR_POINT_PRIMARIES,
	ERROR_POINT_TF_NAMED,
	ERROR_POINT_TF_POWER,
	ERROR_POINT_PRIMARIES_LUM,
	ERROR_POINT_TARGET_LUM,
	ERROR_POINT_IMAGE_DESC,
	ERROR_POINT_GRACEFUL_FAILURE,
};

struct image_description {
	struct wp_image_description_v1 *wp_image_desc;
	enum image_description_status {
		CM_IMAGE_DESC_NOT_CREATED = 0,
		CM_IMAGE_DESC_READY,
		CM_IMAGE_DESC_FAILED,
	} status;

	/* For graceful failures. */
	int32_t failure_reason;
};

struct color_manager {
	struct wp_color_manager_v1 *manager;

	/* Bitfield that holds what color features are supported. If enum
	 * wp_color_manager_v1_feature v is supported, bit v will be set
	 * to 1. */
	uint32_t supported_features;

	/* Bitfield that holds what rendering intents are supported. If enum
	 * wp_color_manager_v1_render_intent v is supported, bit v will be set
	 * to 1. */
	uint32_t supported_rendering_intents;

	/* Bitfield that holds what color primaries are supported. If enum
	 * wp_color_manager_v1_primaries v is supported, bit v will be set
	 * to 1. */
	uint32_t supported_color_primaries;

	/* Bitfield that holds what transfer functions are supported. If enum
	 * wp_color_manager_v1_transfer_function v is supported, bit v will be
	 * set to 1. */
	uint32_t supported_tf;

	bool done;
};

struct test_case {
	int32_t primaries_named;
	const struct weston_color_gamut *primaries;
	int32_t tf_named;
	float tf_power;
	float primaries_min_lum;
	int32_t primaries_max_lum;
	int32_t primaries_ref_lum;
	const struct weston_color_gamut *target_primaries;
	float target_min_lum;
	int32_t target_max_lum;
	int32_t target_max_cll;
	int32_t target_max_fall;
	int32_t expected_error;
	enum error_point error_point;
};

static const struct weston_color_gamut color_gamut_sRGB = {
	.primary = { { 0.64, 0.33 }, /* RGB order */
		     { 0.30, 0.60 },
		     { 0.15, 0.06 },
	},
	.white_point = { 0.3127, 0.3290 },
};

static const struct weston_color_gamut color_gamut_invalid_primaries = {
	.primary = { { -100.00, 0.33 }, /* RGB order */
		     {    0.30, 0.60 },
		     {    0.15, 0.06 },
	},
	.white_point = { 0.3127, 0.3290 },
};

static const struct weston_color_gamut color_gamut_invalid_white_point = {
	.primary = { { 0.64, 0.33 }, /* RGB order */
		     { 0.30, 0.60 },
		     { 0.15, 0.06 },
	},
	.white_point = { 1.0, 1.0 },
};

static const struct test_case good_test_cases[] = {
	{
	  /* sRGB primaries with sRGB TF; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* Custom primaries with sRGB TF; succeeds. */
	  .primaries_named = NOT_SET,
	  .primaries = &color_gamut_sRGB,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries, sRGB TF and valid luminance values; succeeds. */
	  .primaries_named = NOT_SET,
	  .primaries = &color_gamut_sRGB,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = 0.5,
	  .primaries_max_lum = 2000,
	  .primaries_ref_lum = 300,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries with custom power-law TF; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = NOT_SET,
	  .tf_power = 2.4f,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries, sRGB TF and valid target primaries; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = &color_gamut_sRGB,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries, PQ TF and valid target luminance; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = 2.0f,
	  .target_max_lum = 3,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries, PQ TF and valid max cll; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = 5,
	  .target_max_fall = NOT_SET,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries, PQ TF and valid max fall; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = 5,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
	{
	  /* sRGB primaries, PQ TF and valid target luminance, max fall and
	   * max cll; succeeds. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = 1.0f,
	  .target_max_lum = 3,
	  .target_max_cll = 2,
	  .target_max_fall = 2,
	  .expected_error = NOT_SET,
	  .error_point = ERROR_POINT_NONE,
	},
};

static const struct test_case bad_test_cases[] = {
	{
	  /* Invalid named primaries; protocol error. */
	  .primaries_named = BAD_ENUM,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_PRIMARIES_NAMED,
	  .error_point = ERROR_POINT_PRIMARIES_NAMED,
	},
	{
	  /* Invalid TF named; protocol error. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = BAD_ENUM,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
	  .error_point = ERROR_POINT_TF_NAMED,
	},
	{
	  /* Invalid power-law TF exponent (0.9 < 1.0, which is the minimum);
	   * protocol error. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = NOT_SET,
	  .tf_power = 0.9f,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
	  .error_point = ERROR_POINT_TF_POWER,
	},
	{
	  /* Invalid luminance (ref white < min lum); protocol error. */
	  .primaries_named = NOT_SET,
	  .primaries = &color_gamut_sRGB,
	  .tf_named = NOT_SET,
	  .tf_power = 5.0f,
	  .primaries_min_lum = 50.0,
	  .primaries_max_lum = 100,
	  .primaries_ref_lum = 49,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
	  .error_point = ERROR_POINT_PRIMARIES_LUM,
	},
	{
	  /* Invalid target luminance (min_lum == max_lum); protocol error. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = 5.0f,
	  .target_max_lum = 5,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
	  .error_point = ERROR_POINT_TARGET_LUM,
	},
	{
	  /* Invalid max cll (max cll < min target luminance);
	   * protocol error. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = 6.0f,
	  .target_max_lum = 7,
	  .target_max_cll = 5,
	  .target_max_fall = NOT_SET,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
	  .error_point = ERROR_POINT_IMAGE_DESC,
	},
	{
	  /* Invalid max fall (max fall < min target luminance);
	   * protocol error. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = 6.0f,
	  .target_max_lum = 7,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = 5,
	  .expected_error = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
	  .error_point = ERROR_POINT_IMAGE_DESC,
	},
	{
	  /* Invalid custom primaries (CIE XY value out of compositor defined
	   * range); graceful failure. */
	  .primaries_named = NOT_SET,
	  .primaries = &color_gamut_invalid_primaries,
	  .tf_named = NOT_SET,
	  .tf_power = 5.0f,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
	  .error_point = ERROR_POINT_GRACEFUL_FAILURE,
	},
	{
	  /* Invalid custom primaries (white point out of color gamut);
	   * graceful failure. */
	  .primaries_named = NOT_SET,
	  .primaries = &color_gamut_invalid_white_point,
	  .tf_named = NOT_SET,
	  .tf_power = 5.0f,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = NULL,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
	  .error_point = ERROR_POINT_GRACEFUL_FAILURE,
	},
	{
	  /* Invalid custom target primaries (CIE XY value out of compositor
	   * defined range); graceful failure. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = &color_gamut_invalid_primaries,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
	  .error_point = ERROR_POINT_GRACEFUL_FAILURE,
	},
	{
	  /* Invalid custom target primaries (white point out of color gamut);
	   * graceful failure. */
	  .primaries_named = WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
	  .primaries = NULL,
	  .tf_named = WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
	  .tf_power = NOT_SET,
	  .primaries_min_lum = NOT_SET,
	  .primaries_max_lum = NOT_SET,
	  .primaries_ref_lum = NOT_SET,
	  .target_primaries = &color_gamut_invalid_white_point,
	  .target_min_lum = NOT_SET,
	  .target_max_lum = NOT_SET,
	  .target_max_cll = NOT_SET,
	  .target_max_fall = NOT_SET,
	  .expected_error = WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
	  .error_point = ERROR_POINT_GRACEFUL_FAILURE,
	},
};

static void
set_primaries(struct wp_image_description_creator_params_v1 *image_desc_creator,
	      const struct weston_color_gamut *color_gamut)
{
	wp_image_description_creator_params_v1_set_primaries(
		image_desc_creator,
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
set_mastering_display_primaries(struct wp_image_description_creator_params_v1 *image_desc_creator,
				const struct weston_color_gamut *color_gamut)
{
	wp_image_description_creator_params_v1_set_mastering_display_primaries(
		image_desc_creator,
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

static struct image_description *
image_description_create(struct wp_image_description_creator_params_v1 *image_desc_creator_param)
{
	struct image_description *image_desc = xzalloc(sizeof(*image_desc));

	image_desc->wp_image_desc =
		wp_image_description_creator_params_v1_create(image_desc_creator_param);

	wp_image_description_v1_add_listener(image_desc->wp_image_desc,
					     &image_desc_iface, image_desc);

	return image_desc;
}

static void
image_description_destroy(struct image_description *image_desc)
{
	wp_image_description_v1_destroy(image_desc->wp_image_desc);
	free(image_desc);
}

static void
cm_supported_intent(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
		    uint32_t render_intent)
{
	struct color_manager *cm = data;

	cm->supported_rendering_intents |= (1 << render_intent);
}

static void
cm_supported_feature(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
		     uint32_t feature)
{
	struct color_manager *cm = data;

	cm->supported_features |= (1 << feature);
}

static void
cm_supported_tf_named(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
		      uint32_t tf)
{
	struct color_manager *cm = data;

	cm->supported_tf |= (1 << tf);
}

static void
cm_supported_primaries_named(void *data, struct wp_color_manager_v1 *wp_color_manager_v1,
			     uint32_t primaries)
{
	struct color_manager *cm = data;

	cm->supported_color_primaries |= (1 << primaries);
}

static void
cm_done(void *data, struct wp_color_manager_v1 *wp_color_manager_v1)
{
	struct color_manager *cm = data;

	cm->done = true;
}

static const struct wp_color_manager_v1_listener
cm_iface = {
	.supported_intent = cm_supported_intent,
	.supported_feature = cm_supported_feature,
	.supported_tf_named = cm_supported_tf_named,
	.supported_primaries_named = cm_supported_primaries_named,
	.done = cm_done,
};

static void
color_manager_init(struct color_manager *cm, struct client *client)
{
	memset(cm, 0, sizeof(*cm));

	cm->manager = bind_to_singleton_global(client,
					       &wp_color_manager_v1_interface,
					       1);
	wp_color_manager_v1_add_listener(cm->manager, &cm_iface, cm);

	client_roundtrip(client);

	/* Weston supports all color features. */
	test_assert_u32_eq(cm->supported_features,
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_ICC_V2_V4) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_EXTENDED_TARGET_VOLUME));

	/* Weston supports all rendering intents. */
	test_assert_u32_eq(cm->supported_rendering_intents,
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_RELATIVE) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_SATURATION) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_ABSOLUTE) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_RELATIVE_BPC));

	/* Weston supports all primaries. */
	test_assert_u32_eq(cm->supported_color_primaries,
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_SRGB) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_PAL_M) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_PAL) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_NTSC) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_GENERIC_FILM) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_BT2020) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_CIE1931_XYZ) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_DCI_P3) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3) |
			   (1 << WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB));

	/* Weston supports only a few transfer functions, and we make use of
	 * them in our tests. */
	test_assert_u32_eq(cm->supported_tf,
			   (1 << WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22) |
			   (1 << WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA28) |
			   (1 << WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB) |
			   (1 << WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ));

	test_assert_true(cm->done);
}

static void
color_manager_fini(struct color_manager *cm)
{
	wp_color_manager_v1_destroy(cm->manager);
}

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_GL;
	setup.shell = SHELL_TEST_DESKTOP;
	setup.logging_scopes = "log,color-lcms-profiles";

	weston_ini_setup(&setup,
			 cfgln("[core]"),
			 cfgln("color-management=true"));

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

TEST_P(create_parametric_image_description, good_test_cases)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param = NULL;
	const struct test_case *args = data;
	struct image_description *image_desc = NULL;

	/* No good test case should have expected error. */
	test_assert_enum(args->error_point, ERROR_POINT_NONE);
	test_assert_enum(args->expected_error, NOT_SET);

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);

	if (args->primaries_named != NOT_SET)
		wp_image_description_creator_params_v1_set_primaries_named(image_desc_creator_param,
									   args->primaries_named);

	if (args->primaries)
		set_primaries(image_desc_creator_param, args->primaries);

	if (args->tf_named != NOT_SET)
		wp_image_description_creator_params_v1_set_tf_named(image_desc_creator_param,
								    args->tf_named);

	if (args->tf_power != NOT_SET)
		wp_image_description_creator_params_v1_set_tf_power(image_desc_creator_param,
								    args->tf_power * 10000);

	if (args->primaries_min_lum != NOT_SET && args->primaries_max_lum != NOT_SET &&
	    args->primaries_ref_lum != NOT_SET)
		wp_image_description_creator_params_v1_set_luminances(image_desc_creator_param,
								      args->primaries_min_lum * 10000,
								      args->primaries_max_lum,
								      args->primaries_ref_lum);

	if (args->target_primaries)
		set_mastering_display_primaries(image_desc_creator_param, args->target_primaries);

	if (args->target_min_lum != NOT_SET && args->target_max_lum != NOT_SET)
		wp_image_description_creator_params_v1_set_mastering_luminance(image_desc_creator_param,
									       args->target_min_lum * 10000,
									       args->target_max_lum);

	if (args->target_max_cll != NOT_SET)
		wp_image_description_creator_params_v1_set_max_cll(image_desc_creator_param,
								   args->target_max_cll);

	if (args->target_max_fall != NOT_SET)
		wp_image_description_creator_params_v1_set_max_fall(image_desc_creator_param,
								    args->target_max_fall);

	image_desc = image_description_create(image_desc_creator_param);
	image_desc_creator_param = NULL;

	while (image_desc->status == CM_IMAGE_DESC_NOT_CREATED)
		test_assert_int_ge(wl_display_dispatch(client->wl_display), 0);
	test_assert_enum(image_desc->status, CM_IMAGE_DESC_READY);

	image_description_destroy(image_desc);
	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST_P(fail_to_create_parametric_image_description, bad_test_cases)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param = NULL;
	const struct test_case *args = data;
	struct image_description *image_desc = NULL;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);

	if (args->primaries_named != NOT_SET)
		wp_image_description_creator_params_v1_set_primaries_named(image_desc_creator_param,
									   args->primaries_named);
	if (args->error_point == ERROR_POINT_PRIMARIES_NAMED) {
		expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
				      args->expected_error);
		goto out;
	}

	if (args->primaries)
		set_primaries(image_desc_creator_param, args->primaries);
	if (args->error_point == ERROR_POINT_PRIMARIES) {
		expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
				      args->expected_error);
		goto out;
	}

	if (args->tf_named != NOT_SET)
		wp_image_description_creator_params_v1_set_tf_named(image_desc_creator_param,
								    args->tf_named);
	if (args->error_point == ERROR_POINT_TF_NAMED) {
		expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
				      args->expected_error);
		goto out;
	}

	if (args->tf_power != NOT_SET)
		wp_image_description_creator_params_v1_set_tf_power(image_desc_creator_param,
								    args->tf_power * 10000);
	if (args->error_point == ERROR_POINT_TF_POWER) {
		expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
				      args->expected_error);
		goto out;
	}

	if (args->primaries_min_lum != NOT_SET && args->primaries_max_lum != NOT_SET &&
	    args->primaries_ref_lum != NOT_SET)
		wp_image_description_creator_params_v1_set_luminances(image_desc_creator_param,
								      args->primaries_min_lum * 10000,
								      args->primaries_max_lum,
								      args->primaries_ref_lum);

	if (args->error_point == ERROR_POINT_PRIMARIES_LUM) {
		expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
				      args->expected_error);
		goto out;
	}

	if (args->target_primaries)
		set_mastering_display_primaries(image_desc_creator_param, args->target_primaries);
	/**
	 * The only possible failure for set_mastering_display() is ALREADY_SET, but we test
	 * that in another TEST(). So we don't have ERROR_POINT_MASTERING_DISPLAY_PRIMARIES.
	 */

	if (args->target_min_lum != NOT_SET && args->target_max_lum != NOT_SET)
		wp_image_description_creator_params_v1_set_mastering_luminance(image_desc_creator_param,
									       args->target_min_lum * 10000,
									       args->target_max_lum);
	if (args->error_point == ERROR_POINT_TARGET_LUM) {
		expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
				      args->expected_error);
		goto out;
	}

	if (args->target_max_cll != NOT_SET)
		wp_image_description_creator_params_v1_set_max_cll(image_desc_creator_param,
								   args->target_max_cll);
	/**
	 * The only possible failure for set_max_cll() is ALREADY_SET, but we test that
	 * in another TEST(). So we don't have ERROR_POINT_TARGET_MAX_CLL.
	 */

	if (args->target_max_fall != NOT_SET)
		wp_image_description_creator_params_v1_set_max_fall(image_desc_creator_param,
								    args->target_max_fall);
	/**
	 * The only possible failure for set_max_fall() is ALREADY_SET, but we test that
	 * in another TEST(). So we don't have ERROR_POINT_TARGET_MAX_FALL.
	 */

	image_desc = image_description_create(image_desc_creator_param);
	image_desc_creator_param = NULL;
	if (args->error_point == ERROR_POINT_IMAGE_DESC) {
		/* We expect a protocol error from unknown object, because the
		 * image_desc_creator_param wl_proxy will get destroyed with
		 * the create call above. It is a destructor request. */
		expect_protocol_error(client, NULL, args->expected_error);
		goto out;
	}

	while (image_desc->status == CM_IMAGE_DESC_NOT_CREATED)
		test_assert_int_ge(wl_display_dispatch(client->wl_display), 0);

	/* This TEST() is for bad params, so we shouldn't be able to
	 * successfully create an image description. */
	test_assert_enum(args->error_point, ERROR_POINT_GRACEFUL_FAILURE);
	test_assert_enum(image_desc->status, CM_IMAGE_DESC_FAILED);
	test_assert_enum(image_desc->failure_reason, args->expected_error);

out:
	if (image_desc)
		image_description_destroy(image_desc);
	if (image_desc_creator_param)
		wp_image_description_creator_params_v1_destroy(image_desc_creator_param);
	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_primaries_named_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_primaries_named(image_desc_creator_param,
								   WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_primaries_named(image_desc_creator_param,
								   WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_primaries_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	set_primaries(image_desc_creator_param, &color_gamut_sRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	set_primaries(image_desc_creator_param, &color_gamut_sRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_primaries_then_primaries_named)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	set_primaries(image_desc_creator_param, &color_gamut_sRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_primaries_named(image_desc_creator_param,
								   WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_primaries_named_then_primaries)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_primaries_named(image_desc_creator_param,
								   WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	set_primaries(image_desc_creator_param, &color_gamut_sRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_tf_power_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_tf_power(image_desc_creator_param,
							    2.4 * 10000);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_tf_power(image_desc_creator_param,
							    2.4 * 10000);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_tf_named_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_tf_named(image_desc_creator_param,
							    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_tf_named(image_desc_creator_param,
							    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_tf_power_then_tf_named)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_tf_power(image_desc_creator_param,
							    2.4 * 10000);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_tf_named(image_desc_creator_param,
							    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_tf_named_then_tf_power)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_tf_named(image_desc_creator_param,
							    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_tf_power(image_desc_creator_param,
							    2.4 * 10000);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_luminance_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;
	float min_lum = 0.5;
	float max_lum = 2000.0;
	float ref_lum = 300.0;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_luminances(image_desc_creator_param,
							      min_lum * 10000,
							      max_lum,
							      ref_lum);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_luminances(image_desc_creator_param,
							      min_lum * 10000,
							      max_lum,
							      ref_lum);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_target_primaries_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	set_mastering_display_primaries(image_desc_creator_param, &color_gamut_sRGB);
	client_roundtrip(client); /* make sure connection is still valid */
	set_mastering_display_primaries(image_desc_creator_param, &color_gamut_sRGB);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_target_luminance_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;
	float target_min_lum = 2.0f;
	float target_max_lum = 3.0f;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_mastering_luminance(image_desc_creator_param,
								       target_min_lum * 10000,
								       target_max_lum);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_mastering_luminance(image_desc_creator_param,
								       target_min_lum * 10000,
								       target_max_lum);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_max_cll_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_max_cll(image_desc_creator_param, 5.0f);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_max_cll(image_desc_creator_param, 5.0f);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_max_fall_twice)
{
	struct client *client;
	struct color_manager cm;
	struct wp_image_description_creator_params_v1 *image_desc_creator_param;

	client = create_client();
	color_manager_init(&cm, client);

	image_desc_creator_param =
		wp_color_manager_v1_create_parametric_creator(cm.manager);
	wp_image_description_creator_params_v1_set_max_fall(image_desc_creator_param, 5.0f);
	client_roundtrip(client); /* make sure connection is still valid */
	wp_image_description_creator_params_v1_set_max_fall(image_desc_creator_param, 5.0f);
	expect_protocol_error(client, &wp_image_description_creator_params_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
	wp_image_description_creator_params_v1_destroy(image_desc_creator_param);

	color_manager_fini(&cm);
	client_destroy(client);

	return RESULT_OK;
}

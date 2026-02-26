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

#include <fcntl.h>
#include <sys/stat.h>

#include "color-manager-client.h"
#include "weston-test-assert.h"
#include "weston-test-runner.h"
#include "weston-test-client-helper.h"

#include <wayland-client-protocol.h>

#include "shared/helpers.h"
#include "shared/xalloc.h"

enum image_descr_info_event {
	IMAGE_DESCR_INFO_EVENT_ICC_FD = 1,
	IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED,
	IMAGE_DESCR_INFO_EVENT_PRIMARIES,
	IMAGE_DESCR_INFO_EVENT_TF_NAMED,
	IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP,
	IMAGE_DESCR_INFO_EVENT_LUMINANCES,
	IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES,
	IMAGE_DESCR_INFO_EVENT_TARGET_MAXCLL,
	IMAGE_DESCR_INFO_EVENT_TARGET_MAXFALL,
	IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE,
};

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

static struct image_description *
image_description_from_proxy(struct wp_image_description_v1 *proxy)
{
	struct image_description *image_desc;

	image_desc = xzalloc(sizeof *image_desc);
	image_desc->proxy = proxy;
	wp_image_description_v1_add_listener(proxy, &image_desc_iface, image_desc);

	return image_desc;
}

struct image_description *
image_description_from_param(struct wp_image_description_creator_params_v1 *creator)
{
	struct wp_image_description_v1 *proxy;

	proxy = wp_image_description_creator_params_v1_create(creator);
	return image_description_from_proxy(proxy);
}

void
image_description_destroy(struct image_description *image_desc)
{
	wp_image_description_v1_destroy(image_desc->proxy);
	free(image_desc);
}


struct image_description *
image_description_create_for_output(struct color_manager_client *cm,
				    struct output *output)
{
	struct wp_image_description_v1 *proxy;
	struct wp_color_management_output_v1 *cm_output;

	cm_output = wp_color_manager_v1_get_output(cm->manager_proxy, output->wl_output);
	proxy = wp_color_management_output_v1_get_image_description(cm_output);
	wp_color_management_output_v1_destroy(cm_output);

	return image_description_from_proxy(proxy);
}

struct image_description *
image_description_create_for_preferred(struct color_manager_client *cm,
				       struct surface *surface)
{
	struct wp_image_description_v1 *proxy;
	struct wp_color_management_surface_feedback_v1 *cm_feedback;

	cm_feedback = wp_color_manager_v1_get_surface_feedback(cm->manager_proxy, surface->wl_surface);
	proxy = wp_color_management_surface_feedback_v1_get_preferred(cm_feedback);
	wp_color_management_surface_feedback_v1_destroy(cm_feedback);

	return image_description_from_proxy(proxy);
}

struct image_description *
image_description_create_for_icc(struct color_manager_client *cm,
				 const char *icc_file_path)
{
	struct wp_image_description_creator_icc_v1 *creator;
	struct wp_image_description_v1 *proxy;
	int32_t icc_fd;
	struct stat st;

	icc_fd = open(icc_file_path, O_RDONLY);
	test_assert_s32_ge(icc_fd, 0);

	test_assert_int_eq(fstat(icc_fd, &st), 0);

	creator = wp_color_manager_v1_create_icc_creator(cm->manager_proxy);
	wp_image_description_creator_icc_v1_set_icc_file(creator,
							 icc_fd, 0, st.st_size);
	close(icc_fd);
	proxy = wp_image_description_creator_icc_v1_create(creator);

	return image_description_from_proxy(proxy);
}

void
image_description_wait_until_ready(struct client *client,
				   struct image_description *image_descr)
{
	while (image_descr->status == CM_IMAGE_DESC_NOT_CREATED)
		if (!test_assert_int_ge(wl_display_dispatch(client->wl_display), 0))
			break;

	test_assert_enum(image_descr->status, CM_IMAGE_DESC_READY);
}

static void
image_descr_info_received(struct image_description_info *image_descr_info,
			  enum image_descr_info_event ev)
{
	test_assert_bit_not_set(image_descr_info->events_received, bit(ev));
	image_descr_info->events_received |= bit(ev);
}

static void
image_descr_info_primaries(void *data,
			   struct wp_image_description_info_v1 *wp_image_description_info_v1,
			   int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y,
			   int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_PRIMARIES);

	weston_color_gamut_from_protocol(&info->primaries, r_x, r_y, g_x, g_y, b_x, b_y, w_x, w_y);
}

static void
image_descr_info_primaries_named(void *data,
				 struct wp_image_description_info_v1 *wp_image_description_info_v1,
				 uint32_t primaries)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED);
	info->primaries_named = primaries;
}

static void
image_descr_info_tf_named(void *data,
			  struct wp_image_description_info_v1 *wp_image_description_info_v1,
			  uint32_t tf)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_TF_NAMED);
	info->tf_named = tf;
}

static void
image_descr_info_tf_power(void *data,
			  struct wp_image_description_info_v1 *wp_image_description_info_v1,
			  uint32_t tf_power)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP);
	info->tf_power = tf_power / 10000.0;
}

static void
image_descr_info_luminances(void *data,
			    struct wp_image_description_info_v1 *wp_image_description_info_v1,
			    uint32_t min_lum, uint32_t max_lum, uint32_t ref_lum)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_LUMINANCES);

	info->min_lum = min_lum / 10000.0;
	info->max_lum = max_lum;
	info->ref_lum = ref_lum;
}

static void
image_descr_info_target_primaries(void *data,
				  struct wp_image_description_info_v1 *wp_image_description_info_v1,
				  int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y,
				  int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES);

	weston_color_gamut_from_protocol(&info->target_primaries,
					 r_x, r_y, g_x, g_y, b_x, b_y, w_x, w_y);
}

static void
image_descr_info_target_luminance(void *data,
				  struct wp_image_description_info_v1 *wp_image_description_info_v1,
				  uint32_t min_lum, uint32_t max_lum)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE);

	info->target_min_lum = min_lum / 10000.0;
	info->target_max_lum = max_lum;
}

static void
image_descr_info_target_max_cll(void *data,
				struct wp_image_description_info_v1 *wp_image_description_info_v1,
				uint32_t maxCLL)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_TARGET_MAXCLL);
	info->target_max_cll = maxCLL;
}

static void
image_descr_info_target_max_fall(void *data,
				 struct wp_image_description_info_v1 *wp_image_description_info_v1,
				 uint32_t maxFALL)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_TARGET_MAXFALL);
	info->target_max_fall = maxFALL;
}

static void
image_descr_info_icc_file_event(void *data,
				struct wp_image_description_info_v1 *wp_image_description_info_v1,
				int32_t icc_fd, uint32_t icc_size)
{
	struct image_description_info *info = data;

	image_descr_info_received(info, IMAGE_DESCR_INFO_EVENT_ICC_FD);

	info->icc_fd = icc_fd;
	info->icc_size = icc_size;
}

static bool
are_events_received_valid(struct image_description_info *info)
{
	uint32_t events = info->events_received;

	/* ICC-based image description... */
	if (has_bit(events, IMAGE_DESCR_INFO_EVENT_ICC_FD)) {
		/* ...so we shouldn't have receive any other events. */
		if (bit(IMAGE_DESCR_INFO_EVENT_ICC_FD) == events)
			return true;
		testlog("    Error: ICC image description but also received " \
			"parametric events\n");
		return false;
	}

	/* Non-ICC based image description, let's make sure that the received
	 * parameters make sense. */
	bool received_primaries, received_primaries_named;
	bool received_tf_power, received_tf_named;

	/* Should have received the primaries somewhow. */
	received_primaries_named = has_bit(events, IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED);
	received_primaries = has_bit(events, IMAGE_DESCR_INFO_EVENT_PRIMARIES);
	if (!(received_primaries_named || received_primaries)) {
		testlog("    Error: parametric image description but no " \
			"primaries received\n");
		return false;
	}

	/* Should have received tf somehow. */
	received_tf_named = has_bit(events, IMAGE_DESCR_INFO_EVENT_TF_NAMED);
	received_tf_power = has_bit(events, IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP);
	if (!(received_tf_named || received_tf_power)) {
		testlog("    Error: parametric image description but no " \
			" tf received\n");
		return false;
	}

	/* If we received tf named and exp power, they must match. */
	if (received_tf_named && received_tf_power) {
		if (info->tf_named != WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22 &&
		    info->tf_named != WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA28) {
			testlog("    Error: parametric image description tf " \
				"named is not pure power-law, but still received " \
				"tf power event\n");
			return false;
		} else if (info->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22 &&
			   info->tf_power != 2.2f) {
			testlog("    Error: parametric image description tf named " \
				"is pure power-law 2.2, but tf power received is %f\n",
				info->tf_power);
			return false;
		} else if (info->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA28 &&
			   info->tf_power != 2.8f) {
			testlog("    Error: parametric image description tf named " \
				"is pure power-law 2.8, but tf power received is %f\n",
				info->tf_power);
			return false;
		}
	}

	/* We should receive luminance. */
	if (!has_bit(events, IMAGE_DESCR_INFO_EVENT_LUMINANCES)) {
		testlog("    Error: parametric image description but no " \
			"luminances received\n");
		return false;
	}

	/* We should receive target primaries. */
	if (!has_bit(events, IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES)) {
		testlog("    Error: parametric image description but no " \
			"target primaries received\n");
		return false;
	}

	/* We should receive target luminance. */
	if (!has_bit(events, IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE)) {
		testlog("    Error: parametric image description but no " \
			"target luminances received\n");
		return false;
	}

	return true;
}

static void
testlog_gamut(const char *title, const struct weston_color_gamut *g, int indent)
{
	static const char *const chan[] = { "red", "green", "blue", "white point" };
	const struct weston_CIExy *coord = &g->primary[0];
	int i;

	testlog("%*s%s\n", indent, "", title);
	indent += 4;

	for (i = 0; i < 4; i++) {
		testlog("%*s%11s (x, y) = (%.4f, %.4f)\n",
			indent, "", chan[i], coord[i].x, coord[i].y);
	}
}

static void
image_descr_info_done(void *data,
		      struct wp_image_description_info_v1 *wp_image_description_info_v1)
{
	struct image_description_info *info = data;

	info->done = true;
	testlog("Image description info %p done:\n", wp_image_description_info_v1);

	test_assert_true(are_events_received_valid(info));

	/* ICC based image description */
	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_ICC_FD)) {
		testlog("    ICC file: fd %d, icc size %u.\n",
			info->icc_fd, info->icc_size);
		close(info->icc_fd);
		return;
	}

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_PRIMARIES_NAMED))
		testlog("    Primaries named: %u\n", info->primaries_named);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_PRIMARIES))
		testlog_gamut("Primary primaries:", &info->primaries, 4);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_TF_NAMED))
		testlog("    Transfer characteristics named: %u\n", info->tf_named);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_TF_POWER_EXP))
		testlog("    EOTF is a pure power-law curve of exp %.4f\n", info->tf_power);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_TARGET_PRIMARIES))
		testlog_gamut("Target primaries:", &info->target_primaries, 4);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_TARGET_LUMINANCE))
		testlog("    Target luminance: min: %.4f, max %.4f\n",
			info->target_min_lum, info->target_max_lum);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_TARGET_MAXCLL))
		testlog("    Target maxCLL: %.4f\n", info->target_max_cll);

	if (has_bit(info->events_received, IMAGE_DESCR_INFO_EVENT_TARGET_MAXFALL))
		testlog("    Target maxFALL: %.4f\n", info->target_max_fall);
}

static const struct wp_image_description_info_v1_listener
image_descr_info_iface = {
	.primaries = image_descr_info_primaries,
	.primaries_named = image_descr_info_primaries_named,
	.tf_named = image_descr_info_tf_named,
	.tf_power = image_descr_info_tf_power,
	.luminances = image_descr_info_luminances,
	.target_primaries = image_descr_info_target_primaries,
	.target_luminance = image_descr_info_target_luminance,
	.target_max_cll = image_descr_info_target_max_cll,
	.target_max_fall = image_descr_info_target_max_fall,
	.icc_file = image_descr_info_icc_file_event,
	.done = image_descr_info_done,
};

void
image_description_info_destroy(struct image_description_info *info)
{
	wp_image_description_info_v1_destroy(info->wp_image_description_info);

	if (info->icc_fd >= 0)
		close(info->icc_fd);

	free(info);
}

struct image_description_info *
image_description_get_information(struct client *client,
				  struct image_description *image_descr)
{
	struct image_description_info *info;

	info = xzalloc(sizeof(*info));
	info->icc_fd = -1;

	info->wp_image_description_info =
		wp_image_description_v1_get_information(image_descr->proxy);

	wp_image_description_info_v1_add_listener(info->wp_image_description_info,
						  &image_descr_info_iface,
						  info);

	while (!info->done)
		if (!test_assert_int_ge(wl_display_dispatch(client->wl_display), 0))
			break;

	return info;
}

/*
 * Copyright 2023 Collabora, Ltd.
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

#include "color.h"
#include "color-management.h"
#include <libweston/libweston.h>
#include "shared/string-helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"
#include "shared/helpers.h"

#include <fcntl.h>

#include "color-management-v1-server-protocol.h"

enum supports_get_info {
	NO_GET_INFO = false,
	YES_GET_INFO = true,
};

/**
 * Backs the image description abstraction from the protocol. We may have
 * multiple images descriptions for the same color profile.
 *
 * Image description that we failed to create do not have such backing object.
 */
struct cm_image_desc {
	struct wl_resource *owner;
	struct weston_color_manager *cm;

	/* Reference to the color profile that it is backing up. An image
	 * description without a cprof is valid, and that simply means that it
	 * isn't ready (i.e. we didn't send the 'ready' event because we are
	 * still in the process of creating the color profile). */
	struct weston_color_profile *cprof;

	/* Depending how the image description is created, the protocol states
	 * that get_information() request should be invalid. */
	bool supports_get_info;
};

/**
 * Object created when get_info() is called for an image description object. It
 * gets destroyed when all the info is sent, i.e. with the done() event.
 */
struct cm_image_desc_info {
	struct wl_resource *owner;
	struct weston_compositor *compositor;
};

/**
 * Backs protocol objects that are used to create ICC-based image descriptions.
 */
struct cm_creator_icc {
	struct wl_resource *owner;

	struct weston_compositor *compositor;

	/* ICC profile data given by the client. */
	int32_t icc_profile_fd;
	size_t icc_data_length;
	size_t icc_data_offset;
};

/**
 * Backs protocol objects that are used to create parametric image descriptions.
 */
struct cm_creator_params {
	struct wl_resource *owner;
	struct weston_compositor *compositor;

	/* This accumulates the parameters given by the clients. */
	struct weston_color_profile_param_builder *builder;
};

/**
 * For an ICC-based image description, sends the ICC information to the
 * client.
 *
 * If callers fail to create the fd for the ICC, they can call this function
 * with fd == -1 and it should return the proper error to clients.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param fd The ICC profile file descriptor, or -1 in case of a failure
 * \param len The ICC profile size, in bytes
 */
WL_EXPORT void
weston_cm_send_icc_file(struct cm_image_desc_info *cm_image_desc_info,
			int32_t fd, uint32_t len)
{
	/* Caller failed to create fd. At this point we already know that the
	 * ICC is valid, so let's disconnect the client with OOM. */
	if (fd < 0) {
		wl_resource_post_no_memory(cm_image_desc_info->owner);
		return;
	}

	wp_image_description_info_v1_send_icc_file(cm_image_desc_info->owner,
						   fd, len);
}

/**
 * For a parametric image description, sends its
 * enum wp_color_manager_v1_primaries code to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param primaries_info The primaries_info object
 */
WL_EXPORT void
weston_cm_send_primaries_named(struct cm_image_desc_info *cm_image_desc_info,
			       const struct weston_color_primaries_info *primaries_info)
{
	wp_image_description_info_v1_send_primaries_named(cm_image_desc_info->owner,
							  primaries_info->protocol_primaries);
}

/**
 * For a parametric image description, sends the primary color volume primaries
 * and white point using CIE 1931 xy chromaticity coordinates to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param color_gamut The CIE 1931 xy chromaticity coordinates
 */
WL_EXPORT void
weston_cm_send_primaries(struct cm_image_desc_info *cm_image_desc_info,
			 const struct weston_color_gamut *color_gamut)
{
	wp_image_description_info_v1_send_primaries(cm_image_desc_info->owner,
						    /* red */
						    round(color_gamut->primary[0].x * 1000000),
						    round(color_gamut->primary[0].y * 1000000),
						    /* green */
						    round(color_gamut->primary[1].x * 1000000),
						    round(color_gamut->primary[1].y * 1000000),
						    /* blue */
						    round(color_gamut->primary[2].x * 1000000),
						    round(color_gamut->primary[2].y * 1000000),
						    /* white point */
						    round(color_gamut->white_point.x * 1000000),
						    round(color_gamut->white_point.y * 1000000));
}

/**
 * For a parametric image description, sends the target color volume primaries
 * and white point using CIE 1931 xy chromaticity coordinates to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param color_gamut The CIE 1931 xy chromaticity coordinates
 */
WL_EXPORT void
weston_cm_send_target_primaries(struct cm_image_desc_info *cm_image_desc_info,
				const struct weston_color_gamut *color_gamut)
{
	wp_image_description_info_v1_send_target_primaries(cm_image_desc_info->owner,
							   /* red */
							   round(color_gamut->primary[0].x * 1000000),
							   round(color_gamut->primary[0].y * 1000000),
							   /* green */
							   round(color_gamut->primary[1].x * 1000000),
							   round(color_gamut->primary[1].y * 1000000),
							   /* blue */
							   round(color_gamut->primary[2].x * 1000000),
							   round(color_gamut->primary[2].y * 1000000),
							   /* white point */
							   round(color_gamut->white_point.x * 1000000),
							   round(color_gamut->white_point.y * 1000000));
}

/**
 * For a parametric image description, sends its
 * enum wp_color_manager_v1_transfer_function code to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param tf_info The tf_info object
 */
WL_EXPORT void
weston_cm_send_tf_named(struct cm_image_desc_info *cm_image_desc_info,
			const struct weston_color_tf_info *tf_info)
{
	wp_image_description_info_v1_send_tf_named(cm_image_desc_info->owner,
						   tf_info->protocol_tf);
}

/**
 * For a parametric image description, sends the primary luminances
 * to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param min_lum The minimum luminance (cd/m²)
 * \param max_lum The maximum luminance (cd/m²)
 * \param ref_lum The reference white luminance (cd/m²)
 */
WL_EXPORT void
weston_cm_send_luminances(struct cm_image_desc_info *cm_image_desc_info,
			  float min_lum, float max_lum, float ref_lum)
{
	wp_image_description_info_v1_send_luminances(cm_image_desc_info->owner,
						     min_lum * 10000,
						     max_lum, ref_lum);
}

/**
 * For a parametric image description, sends the target luminances
 * to the client.
 *
 * This is a helper function that should be used by the color plugin
 * that owns the color profile and has information about it.
 *
 * \param cm_image_desc_info The image description info object
 * \param min_lum The minimum target luminance (cd/m²)
 * \param max_lum The maximum target luminance (cd/m²)
 */
WL_EXPORT void
weston_cm_send_target_luminances(struct cm_image_desc_info *cm_image_desc_info,
				 float min_lum, float max_lum)
{
	wp_image_description_info_v1_send_target_luminance(cm_image_desc_info->owner,
							   min_lum * 10000,
							   max_lum);
}

/**
 * Destroy an image description info object.
 */
static void
cm_image_desc_info_destroy(struct cm_image_desc_info *cm_image_desc_info)
{
	free(cm_image_desc_info);
}

/**
 * Resource destruction function for the image description info. Destroys the
 * image description info backing object.
 */
static void
image_description_info_resource_destroy(struct wl_resource *cm_image_desc_info_res)
{
	struct cm_image_desc_info *cm_image_desc_info =
		wl_resource_get_user_data(cm_image_desc_info_res);

	cm_image_desc_info_destroy(cm_image_desc_info);
}

/**
 * Creates object to send information of a certain image description.
 */
static struct cm_image_desc_info *
image_description_info_create(struct wl_client *client, uint32_t version,
			      struct weston_compositor *compositor,
			      uint32_t cm_image_desc_info_id)
{
	struct cm_image_desc_info *cm_image_desc_info;

	cm_image_desc_info = xzalloc(sizeof(*cm_image_desc_info));

	cm_image_desc_info->compositor = compositor;

	cm_image_desc_info->owner =
		wl_resource_create(client, &wp_image_description_info_v1_interface,
				   version, cm_image_desc_info_id);
	if (!cm_image_desc_info->owner) {
		free(cm_image_desc_info);
		return NULL;
	}

	wl_resource_set_implementation(cm_image_desc_info->owner,
				       NULL, cm_image_desc_info,
				       image_description_info_resource_destroy);

	return cm_image_desc_info;
}

/**
 * Client wants the image description information.
 */
static void
image_description_get_information(struct wl_client *client,
				  struct wl_resource *cm_image_desc_res,
				  uint32_t cm_image_desc_info_id)
{
	struct cm_image_desc *cm_image_desc =
		wl_resource_get_user_data(cm_image_desc_res);
	uint32_t version = wl_resource_get_version(cm_image_desc_res);
	struct cm_image_desc_info *cm_image_desc_info;
	bool success;

	if (!cm_image_desc) {
		wl_resource_post_error(cm_image_desc_res,
				       WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY,
				       "we gracefully failed to create this image " \
				       "description");
		return;
	}

	if (!cm_image_desc->cprof) {
		wl_resource_post_error(cm_image_desc_res,
				       WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY,
				       "image description not ready yet");
		return;
	}

	if (!cm_image_desc->supports_get_info) {
		wl_resource_post_error(cm_image_desc_res,
				       WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION,
				       "get_information is not allowed for this "
				       "image description");
		return;
	}

	cm_image_desc_info =
		image_description_info_create(client, version,
					      cm_image_desc->cm->compositor,
					      cm_image_desc_info_id);
	if (!cm_image_desc_info) {
		wl_resource_post_no_memory(cm_image_desc_res);
		return;
	}

	/* The color plugin is the one that has information about the color
	 * profile, so we go through it to send the info to clients. */
	success = cm_image_desc->cm->send_image_desc_info(cm_image_desc_info,
							  cm_image_desc->cprof);
	if (success)
		wp_image_description_info_v1_send_done(cm_image_desc_info->owner);

	/* All info sent, so destroy the object. */
	wl_resource_destroy(cm_image_desc_info->owner);
}

/**
 * Client will not use the image description anymore, so we destroy its
 * resource.
 */
static void
image_description_destroy(struct wl_client *client,
			  struct wl_resource *cm_image_desc_res)
{
	wl_resource_destroy(cm_image_desc_res);
}

static void
cm_image_desc_destroy(struct cm_image_desc *cm_image_desc);

/**
 * Resource destruction function for the image description. Destroys the image
 * description backing object.
 */
static void
image_description_resource_destroy(struct wl_resource *cm_image_desc_res)
{
	struct cm_image_desc *cm_image_desc =
		wl_resource_get_user_data(cm_image_desc_res);

	/* Image description that we failed to create do not have a backing
	 * struct cm_image_desc object. */
	if (!cm_image_desc)
		return;

	cm_image_desc_destroy(cm_image_desc);
}

static const struct wp_image_description_v1_interface
image_description_implementation = {
	.destroy = image_description_destroy,
	.get_information = image_description_get_information,
};

/**
 * Creates an image description object for a certain color profile.
 */
static struct cm_image_desc *
cm_image_desc_create(struct weston_color_manager *cm,
		     struct weston_color_profile *cprof,
		     struct wl_client *client, uint32_t version,
		     uint32_t image_description_id,
		     enum supports_get_info supports_get_info)
{
	struct cm_image_desc *cm_image_desc;

	cm_image_desc = xzalloc(sizeof(*cm_image_desc));

	cm_image_desc->owner =
		wl_resource_create(client, &wp_image_description_v1_interface,
				   version, image_description_id);
	if (!cm_image_desc->owner) {
		free(cm_image_desc);
		return NULL;
	}

	wl_resource_set_implementation(cm_image_desc->owner,
				       &image_description_implementation,
				       cm_image_desc,
				       image_description_resource_destroy);

	cm_image_desc->cm = cm;
	cm_image_desc->cprof = weston_color_profile_ref(cprof);
	cm_image_desc->supports_get_info = supports_get_info;

	return cm_image_desc;
}

/**
 * Destroy an image description object.
 */
static void
cm_image_desc_destroy(struct cm_image_desc *cm_image_desc)
{
	weston_color_profile_unref(cm_image_desc->cprof);
	free(cm_image_desc);
}

/**
 * Called by clients when they want to get the output's image description.
 */
static void
cm_output_get_image_description(struct wl_client *client,
				struct wl_resource *cm_output_res,
				uint32_t protocol_object_id)
{
	struct weston_head *head = wl_resource_get_user_data(cm_output_res);
	struct weston_compositor *compositor;
	struct weston_output *output;
	uint32_t version = wl_resource_get_version(cm_output_res);
	struct cm_image_desc *cm_image_desc;
	struct wl_resource *cm_image_desc_res;

	/* The protocol states that if the wl_output global (which is backed by
	 * the weston_head object) no longer exists, we should immediately send
	 * a "failed" event for the image desc. After receiving that, clients
	 * are not allowed to make requests other than "destroy" for the image
	 * description. For such image descriptions that we failed to create, we
	 * do not create a backing cm_image_desc (and other functions can tell
	 * that they are invalid through that). */
	if (!head) {
		cm_image_desc_res =
			wl_resource_create(client, &wp_image_description_v1_interface,
					   version, protocol_object_id);
		if (!cm_image_desc_res) {
			wl_resource_post_no_memory(cm_output_res);
			return;
		}

		wl_resource_set_implementation(cm_image_desc_res,
					       &image_description_implementation,
					       NULL, image_description_resource_destroy);

		wp_image_description_v1_send_failed(cm_image_desc_res,
						    WP_IMAGE_DESCRIPTION_V1_CAUSE_NO_OUTPUT,
						    "the wl_output global no longer exists");
		return;
	}

	compositor = head->compositor;
	output = head->output;

	/* If the head becomes inactive (head->output == NULL), the respective
	 * wl_output global gets destroyed. In such case we make the cm_output
	 * object inert. We do that in weston_head_remove_global(), and the
	 * cm_output_res user data (which was the head itself) is set to NULL.
	 * So if we reached here, head is active and head->output != NULL. */
	weston_assert_ptr_not_null(compositor, output);

	cm_image_desc = cm_image_desc_create(compositor->color_manager,
					     output->color_profile, client,
					     version, protocol_object_id,
					     YES_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(cm_output_res);
		return;
	}

	wp_image_description_v1_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
}

/**
 * Client will not use the cm_output anymore, so we destroy its resource.
 */
static void
cm_output_destroy(struct wl_client *client, struct wl_resource *cm_output_res)
{
	wl_resource_destroy(cm_output_res);
}

/**
 * Resource destruction function for the cm_output.
 */
static void
cm_output_resource_destroy(struct wl_resource *cm_output_res)
{
	struct weston_head *head = wl_resource_get_user_data(cm_output_res);

	/* For inert cm_output, we don't have to do anything.
	 *
	 * If the cm_get_output() was called after we made the head inactive, we
	 * created the cm_output with no resource user data and didn't add the
	 * resource link to weston_head::cm_output_resource_list.
	 *
	 * If the cm_output was created with an active head but it became
	 * inactive later, we have already done what was necessary when
	 * cm_output became inert, in weston_head_remove_global(). */
	if (!head)
		return;

	/* We are destroying the cm_output_res, so simply remove it from
	 * weston_head::cm_output_resource_list. */
	wl_list_remove(wl_resource_get_link(cm_output_res));
}

static const struct wp_color_management_output_v1_interface
cm_output_implementation = {
	.destroy = cm_output_destroy,
	.get_image_description = cm_output_get_image_description,
};

/**
 * Should be called when the struct weston_output color profile is updated.
 *
 * For each weston_head attached to the weston_output, we need to tell clients
 * that the cm_output image description has changed.
 *
 * If this is called during output initialization, this function is no-op. There
 * will be no client resources in weston_head::cm_output_resource_list.
 *
 * \param output The weston_output that changed the color profile.
 */
void
weston_output_send_image_description_changed(struct weston_output *output)
{
	struct weston_head *head;
	struct wl_resource *res;
	int ver;

	/* Send the events for each head attached to this weston_output. */
	wl_list_for_each(head, &output->head_list, output_link) {
		wl_resource_for_each(res, &head->cm_output_resource_list)
			wp_color_management_output_v1_send_image_description_changed(res);

		/* wl_output.done should be sent after collecting all the
		 * changes related to the output. But in Weston we are lacking
		 * an atomic output configuration API, so we have no facilities
		 * to do that.
		 *
		 * TODO: enhance this behavior after we add the atomic output
		 * configuration API.
		 */
		wl_resource_for_each(res, &head->resource_list) {
			ver = wl_resource_get_version(res);
			if (ver >= WL_OUTPUT_DONE_SINCE_VERSION)
				wl_output_send_done(res);
		}
	}
}

/**
 * Client called get_output(). We already have the backing object, so just
 * create a resource for the client.
 */
static void
cm_get_output(struct wl_client *client, struct wl_resource *cm_res,
	      uint32_t cm_output_id, struct wl_resource *output_res)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	uint32_t version = wl_resource_get_version(cm_res);
	struct wl_resource *res;

	res = wl_resource_create(client, &wp_color_management_output_v1_interface,
				 version, cm_output_id);
	if (!res) {
		wl_resource_post_no_memory(cm_res);
		return;
	}

	/* Client wants the cm_output but we've already made the head inactive,
	 * so let's set the implementation data as NULL (and other functions can
	 * tell that they are inert through that). */
	if (!head) {
		wl_resource_set_implementation(res, &cm_output_implementation,
					       NULL, cm_output_resource_destroy);
		return;
	}

	wl_resource_set_implementation(res, &cm_output_implementation,
				       head, cm_output_resource_destroy);

	wl_list_insert(&head->cm_output_resource_list,
		       wl_resource_get_link(res));
}

/**
 * Called by clients to update the image description of a surface.
 *
 * If the surface state is commited, libweston will update the struct
 * weston_surface color profile and render intent.
 */
static void
cm_surface_set_image_description(struct wl_client *client,
				 struct wl_resource *cm_surface_res,
				 struct wl_resource *cm_image_desc_res,
				 uint32_t protocol_render_intent)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_res);
	struct cm_image_desc *cm_image_desc =
		wl_resource_get_user_data(cm_image_desc_res);
	struct weston_color_manager *cm;
	const struct weston_render_intent_info *render_intent;

	/* The surface might have been already gone, in such case cm_surface is
	 * inert. */
	if (!surface) {
		wl_resource_post_error(cm_surface_res,
				       WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT,
				       "the wl_surface has already been destroyed");
		return;
	}

	/* Invalid image description for this request, as we gracefully failed
	 * to create it. */
	if (!cm_image_desc) {
		wl_resource_post_error(cm_surface_res,
				       WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION,
				       "we gracefully failed to create this image description");
		return;
	}

	/* Invalid image description for this request, as it isn't ready yet. */
	if (!cm_image_desc->cprof) {
		wl_resource_post_error(cm_surface_res,
				       WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION,
				       "the image description is not ready");
		return;
	}

	cm = cm_image_desc->cm;

	render_intent = weston_render_intent_info_from_protocol(surface->compositor,
								protocol_render_intent);
	if (!render_intent) {
		wl_resource_post_error(cm_surface_res,
				       WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_RENDER_INTENT,
				       "unknown render intent");
		return;
	}

	if (!((cm->supported_rendering_intents >> render_intent->intent) & 1)) {
		wl_resource_post_error(cm_surface_res,
				       WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_RENDER_INTENT,
				       "unsupported render intent");
		return;
	}

	weston_color_profile_unref(surface->pending.color_profile);
	surface->pending.color_profile =
		weston_color_profile_ref(cm_image_desc->cprof);
	surface->pending.render_intent = render_intent;
}

/**
 * Called by clients to unset the image description.
 *
 * If the surface state is commited, libweston will update the struct
 * weston_surface color profile and render intent.
 */
static void
cm_surface_unset_image_description(struct wl_client *client,
				   struct wl_resource *cm_surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_res);

	/* The surface might have been already gone, in such case cm_surface is
	 * inert. */
	if (!surface) {
		wl_resource_post_error(cm_surface_res,
				       WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT,
				       "the wl_surface has already been destroyed");
		return;
	}

	weston_color_profile_unref(surface->pending.color_profile);
	surface->pending.color_profile = NULL;
	surface->pending.render_intent = NULL;
}

/**
 * Client will not use the cm_surface anymore, so we destroy its resource.
 */
static void
cm_surface_destroy(struct wl_client *client, struct wl_resource *cm_surface_res)
{
	wl_resource_destroy(cm_surface_res);
}

/**
 * Resource destruction function for the cm_surface.
 */
static void
cm_surface_resource_destroy(struct wl_resource *cm_surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_res);

	/* For inert cm_surface, we don't have to do anything.
	 *
	 * We already did what was necessary when cm_surface became inert, in
	 * the surface destruction process (in weston_surface_unref(), which
	 * is the surface destruction function). */
	if (!surface)
		return;

	surface->cm_surface = NULL;

	/* Do the same as unset_image_description */
	weston_color_profile_unref(surface->pending.color_profile);
	surface->pending.color_profile = NULL;
	surface->pending.render_intent = NULL;
}

static const struct wp_color_management_surface_v1_interface
cm_surface_implementation = {
	.destroy = cm_surface_destroy,
	.set_image_description = cm_surface_set_image_description,
	.unset_image_description = cm_surface_unset_image_description,
};

/**
 * Client called get_surface(). We already have the backing object, so just
 * create a resource for the client.
 */
static void
cm_get_surface(struct wl_client *client, struct wl_resource *cm_res,
	       uint32_t cm_surface_id, struct wl_resource *surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(surface_res);
	uint32_t version = wl_resource_get_version(cm_res);
	struct wl_resource *res;

	if (surface->cm_surface) {
		wl_resource_post_error(cm_res,
				       WP_COLOR_MANAGER_V1_ERROR_SURFACE_EXISTS,
				       "surface already requested");
		return;
	}

	res = wl_resource_create(client, &wp_color_management_surface_v1_interface,
				 version, cm_surface_id);
	if (!res) {
		wl_resource_post_no_memory(cm_res);
		return;
	}

	wl_resource_set_implementation(res, &cm_surface_implementation,
				       surface, cm_surface_resource_destroy);

	surface->cm_surface = res;
}

/**
 * Client will not use the cm_surface_feedback anymore, so we destroy its resource.
 */
static void
cm_surface_feedback_destroy(struct wl_client *client,
			    struct wl_resource *cm_surface_feedback_res)
{
	wl_resource_destroy(cm_surface_feedback_res);
}

/**
 * Called by clients when they want to know the preferred image description of
 * the surface.
 */
static void
cm_surface_feedback_get_preferred(struct wl_client *client,
				  struct wl_resource *cm_surface_feedback_res,
				  uint32_t protocol_object_id)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_feedback_res);
	uint32_t version = wl_resource_get_version(cm_surface_feedback_res);
	struct weston_color_manager *cm;
	struct cm_image_desc *cm_image_desc;

	/* The surface might have been already gone, in such case cm_surface_feedback is
	 * inert. */
	if (!surface) {
		wl_resource_post_error(cm_surface_feedback_res,
				       WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_ERROR_INERT,
				       "the wl_surface has already been destroyed");
		return;
	}

	cm = surface->compositor->color_manager;

	cm_image_desc = cm_image_desc_create(cm, surface->preferred_color_profile,
					     client, version, protocol_object_id,
					     YES_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(cm_surface_feedback_res);
		return;
	}

	wp_image_description_v1_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
}

static void
cm_surface_feedback_get_preferred_parametric(struct wl_client *client,
					     struct wl_resource *cm_surface_feedback_res,
					     uint32_t protocol_object_id)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_feedback_res);
	uint32_t version = wl_resource_get_version(cm_surface_feedback_res);
	struct weston_color_manager *cm;
	struct cm_image_desc *cm_image_desc;
	char *err_msg;

	/* The surface might have been already gone, in such case cm_surface_feedback is
	 * inert. */
	if (!surface) {
		wl_resource_post_error(cm_surface_feedback_res,
				       WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_ERROR_INERT,
				       "the wl_surface has already been destroyed");
		return;
	}

	cm = surface->compositor->color_manager;

	/* Create the image description with cprof == NULL. */
	cm_image_desc = cm_image_desc_create(cm, NULL, client, version,
					     protocol_object_id, YES_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(cm_surface_feedback_res);
		return;
	}

	cm_image_desc->cprof =
		cm->get_parametric_color_profile(surface->preferred_color_profile,
						 &err_msg);

	/* Failed to get a parametric cprof for surface preferred cprof. */
	if (!cm_image_desc->cprof) {
		wp_image_description_v1_send_failed(cm_image_desc->owner,
						    WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
						    err_msg);
		free(err_msg);

		/* Failed to create the image description, let's set the
		 * resource userdata to NULL (and other functions can tell that
		 * it is invalid through that). */
		wl_resource_set_user_data(cm_image_desc->owner, NULL);
		cm_image_desc_destroy(cm_image_desc);

		return;
	}

	wp_image_description_v1_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
}

static const struct wp_color_management_surface_feedback_v1_interface
cm_surface_feedback_implementation = {
	.destroy = cm_surface_feedback_destroy,
	.get_preferred = cm_surface_feedback_get_preferred,
	.get_preferred_parametric = cm_surface_feedback_get_preferred_parametric,
};

/**
 * Resource destruction function for the cm_surface_feedback.
 */
static void
cm_surface_feedback_resource_destroy(struct wl_resource *cm_surface_feedback_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(cm_surface_feedback_res);

	/* For inert cm_surface_feedback, we don't have to do anything.
	 *
	 * We already did what was necessary when cm_surface_feedback became
	 * inert, in the surface destruction process: weston_surface_unref(). */
	if (!surface)
		return;

	/* We are destroying the cm_surface_feedback_res, so simply remove it from
	 * weston_surface::cm_surface_feedback_resource_list. */
	wl_list_remove(wl_resource_get_link(cm_surface_feedback_res));
}

/**
 * Notifies clients that their surface preferred image description changed.
 *
 * \param surface The surface that changed its preferred image description.
 */
void
weston_surface_send_preferred_image_description_changed(struct weston_surface *surface)
{
	struct wl_resource *res;
	uint32_t id = surface->preferred_color_profile->id;

	wl_resource_for_each(res, &surface->cm_surface_feedback_resource_list)
		wp_color_management_surface_feedback_v1_send_preferred_changed(res, id);
}

/**
 * Client called get_surface_feedback(). We already have the backing object, so just
 * create a resource for the client.
 */
static void
cm_get_surface_feedback(struct wl_client *client, struct wl_resource *cm_res,
			uint32_t cm_surface_id, struct wl_resource *surface_res)
{
	struct weston_surface *surface = wl_resource_get_user_data(surface_res);
	uint32_t version = wl_resource_get_version(cm_res);
	struct wl_resource *res;

	res = wl_resource_create(client, &wp_color_management_surface_feedback_v1_interface,
				 version, cm_surface_id);
	if (!res) {
		wl_resource_post_no_memory(cm_res);
		return;
	}

	wl_resource_set_implementation(res, &cm_surface_feedback_implementation,
				       surface, cm_surface_feedback_resource_destroy);
	wl_list_insert(&surface->cm_surface_feedback_resource_list, wl_resource_get_link(res));
}

/**
 * Sets the ICC file for the ICC-based image description creator object.
 */
static void
cm_creator_icc_set_icc_file(struct wl_client *client,
			    struct wl_resource *resource,
			    int32_t icc_profile_fd,
			    uint32_t offset, uint32_t length)
{
	struct cm_creator_icc *cm_creator_icc = wl_resource_get_user_data(resource);
	int flags;
	uint32_t err_code;
	const char *err_msg;

	if (cm_creator_icc->icc_data_length > 0) {
		err_code = WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_ALREADY_SET;
		err_msg = "ICC file was already set";
		goto err;
	}

	if (length == 0 || length > (32 * 1024 * 1024)) {
		err_code = WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_SIZE;
		err_msg = "invalid ICC file size, should be in the " \
			  "(0, 32MB] interval";
		goto err;
	}

	flags = fcntl(icc_profile_fd, F_GETFL);
	if ((flags & O_ACCMODE) == O_WRONLY) {
		err_code = WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD;
		err_msg = "ICC fd is not readable";
		goto err;
	}

	if (lseek(icc_profile_fd, 0, SEEK_CUR) < 0) {
		err_code = WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD;
		err_msg = "ICC fd is not seekable";
		goto err;
	}

	cm_creator_icc->icc_profile_fd = icc_profile_fd;

	/* We save length and offset in size_t variables. This ensures that they
	 * fit. We received them as uint32_t from the protocol. */
	static_assert(UINT32_MAX <= SIZE_MAX,
		      "won't be able to save uint32_t var into size_t");
	cm_creator_icc->icc_data_length = length;
	cm_creator_icc->icc_data_offset = offset;

	return;

err:
	close(icc_profile_fd);
	wl_resource_post_error(resource, err_code, "%s", err_msg);
}

static bool
do_length_and_offset_fit(struct cm_creator_icc *cm_creator_icc)
{
	size_t end;
	off_t end_off;

	/* Ensure that length + offset doesn't overflow in size_t. If that isn't
	 * true, we won't be able to make it fit into off_t. And we may need
	 * that to read the ICC file. */
	if (cm_creator_icc->icc_data_length > SIZE_MAX - cm_creator_icc->icc_data_offset)
		return false;

	/* Ensure that length + offset doesn't overflow in off_t. */
	end = cm_creator_icc->icc_data_offset + cm_creator_icc->icc_data_length;
	end_off = end;
	if (end_off < 0 || end != (size_t)end_off)
		return false;

	return true;
}

static int
create_image_description_color_profile_from_icc_creator(struct cm_image_desc *cm_image_desc,
							struct cm_creator_icc *cm_creator_icc)
{
	struct weston_compositor *compositor = cm_creator_icc->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	struct weston_color_profile *cprof;
	char *err_msg;
	void *icc_prof_data;
	size_t bytes_read = 0;
	ssize_t pread_ret;
	bool ret;

	if (!do_length_and_offset_fit(cm_creator_icc)) {
		wp_image_description_v1_send_failed(cm_image_desc->owner,
						    WP_IMAGE_DESCRIPTION_V1_CAUSE_OPERATING_SYSTEM,
						    "length + offset does not fit off_t");
		return -1;
	}

	/* Create buffer to read ICC profile. As they may have up to 32MB, we
	 * send OOM if something fails (instead of using xalloc). */
	icc_prof_data = zalloc(cm_creator_icc->icc_data_length);
	if (!icc_prof_data) {
		wl_resource_post_no_memory(cm_creator_icc->owner);
		return -1;
	}

	/* Read ICC file.
	 *
	 * TODO: it is not that simple. Clients can abuse that to DoS the
	 * compositor. See the discussion in the link below.
	 *
	 * https://gitlab.freedesktop.org/wayland/weston/-/merge_requests/1356#note_2125102
	 */
	while (bytes_read < cm_creator_icc->icc_data_length) {
		pread_ret = pread(cm_creator_icc->icc_profile_fd,
				  icc_prof_data + bytes_read,
				  cm_creator_icc->icc_data_length - bytes_read,
				  (off_t)cm_creator_icc->icc_data_offset + bytes_read);
		if (pread_ret < 0) {
			/* Interruption, so continue trying to read. */
			if (errno == EINTR)
				continue;

			/* Reading the ICC failed */
			free(icc_prof_data);
			str_printf(&err_msg, "failed to read ICC file: %s", strerror(errno));
			wp_image_description_v1_send_failed(cm_image_desc->owner,
							    WP_IMAGE_DESCRIPTION_V1_CAUSE_OPERATING_SYSTEM,
							    err_msg);
			free(err_msg);
			return -1;
		} else if (pread_ret == 0) {
			/* We were expecting to read more than 0 bytes, but we
			 * didn't. That means that we've tried to read beyond
			 * EOF. This is client's fault, it must make sure that
			 * the given ICC file don't simply change. */
			free(icc_prof_data);
			wl_resource_post_error(cm_creator_icc->owner,
					       WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_OUT_OF_FILE,
					       "tried to read ICC beyond EOF");
			return -1;
		}
		bytes_read += (size_t)pread_ret;
	}
	weston_assert_true(compositor, bytes_read == cm_creator_icc->icc_data_length);

	ret = cm->get_color_profile_from_icc(cm, icc_prof_data,
					     cm_creator_icc->icc_data_length,
					     "icc-from-client", &cprof, &err_msg);
	free(icc_prof_data);

	if (!ret) {
		/* We can't tell if it is client's fault that the ICC profile is
		 * invalid, so let's gracefully fail without returning a
		 * protocol error.
		 *
		 * TODO: we need to return proper error codes from the
		 * color-manager plugins and decide if we should gracefully fail
		 * or return a protocol error.
		 */
		wp_image_description_v1_send_failed(cm_image_desc->owner,
						    WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
						    err_msg);
		free(err_msg);
		return -1;
	}

	cm_image_desc->cprof = cprof;
	wp_image_description_v1_send_ready(cm_image_desc->owner,
					   cm_image_desc->cprof->id);
	return 0;
}

/**
 * Creates image description using the ICC-based image description creator
 * object. This is a destructor type request, so the cm_creator_icc resource
 * gets destroyed after this.
 */
static void
cm_creator_icc_create(struct wl_client *client, struct wl_resource *resource,
		      uint32_t image_description_id)
{
	struct cm_creator_icc *cm_creator_icc =
		wl_resource_get_user_data(resource);
	struct weston_compositor *compositor = cm_creator_icc->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	uint32_t version = wl_resource_get_version(cm_creator_icc->owner);
	struct cm_image_desc *cm_image_desc;
	int ret;

	if (cm_creator_icc->icc_data_length == 0) {
		wl_resource_post_error(resource,
				       WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_INCOMPLETE_SET,
				       "trying to create image description before " \
				       "setting the ICC file");
		return;
	}

	/* Create the image description with cprof == NULL. */
	cm_image_desc = cm_image_desc_create(cm, NULL, client, version,
					     image_description_id, NO_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(resource);
		return;
	}

	/* Create the cprof for the image description. */
	ret = create_image_description_color_profile_from_icc_creator(cm_image_desc,
								      cm_creator_icc);
	if (ret < 0) {
		/* Failed to create the image description, let's set the
		 * resource userdata to NULL (and other functions can tell that
		 * it is invalid through that). */
		wl_resource_set_user_data(cm_image_desc->owner, NULL);
		cm_image_desc_destroy(cm_image_desc);
	}

	/* Destroy the cm_creator_icc resource. This is a destructor request. */
	wl_resource_destroy(cm_creator_icc->owner);
}

/**
 * Resource destruction function for the cm_creator_icc.
 * It should only destroy itself, but not the image description it creates.
 */
static void
cm_creator_icc_destructor(struct wl_resource *resource)
{
	struct cm_creator_icc *cm_creator_icc =
		wl_resource_get_user_data(resource);

	if (cm_creator_icc->icc_profile_fd >= 0)
		close(cm_creator_icc->icc_profile_fd);

	free(cm_creator_icc);
}

static const struct wp_image_description_creator_icc_v1_interface
cm_creator_icc_implementation = {
	.create = cm_creator_icc_create,
	.set_icc_file = cm_creator_icc_set_icc_file,
};

/**
 * Creates an ICC-based image description creator for the client.
 */
static void
cm_create_image_description_creator_icc(struct wl_client *client,
					struct wl_resource *cm_res,
					uint32_t cm_creator_icc_id)
{
	struct cm_creator_icc *cm_creator_icc;
	struct weston_compositor *compositor = wl_resource_get_user_data(cm_res);
	struct weston_color_manager *cm = compositor->color_manager;
	uint32_t version = wl_resource_get_version(cm_res);

	if (!((cm->supported_color_features >> WESTON_COLOR_FEATURE_ICC) & 1)) {
		wl_resource_post_error(cm_res, WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
				       "creating ICC image descriptions is not supported");
		return;
	}

	cm_creator_icc = xzalloc(sizeof(*cm_creator_icc));

	cm_creator_icc->compositor = compositor;
	cm_creator_icc->icc_profile_fd = -1;

	cm_creator_icc->owner =
		wl_resource_create(client, &wp_image_description_creator_icc_v1_interface,
				   version, cm_creator_icc_id);
	if (!cm_creator_icc->owner)
		goto err;

	wl_resource_set_implementation(cm_creator_icc->owner, &cm_creator_icc_implementation,
				       cm_creator_icc, cm_creator_icc_destructor);

	return;

err:
	free(cm_creator_icc);
	wl_resource_post_no_memory(cm_res);
}

/**
 * Convert from param builder error to protocol error.
 *
 * If the error does not have a protocol counterpart, this returns -1.
 */
static int32_t
cm_creator_params_error_to_protocol(struct weston_compositor *compositor,
				    enum weston_color_profile_param_builder_error err)
{
	switch(err) {
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_TF:
		return WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF;
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_PRIMARIES_NAMED:
		return WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_PRIMARIES_NAMED;
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_LUMINANCE:
		return WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE;
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCOMPLETE_SET:
		return WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INCOMPLETE_SET;
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET:
		return WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET;
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_UNSUPPORTED:
		return WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_UNSUPPORTED_FEATURE;
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_CREATE_FAILED:
	case WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_CIE_XY_OUT_OF_RANGE:
		/* These are not protocol errors, but should result in graceful
		 * failures when creating the image description. */
		return -1;
	}

	weston_assert_not_reached(compositor, "unknown params profile builder error");
}

/**
 * Used by cm_creator_params setters to post protocol errors.
 *
 * Errors that should not result in a protocol error are not posted. These are
 * graceful failures that we handle in cm_creator_params_create().
 */
static void
cm_creator_params_post_protocol_error(struct cm_creator_params *cm_creator_params)
{
	struct weston_compositor *compositor = cm_creator_params->compositor;
	enum weston_color_profile_param_builder_error err;
	int32_t protocol_err;
	char *err_msg;

	if (!weston_color_profile_param_builder_get_error(cm_creator_params->builder,
							  &err, &err_msg))
		return;

	protocol_err = cm_creator_params_error_to_protocol(compositor, err);
	if (protocol_err >= 0)
		wl_resource_post_error(cm_creator_params->owner,
				       protocol_err, "%s", err_msg);

	free(err_msg);
}

/**
 * Set named primaries for parametric-based image description creator object.
 */
static void
cm_creator_params_set_primaries_named(struct wl_client *client, struct wl_resource *resource,
				      uint32_t primaries_named)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);
	const struct weston_color_primaries_info *primaries_info;

	primaries_info = weston_color_primaries_info_from_protocol(primaries_named);
	if (!primaries_info) {
		wl_resource_post_error(resource,
				       WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_PRIMARIES_NAMED,
				       "invalid primaries named: %u", primaries_named);
		return;
	}

	if (!weston_color_profile_param_builder_set_primaries_named(cm_creator_params->builder,
								    primaries_info->primaries))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set primaries for parametric-based image description creator object.
 *
 * The primaries we receive from clients are multiplied by 1000000.
 */
static void
cm_creator_params_set_primaries(struct wl_client *client, struct wl_resource *resource,
				int32_t r_x, int32_t r_y,
				int32_t g_x, int32_t g_y,
				int32_t b_x, int32_t b_y,
				int32_t w_x, int32_t w_y)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);
	struct weston_color_gamut primaries;

	primaries.primary[0].x = r_x / 1000000.0f;
	primaries.primary[0].y = r_y / 1000000.0f;
	primaries.primary[1].x = g_x / 1000000.0f;
	primaries.primary[1].y = g_y / 1000000.0f;
	primaries.primary[2].x = b_x / 1000000.0f;
	primaries.primary[2].y = b_y / 1000000.0f;
	primaries.white_point.x = w_x / 1000000.0f;
	primaries.white_point.y = w_y / 1000000.0f;

	if (!weston_color_profile_param_builder_set_primaries(cm_creator_params->builder,
							      &primaries))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set tf named for parametric-based image description creator object.
 */
static void
cm_creator_params_set_tf_named(struct wl_client *client, struct wl_resource *resource,
			       uint32_t tf_named)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);
	const struct weston_color_tf_info *tf_info;

	tf_info = weston_color_tf_info_from_protocol(tf_named);
	if (!tf_info) {
		wl_resource_post_error(resource,
				       WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
				       "invalid tf named: %u", tf_named);
		return;
	}

	if (!weston_color_profile_param_builder_set_tf_named(cm_creator_params->builder,
							     tf_info->tf))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set tf power for parametric-based image description creator object.
 *
 * The exponent we receive from clients is multiplied by 10000.
 */
static void
cm_creator_params_set_tf_power(struct wl_client *client, struct wl_resource *resource,
			       uint32_t exp)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);

	if (!weston_color_profile_param_builder_set_tf_power_exponent(cm_creator_params->builder,
								      exp / 10000.0f))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set primary luminance for parametric-based image description creator object.
 *
 * The min luminance we receive from clients is multiplied by 10000.
 */
static void
cm_creator_params_set_luminances(struct wl_client *client, struct wl_resource *resource,
				 uint32_t min_lum, uint32_t max_lum,
				 uint32_t reference_lum)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);

	if (!weston_color_profile_param_builder_set_primary_luminance(cm_creator_params->builder,
								      reference_lum,
								      min_lum / 10000.f, max_lum))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set mastering display primaries for parametric-based image description creator object.
 *
 * The primaries we receive from clients are multiplied by 1000000.
 */
static void
cm_creator_params_set_mastering_display_primaries(struct wl_client *client,
						  struct wl_resource *resource,
						  int32_t r_x, int32_t r_y,
						  int32_t g_x, int32_t g_y,
						  int32_t b_x, int32_t b_y,
						  int32_t w_x, int32_t w_y)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);
	struct weston_color_gamut primaries;

	primaries.primary[0].x = r_x / 1000000.0f;
	primaries.primary[0].y = r_y / 1000000.0f;
	primaries.primary[1].x = g_x / 1000000.0f;
	primaries.primary[1].y = g_y / 1000000.0f;
	primaries.primary[2].x = b_x / 1000000.0f;
	primaries.primary[2].y = b_y / 1000000.0f;
	primaries.white_point.x = w_x / 1000000.0f;
	primaries.white_point.y = w_y / 1000000.0f;

	if (!weston_color_profile_param_builder_set_target_primaries(cm_creator_params->builder,
								     &primaries))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set mastering display luminance for parametric-based image description creator object.
 *
 * The min luminance we receive from clients is multiplied by 10000.
 */
static void
cm_creator_params_set_mastering_luminance(struct wl_client *client,
					  struct wl_resource *resource,
					  uint32_t min_lum, uint32_t max_lum)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);

	if (!weston_color_profile_param_builder_set_target_luminance(cm_creator_params->builder,
								     min_lum / 10000.0f, max_lum))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set max cll for parametric-based image description creator object.
 */
static void
cm_creator_params_set_max_cll(struct wl_client *client, struct wl_resource *resource,
			      uint32_t max_cll)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);

	if (!weston_color_profile_param_builder_set_maxCLL(cm_creator_params->builder,
							   max_cll))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Set max fall for parametric-based image description creator object.
 */
static void
cm_creator_params_set_max_fall(struct wl_client *client, struct wl_resource *resource,
			       uint32_t max_fall)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);

	if (!weston_color_profile_param_builder_set_maxFALL(cm_creator_params->builder,
							    max_fall))
		cm_creator_params_post_protocol_error(cm_creator_params);
}

/**
 * Creates image description using the parametric-based image description
 * creator object. This is a destructor type request, so the cm_creator_params
 * resource gets destroyed after this.
 */
static void
cm_creator_params_create(struct wl_client *client, struct wl_resource *resource,
			 uint32_t protocol_object_id)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);
	struct weston_compositor *compositor = cm_creator_params->compositor;
	struct weston_color_manager *cm = compositor->color_manager;
	uint32_t version = wl_resource_get_version(cm_creator_params->owner);
	struct cm_image_desc *cm_image_desc;
	enum weston_color_profile_param_builder_error err;
	int32_t protocol_err;
	char *err_msg;

	/* Create the image description with cprof == NULL. */
	cm_image_desc = cm_image_desc_create(cm, NULL, client, version,
					     protocol_object_id, NO_GET_INFO);
	if (!cm_image_desc) {
		wl_resource_post_no_memory(resource);
		return;
	}

	/* Create the color profile through the param builder. This will destroy
	 * the builder object. */
	cm_image_desc->cprof =
		weston_color_profile_param_builder_create_color_profile(cm_creator_params->builder,
									"client",
									&err, &err_msg);
	cm_creator_params->builder = NULL;

	if (cm_image_desc->cprof) {
		wp_image_description_v1_send_ready(cm_image_desc->owner,
						   cm_image_desc->cprof->id);
	} else {
		protocol_err = cm_creator_params_error_to_protocol(compositor, err);
		if (protocol_err >= 0)
			wl_resource_post_error(cm_creator_params->owner,
					       protocol_err, "%s", err_msg);
		else
			wp_image_description_v1_send_failed(cm_image_desc->owner,
							    WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
							    err_msg);
		free(err_msg);

		/* Failed to create the cprof (and so the image description).
		 * Let's set the image description resource userdata to NULL
		 * (and other functions can tell that it is invalid through
		 * that). */
		wl_resource_set_user_data(cm_image_desc->owner, NULL);
		cm_image_desc_destroy(cm_image_desc);
	}

	/* Destroy the cm_creator_params resource. This is a destructor request. */
	wl_resource_destroy(cm_creator_params->owner);
}

/**
 * Resource destruction function for the cm_creator_params.
 * It should only destroy itself, but not the image description it creates.
 */
static void
cm_creator_params_destructor(struct wl_resource *resource)
{
	struct cm_creator_params *cm_creator_params =
		wl_resource_get_user_data(resource);

	if (cm_creator_params->builder)
		weston_color_profile_param_builder_destroy(cm_creator_params->builder);

	free(cm_creator_params);
}

static const struct wp_image_description_creator_params_v1_interface
cm_creator_params_implementation = {
	.set_primaries_named = cm_creator_params_set_primaries_named,
	.set_primaries = cm_creator_params_set_primaries,
	.set_tf_named = cm_creator_params_set_tf_named,
	.set_tf_power = cm_creator_params_set_tf_power,
	.set_luminances = cm_creator_params_set_luminances,
	.set_mastering_display_primaries = cm_creator_params_set_mastering_display_primaries,
	.set_mastering_luminance = cm_creator_params_set_mastering_luminance,
	.set_max_cll = cm_creator_params_set_max_cll,
	.set_max_fall = cm_creator_params_set_max_fall,
	.create = cm_creator_params_create,
};

/**
 * Creates a parametric image description creator for the client.
 */
static void
cm_create_image_description_creator_params(struct wl_client *client,
					   struct wl_resource *cm_res,
					   uint32_t cm_creator_params_id)
{
	struct cm_creator_params *cm_creator_params;
	struct weston_compositor *compositor = wl_resource_get_user_data(cm_res);
	struct weston_color_manager *cm = compositor->color_manager;
	uint32_t version = wl_resource_get_version(cm_res);

	if (!((cm->supported_color_features >> WESTON_COLOR_FEATURE_PARAMETRIC) & 1)) {
		wl_resource_post_error(cm_res, WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
				       "creating parametric image descriptions " \
				       "is not supported");
		return;
	}

	cm_creator_params = xzalloc(sizeof(*cm_creator_params));

	cm_creator_params->compositor = compositor;

	cm_creator_params->builder =
		weston_color_profile_param_builder_create(compositor);

	cm_creator_params->owner =
		wl_resource_create(client, &wp_image_description_creator_params_v1_interface,
				   version, cm_creator_params_id);
	if (!cm_creator_params->owner)
		goto err;

	wl_resource_set_implementation(cm_creator_params->owner, &cm_creator_params_implementation,
				       cm_creator_params, cm_creator_params_destructor);

	return;

err:
	weston_color_profile_param_builder_destroy(cm_creator_params->builder);
	free(cm_creator_params);
	wl_resource_post_no_memory(cm_res);
}

static void
cm_create_windows_scrgb(struct wl_client *client, struct wl_resource *cm_res,
			uint32_t image_description)
{
	wl_resource_post_error(cm_res,
			       WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
			       "creating windows scrgb is not supported");
}

/**
 * Client will not use the color management object anymore, so we destroy its
 * resource. That should not affect the other objects in any way.
 */
static void
cm_destroy(struct wl_client *client, struct wl_resource *cm_res)
{
	wl_resource_destroy(cm_res);
}

static const struct wp_color_manager_v1_interface
color_manager_implementation = {
	.destroy = cm_destroy,
	.get_output = cm_get_output,
	.get_surface = cm_get_surface,
	.get_surface_feedback = cm_get_surface_feedback,
	.create_icc_creator = cm_create_image_description_creator_icc,
	.create_parametric_creator = cm_create_image_description_creator_params,
	.create_windows_scrgb = cm_create_windows_scrgb,
};

/**
 * Called when clients bind to the color-management protocol.
 */
static void
bind_color_management(struct wl_client *client, void *data, uint32_t version,
		      uint32_t id)
{
	struct wl_resource *resource;
	struct weston_compositor *compositor = data;
	struct weston_color_manager *cm = compositor->color_manager;
	const struct weston_color_feature_info *feature_info;
	const struct weston_render_intent_info *render_intent;
	const struct weston_color_primaries_info *primaries;
	const struct weston_color_tf_info *tf;
	unsigned int i;

	resource = wl_resource_create(client, &wp_color_manager_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &color_manager_implementation,
				       compositor, NULL);

	/* Expose the supported color features to the client. */
	for (i = 0; i < 32; i++) {
		if (!((cm->supported_color_features >> i) & 1))
			continue;
		feature_info = weston_color_feature_info_from(compositor, i);
		wp_color_manager_v1_send_supported_feature(resource,
							   feature_info->protocol_feature);
	}

	/* Expose the supported rendering intents to the client. */
	for (i = 0; i < 32; i++) {
		if (!((cm->supported_rendering_intents >> i) & 1))
			continue;
		render_intent = weston_render_intent_info_from(compositor, i);
		wp_color_manager_v1_send_supported_intent(resource,
							  render_intent->protocol_intent);
	}

	/* Expose the supported primaries named to the client. */
	for (i = 0; i < 32; i++) {
		if (!((cm->supported_primaries_named >> i) & 1))
			continue;
		primaries = weston_color_primaries_info_from(compositor, i);
		wp_color_manager_v1_send_supported_primaries_named(resource,
								   primaries->protocol_primaries);
	}

	/* Expose the supported tf named to the client. */
	for (i = 0; i < 32; i++) {
		if (!((cm->supported_tf_named >> i) & 1))
			continue;
		tf = weston_color_tf_info_from(compositor, i);
		wp_color_manager_v1_send_supported_tf_named(resource,
							    tf->protocol_tf);
	}

	wp_color_manager_v1_send_done(resource);
}

/** Advertise color-management support
 *
 * Calling this initializes the color-management protocol support, so that
 * wp_color_manager_v1_interface will be advertised to clients. Essentially it
 * creates a global. Do not call this function multiple times in the
 * compositor's lifetime. There is no way to deinit explicitly, globals will be
 * reaped when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int
weston_compositor_enable_color_management_protocol(struct weston_compositor *compositor)
{
	uint32_t version = 1;

	weston_assert_bit_is_set(compositor,
				 compositor->color_manager->supported_rendering_intents,
				 1ull << WESTON_RENDER_INTENT_PERCEPTUAL);

	if (!wl_global_create(compositor->wl_display,
			      &wp_color_manager_v1_interface,
			      version, compositor, bind_color_management))
		return -1;

	return 0;
}

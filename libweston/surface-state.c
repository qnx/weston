/*
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2012-2018, 2021 Collabora, Ltd.
 * Copyright © 2017, 2018 General Electric Company
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

#include <libweston/libweston.h>
#include "libweston-internal.h"

#include "backend.h"
#include "pixel-formats.h"
#include "shared/fd-util.h"
#include "timeline.h"
#include "weston-trace.h"

static enum weston_surface_status
weston_surface_apply(struct weston_surface *surface,
		     struct weston_surface_state *state);

static void
weston_surface_dirty_paint_nodes(struct weston_surface *surface,
				 enum paint_node_status status)
{
	struct weston_paint_node *node;

	wl_list_for_each(node, &surface->paint_node_list, surface_link) {
		assert(node->surface == surface);

		node->status |= status;
	}
}

void
weston_surface_state_init(struct weston_surface *surface,
			  struct weston_surface_state *state)
{
	state->status = WESTON_SURFACE_CLEAN;
	state->buffer_ref.buffer = NULL;
	state->buf_offset = weston_coord_surface(0, 0, surface);

	pixman_region32_init(&state->damage_surface);
	pixman_region32_init(&state->damage_buffer);
	pixman_region32_init(&state->opaque);
	region_init_infinite(&state->input);

	wl_list_init(&state->frame_callback_list);
	wl_list_init(&state->feedback_list);

	state->buffer_viewport.buffer.transform = WL_OUTPUT_TRANSFORM_NORMAL;
	state->buffer_viewport.buffer.scale = 1;
	state->buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
	state->buffer_viewport.surface.width = -1;

	state->acquire_fence_fd = -1;

	state->desired_protection = WESTON_HDCP_DISABLE;
	state->protection_mode = WESTON_SURFACE_PROTECTION_MODE_RELAXED;

	state->color_profile = NULL;
	state->render_intent = NULL;
}

void
weston_surface_state_fini(struct weston_surface_state *state)
{
	struct wl_resource *cb, *next;

	wl_resource_for_each_safe(cb, next, &state->frame_callback_list)
		wl_resource_destroy(cb);

	weston_presentation_feedback_discard_list(&state->feedback_list);

	pixman_region32_fini(&state->input);
	pixman_region32_fini(&state->opaque);
	pixman_region32_fini(&state->damage_surface);
	pixman_region32_fini(&state->damage_buffer);

	weston_buffer_reference(&state->buffer_ref, NULL,
				BUFFER_WILL_NOT_BE_ACCESSED);

	fd_clear(&state->acquire_fence_fd);
	weston_buffer_release_reference(&state->buffer_release_ref, NULL);

	weston_color_profile_unref(state->color_profile);
	state->color_profile = NULL;
	state->render_intent = NULL;
}

static enum weston_surface_status
weston_surface_attach(struct weston_surface *surface,
		      struct weston_surface_state *state,
		      enum weston_surface_status status)
{
	WESTON_TRACE_FUNC_FLOW(&surface->flow_id);
	struct weston_buffer *buffer = state->buffer_ref.buffer;
	struct weston_buffer *old_buffer = surface->buffer_ref.buffer;

	if (!buffer) {
		if (weston_surface_is_mapped(surface)) {
			weston_surface_unmap(surface);
			/* This is the unmapping commit */
			surface->is_unmapping = true;
			status |= WESTON_SURFACE_DIRTY_BUFFER;
			status |= WESTON_SURFACE_DIRTY_BUFFER_PARAMS;
			status |= WESTON_SURFACE_DIRTY_SIZE;
		}

		weston_buffer_reference(&surface->buffer_ref, NULL,
					BUFFER_WILL_NOT_BE_ACCESSED);

		surface->width_from_buffer = 0;
		surface->height_from_buffer = 0;

		return status;
	}

	/* Recalculate the surface size if the buffer dimensions or the
	 * surface transforms (viewport, rotation/mirror, scale) have
	 * changed. */
	if (!old_buffer ||
	    buffer->width != old_buffer->width ||
	    buffer->height != old_buffer->height ||
	    (status & WESTON_SURFACE_DIRTY_SIZE)) {
		struct weston_buffer_viewport *vp = &state->buffer_viewport;
		int32_t old_width = surface->width_from_buffer;
		int32_t old_height = surface->height_from_buffer;

		convert_size_by_transform_scale(&surface->width_from_buffer,
						&surface->height_from_buffer,
						buffer->width,
						buffer->height,
						vp->buffer.transform,
						vp->buffer.scale);

		if (surface->width_from_buffer != old_width ||
		    surface->height_from_buffer != old_height) {
			status |= WESTON_SURFACE_DIRTY_SIZE;
		}
	}

	if (!old_buffer ||
	    buffer->pixel_format != old_buffer->pixel_format ||
	    buffer->format_modifier != old_buffer->format_modifier) {
		surface->is_opaque = pixel_format_is_opaque(buffer->pixel_format);
		status |= WESTON_SURFACE_DIRTY_BUFFER_PARAMS;
	}

	status |= WESTON_SURFACE_DIRTY_BUFFER;
	weston_surface_dirty_paint_nodes(surface,
					 PAINT_NODE_BUFFER_DIRTY);
	old_buffer = NULL;
	weston_buffer_reference(&surface->buffer_ref, buffer,
				BUFFER_MAY_BE_ACCESSED);

	return status;
}

static void
weston_surface_apply_subsurface_order(struct weston_surface *surface)
{
	struct weston_subsurface *sub;
	struct weston_view *view;

	wl_list_for_each_reverse(sub, &surface->subsurface_list_pending,
				 parent_link_pending) {
		wl_list_remove(&sub->parent_link);
		wl_list_insert(&surface->subsurface_list, &sub->parent_link);
		wl_list_for_each(view, &sub->surface->views, surface_link)
			weston_view_geometry_dirty(view);
	}
}

/* Translate pending damage in buffer co-ordinates to surface
 * co-ordinates and union it with a pixman_region32_t.
 * This should only be called after the buffer is attached.
 */
static void
apply_damage_buffer(pixman_region32_t *dest,
		    struct weston_surface *surface,
		    struct weston_surface_state *state)
{
	struct weston_buffer *buffer = surface->buffer_ref.buffer;
	pixman_region32_t buffer_damage;

	/* wl_surface.damage_buffer needs to be clipped to the buffer,
	 * translated into surface co-ordinates and unioned with
	 * any other surface damage.
	 * None of this makes sense if there is no buffer though.
	 */
	if (!buffer || !pixman_region32_not_empty(&state->damage_buffer))
		return;

	pixman_region32_intersect_rect(&state->damage_buffer,
				       &state->damage_buffer,
				       0, 0,
				       buffer->width, buffer->height);
	pixman_region32_init(&buffer_damage);
	weston_matrix_transform_region(&buffer_damage,
				       &surface->buffer_to_surface_matrix,
				       &state->damage_buffer);
	pixman_region32_union(dest, dest, &buffer_damage);
	pixman_region32_fini(&buffer_damage);
}

static void
weston_surface_set_desired_protection(struct weston_surface *surface,
				      enum weston_hdcp_protection protection)
{
	struct weston_paint_node *pnode;

	if (surface->desired_protection == protection)
		return;

	surface->desired_protection = protection;

	wl_list_for_each(pnode, &surface->paint_node_list, surface_link) {
		if (pixman_region32_not_empty(&pnode->visible))
			weston_output_damage(pnode->output);
	}
}

static void
weston_surface_set_protection_mode(struct weston_surface *surface,
				   enum weston_surface_protection_mode p_mode)
{
	struct content_protection *cp = surface->compositor->content_protection;
	struct protected_surface *psurface;

	surface->protection_mode = p_mode;
	wl_list_for_each(psurface, &cp->protected_list, link) {
		if (!psurface || psurface->surface != surface)
			continue;
		weston_protected_surface_send_event(psurface,
						    surface->current_protection);
	}
}

static enum weston_surface_status
weston_surface_apply_state(struct weston_surface *surface,
			   struct weston_surface_state *state)
{
	WESTON_TRACE_FUNC_FLOW(&surface->flow_id);
	struct weston_view *view;
	pixman_region32_t opaque;
	enum weston_surface_status status = state->status;

	/* wl_surface.set_buffer_transform */
	/* wl_surface.set_buffer_scale */
	/* wp_viewport.set_source */
	/* wp_viewport.set_destination */
	surface->buffer_viewport = state->buffer_viewport;

	/* wl_surface.attach */
	if (status & WESTON_SURFACE_DIRTY_BUFFER) {
		/* zwp_surface_synchronization_v1.set_acquire_fence */
		fd_move(&surface->acquire_fence_fd,
			&state->acquire_fence_fd);
		/* zwp_surface_synchronization_v1.get_release */
		weston_buffer_release_move(&surface->buffer_release_ref,
					   &state->buffer_release_ref);

		/* wp_presentation.feedback */
		weston_presentation_feedback_discard_list(&surface->feedback_list);

		status |= weston_surface_attach(surface, state, status);
	}
	weston_buffer_reference(&state->buffer_ref, NULL,
				BUFFER_WILL_NOT_BE_ACCESSED);
	assert(state->acquire_fence_fd == -1);
	assert(state->buffer_release_ref.buffer_release == NULL);

	if (status & WESTON_SURFACE_DIRTY_SIZE) {
		weston_surface_build_buffer_matrix(surface,
						   &surface->surface_to_buffer_matrix);
		weston_matrix_invert(&surface->buffer_to_surface_matrix,
				     &surface->surface_to_buffer_matrix);
		weston_surface_dirty_paint_nodes(surface,
						 PAINT_NODE_VIEW_DIRTY);
		weston_surface_update_size(surface);
	}

	if ((status & (WESTON_SURFACE_DIRTY_BUFFER | WESTON_SURFACE_DIRTY_SIZE |
		       WESTON_SURFACE_DIRTY_POS)) &&
	     surface->committed)
		surface->committed(surface, state->buf_offset);

	state->buf_offset = weston_coord_surface(0, 0, surface);

	/* wl_surface.damage and wl_surface.damage_buffer; only valid
	 * in the same cycle as wl_surface.commit */
	if (status & WESTON_SURFACE_DIRTY_BUFFER) {
		TL_POINT(surface->compositor, TLP_CORE_COMMIT_DAMAGE,
			TLP_SURFACE(surface), TLP_END);

		pixman_region32_union(&surface->damage, &surface->damage,
				      &state->damage_surface);

		apply_damage_buffer(&surface->damage, surface, state);
		surface->frame_commit_counter++;

		pixman_region32_intersect_rect(&surface->damage,
					       &surface->damage,
					       0, 0,
					       surface->width, surface->height);
	}
	pixman_region32_clear(&state->damage_buffer);
	pixman_region32_clear(&state->damage_surface);

	/* wl_surface.set_opaque_region */
	if (status & (WESTON_SURFACE_DIRTY_SIZE |
		      WESTON_SURFACE_DIRTY_BUFFER_PARAMS)) {
		pixman_region32_init(&opaque);
		pixman_region32_intersect_rect(&opaque, &state->opaque,
					       0, 0,
					       surface->width, surface->height);

		if (!pixman_region32_equal(&opaque, &surface->opaque)) {
			pixman_region32_copy(&surface->opaque, &opaque);
			wl_list_for_each(view, &surface->views, surface_link)
				weston_view_geometry_dirty(view);
		}

		pixman_region32_fini(&opaque);
	}

	/* wl_surface.set_input_region */
	if (status & (WESTON_SURFACE_DIRTY_SIZE | WESTON_SURFACE_DIRTY_INPUT)) {
		pixman_region32_intersect_rect(&surface->input, &state->input,
					       0, 0,
					       surface->width, surface->height);
	}

	/* wl_surface.frame */
	wl_list_insert_list(&surface->frame_callback_list,
			    &state->frame_callback_list);
	wl_list_init(&state->frame_callback_list);

	/* XXX:
	 * What should happen with a feedback request, if there
	 * is no wl_buffer attached for this commit?
	 */

	/* presentation.feedback */
	wl_list_insert_list(&surface->feedback_list,
			    &state->feedback_list);
	wl_list_init(&state->feedback_list);

	/* weston_protected_surface.enforced/relaxed */
	if (surface->protection_mode != state->protection_mode)
		weston_surface_set_protection_mode(surface,
						   state->protection_mode);

	/* weston_protected_surface.set_type */
	weston_surface_set_desired_protection(surface, state->desired_protection);

	/* color_management_surface_v1_interface.set_image_description or
	 * color_management_surface_v1_interface.unset_image_description */
	weston_surface_set_color_profile(surface, state->color_profile,
					 state->render_intent);

	wl_signal_emit(&surface->commit_signal, surface);

	/* Surface is now quiescent */
	surface->is_unmapping = false;
	surface->is_mapping = false;
	state->status = WESTON_SURFACE_CLEAN;

	return status;
}

static enum weston_surface_status
weston_subsurface_parent_apply(struct weston_subsurface *sub)
{
	enum weston_surface_status status = WESTON_SURFACE_CLEAN;
	struct weston_view *view;

	if (sub->position.changed) {
		wl_list_for_each(view, &sub->surface->views, surface_link)
			weston_view_set_rel_position(view,
						     sub->position.offset);

		sub->position.changed = false;
	}

	if (sub->effectively_synchronized)
		status = weston_surface_apply(sub->surface, &sub->cached);

	return status;
}

static enum weston_surface_status
weston_surface_apply(struct weston_surface *surface,
		     struct weston_surface_state *state)
{
	WESTON_TRACE_FUNC_FLOW(&surface->flow_id);
	enum weston_surface_status status;
	struct weston_subsurface *sub;

	status = weston_surface_apply_state(surface, state);

	if (status & WESTON_SURFACE_DIRTY_SUBSURFACE_CONFIG)
		weston_surface_apply_subsurface_order(surface);

	weston_surface_schedule_repaint(surface);

	wl_list_for_each(sub, &surface->subsurface_list, parent_link) {
		if (sub->surface != surface)
			status |= weston_subsurface_parent_apply(sub);
	}

	return status;
}

static void
weston_surface_state_merge_from(struct weston_surface_state *dst,
				struct weston_surface_state *src,
				struct weston_surface *surface)
{
	WESTON_TRACE_FUNC();

	/*
	 * If this commit would cause the surface to move by the
	 * attach(dx, dy) parameters, the old damage region must be
	 * translated to correspond to the new surface coordinate system
	 * origin.
	 */
	if (surface->pending.status & WESTON_SURFACE_DIRTY_POS) {
		pixman_region32_translate(&dst->damage_surface,
					  -src->buf_offset.c.x,
					  -src->buf_offset.c.y);
	}
	pixman_region32_union(&dst->damage_surface,
			      &dst->damage_surface,
			      &src->damage_surface);
	pixman_region32_clear(&src->damage_surface);

	pixman_region32_union(&dst->damage_buffer,
			      &dst->damage_buffer,
			      &src->damage_buffer);
	pixman_region32_clear(&src->damage_buffer);

	dst->render_intent = src->render_intent;
	weston_color_profile_unref(dst->color_profile);
	dst->color_profile =
		weston_color_profile_ref(src->color_profile);

	if (src->status & WESTON_SURFACE_DIRTY_BUFFER) {
		weston_buffer_reference(&dst->buffer_ref,
					src->buffer_ref.buffer,
					src->buffer_ref.buffer ?
						BUFFER_MAY_BE_ACCESSED :
						BUFFER_WILL_NOT_BE_ACCESSED);
		weston_presentation_feedback_discard_list(
					&dst->feedback_list);
		/* zwp_surface_synchronization_v1.set_acquire_fence */
		fd_move(&dst->acquire_fence_fd,
			&src->acquire_fence_fd);
		/* zwp_surface_synchronization_v1.get_release */
		weston_buffer_release_move(&dst->buffer_release_ref,
					   &src->buffer_release_ref);
	}
	dst->desired_protection = src->desired_protection;
	dst->protection_mode = src->protection_mode;
	assert(src->acquire_fence_fd == -1);
	assert(src->buffer_release_ref.buffer_release == NULL);
	dst->buf_offset = weston_coord_surface_add(dst->buf_offset,
						   src->buf_offset);

	dst->buffer_viewport.buffer = src->buffer_viewport.buffer;
	dst->buffer_viewport.surface = src->buffer_viewport.surface;

	weston_buffer_reference(&src->buffer_ref,
				NULL, BUFFER_WILL_NOT_BE_ACCESSED);

	src->buf_offset = weston_coord_surface(0, 0, surface);

	pixman_region32_copy(&dst->opaque, &src->opaque);

	pixman_region32_copy(&dst->input, &src->input);

	wl_list_insert_list(&dst->frame_callback_list,
			    &src->frame_callback_list);
	wl_list_init(&src->frame_callback_list);

	wl_list_insert_list(&dst->feedback_list,
			    &src->feedback_list);
	wl_list_init(&src->feedback_list);

	dst->status |= src->status;
	src->status = WESTON_SURFACE_CLEAN;
}

enum weston_surface_status
weston_surface_commit(struct weston_surface *surface)
{
	struct weston_subsurface *sub = weston_surface_to_subsurface(surface);
	struct weston_surface_state *state = &surface->pending;
	enum weston_surface_status status;

	if (sub) {
		weston_surface_state_merge_from(&sub->cached,
						state,
						surface);
		if (sub->effectively_synchronized)
			return WESTON_SURFACE_CLEAN;

		state = &sub->cached;
	}

	status = weston_surface_apply(surface, state);
	return status;
}

/** Recursively update effectively_synchronized state for a subsurface tree
 *
 * \param sub Subsurface to start from
 *
 * From wayland.xml :
 *   Even if a sub-surface is in desynchronized mode, it will behave as
 *   in synchronized mode, if its parent surface behaves as in
 *   synchronized mode. This rule is applied recursively throughout the
 *   tree of surfaces.
 *
 * In Weston, we call a surface "effectively synchronized" if it is either
 * synchronized, or is forced to "behave as in synchronized mode" by a
 * parent surface that is effectively synchronized.
 *
 * Calling weston_subsurface_update_effectively_synchronized on a subsurface
 * will update the tree of subsurfaces to have accurate
 * effectively_synchronized state below that point, by walking all descendants
 * and combining their state with their immediate parent's state.
 *
 * Since every subsurface starts off synchronized, they also start off
 * effectively synchronized, so we only need to call this function in response
 * to synchronization changes from protocol requests (set_sync, set_desync) to
 * keep the subsurface tree state up to date.
 */
static void
weston_subsurface_update_effectively_synchronized(struct weston_subsurface *sub)
{
	bool parent_e_sync = false;
	struct weston_subsurface *child;
	struct weston_surface *surf = sub->surface;

	if (sub->parent) {
		struct weston_subsurface *parent;

		parent = weston_surface_to_subsurface(sub->parent);
		if (parent)
			parent_e_sync = parent->effectively_synchronized;
	}

	/* This subsurface will be effectively synchronized if it is
	 * explicitly synchronized, or if a parent surface is effectively
	 * synchronized.
	 *
	 * Since we're called for every protocol driven change, and update
	 * recursively at that point, we know that the immediate parent
	 * state is always up to date, so we only have to test that here.
	 */
	sub->effectively_synchronized = parent_e_sync || sub->synchronized;

	wl_list_for_each(child, &surf->subsurface_list, parent_link) {
		if (child->surface == surf)
			continue;

		weston_subsurface_update_effectively_synchronized(child);
	}
}

void
weston_subsurface_set_synchronized(struct weston_subsurface *sub, bool sync)
{
	bool old_e_sync = sub->effectively_synchronized;

	if (sub->synchronized == sync)
		return;

	sub->synchronized = sync;

	weston_subsurface_update_effectively_synchronized(sub);

	/* If sub became effectively desynchronized, flush */
	if (old_e_sync && !sub->effectively_synchronized)
		weston_surface_apply(sub->surface, &sub->cached);
}

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

#include <assert.h>

#include <libweston/libweston.h>
#include <libweston/commit-timing.h>
#include "libweston-internal.h"
#include "shared/helpers.h"
#include "shared/timespec-util.h"
#include "shared/xalloc.h"
#include "weston-trace.h"

struct weston_commit_timer {
	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;
	uint64_t flow_id;
};

static void
commit_timer_destructor(struct wl_resource *resource)
{
	struct weston_commit_timer *ct = wl_resource_get_user_data(resource);

	if (ct->surface)
		wl_list_remove(&ct->surface_destroy_listener.link);

	free(ct);
}

static void
commit_timer_set_target_time(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t sec_hi,
			     uint32_t sec_lo,
			     uint32_t nsec)
{
	struct weston_commit_timer *ct = wl_resource_get_user_data(resource);
	struct weston_surface *surface = ct->surface;
	uint64_t sec_u64 = u64_from_u32s(sec_hi, sec_lo);

	if (!surface) {
		wl_resource_post_error(resource,
				       WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED,
				       "surface destroyed");
		return;
	}
	if (surface->pending.update_time.valid) {
		wl_resource_post_error(resource,
				       WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS,
				       "target timestamp already set");
		return;
	}
	if (nsec > 999999999) {
		wl_resource_post_error(resource,
				       WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP,
				       "target timestamp invalid");
		return;
	}
	if (sec_u64 > INT64_MAX) {
		wl_resource_post_error(resource,
				       WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP,
				       "target timestamp invalid");
		return;
	}

	surface->pending.update_time.valid = true;
	surface->pending.update_time.satisfied = false;
	surface->pending.update_time.time.tv_sec = (time_t) sec_u64;
	surface->pending.update_time.time.tv_nsec = nsec;
}

static void
commit_timer_destroy(struct wl_client *client, struct wl_resource *resource)
{
	struct weston_commit_timer *ct = wl_resource_get_user_data(resource);
	struct weston_surface *surface = ct->surface;

	wl_resource_destroy(resource);
	if (!surface)
		return;

	surface->commit_timer = NULL;
}

static const struct wp_commit_timer_v1_interface weston_commit_timer_interface = {
	commit_timer_set_target_time,
	commit_timer_destroy,
};

static void
commit_timing_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
commit_timer_surface_destroy_cb(struct wl_listener *listener, void *data)
{
	struct weston_commit_timer *ct =
		container_of(listener,
			struct weston_commit_timer, surface_destroy_listener);

	ct->surface = NULL;
}

static void
commit_timing_manager_get_commit_timer(struct wl_client *client,
				       struct wl_resource *manager_resource,
				       uint32_t id,
				       struct wl_resource *surface_resource)
{
	struct weston_commit_timer *ct;
	struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wl_resource *res;

	if (surface->commit_timer) {
		wl_resource_post_error(manager_resource,
				       WP_COMMIT_TIMING_MANAGER_V1_ERROR_COMMIT_TIMER_EXISTS,
				       "Commit timing resource already exists on surface");
		return;
	}

	res = wl_resource_create(client, &wp_commit_timer_v1_interface,
				 wl_resource_get_version(manager_resource), id);
	ct = xzalloc(sizeof *ct);
	ct->surface = surface;
	surface->commit_timer = ct;
	ct->surface_destroy_listener.notify = commit_timer_surface_destroy_cb;
	wl_signal_add(&surface->destroy_signal, &ct->surface_destroy_listener);
	wl_resource_set_implementation(res, &weston_commit_timer_interface, ct,
				       commit_timer_destructor);
}

static const struct wp_commit_timing_manager_v1_interface weston_commit_timing_manager_v1_interface = {
	commit_timing_manager_destroy,
	commit_timing_manager_get_commit_timer,
};

static void
bind_commit_timing(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;
	struct weston_compositor *compositor = data;

	resource = wl_resource_create(client,
				      &wp_commit_timing_manager_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource,
				       &weston_commit_timing_manager_v1_interface,
				       compositor, NULL);
}

/** Advertise commit-timing protocol support
 *
 * Sets up commit_timing_v1 support so it is advertised to clients.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int
commit_timing_setup(struct weston_compositor *compositor)
{
	if (!wl_global_create(compositor->wl_display,
			      &wp_commit_timing_manager_v1_interface,
			      1, compositor,
			      bind_commit_timing))
		return -1;

	return 0;
}

/* Checks if surface state's timing requirements have been satisfied.
 * Once it's satisfied, it can never become unsatisfied, and we never
 * need to test it again. We still need to keep the timing information
 * around in case we're using it to move the frame presentation time
 * with VRR.
 */
bool
weston_commit_timing_surface_state_ready(struct weston_surface *surface,
					 struct weston_surface_state *state)
{
	struct weston_output *output = surface->output;
	struct timespec target_repaint;
	struct timespec now_ts;

	if (!state->update_time.valid || state->update_time.satisfied)
		return true;

	weston_compositor_read_presentation_clock(surface->compositor,
						  &now_ts);

	if (timespec_sub_to_nsec(&state->update_time.time, &now_ts) < 0)
		goto ready;

	/* If we have no output, the previous check against wall clock time
	 * is all we can do.
	 */
	if (!output)
		return false;

	/* If the output has a scheduled repaint, we should know for certain
	 * when its content will be displayed, so we know for certain if
	 * this content update is ready or not.
	 */
	if (output->repaint_status == REPAINT_SCHEDULED) {
		int64_t time_since = timespec_sub_to_nsec(&output->next_present,
							  &state->update_time.time);

		if (time_since >= 0)
			goto ready;

		return false;
	}

	target_repaint = weston_output_repaint_from_present(output, &now_ts,
							    &state->update_time.time);

	if (timespec_sub_to_nsec(&target_repaint, &now_ts) < 0)
		goto ready;

	return false;
ready:
	state->update_time.satisfied = true;
	return true;
}

/** Clear a weston_commit_timing_target
 *
 * \param target target to clear
 *
 * Sets a timing target to invalid and clears all fields to known state.
 */
void
weston_commit_timing_clear_target(struct weston_commit_timing_target *target)
{
	target->valid = false;
	target->satisfied = false;
	target->time.tv_nsec = 0;
	target->time.tv_sec = 0;
}

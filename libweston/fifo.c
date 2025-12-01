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
#include <libweston/fifo.h>
#include "libweston-internal.h"
#include "shared/helpers.h"
#include "shared/xalloc.h"

struct weston_fifo {
	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;
	uint64_t flow_id;
};

static void
fifo_destructor(struct wl_resource *resource)
{
	struct weston_fifo *fifo = wl_resource_get_user_data(resource);

	if (fifo->surface)
		wl_list_remove(&fifo->surface_destroy_listener.link);

	free(fifo);
}

static void
fifo_set_barrier(struct wl_client *client, struct wl_resource *resource)
{
	struct weston_fifo *fifo = wl_resource_get_user_data(resource);
	struct weston_surface *surface = fifo->surface;

	if (!surface) {
		wl_resource_post_error(resource,
				       WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
				       "surface destroyed");
		return;
	}
	surface->pending.fifo_barrier = true;
}

static void
fifo_wait_barrier(struct wl_client *client, struct wl_resource *resource)
{
	struct weston_fifo *fifo = wl_resource_get_user_data(resource);
	struct weston_surface *surface = fifo->surface;

	if (!surface) {
		wl_resource_post_error(resource,
				       WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
				       "surface destroyed");
		return;
	}
	surface->pending.fifo_wait = true;
}

static void
fifo_destroy(struct wl_client *client, struct wl_resource *resource)
{
	struct weston_fifo *fifo = wl_resource_get_user_data(resource);
	struct weston_surface *surface = fifo->surface;

	wl_resource_destroy(resource);
	if (!surface)
		return;

	surface->fifo = NULL;
}

static const struct wp_fifo_v1_interface weston_fifo_interface = {
	.set_barrier = fifo_set_barrier,
	.wait_barrier = fifo_wait_barrier,
	.destroy = fifo_destroy,
};

static void
fifo_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
fifo_surface_destroy_cb(struct wl_listener *listener, void *data)
{
	struct weston_fifo *fifo =
		container_of(listener,
			struct weston_fifo, surface_destroy_listener);

	fifo->surface = NULL;
}

static void
fifo_manager_get_fifo(struct wl_client *client,
		      struct wl_resource *fm_resource,
		      uint32_t id,
		      struct wl_resource *surface_resource)
{
	struct weston_fifo *fifo;
	struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wl_resource *res;

	if (surface->fifo) {
		wl_resource_post_error(fm_resource,
				       WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS,
				       "Fifo resource already exists on surface");
		return;
	}

	res = wl_resource_create(client, &wp_fifo_v1_interface,
				 wl_resource_get_version(fm_resource), id);
	fifo = xzalloc(sizeof *fifo);
	fifo->surface = surface;
	fifo->surface_destroy_listener.notify = fifo_surface_destroy_cb;
	wl_signal_add(&surface->destroy_signal, &fifo->surface_destroy_listener);
	wl_resource_set_implementation(res, &weston_fifo_interface, fifo,
				       fifo_destructor);
	surface->fifo = fifo;
}

static const struct wp_fifo_manager_v1_interface fifo_manager_interface_v1 = {
	.destroy = fifo_manager_destroy,
	.get_fifo = fifo_manager_get_fifo,
};

static void
bind_fifo_manager(struct wl_client *client,
		  void *data,
		  uint32_t version,
		  uint32_t id)
{
	struct wl_resource *resource;
	struct weston_compositor *compositor = data;

	resource = wl_resource_create(client,
				      &wp_fifo_manager_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource,
				       &fifo_manager_interface_v1,
				       compositor, NULL);
}

/** Advertise fifo protocol support
 *
 * Sets up fifo_v1 support so it is advertiszed to clients.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int
fifo_setup(struct weston_compositor *compositor)
{
        if (!wl_global_create(compositor->wl_display,
                              &wp_fifo_manager_v1_interface,
                              1, compositor,
                              bind_fifo_manager))
                return -1;

        return 0;
}

static void
weston_fifo_surface_clear_barrier(struct weston_surface *surface)
{
	surface->fifo_barrier = false;
	wl_list_remove(&surface->fifo_barrier_link);
	wl_list_init(&surface->fifo_barrier_link);
}

void
weston_fifo_surface_set_barrier(struct weston_surface *surface)
{
	/* If nothing is waiting on barriers, we could set multiple times
	 * before a repaint occurs.
	 *
	 * Theoretically, this surface could have a different primary
	 * output than the last time a barrier was created, so let's
	 * just blow away any old barrier (should one exist) before
	 * setting the current one.
	 */
	weston_fifo_surface_clear_barrier(surface);

	/* If the surface isn't associated with an output, we have no way
	 * to clear a barrier - so just don't set one.
	 */
	if (!surface->output)
		return;

	surface->fifo_barrier = true;
	wl_list_insert(&surface->output->fifo_barrier_surfaces,
		       &surface->fifo_barrier_link);
}

void
weston_fifo_output_clear_barriers(struct weston_output *output)
{
	struct weston_surface *surf, *tmp;

	wl_list_for_each_safe(surf, tmp,
			      &output->fifo_barrier_surfaces,
			      fifo_barrier_link)
		weston_fifo_surface_clear_barrier(surf);
}

bool
weston_fifo_output_has_barriers(struct weston_output *output)
{
	return !wl_list_empty(&output->fifo_barrier_surfaces);
}

bool
weston_fifo_surface_state_ready(struct weston_surface *surface,
				struct weston_surface_state *state)
{
	struct weston_subsurface *sub = weston_surface_to_subsurface(surface);
	bool e_sync = sub && sub->effectively_synchronized;

	if (!state->fifo_wait)
		return true;

	/* The barrier is clear. */
	if (!surface->fifo_barrier)
		return true;

	/* Effectively synchronized surfaces ignore fifo */
	if (e_sync)
		return true;

	/* If there's no driving output, fifo will never clear,
	 * so just ignore the condition.
	 */
	if (!surface->output)
		return true;

	/* Occluded surfaces ignore fifo */
	if (!weston_surface_visibility_mask(surface))
		return true;

	return false;
}

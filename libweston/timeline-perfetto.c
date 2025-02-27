/*
 * Copyright © 2025 Collabora, Ltd.
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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <libweston/libweston.h>
#include "shared/timespec-util.h"
#include "timeline.h"
#include "weston-trace.h"

static void
weston_perfetto_ensure_output_ids(struct weston_output *output)
{
	char track_name[512];

	if (output->gpu_track_id)
		return;

	snprintf(track_name, sizeof(track_name), "%s GPU activity", output->name);
	output->gpu_track_id = util_perfetto_new_track(track_name);

	snprintf(track_name, sizeof(track_name), "%s paint", output->name);
	output->paint_track_id = util_perfetto_new_track(track_name);

	snprintf(track_name, sizeof(track_name), "%s present", output->name);
	output->presentation_track_id = util_perfetto_new_track(track_name);
}

static void
build_track_name(struct weston_surface *surface, char *name, int size)
{
	char surface_label[512];

	/* Make sure we only call this once, so we don't accidentally
	 * make multiple names for the same surface */
	assert(surface->damage_track_id == 0);

	if (surface->get_label)
		surface->get_label(surface, surface_label, sizeof(surface_label));
	else {
		uint32_t res_id;

		res_id = wl_resource_get_id(surface->resource);
		snprintf(surface_label, sizeof(surface_label), "unlabelled surface %d", res_id);
	}

	snprintf(name, size, "%s #%d", surface_label, surface->s_id);
}

static void
weston_perfetto_ensure_surface_id(struct weston_surface *surface)
{
	char track_name[600];

	if (surface->damage_track_id)
		return;

	build_track_name(surface, track_name, sizeof(track_name));

	surface->damage_track_id = util_perfetto_new_track(track_name);
}

/**
 * Translates a timeline point for perfetto.
 *
 * The TL_POINT() is a wrapper over this function, but it uses the weston_compositor
 * instance to pass the timeline scope.
 *
 * @param timeline_scope the timeline scope
 * @param tlp_name the name of the timeline point.
 *
 * @ingroup log
 */
WL_EXPORT void
weston_timeline_perfetto(struct weston_log_scope *timeline_scope,
			 enum timeline_point_name tlp_name, ...)
{
	struct weston_output *output = NULL;
	struct weston_surface *surface = NULL;
	struct timespec ts;
	uint64_t now_ns;
	uint64_t vblank_ns = 0, gpu_ns = 0;
	va_list argp;

	if (!util_perfetto_is_tracing_enabled())
		return;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	now_ns = timespec_to_nsec(&ts);

	va_start(argp, tlp_name);
	while (1) {
		enum timeline_type otype;
		void *obj;

		otype = va_arg(argp, enum timeline_type);
		if (otype == TLT_END)
			break;

		obj = va_arg(argp, void *);
		switch (otype) {
		case TLT_OUTPUT:
			output = obj;
			weston_perfetto_ensure_output_ids(output);
			break;
		case TLT_SURFACE:
			surface = obj;
			weston_perfetto_ensure_surface_id(surface);
			break;
		case TLT_VBLANK:
			vblank_ns = timespec_to_nsec(obj);
			break;
		case TLT_GPU:
			gpu_ns = timespec_to_nsec(obj);
			break;
		default:
			assert(!"not reached");
		}
	}
	va_end(argp);

	switch (tlp_name) {
	case TLP_CORE_REPAINT_ENTER_LOOP:
	case TLP_CORE_REPAINT_RESTART:
	case TLP_CORE_REPAINT_EXIT_LOOP:
		break;
	case TLP_CORE_FLUSH_DAMAGE:
		WESTON_TRACE_TIMESTAMP_END("Damaged", surface->damage_track_id, CLOCK_MONOTONIC, now_ns);
		WESTON_TRACE_TIMESTAMP_BEGIN("Clean", surface->damage_track_id, surface->flow_id, CLOCK_MONOTONIC, now_ns);
		break;
	case TLP_CORE_REPAINT_BEGIN:
		WESTON_TRACE_TIMESTAMP_END("Scheduled", output->paint_track_id, CLOCK_MONOTONIC, now_ns);
		WESTON_TRACE_TIMESTAMP_BEGIN("Paint", output->paint_track_id, 0, CLOCK_MONOTONIC, now_ns);
		break;
	case TLP_CORE_REPAINT_POSTED:
		WESTON_TRACE_TIMESTAMP_END("Paint", output->paint_track_id, CLOCK_MONOTONIC, now_ns);
		WESTON_TRACE_TIMESTAMP_BEGIN("Posted", output->presentation_track_id, 0, CLOCK_MONOTONIC, now_ns);
		break;
	case TLP_CORE_REPAINT_FINISHED:
		WESTON_TRACE_TIMESTAMP_END("Posted", output->presentation_track_id, CLOCK_MONOTONIC, vblank_ns);
		break;
	case TLP_CORE_REPAINT_REQ:
		WESTON_TRACE_TIMESTAMP_BEGIN("Scheduled", output->paint_track_id, 0, CLOCK_MONOTONIC, now_ns);
		break;
	case TLP_CORE_COMMIT_DAMAGE:
		WESTON_TRACE_TIMESTAMP_END("Clean", surface->damage_track_id, CLOCK_MONOTONIC, now_ns);
		WESTON_TRACE_TIMESTAMP_END("Damaged", surface->damage_track_id, CLOCK_MONOTONIC, now_ns);
		WESTON_TRACE_TIMESTAMP_BEGIN("Damaged", surface->damage_track_id, 0, CLOCK_MONOTONIC, now_ns);
		break;
	case TLP_RENDERER_GPU_BEGIN:
		WESTON_TRACE_TIMESTAMP_BEGIN("Active", output->gpu_track_id, 0, CLOCK_MONOTONIC, gpu_ns);
		break;
	case TLP_RENDERER_GPU_END:
		WESTON_TRACE_TIMESTAMP_END("Active", output->gpu_track_id, CLOCK_MONOTONIC, gpu_ns);
		break;
	default:
		assert(!"not reached");
	}
}

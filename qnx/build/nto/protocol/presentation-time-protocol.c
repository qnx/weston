/* Generated by wayland-scanner 1.21.0 */

/*
 * Copyright © 2013-2014 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

#ifndef __has_attribute
# define __has_attribute(x) 0  /* Compatibility with non-clang compilers. */
#endif

#if (__has_attribute(visibility) || defined(__GNUC__) && __GNUC__ >= 4)
#define WL_PRIVATE __attribute__ ((visibility("hidden")))
#else
#define WL_PRIVATE
#endif

extern const struct wl_interface wl_output_interface;
extern const struct wl_interface wl_surface_interface;
extern const struct wl_interface wp_presentation_feedback_interface;

static const struct wl_interface *presentation_time_types[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&wl_surface_interface,
	&wp_presentation_feedback_interface,
	&wl_output_interface,
};

static const struct wl_message wp_presentation_requests[] = {
	{ "destroy", "", presentation_time_types + 0 },
	{ "feedback", "on", presentation_time_types + 7 },
};

static const struct wl_message wp_presentation_events[] = {
	{ "clock_id", "u", presentation_time_types + 0 },
};

WL_PRIVATE const struct wl_interface wp_presentation_interface = {
	"wp_presentation", 1,
	2, wp_presentation_requests,
	1, wp_presentation_events,
};

static const struct wl_message wp_presentation_feedback_events[] = {
	{ "sync_output", "o", presentation_time_types + 9 },
	{ "presented", "uuuuuuu", presentation_time_types + 0 },
	{ "discarded", "", presentation_time_types + 0 },
};

WL_PRIVATE const struct wl_interface wp_presentation_feedback_interface = {
	"wp_presentation_feedback", 1,
	0, NULL,
	3, wp_presentation_feedback_events,
};


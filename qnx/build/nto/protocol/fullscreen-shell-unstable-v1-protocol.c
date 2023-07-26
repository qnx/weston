/* Generated by wayland-scanner 1.21.0 */

/*
 * Copyright © 2016 Yong Bakos
 * Copyright © 2015 Jason Ekstrand
 * Copyright © 2015 Jonas Ådahl
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
extern const struct wl_interface zwp_fullscreen_shell_mode_feedback_v1_interface;

static const struct wl_interface *fullscreen_shell_unstable_v1_types[] = {
	NULL,
	&wl_surface_interface,
	NULL,
	&wl_output_interface,
	&wl_surface_interface,
	&wl_output_interface,
	NULL,
	&zwp_fullscreen_shell_mode_feedback_v1_interface,
};

static const struct wl_message zwp_fullscreen_shell_v1_requests[] = {
	{ "release", "", fullscreen_shell_unstable_v1_types + 0 },
	{ "present_surface", "?ou?o", fullscreen_shell_unstable_v1_types + 1 },
	{ "present_surface_for_mode", "ooin", fullscreen_shell_unstable_v1_types + 4 },
};

static const struct wl_message zwp_fullscreen_shell_v1_events[] = {
	{ "capability", "u", fullscreen_shell_unstable_v1_types + 0 },
};

WL_PRIVATE const struct wl_interface zwp_fullscreen_shell_v1_interface = {
	"zwp_fullscreen_shell_v1", 1,
	3, zwp_fullscreen_shell_v1_requests,
	1, zwp_fullscreen_shell_v1_events,
};

static const struct wl_message zwp_fullscreen_shell_mode_feedback_v1_events[] = {
	{ "mode_successful", "", fullscreen_shell_unstable_v1_types + 0 },
	{ "mode_failed", "", fullscreen_shell_unstable_v1_types + 0 },
	{ "present_cancelled", "", fullscreen_shell_unstable_v1_types + 0 },
};

WL_PRIVATE const struct wl_interface zwp_fullscreen_shell_mode_feedback_v1_interface = {
	"zwp_fullscreen_shell_mode_feedback_v1", 1,
	0, NULL,
	3, zwp_fullscreen_shell_mode_feedback_v1_events,
};

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL$ $Rev$")
#endif

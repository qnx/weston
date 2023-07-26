/* Generated by wayland-scanner 1.21.0 */

/*
 * Copyright (C) 2013 DENSO CORPORATION
 * Copyright (c) 2013 BMW Car IT GmbH
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

extern const struct wl_interface wl_seat_interface;

static const struct wl_interface *ivi_hmi_controller_types[] = {
	NULL,
	&wl_seat_interface,
	NULL,
};

static const struct wl_message ivi_hmi_controller_requests[] = {
	{ "UI_ready", "", ivi_hmi_controller_types + 0 },
	{ "workspace_control", "ou", ivi_hmi_controller_types + 1 },
	{ "switch_mode", "u", ivi_hmi_controller_types + 0 },
	{ "home", "u", ivi_hmi_controller_types + 0 },
};

static const struct wl_message ivi_hmi_controller_events[] = {
	{ "workspace_end_control", "i", ivi_hmi_controller_types + 0 },
};

WL_PRIVATE const struct wl_interface ivi_hmi_controller_interface = {
	"ivi_hmi_controller", 1,
	4, ivi_hmi_controller_requests,
	1, ivi_hmi_controller_events,
};

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL$ $Rev$")
#endif

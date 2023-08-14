/* Generated by wayland-scanner 1.21.0 */

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

extern const struct wl_interface wl_surface_interface;

static const struct wl_interface *text_cursor_position_types[] = {
	&wl_surface_interface,
	NULL,
	NULL,
};

static const struct wl_message text_cursor_position_requests[] = {
	{ "notify", "off", text_cursor_position_types + 0 },
};

WL_PRIVATE const struct wl_interface text_cursor_position_interface = {
	"text_cursor_position", 1,
	1, text_cursor_position_requests,
	0, NULL,
};


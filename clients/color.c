/*
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
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

#include "config.h"

#include <assert.h>
#include <errno.h>

#include "color-management-v1-client-protocol.h"
#include <libweston/helpers.h>
#include "shared/xalloc.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "window.h"

enum image_description_status {
	IMAGE_DESCRIPTION_NOT_CREATED = 0,
	IMAGE_DESCRIPTION_READY,
	IMAGE_DESCRIPTION_FAILED,
};

struct pixel_color {
	uint32_t r;
	uint32_t g;
	uint32_t b;
	uint32_t a;
};

struct color {
	struct display *display;
	struct window *window;
	struct widget *parent_widget;
	struct widget *widget;

	struct xx_color_manager_v4 *color_manager;
	struct xx_color_management_surface_v4 *color_surface;
	struct wp_single_pixel_buffer_manager_v1 *single_pixel_manager;
	struct wp_viewporter *viewporter;
	struct wp_viewport *viewport;

	struct pixel_color pixel_color;

	enum xx_color_manager_v4_primaries primaries;
	enum xx_color_manager_v4_transfer_function transfer_function;
	float min_lum;
	float max_lum;
	float ref_lum;

	uint32_t supported_color_features;
	uint32_t supported_rendering_intents;
	uint32_t supported_primaries_named;
	uint32_t supported_tf_named;
};

struct valid_enum {
	const char *name;
	uint32_t value;
};

static bool opt_help = false;
static uint32_t opt_width = 250;
static uint32_t opt_height = 250;
static const char *opt_r = NULL;
static const char *opt_g = NULL;
static const char *opt_b = NULL;
static const char *opt_a = NULL;
static const char *opt_primaries = NULL;
static const char *opt_transfer_function = NULL;
static const char *opt_min_lum = NULL;
static const char *opt_max_lum = NULL;
static const char *opt_ref_lum = NULL;
static const struct weston_option cli_options[] = {
	{ WESTON_OPTION_BOOLEAN, "help", 0, &opt_help },
	{ WESTON_OPTION_UNSIGNED_INTEGER, "width", 'w', &opt_width },
	{ WESTON_OPTION_UNSIGNED_INTEGER, "height", 'h', &opt_height },
	{ WESTON_OPTION_STRING, 0, 'R', &opt_r },
	{ WESTON_OPTION_STRING, 0, 'G', &opt_g },
	{ WESTON_OPTION_STRING, 0, 'B', &opt_b },
	{ WESTON_OPTION_STRING, 0, 'A', &opt_a },
	{ WESTON_OPTION_STRING, "primaries", 'p', &opt_primaries },
	{ WESTON_OPTION_STRING, "transfer-function", 't', &opt_transfer_function },
	{ WESTON_OPTION_STRING, "min-lum", 'm', &opt_min_lum },
	{ WESTON_OPTION_STRING, "max-lum", 'M', &opt_max_lum },
	{ WESTON_OPTION_STRING, "ref-lum", 'r', &opt_ref_lum },
};

static const struct valid_enum valid_primaries[] = {
	{ "srgb", XX_COLOR_MANAGER_V4_PRIMARIES_SRGB },
	{ "bt2020", XX_COLOR_MANAGER_V4_PRIMARIES_BT2020 },
};

static const struct valid_enum valid_transfer_functions[] = {
	{ "srgb", XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB },
	{ "pq", XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ },
	{ "linear", XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LINEAR },
};

static bool
validate_color(const char *c, uint32_t *dest, uint32_t fallback)
{
	char *end;
	double value;

	if (!c) {
		*dest = fallback;
		return true;
	}

	value = strtod(c, &end);
	if (value < 0.0 || value > 1.0 || *end != '\0') {
		fprintf(stderr, "Validating color failed, it should be between 0.0 and 1.0\n");
		return false;
	}

	*dest = value * UINT32_MAX;

	return true;
}

static bool
validate_option(const char *option, uint32_t *dest,
		const struct valid_enum *valid_options,
		int count, uint32_t fallback)
{
	int i;

	if (!option) {
		*dest = fallback;
		return true;
	}

	for (i = 0; i < count; i++) {
		if (strcmp(valid_options[i].name, option) == 0) {
			*dest = valid_options[i].value;
			return true;
		}
	}

	fprintf(stderr, "Validating option '%s' failed, valid options:\n", option);
	for (i = 0; i < count; i++)
		fprintf(stderr, "'%s' ", valid_options[i].name);
	fprintf(stderr, "\n");

	return false;
}

static bool
validate_luminance(const char *c, float *dest, float fallback)
{
	char *end;
	float value;

	if (!c) {
		*dest = fallback;
		return true;
	}

	value = strtof(c, &end);
	if (value < 0.f || value > 10000.f || *end != '\0') {
		fprintf(stderr, "Validating luminance failed, it should be between 0 and 10,000\n");
		return false;
	}

	*dest = value;

	return true;
}

static bool
validate_options(struct color *color)
{
	return validate_color(opt_r, &color->pixel_color.r, 0) &&
	       validate_color(opt_g, &color->pixel_color.g, 0) &&
	       validate_color(opt_b, &color->pixel_color.b, 0) &&
	       validate_color(opt_a, &color->pixel_color.a, UINT32_MAX) &&
	       validate_option(opt_primaries, &color->primaries,
			       valid_primaries,
			       ARRAY_LENGTH(valid_primaries),
			       XX_COLOR_MANAGER_V4_PRIMARIES_SRGB) &&
	       validate_option(opt_transfer_function, &color->transfer_function,
			       valid_transfer_functions,
			       ARRAY_LENGTH(valid_transfer_functions),
			       XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB) &&
	       validate_luminance(opt_min_lum, &color->min_lum, -1.f) &&
	       validate_luminance(opt_max_lum, &color->max_lum, -1.f) &&
	       validate_luminance(opt_ref_lum, &color->ref_lum, -1.f);
}

static void
usage(const char *program_name, int exit_code)
{
	unsigned int i;

	fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
	fprintf(stderr, "  --help\n");
	fprintf(stderr, "  --width or -w\n");
	fprintf(stderr, "  --height or -h\n");
	fprintf(stderr, "  -R (0.0 to 1.0)\n");
	fprintf(stderr, "  -G (0.0 to 1.0)\n");
	fprintf(stderr, "  -B (0.0 to 1.0)\n");
	fprintf(stderr, "  -A (0.0 to 1.0)\n");
	fprintf(stderr, "  --primaries or -p:");
	fprintf(stderr, "\n     ");
	for (i = 0; i < ARRAY_LENGTH(valid_primaries); i++)
		fprintf(stderr, " '%s'", valid_primaries[i].name);
	fprintf(stderr, "\n");
	fprintf(stderr, "  --transfer-function or -t:");
	fprintf(stderr, "\n     ");
	for (i = 0; i < ARRAY_LENGTH(valid_transfer_functions); i++)
		fprintf(stderr, " '%s'", valid_transfer_functions[i].name);
	fprintf(stderr, "\n");
	fprintf(stderr, "  --min-lum or -m (0.0 to 10000.0)\n");
	fprintf(stderr, "  --max-lum or -M (0.0 to 10000.0)\n");
	fprintf(stderr, "  --ref-lum or -r (0.0 to 10000.0)\n");

	exit(exit_code);
}

static void
supported_intent(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
		 uint32_t render_intent)
{
	struct color *color = data;

	color->supported_rendering_intents |= 1 << render_intent;
}

static void
supported_feature(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
		  uint32_t feature)
{
	struct color *color = data;

	color->supported_color_features |= 1 << feature;
}

static void
supported_tf_named(void *data, struct xx_color_manager_v4 *xx_color_manager_v4,
		   uint32_t tf)
{
	struct color *color = data;

	color->supported_tf_named |= 1 << tf;
}

static void
supported_primaries_named(void *data,
			  struct xx_color_manager_v4 *xx_color_manager_v4,
			  uint32_t primaries)
{
	struct color *color = data;

	color->supported_primaries_named |= 1 << primaries;
}

static const struct xx_color_manager_v4_listener color_manager_listener = {
	supported_intent,
	supported_feature,
	supported_tf_named,
	supported_primaries_named,
};

static void
global_handler(struct display *display, uint32_t name,
	       const char *interface, uint32_t version, void *data)
{
	struct color *color = data;
	struct wl_surface *surface = widget_get_wl_surface(color->widget);

	if (strcmp(interface, xx_color_manager_v4_interface.name) == 0) {
		color->color_manager = display_bind(display, name,
						    &xx_color_manager_v4_interface, 1);
		color->color_surface = xx_color_manager_v4_get_surface(color->color_manager,
								       surface);
		xx_color_manager_v4_add_listener(color->color_manager,
						 &color_manager_listener, color);
	} else if (strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
		color->single_pixel_manager =
			display_bind(display, name,
				     &wp_single_pixel_buffer_manager_v1_interface, 1);
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
		color->viewporter = display_bind(display, name,
				                 &wp_viewporter_interface, 1);
		color->viewport = wp_viewporter_get_viewport(color->viewporter, surface);
	}
}

static bool
check_color_requirements(struct color *color)
{

	if (!color->color_manager) {
		fprintf(stderr, "The compositor doesn't expose %s\n",
			xx_color_manager_v4_interface.name);
		return false;
	}
	if (!(color->supported_color_features & (1 << XX_COLOR_MANAGER_V4_FEATURE_PARAMETRIC))) {
		fprintf(stderr, "The color manager doesn't support the parametric creator\n");
		return false;
	}
	if (!(color->supported_primaries_named & (1 << color->primaries))) {
		fprintf(stderr, "The color manager doesn't support the primaries name\n");
		return false;
	}
	if (!(color->supported_tf_named & (1 << color->transfer_function))) {
		fprintf(stderr, "The color manager doesn't support the transfer function\n");
		return false;
	}
	if (!(color->supported_rendering_intents & (1 << XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL))) {
		fprintf(stderr, "The color manager doesn't support perceptual render intent\n");
		return false;
	}
	if (color->min_lum != -1.f || color->max_lum != -1.f || color->ref_lum != -1.f) {
		if (!(color->supported_color_features & (1 << XX_COLOR_MANAGER_V4_FEATURE_SET_LUMINANCES))) {
			fprintf(stderr, "The color manager doesn't support setting luminances\n");
			return false;
		}
		if (color->min_lum == -1.f || color->max_lum == -1.f || color->ref_lum == -1.f) {
			fprintf(stderr, "To set the luminances it is required min-lum, max-lum and ref-lum\n");
			return false;
		}
	}

	return true;
}

static void
color_destroy(struct color *color)
{
	if (color->color_surface)
		xx_color_management_surface_v4_destroy(color->color_surface);

	if (color->color_manager)
		xx_color_manager_v4_destroy(color->color_manager);

	if (color->single_pixel_manager)
		wp_single_pixel_buffer_manager_v1_destroy(color->single_pixel_manager);

	if (color->viewport)
		wp_viewport_destroy(color->viewport);

	if (color->viewporter)
		wp_viewporter_destroy(color->viewporter);

	if (color->widget)
		widget_destroy(color->widget);

	if (color->parent_widget)
		widget_destroy(color->parent_widget);

	if (color->window)
		window_destroy(color->window);

	if (color->display)
		display_destroy(color->display);

	free(color);
}

static void
resize_handler(struct widget *parent_widget, int32_t width, int32_t height, void *data)
{
	struct color *color = data;
	struct rectangle allocation;
	struct wl_surface *surface = widget_get_wl_surface(color->widget);
	struct wl_subsurface *subsurface = widget_get_wl_subsurface(color->widget);

	widget_get_allocation(parent_widget, &allocation);
	wl_subsurface_set_position(subsurface, allocation.x, allocation.y);

	wp_viewport_set_destination(color->viewport, width, height);

	wl_surface_commit(surface);
}

static void
set_empty_input_region(struct color *color, struct widget *widget)
{
	struct wl_region *region;
	struct wl_compositor *compositor;
	struct wl_surface *surface = widget_get_wl_surface(widget);

	compositor = display_get_compositor(color->display);
	region = wl_compositor_create_region(compositor);
	wl_surface_set_input_region(surface, region);
	wl_region_destroy(region);
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static void
set_single_pixel(struct color *color, struct widget *widget)
{
	struct wl_surface *surface = widget_get_wl_surface(widget);
	struct wl_buffer *buffer =
		wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(color->single_pixel_manager,
									 color->pixel_color.r,
									 color->pixel_color.g,
									 color->pixel_color.b,
									 color->pixel_color.a);
	wl_buffer_add_listener(buffer, &buffer_listener, NULL);
	wl_surface_attach(surface, buffer, 0, 0);
}

static void
image_description_failed(void *data,
			 struct xx_image_description_v4 *xx_image_description_v4,
			 uint32_t cause, const char *msg)
{
	enum image_description_status *image_desc_status = data;

	fprintf(stderr, "Failed to create image description: %u - %s\n",
		cause, msg);

	*image_desc_status = IMAGE_DESCRIPTION_FAILED;
}

static void
image_description_ready(void *data, struct xx_image_description_v4 *xx_image_description_v4,
			uint32_t identity)
{
	enum image_description_status *image_desc_status = data;

	*image_desc_status = IMAGE_DESCRIPTION_READY;
}

static const struct xx_image_description_v4_listener image_description_listener = {
	image_description_failed,
	image_description_ready,
};

static struct xx_image_description_v4 *
create_image_description(struct color *color, uint32_t primaries_named, uint32_t tf_named)
{
	struct xx_image_description_creator_params_v4 *params_creator;
	struct xx_image_description_v4 *image_description;
	enum image_description_status image_desc_status = IMAGE_DESCRIPTION_NOT_CREATED;
	int ret = 0;

	params_creator = xx_color_manager_v4_new_parametric_creator(color->color_manager);
        xx_image_description_creator_params_v4_set_primaries_named(params_creator, primaries_named);
        xx_image_description_creator_params_v4_set_tf_named(params_creator, tf_named);
	if (color->min_lum != -1 && color->max_lum != -1 && color->ref_lum != -1)
		xx_image_description_creator_params_v4_set_luminances(params_creator,
								      color->min_lum * 10000,
								      color->max_lum,
								      color->ref_lum);

	image_description = xx_image_description_creator_params_v4_create(params_creator);
        xx_image_description_v4_add_listener(image_description,
					     &image_description_listener,
					     &image_desc_status);

	while (ret != -1 && image_desc_status == IMAGE_DESCRIPTION_NOT_CREATED)
		ret = wl_display_dispatch(display_get_display(color->display));
	if (ret == -1) {
		xx_image_description_v4_destroy(image_description);
		fprintf(stderr, "Error when creating the image description: %s\n", strerror(errno));
		return NULL;
	}

	if (image_desc_status == IMAGE_DESCRIPTION_FAILED) {
		xx_image_description_v4_destroy(image_description);
		return NULL;
	}

	assert(image_desc_status == IMAGE_DESCRIPTION_READY);

	return image_description;
}

static bool
set_image_description(struct color *color, struct widget *widget)
{
	struct xx_image_description_v4 *image_description;

	image_description =
		create_image_description(color,
					 color->primaries,
					 color->transfer_function);
	if (!image_description)
		return false;

	xx_color_management_surface_v4_set_image_description(
		color->color_surface,
		image_description,
		XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL);

	xx_image_description_v4_destroy(image_description);

	return true;
}

int
main(int argc, char *argv[])
{
	struct color *color;

	if (parse_options(cli_options, ARRAY_LENGTH(cli_options), &argc, argv) > 1)
		usage(argv[0], EXIT_FAILURE);

	if (opt_help)
		usage(argv[0], EXIT_SUCCESS);

	color = xzalloc(sizeof *color);
	if (!validate_options(color)) {
		color_destroy(color);
		exit(EXIT_FAILURE);
	}

	color->display = display_create(&argc, argv);
	if (!color->display) {
		color_destroy(color);
		exit(EXIT_FAILURE);
	}

	color->window = window_create(color->display);
	color->parent_widget = window_frame_create(color->window, color);
	color->widget = window_add_subsurface(color->window, color, SUBSURFACE_SYNCHRONIZED);

	display_set_user_data(color->display, color);
	display_set_global_handler(color->display, global_handler);
	wl_display_roundtrip(display_get_display(color->display));

	if (!check_color_requirements(color)) {
		color_destroy(color);
		exit(EXIT_SUCCESS);
	}

	window_unset_shadow(color->window);
	window_set_title(color->window, "Color");
	window_set_appid(color->window, "org.freedesktop.weston.color");
	/* The first resize call sets the min size,
	 * setting 0, 0 sets a default size */
	window_schedule_resize(color->window, 0, 0);
	window_schedule_resize(color->window, opt_width, opt_height);

	widget_set_resize_handler(color->parent_widget, resize_handler);
	widget_set_use_cairo(color->widget, 0);

	set_empty_input_region(color, color->widget);
	set_single_pixel(color, color->widget);

	if (set_image_description(color, color->widget))
		display_run(color->display);

	color_destroy(color);

	return 0;
}

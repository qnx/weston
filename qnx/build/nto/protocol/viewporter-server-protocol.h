/* Generated by wayland-scanner 1.21.0 */

#ifndef VIEWPORTER_SERVER_PROTOCOL_H
#define VIEWPORTER_SERVER_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct wl_client;
struct wl_resource;

/**
 * @page page_viewporter The viewporter protocol
 * @section page_ifaces_viewporter Interfaces
 * - @subpage page_iface_wp_viewporter - surface cropping and scaling
 * - @subpage page_iface_wp_viewport - crop and scale interface to a wl_surface
 * @section page_copyright_viewporter Copyright
 * <pre>
 *
 * Copyright © 2013-2016 Collabora, Ltd.
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
 * </pre>
 */
struct wl_surface;
struct wp_viewport;
struct wp_viewporter;

#ifndef WP_VIEWPORTER_INTERFACE
#define WP_VIEWPORTER_INTERFACE
/**
 * @page page_iface_wp_viewporter wp_viewporter
 * @section page_iface_wp_viewporter_desc Description
 *
 * The global interface exposing surface cropping and scaling
 * capabilities is used to instantiate an interface extension for a
 * wl_surface object. This extended interface will then allow
 * cropping and scaling the surface contents, effectively
 * disconnecting the direct relationship between the buffer and the
 * surface size.
 * @section page_iface_wp_viewporter_api API
 * See @ref iface_wp_viewporter.
 */
/**
 * @defgroup iface_wp_viewporter The wp_viewporter interface
 *
 * The global interface exposing surface cropping and scaling
 * capabilities is used to instantiate an interface extension for a
 * wl_surface object. This extended interface will then allow
 * cropping and scaling the surface contents, effectively
 * disconnecting the direct relationship between the buffer and the
 * surface size.
 */
extern const struct wl_interface wp_viewporter_interface;
#endif
#ifndef WP_VIEWPORT_INTERFACE
#define WP_VIEWPORT_INTERFACE
/**
 * @page page_iface_wp_viewport wp_viewport
 * @section page_iface_wp_viewport_desc Description
 *
 * An additional interface to a wl_surface object, which allows the
 * client to specify the cropping and scaling of the surface
 * contents.
 *
 * This interface works with two concepts: the source rectangle (src_x,
 * src_y, src_width, src_height), and the destination size (dst_width,
 * dst_height). The contents of the source rectangle are scaled to the
 * destination size, and content outside the source rectangle is ignored.
 * This state is double-buffered, and is applied on the next
 * wl_surface.commit.
 *
 * The two parts of crop and scale state are independent: the source
 * rectangle, and the destination size. Initially both are unset, that
 * is, no scaling is applied. The whole of the current wl_buffer is
 * used as the source, and the surface size is as defined in
 * wl_surface.attach.
 *
 * If the destination size is set, it causes the surface size to become
 * dst_width, dst_height. The source (rectangle) is scaled to exactly
 * this size. This overrides whatever the attached wl_buffer size is,
 * unless the wl_buffer is NULL. If the wl_buffer is NULL, the surface
 * has no content and therefore no size. Otherwise, the size is always
 * at least 1x1 in surface local coordinates.
 *
 * If the source rectangle is set, it defines what area of the wl_buffer is
 * taken as the source. If the source rectangle is set and the destination
 * size is not set, then src_width and src_height must be integers, and the
 * surface size becomes the source rectangle size. This results in cropping
 * without scaling. If src_width or src_height are not integers and
 * destination size is not set, the bad_size protocol error is raised when
 * the surface state is applied.
 *
 * The coordinate transformations from buffer pixel coordinates up to
 * the surface-local coordinates happen in the following order:
 * 1. buffer_transform (wl_surface.set_buffer_transform)
 * 2. buffer_scale (wl_surface.set_buffer_scale)
 * 3. crop and scale (wp_viewport.set*)
 * This means, that the source rectangle coordinates of crop and scale
 * are given in the coordinates after the buffer transform and scale,
 * i.e. in the coordinates that would be the surface-local coordinates
 * if the crop and scale was not applied.
 *
 * If src_x or src_y are negative, the bad_value protocol error is raised.
 * Otherwise, if the source rectangle is partially or completely outside of
 * the non-NULL wl_buffer, then the out_of_buffer protocol error is raised
 * when the surface state is applied. A NULL wl_buffer does not raise the
 * out_of_buffer error.
 *
 * If the wl_surface associated with the wp_viewport is destroyed,
 * all wp_viewport requests except 'destroy' raise the protocol error
 * no_surface.
 *
 * If the wp_viewport object is destroyed, the crop and scale
 * state is removed from the wl_surface. The change will be applied
 * on the next wl_surface.commit.
 * @section page_iface_wp_viewport_api API
 * See @ref iface_wp_viewport.
 */
/**
 * @defgroup iface_wp_viewport The wp_viewport interface
 *
 * An additional interface to a wl_surface object, which allows the
 * client to specify the cropping and scaling of the surface
 * contents.
 *
 * This interface works with two concepts: the source rectangle (src_x,
 * src_y, src_width, src_height), and the destination size (dst_width,
 * dst_height). The contents of the source rectangle are scaled to the
 * destination size, and content outside the source rectangle is ignored.
 * This state is double-buffered, and is applied on the next
 * wl_surface.commit.
 *
 * The two parts of crop and scale state are independent: the source
 * rectangle, and the destination size. Initially both are unset, that
 * is, no scaling is applied. The whole of the current wl_buffer is
 * used as the source, and the surface size is as defined in
 * wl_surface.attach.
 *
 * If the destination size is set, it causes the surface size to become
 * dst_width, dst_height. The source (rectangle) is scaled to exactly
 * this size. This overrides whatever the attached wl_buffer size is,
 * unless the wl_buffer is NULL. If the wl_buffer is NULL, the surface
 * has no content and therefore no size. Otherwise, the size is always
 * at least 1x1 in surface local coordinates.
 *
 * If the source rectangle is set, it defines what area of the wl_buffer is
 * taken as the source. If the source rectangle is set and the destination
 * size is not set, then src_width and src_height must be integers, and the
 * surface size becomes the source rectangle size. This results in cropping
 * without scaling. If src_width or src_height are not integers and
 * destination size is not set, the bad_size protocol error is raised when
 * the surface state is applied.
 *
 * The coordinate transformations from buffer pixel coordinates up to
 * the surface-local coordinates happen in the following order:
 * 1. buffer_transform (wl_surface.set_buffer_transform)
 * 2. buffer_scale (wl_surface.set_buffer_scale)
 * 3. crop and scale (wp_viewport.set*)
 * This means, that the source rectangle coordinates of crop and scale
 * are given in the coordinates after the buffer transform and scale,
 * i.e. in the coordinates that would be the surface-local coordinates
 * if the crop and scale was not applied.
 *
 * If src_x or src_y are negative, the bad_value protocol error is raised.
 * Otherwise, if the source rectangle is partially or completely outside of
 * the non-NULL wl_buffer, then the out_of_buffer protocol error is raised
 * when the surface state is applied. A NULL wl_buffer does not raise the
 * out_of_buffer error.
 *
 * If the wl_surface associated with the wp_viewport is destroyed,
 * all wp_viewport requests except 'destroy' raise the protocol error
 * no_surface.
 *
 * If the wp_viewport object is destroyed, the crop and scale
 * state is removed from the wl_surface. The change will be applied
 * on the next wl_surface.commit.
 */
extern const struct wl_interface wp_viewport_interface;
#endif

#ifndef WP_VIEWPORTER_ERROR_ENUM
#define WP_VIEWPORTER_ERROR_ENUM
enum wp_viewporter_error {
	/**
	 * the surface already has a viewport object associated
	 */
	WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS = 0,
};
#endif /* WP_VIEWPORTER_ERROR_ENUM */

/**
 * @ingroup iface_wp_viewporter
 * @struct wp_viewporter_interface
 */
struct wp_viewporter_interface {
	/**
	 * unbind from the cropping and scaling interface
	 *
	 * Informs the server that the client will not be using this
	 * protocol object anymore. This does not affect any other objects,
	 * wp_viewport objects included.
	 */
	void (*destroy)(struct wl_client *client,
			struct wl_resource *resource);
	/**
	 * extend surface interface for crop and scale
	 *
	 * Instantiate an interface extension for the given wl_surface to
	 * crop and scale its content. If the given wl_surface already has
	 * a wp_viewport object associated, the viewport_exists protocol
	 * error is raised.
	 * @param id the new viewport interface id
	 * @param surface the surface
	 */
	void (*get_viewport)(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t id,
			     struct wl_resource *surface);
};


/**
 * @ingroup iface_wp_viewporter
 */
#define WP_VIEWPORTER_DESTROY_SINCE_VERSION 1
/**
 * @ingroup iface_wp_viewporter
 */
#define WP_VIEWPORTER_GET_VIEWPORT_SINCE_VERSION 1

#ifndef WP_VIEWPORT_ERROR_ENUM
#define WP_VIEWPORT_ERROR_ENUM
enum wp_viewport_error {
	/**
	 * negative or zero values in width or height
	 */
	WP_VIEWPORT_ERROR_BAD_VALUE = 0,
	/**
	 * destination size is not integer
	 */
	WP_VIEWPORT_ERROR_BAD_SIZE = 1,
	/**
	 * source rectangle extends outside of the content area
	 */
	WP_VIEWPORT_ERROR_OUT_OF_BUFFER = 2,
	/**
	 * the wl_surface was destroyed
	 */
	WP_VIEWPORT_ERROR_NO_SURFACE = 3,
};
#endif /* WP_VIEWPORT_ERROR_ENUM */

/**
 * @ingroup iface_wp_viewport
 * @struct wp_viewport_interface
 */
struct wp_viewport_interface {
	/**
	 * remove scaling and cropping from the surface
	 *
	 * The associated wl_surface's crop and scale state is removed.
	 * The change is applied on the next wl_surface.commit.
	 */
	void (*destroy)(struct wl_client *client,
			struct wl_resource *resource);
	/**
	 * set the source rectangle for cropping
	 *
	 * Set the source rectangle of the associated wl_surface. See
	 * wp_viewport for the description, and relation to the wl_buffer
	 * size.
	 *
	 * If all of x, y, width and height are -1.0, the source rectangle
	 * is unset instead. Any other set of values where width or height
	 * are zero or negative, or x or y are negative, raise the
	 * bad_value protocol error.
	 *
	 * The crop and scale state is double-buffered state, and will be
	 * applied on the next wl_surface.commit.
	 * @param x source rectangle x
	 * @param y source rectangle y
	 * @param width source rectangle width
	 * @param height source rectangle height
	 */
	void (*set_source)(struct wl_client *client,
			   struct wl_resource *resource,
			   wl_fixed_t x,
			   wl_fixed_t y,
			   wl_fixed_t width,
			   wl_fixed_t height);
	/**
	 * set the surface size for scaling
	 *
	 * Set the destination size of the associated wl_surface. See
	 * wp_viewport for the description, and relation to the wl_buffer
	 * size.
	 *
	 * If width is -1 and height is -1, the destination size is unset
	 * instead. Any other pair of values for width and height that
	 * contains zero or negative values raises the bad_value protocol
	 * error.
	 *
	 * The crop and scale state is double-buffered state, and will be
	 * applied on the next wl_surface.commit.
	 * @param width surface width
	 * @param height surface height
	 */
	void (*set_destination)(struct wl_client *client,
				struct wl_resource *resource,
				int32_t width,
				int32_t height);
};


/**
 * @ingroup iface_wp_viewport
 */
#define WP_VIEWPORT_DESTROY_SINCE_VERSION 1
/**
 * @ingroup iface_wp_viewport
 */
#define WP_VIEWPORT_SET_SOURCE_SINCE_VERSION 1
/**
 * @ingroup iface_wp_viewport
 */
#define WP_VIEWPORT_SET_DESTINATION_SINCE_VERSION 1

#ifdef  __cplusplus
}
#endif

#endif


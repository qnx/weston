/* Generated by wayland-scanner 1.21.0 */

#ifndef WESTON_DEBUG_CLIENT_PROTOCOL_H
#define WESTON_DEBUG_CLIENT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * @page page_weston_debug The weston_debug protocol
 * @section page_ifaces_weston_debug Interfaces
 * - @subpage page_iface_weston_debug_v1 - weston internal debugging
 * - @subpage page_iface_weston_debug_stream_v1 - A subscribed debug stream
 * @section page_copyright_weston_debug Copyright
 * <pre>
 *
 * Copyright © 2017 Pekka Paalanen pq@iki.fi
 * Copyright © 2018 Zodiac Inflight Innovations
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
struct weston_debug_stream_v1;
struct weston_debug_v1;

#ifndef WESTON_DEBUG_V1_INTERFACE
#define WESTON_DEBUG_V1_INTERFACE
/**
 * @page page_iface_weston_debug_v1 weston_debug_v1
 * @section page_iface_weston_debug_v1_desc Description
 *
 * This is a generic debugging interface for Weston internals, the global
 * object advertized through wl_registry.
 *
 * WARNING: This interface by design allows a denial-of-service attack. It
 * should not be offered in production, or proper authorization mechanisms
 * must be enforced.
 *
 * The idea is for a client to provide a file descriptor that the server
 * uses for printing debug information. The server uses the file
 * descriptor in blocking writes mode, which exposes the denial-of-service
 * risk. The blocking mode is necessary to ensure all debug messages can
 * be easily printed in place. It also ensures message ordering if a
 * client subscribes to more than one debug stream.
 *
 * The available debugging features depend on the server.
 *
 * A debug stream can be one-shot where the server prints the requested
 * information and then closes it, or continuous where server keeps on
 * printing until the client stops it. Or anything in between.
 * @section page_iface_weston_debug_v1_api API
 * See @ref iface_weston_debug_v1.
 */
/**
 * @defgroup iface_weston_debug_v1 The weston_debug_v1 interface
 *
 * This is a generic debugging interface for Weston internals, the global
 * object advertized through wl_registry.
 *
 * WARNING: This interface by design allows a denial-of-service attack. It
 * should not be offered in production, or proper authorization mechanisms
 * must be enforced.
 *
 * The idea is for a client to provide a file descriptor that the server
 * uses for printing debug information. The server uses the file
 * descriptor in blocking writes mode, which exposes the denial-of-service
 * risk. The blocking mode is necessary to ensure all debug messages can
 * be easily printed in place. It also ensures message ordering if a
 * client subscribes to more than one debug stream.
 *
 * The available debugging features depend on the server.
 *
 * A debug stream can be one-shot where the server prints the requested
 * information and then closes it, or continuous where server keeps on
 * printing until the client stops it. Or anything in between.
 */
extern const struct wl_interface weston_debug_v1_interface;
#endif
#ifndef WESTON_DEBUG_STREAM_V1_INTERFACE
#define WESTON_DEBUG_STREAM_V1_INTERFACE
/**
 * @page page_iface_weston_debug_stream_v1 weston_debug_stream_v1
 * @section page_iface_weston_debug_stream_v1_desc Description
 *
 * Represents one subscribed debug stream, created with
 * weston_debug_v1.subscribe. When the object is created, it is associated
 * with a given file descriptor. The server will continue writing to the
 * file descriptor until the object is destroyed or the server sends an
 * event through the object.
 * @section page_iface_weston_debug_stream_v1_api API
 * See @ref iface_weston_debug_stream_v1.
 */
/**
 * @defgroup iface_weston_debug_stream_v1 The weston_debug_stream_v1 interface
 *
 * Represents one subscribed debug stream, created with
 * weston_debug_v1.subscribe. When the object is created, it is associated
 * with a given file descriptor. The server will continue writing to the
 * file descriptor until the object is destroyed or the server sends an
 * event through the object.
 */
extern const struct wl_interface weston_debug_stream_v1_interface;
#endif

/**
 * @ingroup iface_weston_debug_v1
 * @struct weston_debug_v1_listener
 */
struct weston_debug_v1_listener {
	/**
	 * advertise available debug scope
	 *
	 * Advertises an available debug scope which the client may be
	 * able to bind to. No information is provided by the server about
	 * the content contained within the debug streams provided by the
	 * scope, once a client has subscribed.
	 * @param name debug stream name
	 * @param description human-readable description of the debug scope
	 */
	void (*available)(void *data,
			  struct weston_debug_v1 *weston_debug_v1,
			  const char *name,
			  const char *description);
};

/**
 * @ingroup iface_weston_debug_v1
 */
static inline int
weston_debug_v1_add_listener(struct weston_debug_v1 *weston_debug_v1,
			     const struct weston_debug_v1_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) weston_debug_v1,
				     (void (**)(void)) listener, data);
}

#define WESTON_DEBUG_V1_DESTROY 0
#define WESTON_DEBUG_V1_SUBSCRIBE 1

/**
 * @ingroup iface_weston_debug_v1
 */
#define WESTON_DEBUG_V1_AVAILABLE_SINCE_VERSION 1

/**
 * @ingroup iface_weston_debug_v1
 */
#define WESTON_DEBUG_V1_DESTROY_SINCE_VERSION 1
/**
 * @ingroup iface_weston_debug_v1
 */
#define WESTON_DEBUG_V1_SUBSCRIBE_SINCE_VERSION 1

/** @ingroup iface_weston_debug_v1 */
static inline void
weston_debug_v1_set_user_data(struct weston_debug_v1 *weston_debug_v1, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) weston_debug_v1, user_data);
}

/** @ingroup iface_weston_debug_v1 */
static inline void *
weston_debug_v1_get_user_data(struct weston_debug_v1 *weston_debug_v1)
{
	return wl_proxy_get_user_data((struct wl_proxy *) weston_debug_v1);
}

static inline uint32_t
weston_debug_v1_get_version(struct weston_debug_v1 *weston_debug_v1)
{
	return wl_proxy_get_version((struct wl_proxy *) weston_debug_v1);
}

/**
 * @ingroup iface_weston_debug_v1
 *
 * Destroys the factory object, but does not affect any other objects.
 */
static inline void
weston_debug_v1_destroy(struct weston_debug_v1 *weston_debug_v1)
{
	wl_proxy_marshal_flags((struct wl_proxy *) weston_debug_v1,
			 WESTON_DEBUG_V1_DESTROY, NULL, wl_proxy_get_version((struct wl_proxy *) weston_debug_v1), WL_MARSHAL_FLAG_DESTROY);
}

/**
 * @ingroup iface_weston_debug_v1
 *
 * Subscribe to a named debug stream. The server will start printing
 * to the given file descriptor.
 *
 * If the named debug stream is a one-shot dump, the server will send
 * weston_debug_stream_v1.complete event once all requested data has
 * been printed. Otherwise, the server will continue streaming debug
 * prints until the subscription object is destroyed.
 *
 * If the debug stream name is unknown to the server, the server will
 * immediately respond with weston_debug_stream_v1.failure event.
 */
static inline struct weston_debug_stream_v1 *
weston_debug_v1_subscribe(struct weston_debug_v1 *weston_debug_v1, const char *name, int32_t streamfd)
{
	struct wl_proxy *stream;

	stream = wl_proxy_marshal_flags((struct wl_proxy *) weston_debug_v1,
			 WESTON_DEBUG_V1_SUBSCRIBE, &weston_debug_stream_v1_interface, wl_proxy_get_version((struct wl_proxy *) weston_debug_v1), 0, name, streamfd, NULL);

	return (struct weston_debug_stream_v1 *) stream;
}

/**
 * @ingroup iface_weston_debug_stream_v1
 * @struct weston_debug_stream_v1_listener
 */
struct weston_debug_stream_v1_listener {
	/**
	 * server completed the debug stream
	 *
	 * The server has successfully finished writing to and has closed
	 * the associated file descriptor.
	 *
	 * This event is delivered only for one-shot debug streams where
	 * the server dumps some data and stop. This is never delivered for
	 * continuous debbug streams because they by definition never
	 * complete.
	 */
	void (*complete)(void *data,
			 struct weston_debug_stream_v1 *weston_debug_stream_v1);
	/**
	 * server cannot continue the debug stream
	 *
	 * The server has stopped writing to and has closed the
	 * associated file descriptor. The data already written to the file
	 * descriptor is correct, but it may be truncated.
	 *
	 * This event may be delivered at any time and for any kind of
	 * debug stream. It may be due to a failure in or shutdown of the
	 * server. The message argument may provide a hint of the reason.
	 * @param message human readable reason
	 */
	void (*failure)(void *data,
			struct weston_debug_stream_v1 *weston_debug_stream_v1,
			const char *message);
};

/**
 * @ingroup iface_weston_debug_stream_v1
 */
static inline int
weston_debug_stream_v1_add_listener(struct weston_debug_stream_v1 *weston_debug_stream_v1,
				    const struct weston_debug_stream_v1_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) weston_debug_stream_v1,
				     (void (**)(void)) listener, data);
}

#define WESTON_DEBUG_STREAM_V1_DESTROY 0

/**
 * @ingroup iface_weston_debug_stream_v1
 */
#define WESTON_DEBUG_STREAM_V1_COMPLETE_SINCE_VERSION 1
/**
 * @ingroup iface_weston_debug_stream_v1
 */
#define WESTON_DEBUG_STREAM_V1_FAILURE_SINCE_VERSION 1

/**
 * @ingroup iface_weston_debug_stream_v1
 */
#define WESTON_DEBUG_STREAM_V1_DESTROY_SINCE_VERSION 1

/** @ingroup iface_weston_debug_stream_v1 */
static inline void
weston_debug_stream_v1_set_user_data(struct weston_debug_stream_v1 *weston_debug_stream_v1, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) weston_debug_stream_v1, user_data);
}

/** @ingroup iface_weston_debug_stream_v1 */
static inline void *
weston_debug_stream_v1_get_user_data(struct weston_debug_stream_v1 *weston_debug_stream_v1)
{
	return wl_proxy_get_user_data((struct wl_proxy *) weston_debug_stream_v1);
}

static inline uint32_t
weston_debug_stream_v1_get_version(struct weston_debug_stream_v1 *weston_debug_stream_v1)
{
	return wl_proxy_get_version((struct wl_proxy *) weston_debug_stream_v1);
}

/**
 * @ingroup iface_weston_debug_stream_v1
 *
 * Destroys the object, which causes the server to stop writing into
 * and closes the associated file descriptor if it was not closed
 * already.
 *
 * Use a wl_display.sync if the clients needs to guarantee the file
 * descriptor is closed before continuing.
 */
static inline void
weston_debug_stream_v1_destroy(struct weston_debug_stream_v1 *weston_debug_stream_v1)
{
	wl_proxy_marshal_flags((struct wl_proxy *) weston_debug_stream_v1,
			 WESTON_DEBUG_STREAM_V1_DESTROY, NULL, wl_proxy_get_version((struct wl_proxy *) weston_debug_stream_v1), WL_MARSHAL_FLAG_DESTROY);
}

#ifdef  __cplusplus
}
#endif

#endif


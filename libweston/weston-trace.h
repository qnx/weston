/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

/* This code was taken from the Mesa project, and heavily modified to
 * suit weston's needs.
 */

#ifndef WESTON_TRACE_H
#define WESTON_TRACE_H

#include "perfetto/u_perfetto.h"

#if defined(HAVE_PERFETTO)

#if !defined(HAVE___BUILTIN_EXPECT)
#  define __builtin_expect(x, y) (x)
#endif

#ifndef likely
#  ifdef HAVE___BUILTIN_EXPECT
#    define likely(x)   __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x)   (x)
#    define unlikely(x) (x)
#  endif
#endif

/* note that util_perfetto_is_tracing_enabled always returns false until
 * util_perfetto_init is called
 */
#define _WESTON_TRACE_BEGIN(name)                                             \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_begin(name);                      \
	} while (0)

#define _WESTON_TRACE_FLOW_BEGIN(name, id)                                    \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_begin_flow(name, id);             \
	} while (0)

#define _WESTON_TRACE_END()                                                   \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_end();                            \
	} while (0)

#define _WESTON_TRACE_SET_COUNTER(name, value)                                \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_counter_set(name, value);               \
	} while (0)

#define _WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp) \
	do {                                                                     \
		if (unlikely(util_perfetto_is_tracing_enabled()))                \
			util_perfetto_trace_full_begin(name, track_id, flow_id,  \
						       clock, timestamp);        \
	} while (0)

#define _WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)         \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_full_end(name, track_id,          \
						     clock, timestamp);       \
	} while (0)

#if __has_attribute(cleanup) && __has_attribute(unused)

#define _WESTON_TRACE_SCOPE_VAR_CONCAT(name, suffix) name##suffix
#define _WESTON_TRACE_SCOPE_VAR(suffix)                                       \
	_WESTON_TRACE_SCOPE_VAR_CONCAT(_weston_trace_scope_, suffix)

/* This must expand to a single non-scoped statement for
 *
 *    if (cond)
 *       _WESTON_TRACE_SCOPE(...)
 *
 * to work.
 */
#define _WESTON_TRACE_SCOPE(name)                                             \
	int _WESTON_TRACE_SCOPE_VAR(__LINE__)                                 \
		__attribute__((cleanup(_weston_trace_scope_end), unused)) =   \
			_weston_trace_scope_begin(name)

#define _WESTON_TRACE_SCOPE_FLOW(name, id)                                    \
	int _WESTON_TRACE_SCOPE_VAR(__LINE__)                                 \
		__attribute__((cleanup(_weston_trace_scope_end), unused)) =   \
			_weston_trace_scope_flow_begin(name, id)

static inline int
_weston_trace_scope_begin(const char *name)
{
	_WESTON_TRACE_BEGIN(name);
	return 0;
}

static inline int
_weston_trace_scope_flow_begin(const char *name, uint64_t *id)
{
	if (*id == 0)
		*id = util_perfetto_next_id();
	_WESTON_TRACE_FLOW_BEGIN(name, *id);
	return 0;
}

static inline void
_weston_trace_scope_end(int *scope)
{
	_WESTON_TRACE_END();
}

#else

#define _WESTON_TRACE_SCOPE(name)

#endif /* __has_attribute(cleanup) && __has_attribute(unused) */

#else /* No perfetto, make these all do nothing */

#define _WESTON_TRACE_SCOPE(name)
#define _WESTON_TRACE_SCOPE_FLOW(name, id)
#define _WESTON_TRACE_FUNC()
#define _WESTON_TRACE_FUNC_FLOW(id)
#define _WESTON_TRACE_SET_COUNTER(name, value)
#define _WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define _WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)

#endif /* HAVE_PERFETTO */

#define WESTON_TRACE_SCOPE(name) _WESTON_TRACE_SCOPE(name)
#define WESTON_TRACE_SCOPE_FLOW(name, id) _WESTON_TRACE_SCOPE_FLOW(name, id)
#define WESTON_TRACE_FUNC() _WESTON_TRACE_SCOPE(__func__)
#define WESTON_TRACE_FUNC_FLOW(id) _WESTON_TRACE_SCOPE_FLOW(__func__, id)
#define WESTON_TRACE_SET_COUNTER(name, value) _WESTON_TRACE_SET_COUNTER(name, value)
#define WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp) \
	_WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp) \
	_WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)

#endif /* WESTON_TRACE_H */

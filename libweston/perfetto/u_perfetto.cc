/*
 * Copyright © 2021 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* This code was lifted from Mesa, some code removed (eg: weston does not support
 * WIN32), and edited to conform to weston coding style.
 *
 * The original copyright boilerplate has been retained.
 */

#include "config.h"

#include "u_perfetto.h"

#include <perfetto.h>

/* perfetto requires string literals */
#define UTIL_PERFETTO_CATEGORY_DEFAULT_STR "weston.default"

PERFETTO_DEFINE_CATEGORIES(
	perfetto::Category(UTIL_PERFETTO_CATEGORY_DEFAULT_STR)
	.SetDescription("Weston default events"));

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

int util_perfetto_tracing_state;

static uint64_t util_perfetto_unique_id = 1;

static uint32_t
clockid_to_perfetto_clock(clockid_t clock)
{
	switch (clock) {
	case CLOCK_REALTIME:
		return perfetto::protos::pbzero::BUILTIN_CLOCK_REALTIME;
	case CLOCK_REALTIME_COARSE:
		return perfetto::protos::pbzero::BUILTIN_CLOCK_REALTIME_COARSE;
	case CLOCK_MONOTONIC:
		return perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC;
	case CLOCK_MONOTONIC_COARSE:
		return perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE;
	case CLOCK_MONOTONIC_RAW:
		return perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW;
	case CLOCK_BOOTTIME:
		return perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
	}
	return perfetto::protos::pbzero::BUILTIN_CLOCK_UNKNOWN;
}

static void
util_perfetto_update_tracing_state(void)
{
	p_atomic_set(&util_perfetto_tracing_state,
		     TRACE_EVENT_CATEGORY_ENABLED(UTIL_PERFETTO_CATEGORY_DEFAULT_STR));
}

void
util_perfetto_trace_begin(const char *name)
{
	TRACE_EVENT_BEGIN(UTIL_PERFETTO_CATEGORY_DEFAULT_STR, nullptr,
			  [&](perfetto::EventContext ctx) { ctx.event()->set_name(name); });
}

void
util_perfetto_trace_end(void)
{
	TRACE_EVENT_END(UTIL_PERFETTO_CATEGORY_DEFAULT_STR);

	util_perfetto_update_tracing_state();
}

void
util_perfetto_trace_begin_flow(const char *fname, uint64_t id)
{
	TRACE_EVENT_BEGIN(UTIL_PERFETTO_CATEGORY_DEFAULT_STR, nullptr,
			  perfetto::Flow::ProcessScoped(id),
			  [&](perfetto::EventContext ctx) { ctx.event()->set_name(fname); });
}

void
util_perfetto_trace_full_begin(const char *fname, uint64_t track_id, uint64_t id, clockid_t clock, uint64_t timestamp)
{
	if (id) {
		TRACE_EVENT_BEGIN(UTIL_PERFETTO_CATEGORY_DEFAULT_STR,
				  nullptr,
				  perfetto::Track(track_id),
				  perfetto::TraceTimestamp{clockid_to_perfetto_clock(clock), timestamp},
				  perfetto::Flow::ProcessScoped(id),
				  [&](perfetto::EventContext ctx) { ctx.event()->set_name(fname); });
		return;
	}

   TRACE_EVENT_BEGIN(UTIL_PERFETTO_CATEGORY_DEFAULT_STR,
		     nullptr,
		     perfetto::Track(track_id),
		     perfetto::TraceTimestamp{clockid_to_perfetto_clock(clock), timestamp},
		     [&](perfetto::EventContext ctx) { ctx.event()->set_name(fname); });
}

uint64_t
util_perfetto_new_track(const char *name)
{
	uint64_t track_id = util_perfetto_next_id();
	auto track = perfetto::Track(track_id);
	auto desc = track.Serialize();

	desc.set_name(name);
	perfetto::TrackEvent::SetTrackDescriptor(track, desc);
	return track_id;
}

void
util_perfetto_trace_full_end(const char *name, uint64_t track_id, clockid_t clock, uint64_t timestamp)
{
	TRACE_EVENT_END(UTIL_PERFETTO_CATEGORY_DEFAULT_STR,
			perfetto::Track(track_id),
			perfetto::TraceTimestamp{clockid_to_perfetto_clock(clock), timestamp});

	util_perfetto_update_tracing_state();
}

void
util_perfetto_counter_set(const char *name, double value)
{
	TRACE_COUNTER(UTIL_PERFETTO_CATEGORY_DEFAULT_STR,
		      perfetto::DynamicString(name), value);
}

uint64_t
util_perfetto_next_id(void)
{
	return p_atomic_inc_return(&util_perfetto_unique_id);
}

class UtilPerfettoObserver : public perfetto::TrackEventSessionObserver {
 public:
	UtilPerfettoObserver() { perfetto::TrackEvent::AddSessionObserver(this); }

	void OnStart(const perfetto::DataSourceBase::StartArgs &) override {
		util_perfetto_update_tracing_state();
	}

	/* XXX There is no PostStop callback.  We have to call
	 * util_perfetto_update_tracing_state occasionally to poll.
	 */
};

static void
util_perfetto_fini(void)
{
	perfetto::Tracing::Shutdown();
}

static bool perfetto_initialized = false;

void
util_perfetto_init(void)
{
	if (perfetto_initialized)
		return;

	perfetto::TracingInitArgs args;
	args.backends = perfetto::kSystemBackend;
	perfetto::Tracing::Initialize(args);

	static UtilPerfettoObserver observer;
	perfetto::TrackEvent::Register();

	atexit(&util_perfetto_fini);
}

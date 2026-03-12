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

#include "color-representation-common.h"

#include "image-iter.h"
#include "pixel-formats.h"
#include "shared/weston-drm-fourcc.h"
#include "shared/xalloc.h"
#include "weston-test-client-helper.h"
#include "weston-test-assert.h"
#include "xdg-client-helper.h"

static const struct setup_args my_setup_args[] = {
	{
		.meta.name = "GL - dmabuf renderer",
		.renderer = WESTON_RENDERER_GL,
		.buffer_type = CLIENT_BUFFER_TYPE_DMABUF,
	},
};

static enum test_result_code
fixture_setup(struct weston_test_harness *harness, const struct setup_args *arg)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.backend = WESTON_BACKEND_DRM;
	setup.renderer = arg->renderer;
	setup.logging_scopes = "log,drm-backend";

	/* Currently enforced by vkms. Set as a reminder for the future. */
	setup.width = 1024;
	setup.height = 768;

	setup.test_quirks.required_capabilities = WESTON_CAP_COLOR_REP;
	setup.test_quirks.gl_force_import_yuv_fallback =
		arg->gl_force_import_yuv_fallback;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

TEST_P(color_representation_drm, color_state_cases) {
	const struct color_state *color_state = data;
	const struct setup_args *args = &my_setup_args[get_test_fixture_index()];

	return test_color_representation(color_state, args->buffer_type,
					 FB_PRESENTED_ZERO_COPY);
}

/*
 * Copyright 2025 Collabora, Ltd.
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

#include <libweston/libweston.h>
#include <libweston/linalg-3.h>
#include <lcms2.h>

#include "color.h"
#include "color-lcms.h"

/** Release all transformer members. */
void
cmlcms_color_transformer_fini(struct cmlcms_color_transformer *t)
{
	if (t->icc_chain) {
		cmsDeleteTransform(t->icc_chain);
		t->icc_chain = NULL;
	}
}

/** Push the given points through the transformer
 *
 * \param compositor The compositor, for logging assertions.
 * \param t The transformer to execute.
 * \param[out] dst The destination array.
 * \param[in] src The source array.
 * \param len The length of both arrays.
 */
void
cmlcms_color_transformer_eval(struct weston_compositor *compositor,
			      const struct cmlcms_color_transformer *t,
			      struct weston_vec3f *dst,
			      const struct weston_vec3f *src,
			      size_t len)
{
	cmsDoTransform(t->icc_chain, src, dst, len);
}

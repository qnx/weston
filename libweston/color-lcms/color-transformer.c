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
#include "color-properties.h"
#include "color-operations.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"
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
	const struct weston_vec3f *in = src;
	struct weston_vec3f *end = dst + len;
	struct weston_vec3f *out;

	weston_assert_u8_ne(compositor, t->element_mask, 0);

	if (t->element_mask & CMLCMS_TRANSFORMER_CURVE1) {
		weston_color_curve_sample(compositor, &t->curve1, in, dst, len);
		in = dst;
	}

	if (t->element_mask & CMLCMS_TRANSFORMER_LIN1) {
		for (out = dst; out < end; out++, in++) {
			*out = weston_v3f_add_v3f(weston_m3f_mul_v3f(t->lin1.matrix, *in),
						  t->lin1.offset);
		}
		in = dst;
	}

	if (t->element_mask & CMLCMS_TRANSFORMER_ICC_CHAIN) {
		cmsDoTransform(t->icc_chain, in, dst, len);
		in = dst;
	}

	if (t->element_mask & CMLCMS_TRANSFORMER_LIN2) {
		for (out = dst; out < end; out++, in++) {
			*out = weston_v3f_add_v3f(weston_m3f_mul_v3f(t->lin2.matrix, *in),
						  t->lin2.offset);
		}
		in = dst;
	}

	if (t->element_mask & CMLCMS_TRANSFORMER_CURVE2) {
		weston_color_curve_sample(compositor, &t->curve2, in, dst, len);
		in = dst;
	}
}

static void
transformer_curve_fprint(FILE *fp,
			 int indent,
			 const char *step,
			 const struct weston_color_curve *curve)
{
	const struct weston_color_curve_enum *en;
	const char *dir = "???";
	unsigned i;

	if (curve->type != WESTON_COLOR_CURVE_TYPE_ENUM) {
		fprintf(fp, "%*s[unexpectedly not enum]\n", indent, "");
		return;
	}
	en = &curve->u.enumerated;

	switch (en->tf_direction) {
	case WESTON_FORWARD_TF:
		dir = "forward";
		break;
	case WESTON_INVERSE_TF:
		dir = "inverse";
		break;
	}

	fprintf(fp, "%*s%s, %s %s", indent, "", step, dir, en->tf.info->desc);
	if (en->tf.info->count_parameters > 0) {
		fprintf(fp, ": ");
		for (i = 0; i < en->tf.info->count_parameters; i++)
			fprintf(fp, " % .4f", en->tf.params[i]);
	}
	fprintf(fp, "\n");
}

static void
transformer_linear_fprint(FILE *fp,
			  int indent,
			  const char *step,
			  const struct weston_color_mapping_matrix *lin)
{
	unsigned r, c;

	fprintf(fp, "%*s%s\n", indent, "", step);
	for (r = 0; r < 3; r++) {
		fprintf(fp, "%*s", indent + 1, "");
		for (c = 0; c < 3; c++)
			fprintf(fp, " %8.4f", lin->matrix.col[c].el[r]);
		fprintf(fp, " %8.4f\n", lin->offset.el[r]);
	}
}

static void
cmlcms_color_transformer_details_fprint(FILE *fp,
					int indent,
					const struct cmlcms_color_transformer *t)
{
	if (t->element_mask & CMLCMS_TRANSFORMER_CURVE1)
		transformer_curve_fprint(fp, indent, "curve1", &t->curve1);

	if (t->element_mask & CMLCMS_TRANSFORMER_LIN1)
		transformer_linear_fprint(fp, indent, "lin1", &t->lin1);

	if (t->element_mask & CMLCMS_TRANSFORMER_ICC_CHAIN)
		fprintf(fp, "%*sICC-to-ICC transform pipeline\n", indent, "");

	if (t->element_mask & CMLCMS_TRANSFORMER_LIN2)
		transformer_linear_fprint(fp, indent, "lin2", &t->lin2);

	if (t->element_mask & CMLCMS_TRANSFORMER_CURVE2)
		transformer_curve_fprint(fp, indent, "curve2", &t->curve2);
}

char *
cmlcms_color_transformer_string(int indent,
				const struct cmlcms_color_transformer *t)
{
	FILE *fp;
	char *str = NULL;
	size_t size = 0;

	fp = open_memstream(&str, &size);
	abort_oom_if_null(fp);

	fprintf(fp, "%*sColor transform sampler for 3D LUT\n", indent, "");
	cmlcms_color_transformer_details_fprint(fp, indent + 2, t);

	fclose(fp);
	abort_oom_if_null(str);

	return str;
}

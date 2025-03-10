/*
 * Copyright 2021 Collabora, Ltd.
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include <assert.h>
#include <string.h>

#include <libweston/libweston.h>
#include "color.h"
#include "gl-renderer.h"
#include "gl-renderer-internal.h"

#include "shared/weston-egl-ext.h"

struct gl_renderer_color_curve {
	enum gl_shader_color_curve type;
	union {
		struct {
			GLuint tex;
			float scale;
			float offset;
		} lut_3x1d;
		struct {
			GLfloat params[3][10];
			GLboolean clamped_input;
		} parametric;
	} u;
};

struct gl_renderer_color_mapping {
	enum gl_shader_color_mapping type;
	union {
		struct {
			GLuint tex3d;
			float scale;
			float offset;
		} lut3d;
		struct weston_color_mapping_matrix mat;
	};
} ;

struct gl_renderer_color_transform {
	struct weston_color_transform *owner;
	struct wl_listener destroy_listener;
	struct gl_renderer_color_curve pre_curve;
	struct gl_renderer_color_mapping mapping;
	struct gl_renderer_color_curve post_curve;
};

static void
gl_renderer_color_curve_fini(struct gl_renderer_color_curve *gl_curve)
{
	switch (gl_curve->type) {
	case SHADER_COLOR_CURVE_IDENTITY:
	case SHADER_COLOR_CURVE_LINPOW:
	case SHADER_COLOR_CURVE_POWLIN:
		break;
	case SHADER_COLOR_CURVE_LUT_3x1D:
		gl_texture_fini(&gl_curve->u.lut_3x1d.tex);
		break;
	};
}

static void
gl_renderer_color_mapping_fini(struct gl_renderer_color_mapping *gl_mapping)
{
	if (gl_mapping->type == SHADER_COLOR_MAPPING_3DLUT &&
	    gl_mapping->lut3d.tex3d)
		gl_texture_fini(&gl_mapping->lut3d.tex3d);
}

static void
gl_renderer_color_transform_destroy(struct gl_renderer_color_transform *gl_xform)
{
	gl_renderer_color_curve_fini(&gl_xform->pre_curve);
	gl_renderer_color_curve_fini(&gl_xform->post_curve);
	gl_renderer_color_mapping_fini(&gl_xform->mapping);
	wl_list_remove(&gl_xform->destroy_listener.link);
	free(gl_xform);
}

static void
color_transform_destroy_handler(struct wl_listener *l, void *data)
{
	struct gl_renderer_color_transform *gl_xform;

	gl_xform = wl_container_of(l, gl_xform, destroy_listener);
	assert(gl_xform->owner == data);

	gl_renderer_color_transform_destroy(gl_xform);
}

static struct gl_renderer_color_transform *
gl_renderer_color_transform_create(struct weston_color_transform *xform)
{
	struct gl_renderer_color_transform *gl_xform;

	gl_xform = zalloc(sizeof *gl_xform);
	if (!gl_xform)
		return NULL;

	gl_xform->owner = xform;
	gl_xform->destroy_listener.notify = color_transform_destroy_handler;
	wl_signal_add(&xform->destroy_signal, &gl_xform->destroy_listener);

	return gl_xform;
}

static struct gl_renderer_color_transform *
gl_renderer_color_transform_get(struct weston_color_transform *xform)
{
	struct wl_listener *l;

	l = wl_signal_get(&xform->destroy_signal,
			  color_transform_destroy_handler);
	if (!l)
		return NULL;

	return container_of(l, struct gl_renderer_color_transform,
			    destroy_listener);
}

static void
gl_color_curve_parametric(struct gl_renderer_color_curve *gl_curve,
			  const struct weston_color_curve *curve)
{
	const struct weston_color_curve_parametric *parametric = &curve->u.parametric;

	ARRAY_COPY(gl_curve->u.parametric.params, parametric->params);

	gl_curve->u.parametric.clamped_input = parametric->clamped_input;
}

static bool
gl_color_curve_linpow(struct gl_renderer_color_curve *gl_curve,
		      const struct weston_color_curve *curve)
{
	gl_curve->type = SHADER_COLOR_CURVE_LINPOW;
	gl_color_curve_parametric(gl_curve, curve);

	return true;
}

static bool
gl_color_curve_powlin(struct gl_renderer_color_curve *gl_curve,
		      const struct weston_color_curve *curve)
{
	gl_curve->type = SHADER_COLOR_CURVE_POWLIN;
	gl_color_curve_parametric(gl_curve, curve);

	return true;
}

static bool
gl_color_curve_lut_3x1d(struct gl_renderer *gr,
			struct gl_renderer_color_curve *gl_curve,
			const struct weston_color_curve *curve,
			struct weston_color_transform *xform)
{
	GLint filters[] = { GL_LINEAR, GL_LINEAR };
	const unsigned lut_len = curve->u.lut_3x1d.optimal_len;
	const unsigned nr_rows = 4;
	struct gl_texture_parameters params;
	GLuint tex;
	float *lut;

	/*
	 * Four rows, see fragment.glsl sample_color_pre_curve_lut_2d().
	 * The fourth row is unused in fragment.glsl color_pre_curve().
	 * Four rows, see fragment.glsl sample_color_post_curve_lut_2d().
	 * The fourth row is unused in fragment.glsl color_post_curve().
	 */
	lut = calloc(lut_len * nr_rows, sizeof *lut);
	if (!lut)
		return false;

	curve->u.lut_3x1d.fill_in(xform, lut, lut_len);

	gl_texture_2d_init(gr, 1, GL_R32F, lut_len, nr_rows, &tex);
	gl_texture_2d_store(gr, 0, 0, 0, lut_len, nr_rows, GL_RED, GL_FLOAT,
			    lut);
	gl_texture_parameters_init(gr, &params, GL_TEXTURE_2D, filters, NULL,
				   NULL, true);

	free(lut);

	glBindTexture(GL_TEXTURE_2D, 0);
	gl_curve->type = SHADER_COLOR_CURVE_LUT_3x1D;
	gl_curve->u.lut_3x1d.tex = tex;
	gl_curve->u.lut_3x1d.scale = (float)(lut_len - 1) / lut_len;
	gl_curve->u.lut_3x1d.offset = 0.5f / lut_len;

	return true;
}

static bool
gl_3d_lut(struct gl_renderer *gr,
	  struct gl_renderer_color_transform *gl_xform,
	  struct weston_color_transform *xform)
{
	GLint filters[] = { GL_LINEAR, GL_LINEAR };
	struct gl_texture_parameters params;
	GLuint tex3d;
	float *lut;
	const unsigned dim_size = xform->mapping.u.lut3d.optimal_len;

	lut = calloc(3 * dim_size * dim_size * dim_size, sizeof *lut);
	if (!lut)
		return false;

	xform->mapping.u.lut3d.fill_in(xform, lut, dim_size);

	gl_texture_3d_init(gr, 1, GL_RGB32F, dim_size, dim_size, dim_size,
			   &tex3d);
	gl_texture_3d_store(gr, 0, 0, 0, 0, dim_size, dim_size, dim_size,
			    GL_RGB, GL_FLOAT, lut);
	gl_texture_parameters_init(gr, &params, GL_TEXTURE_3D, filters, NULL,
				   NULL, true);

	glBindTexture(GL_TEXTURE_3D, 0);
	gl_xform->mapping.type = SHADER_COLOR_MAPPING_3DLUT;
	gl_xform->mapping.lut3d.tex3d = tex3d;
	gl_xform->mapping.lut3d.scale = (float)(dim_size - 1) / dim_size;
	gl_xform->mapping.lut3d.offset = 0.5f / dim_size;

	free(lut);

	return true;
}


static const struct gl_renderer_color_transform *
gl_renderer_color_transform_from(struct gl_renderer *gr,
				 struct weston_color_transform *xform)
{
	static const struct gl_renderer_color_transform no_op_gl_xform = {
		.pre_curve.type = SHADER_COLOR_CURVE_IDENTITY,
		.mapping.type = SHADER_COLOR_MAPPING_IDENTITY,
		.post_curve.type = SHADER_COLOR_CURVE_IDENTITY,
	};
	struct gl_renderer_color_transform *gl_xform;
	bool ok = false;

	/* Identity transformation */
	if (!xform)
		return &no_op_gl_xform;

	/* Cached transformation */
	gl_xform = gl_renderer_color_transform_get(xform);
	if (gl_xform)
		return gl_xform;

	/* New transformation */

	gl_xform = gl_renderer_color_transform_create(xform);
	if (!gl_xform)
		return NULL;

	switch (xform->pre_curve.type) {
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		gl_xform->pre_curve = no_op_gl_xform.pre_curve;
		ok = true;
		break;
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		ok = gl_color_curve_lut_3x1d(gr, &gl_xform->pre_curve,
					     &xform->pre_curve, xform);
		break;
	case WESTON_COLOR_CURVE_TYPE_LINPOW:
		ok = gl_color_curve_linpow(&gl_xform->pre_curve,
					   &xform->pre_curve);
		break;
	case WESTON_COLOR_CURVE_TYPE_POWLIN:
		ok = gl_color_curve_powlin(&gl_xform->pre_curve,
					   &xform->pre_curve);
		break;
	}
	if (!ok) {
		gl_renderer_color_transform_destroy(gl_xform);
		return NULL;
	}

	switch (xform->mapping.type) {
	case WESTON_COLOR_MAPPING_TYPE_IDENTITY:
		gl_xform->mapping = no_op_gl_xform.mapping;
		ok = true;
		break;
	case WESTON_COLOR_MAPPING_TYPE_3D_LUT:
		ok = gl_3d_lut(gr, gl_xform, xform);
		break;
	case WESTON_COLOR_MAPPING_TYPE_MATRIX:
		gl_xform->mapping.type = SHADER_COLOR_MAPPING_MATRIX;
		gl_xform->mapping.mat = xform->mapping.u.mat;
		ok = true;
		break;
	}
	if (!ok) {
		gl_renderer_color_transform_destroy(gl_xform);
		return NULL;
	}

	switch (xform->post_curve.type) {
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		gl_xform->post_curve = no_op_gl_xform.post_curve;
		ok = true;
		break;
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		ok = gl_color_curve_lut_3x1d(gr, &gl_xform->post_curve,
					     &xform->post_curve, xform);
		break;
	case WESTON_COLOR_CURVE_TYPE_LINPOW:
		ok = gl_color_curve_linpow(&gl_xform->post_curve,
					   &xform->post_curve);
		break;
	case WESTON_COLOR_CURVE_TYPE_POWLIN:
		ok = gl_color_curve_powlin(&gl_xform->post_curve,
					   &xform->post_curve);
		break;
	}
	if (!ok) {
		gl_renderer_color_transform_destroy(gl_xform);
		return NULL;
	}

	return gl_xform;
}

bool
gl_shader_config_set_color_transform(struct gl_renderer *gr,
				     struct gl_shader_config *sconf,
				     struct weston_color_transform *xform)
{
	const struct gl_renderer_color_transform *gl_xform;
	bool ret = false;

	gl_xform = gl_renderer_color_transform_from(gr, xform);
	if (!gl_xform)
		return false;

	sconf->req.color_pre_curve = gl_xform->pre_curve.type;
	switch (gl_xform->pre_curve.type) {
	case SHADER_COLOR_CURVE_IDENTITY:
		break;
	case SHADER_COLOR_CURVE_LUT_3x1D:
		sconf->color_pre_curve.lut_3x1d.tex = gl_xform->pre_curve.u.lut_3x1d.tex;
		sconf->color_pre_curve.lut_3x1d.scale_offset[0] = gl_xform->pre_curve.u.lut_3x1d.scale;
		sconf->color_pre_curve.lut_3x1d.scale_offset[1] = gl_xform->pre_curve.u.lut_3x1d.offset;
		break;
	case SHADER_COLOR_CURVE_LINPOW:
	case SHADER_COLOR_CURVE_POWLIN:
		memcpy(sconf->color_pre_curve.parametric.params,
		       gl_xform->pre_curve.u.parametric.params,
		       sizeof(sconf->color_pre_curve.parametric.params));
		sconf->color_pre_curve.parametric.clamped_input =
			gl_xform->pre_curve.u.parametric.clamped_input;
		break;
	}

	sconf->req.color_post_curve = gl_xform->post_curve.type;
	switch (gl_xform->post_curve.type) {
	case SHADER_COLOR_CURVE_IDENTITY:
		break;
	case SHADER_COLOR_CURVE_LUT_3x1D:
		sconf->color_post_curve.lut_3x1d.tex = gl_xform->post_curve.u.lut_3x1d.tex;
		sconf->color_post_curve.lut_3x1d.scale_offset[0] = gl_xform->post_curve.u.lut_3x1d.scale;
		sconf->color_post_curve.lut_3x1d.scale_offset[1] = gl_xform->post_curve.u.lut_3x1d.offset;
		break;
	case SHADER_COLOR_CURVE_LINPOW:
	case SHADER_COLOR_CURVE_POWLIN:
		memcpy(&sconf->color_post_curve.parametric.params,
		       &gl_xform->post_curve.u.parametric.params,
		       sizeof(sconf->color_post_curve.parametric.params));
		sconf->color_post_curve.parametric.clamped_input =
			gl_xform->post_curve.u.parametric.clamped_input;
		break;
	}

	sconf->req.color_mapping = gl_xform->mapping.type;
	switch (gl_xform->mapping.type) {
	case SHADER_COLOR_MAPPING_3DLUT:
		sconf->color_mapping.lut3d.tex = gl_xform->mapping.lut3d.tex3d;
		sconf->color_mapping.lut3d.scale_offset[0] =
				gl_xform->mapping.lut3d.scale;
		sconf->color_mapping.lut3d.scale_offset[1] =
				gl_xform->mapping.lut3d.offset;
		assert(sconf->color_mapping.lut3d.scale_offset[0] > 0.0);
		assert(sconf->color_mapping.lut3d.scale_offset[1] > 0.0);
		ret = true;
		break;
	case SHADER_COLOR_MAPPING_MATRIX:
		assert(sconf->req.color_mapping == SHADER_COLOR_MAPPING_MATRIX);
		ARRAY_COPY(sconf->color_mapping.matrix, gl_xform->mapping.mat.matrix);
		ret = true;
		break;
	case SHADER_COLOR_MAPPING_IDENTITY:
		ret = true;
		break;
	}

	return ret;
}

/*
 * Copyright © 2012 Intel Corporation
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
#include <assert.h>
#include <float.h>
#include <math.h>

#include "shared/helpers.h"
#include "vertex-clipping.h"

WESTON_EXPORT_FOR_TESTS float
float_difference(float a, float b)
{
	/* https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/ */
	static const float max_diff = 4.0f * FLT_MIN;
	static const float max_rel_diff = 4.0e-5;
	float diff = a - b;
	float adiff = fabsf(diff);

	if (adiff <= max_diff)
		return 0.0f;

	a = fabsf(a);
	b = fabsf(b);
	if (adiff <= (a > b ? a : b) * max_rel_diff)
		return 0.0f;

	return diff;
}

/* A line segment (p1x, p1y)-(p2x, p2y) intersects the line x = x_arg.
 * Compute the y coordinate of the intersection.
 */
static float
clip_intersect_y(float p1x, float p1y, float p2x, float p2y,
		 float x_arg)
{
	float a;
	float diff = float_difference(p1x, p2x);

	/* Practically vertical line segment, yet the end points have already
	 * been determined to be on different sides of the line. Therefore
	 * the line segment is part of the line and intersects everywhere.
	 * Return the end point, so we use the whole line segment.
	 */
	if (diff == 0.0f)
		return p2y;

	a = (x_arg - p2x) / diff;
	return p2y + (p1y - p2y) * a;
}

/* A line segment (p1x, p1y)-(p2x, p2y) intersects the line y = y_arg.
 * Compute the x coordinate of the intersection.
 */
static float
clip_intersect_x(float p1x, float p1y, float p2x, float p2y,
		 float y_arg)
{
	float a;
	float diff = float_difference(p1y, p2y);

	/* Practically horizontal line segment, yet the end points have already
	 * been determined to be on different sides of the line. Therefore
	 * the line segment is part of the line and intersects everywhere.
	 * Return the end point, so we use the whole line segment.
	 */
	if (diff == 0.0f)
		return p2x;

	a = (y_arg - p2y) / diff;
	return p2x + (p1x - p2x) * a;
}

enum path_transition {
	PATH_TRANSITION_OUT_TO_OUT = 0,
	PATH_TRANSITION_OUT_TO_IN = 1,
	PATH_TRANSITION_IN_TO_OUT = 2,
	PATH_TRANSITION_IN_TO_IN = 3,
};

static void
clip_append_vertex(struct clip_context *ctx, float x, float y)
{
	ctx->vertices->x = x;
	ctx->vertices->y = y;
	ctx->vertices++;
}

static enum path_transition
path_transition_left_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.x >= ctx->clip.x1) << 1) | (x >= ctx->clip.x1);
}

static enum path_transition
path_transition_right_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.x < ctx->clip.x2) << 1) | (x < ctx->clip.x2);
}

static enum path_transition
path_transition_top_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.y >= ctx->clip.y1) << 1) | (y >= ctx->clip.y1);
}

static enum path_transition
path_transition_bottom_edge(struct clip_context *ctx, float x, float y)
{
	return ((ctx->prev.y < ctx->clip.y2) << 1) | (y < ctx->clip.y2);
}

static void
clip_polygon_leftright(struct clip_context *ctx,
		       enum path_transition transition,
		       float x, float y, float clip_x)
{
	float yi;

	switch (transition) {
	case PATH_TRANSITION_IN_TO_IN:
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_IN_TO_OUT:
		yi = clip_intersect_y(ctx->prev.x, ctx->prev.y, x, y, clip_x);
		clip_append_vertex(ctx, clip_x, yi);
		break;
	case PATH_TRANSITION_OUT_TO_IN:
		yi = clip_intersect_y(ctx->prev.x, ctx->prev.y, x, y, clip_x);
		clip_append_vertex(ctx, clip_x, yi);
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_OUT_TO_OUT:
		/* nothing */
		break;
	default:
		assert(0 && "bad enum path_transition");
	}

	ctx->prev.x = x;
	ctx->prev.y = y;
}

static void
clip_polygon_topbottom(struct clip_context *ctx,
		       enum path_transition transition,
		       float x, float y, float clip_y)
{
	float xi;

	switch (transition) {
	case PATH_TRANSITION_IN_TO_IN:
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_IN_TO_OUT:
		xi = clip_intersect_x(ctx->prev.x, ctx->prev.y, x, y, clip_y);
		clip_append_vertex(ctx, xi, clip_y);
		break;
	case PATH_TRANSITION_OUT_TO_IN:
		xi = clip_intersect_x(ctx->prev.x, ctx->prev.y, x, y, clip_y);
		clip_append_vertex(ctx, xi, clip_y);
		clip_append_vertex(ctx, x, y);
		break;
	case PATH_TRANSITION_OUT_TO_OUT:
		/* nothing */
		break;
	default:
		assert(0 && "bad enum path_transition");
	}

	ctx->prev.x = x;
	ctx->prev.y = y;
}

static void
clip_context_prepare(struct clip_context *ctx, const struct polygon8 *src,
		     struct clip_vertex *dst)
{
	ctx->prev.x = src->pos[src->n - 1].x;
	ctx->prev.y = src->pos[src->n - 1].y;
	ctx->vertices = dst;
}

static int
clip_polygon_left(struct clip_context *ctx, const struct polygon8 *src,
		  struct clip_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_left_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_leftright(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->clip.x1);
	}
	return ctx->vertices - dst;
}

static int
clip_polygon_right(struct clip_context *ctx, const struct polygon8 *src,
		   struct clip_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_right_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_leftright(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->clip.x2);
	}
	return ctx->vertices - dst;
}

static int
clip_polygon_top(struct clip_context *ctx, const struct polygon8 *src,
		 struct clip_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_top_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_topbottom(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->clip.y1);
	}
	return ctx->vertices - dst;
}

static int
clip_polygon_bottom(struct clip_context *ctx, const struct polygon8 *src,
		    struct clip_vertex *dst)
{
	enum path_transition trans;
	int i;

	if (src->n < 2)
		return 0;

	clip_context_prepare(ctx, src, dst);
	for (i = 0; i < src->n; i++) {
		trans = path_transition_bottom_edge(ctx, src->pos[i].x, src->pos[i].y);
		clip_polygon_topbottom(ctx, trans, src->pos[i].x, src->pos[i].y,
				       ctx->clip.y2);
	}
	return ctx->vertices - dst;
}

WESTON_EXPORT_FOR_TESTS int
clip_simple(struct clip_context *ctx,
	    struct polygon8 *surf,
	    struct clip_vertex *restrict vertices)
{
	int i;
	for (i = 0; i < surf->n; i++) {
		vertices[i].x = CLIP(surf->pos[i].x, ctx->clip.x1, ctx->clip.x2);
		vertices[i].y = CLIP(surf->pos[i].y, ctx->clip.y1, ctx->clip.y2);
	}
	return surf->n;
}

WESTON_EXPORT_FOR_TESTS int
clip_transformed(struct clip_context *ctx,
		 const struct polygon8 *surf,
		 struct clip_vertex *restrict vertices)
{
	struct polygon8 p = *surf, tmp;
	int i, n;

	tmp.n = clip_polygon_left(ctx, &p, tmp.pos);
	p.n = clip_polygon_right(ctx, &tmp, p.pos);
	tmp.n = clip_polygon_top(ctx, &p, tmp.pos);
	p.n = clip_polygon_bottom(ctx, &tmp, p.pos);

	/* Get rid of duplicate vertices */
	vertices[0] = p.pos[0];
	n = 1;
	for (i = 1; i < p.n; i++) {
		if (float_difference(vertices[n - 1].x, p.pos[i].x) == 0.0f &&
		    float_difference(vertices[n - 1].y, p.pos[i].y) == 0.0f)
			continue;
		vertices[n] = p.pos[i];
		n++;
	}
	if (float_difference(vertices[n - 1].x, p.pos[0].x) == 0.0f &&
	    float_difference(vertices[n - 1].y, p.pos[0].y) == 0.0f)
		n--;

	return n;
}

int
clip_quad(struct gl_quad *quad, pixman_box32_t *surf_rect,
	  struct clip_vertex *vertices)
{
	struct clip_context ctx = {
		.clip.x1 = surf_rect->x1,
		.clip.y1 = surf_rect->y1,
		.clip.x2 = surf_rect->x2,
		.clip.y2 = surf_rect->y2,
	};
	int n;

	/* Simple case: quad edges are parallel to surface rect edges, there
	 * will be either four or zero edges. We just need to clip the quad to
	 * the surface rect bounds and test for non-zero area:
	 */
	if (quad->axis_aligned) {
		clip_simple(&ctx, &quad->vertices, vertices);
		if ((vertices[0].x != vertices[1].x) &&
		    (vertices[0].y != vertices[2].y))
			return 4;
		else
			return 0;
	}

	/* Transformed case: first, simple bounding box check to discard early a
	 * quad that does not intersect with the rect:
	 */
	if ((quad->bbox.x1 >= ctx.clip.x2) || (quad->bbox.x2 <= ctx.clip.x1) ||
	    (quad->bbox.y1 >= ctx.clip.y2) || (quad->bbox.y2 <= ctx.clip.y1))
		return 0;

	/* Then, use a general polygon clipping algorithm to clip the quad with
	 * each side of the surface rect. The algorithm is Sutherland-Hodgman,
	 * as explained in
	 * https://www.codeguru.com/cplusplus/polygon-clipping/
	 * but without looking at any of that code.
	 */
	n = clip_transformed(&ctx, &quad->vertices, vertices);

	if (n < 3)
		return 0;

	return n;
}

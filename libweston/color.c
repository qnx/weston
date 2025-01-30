/*
 * Copyright 2019 Sebastian Wick
 * Copyright 2021 Collabora, Ltd.
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
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include "color.h"
#include "color-operations.h"
#include "color-properties.h"
#include "id-number-allocator.h"
#include "libweston-internal.h"
#include <libweston/weston-log.h>
#include "shared/string-helpers.h"
#include "shared/helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"
#include "shared/weston-assert.h"

/**
 * Increase reference count of the color profile object
 *
 * \param cprof The color profile. NULL is accepted too.
 * \return cprof.
 */
WL_EXPORT struct weston_color_profile *
weston_color_profile_ref(struct weston_color_profile *cprof)
{
	if (!cprof)
		return NULL;

	assert(cprof->ref_count > 0);
	cprof->ref_count++;
	return cprof;
}

/**
 * Decrease reference count and potentially destroy the color profile object
 *
 * \param cprof The color profile. NULL is accepted too.
 */
WL_EXPORT void
weston_color_profile_unref(struct weston_color_profile *cprof)
{
	if (!cprof)
		return;

	assert(cprof->ref_count > 0);
	if (--cprof->ref_count > 0)
		return;

	weston_idalloc_put_id(cprof->cm->compositor->color_profile_id_generator,
			      cprof->id);

	cprof->cm->destroy_color_profile(cprof);
}

/**
 * Get color profile description
 *
 * A description of the profile is meant for human readable logs.
 *
 * \param cprof The color profile, NULL is accepted too.
 * \returns The color profile description, valid as long as the
 * color profile itself is.
 */
WL_EXPORT const char *
weston_color_profile_get_description(struct weston_color_profile *cprof)
{
	if (cprof)
		return cprof->description;
	else
		return "(untagged)";
}

/**
 * Initializes a newly allocated color profile object
 *
 * This is used only by color managers. They sub-class weston_color_profile.
 *
 * The reference count starts at 1.
 *
 * To destroy a weston_color_profile, use weston_color_profile_unref().
 */
WL_EXPORT void
weston_color_profile_init(struct weston_color_profile *cprof,
			  struct weston_color_manager *cm)
{
	cprof->cm = cm;
	cprof->ref_count = 1;
	cprof->id = weston_idalloc_get_id(cm->compositor->color_profile_id_generator);
}

/**
 * Print color profile parameters to string.
 *
 * \param params The parameters of the color profile.
 * \param ident Indentation to add before each line of the return'ed string.
 * \returns The color profile parameters as string. Callers must free() it.
 */
WL_EXPORT char *
weston_color_profile_params_to_str(struct weston_color_profile_params *params,
				   const char *ident)
{
	FILE *fp;
	char *str;
	size_t size;
	unsigned int i;

	fp = open_memstream(&str, &size);
	abort_oom_if_null(fp);

	fprintf(fp, "%sprimaries (CIE xy):\n", ident);
	fprintf(fp, "%s    R  = (%.5f, %.5f)\n", ident, params->primaries.primary[0].x,
							params->primaries.primary[0].y);
	fprintf(fp, "%s    G  = (%.5f, %.5f)\n", ident, params->primaries.primary[1].x,
							params->primaries.primary[1].y);
	fprintf(fp, "%s    B  = (%.5f, %.5f)\n", ident, params->primaries.primary[2].x,
							params->primaries.primary[2].y);
	fprintf(fp, "%s    WP = (%.5f, %.5f)\n", ident, params->primaries.white_point.x,
							params->primaries.white_point.y);

	if (params->primaries_info)
		fprintf(fp, "%sprimaries named: %s\n", ident, params->primaries_info->desc);

	fprintf(fp, "%stransfer function: %s\n", ident, params->tf_info->desc);

	if (params->tf_info->count_parameters > 0) {
		fprintf(fp, "%s    params:", ident);
		for (i = 0; i < params->tf_info->count_parameters; i++)
			fprintf(fp, " %.4f", params->tf_params[i]);
		fprintf(fp, "\n");
	}

	fprintf(fp, "%sluminance: [%.3f, %.2f], ref white %.2f (cd/m²)\n", ident, params->min_luminance,
										  params->max_luminance,
										  params->reference_white_luminance);

	fprintf(fp, "%starget primaries (CIE xy):\n", ident);
	fprintf(fp, "%s    R  = (%.5f, %.5f)\n", ident, params->target_primaries.primary[0].x,
							params->target_primaries.primary[0].y);
	fprintf(fp, "%s    G  = (%.5f, %.5f)\n", ident, params->target_primaries.primary[1].x,
							params->target_primaries.primary[1].y);
	fprintf(fp, "%s    B  = (%.5f, %.5f)\n", ident, params->target_primaries.primary[2].x,
							params->target_primaries.primary[2].y);
	fprintf(fp, "%s    WP = (%.5f, %.5f)\n", ident, params->target_primaries.white_point.x,
							params->target_primaries.white_point.y);

	if (params->target_min_luminance >= 0.0f && params->target_max_luminance >= 0.0f)
		fprintf(fp, "%starget luminance: [%.3f, %.2f] (cd/m²)\n", ident, params->target_min_luminance,
										 params->target_max_luminance);

	if (params->maxCLL >= 0.0f)
		fprintf(fp, "%smax cll: %.2f (cd/m²)\n", ident, params->maxCLL);

	if (params->maxFALL >= 0.0f)
		fprintf(fp, "%smax fall: %.2f (cd/m²)\n", ident, params->maxFALL);

	fclose(fp);
	return str;
}

/**
 * Initializes a struct weston_color_curve given a transfer function.
 *
 * \param curve The curve to initialize.
 * \param tf_info The tf_info object.
 * \param tf_params When the tf is parametric, this is required. Otherwise,
 * this is ignored.
 * \param tf_direction Determines if this inits a curve equivalent to the direct
 * or the inverse of the tf.
 */
WL_EXPORT void
weston_color_curve_from_tf_info(struct weston_color_curve *curve,
				const struct weston_color_tf_info *tf_info,
				const float tf_params[MAX_PARAMS_TF],
				enum weston_tf_direction tf_direction)
{
	unsigned int i, j;

	curve->type = WESTON_COLOR_CURVE_TYPE_ENUM;

	curve->u.enumerated.tf = tf_info;
	curve->u.enumerated.tf_direction = tf_direction;

	for (j = 0; j < tf_info->count_parameters; j++) {
		for (i = 0; i < 3; i++)
			curve->u.enumerated.params[i][j] = tf_params[j];
	}
}

/**
 * Given an enumerated color curve, returns an equivalent parametric curve.
 *
 * \param compositor The compositor instance.
 * \param curve The enumerated color curve.
 * \param out Where this stores the parametric curve.
 * \return True on success, false otherwise.
 */
WL_EXPORT bool
weston_color_curve_enum_get_parametric(struct weston_compositor *compositor,
				       const struct weston_color_curve_enum *curve,
				       struct weston_color_curve_parametric *out)
{
	unsigned int i;

	memset(out, 0, sizeof(*out));

	/* This one is special, the only parametric TF we currently have. */
	if (curve->tf->tf == WESTON_TF_POWER) {
		out->type = WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW;
		out->clamped_input = false;
		for (i = 0; i < 3; i++) {
			float exp = curve->params[i][0];
			/* LINPOW with such params matches pure power-law */
			out->params[i][0] = (curve->tf_direction == WESTON_FORWARD_TF) ?
					    exp : 1.0f / exp; /* g */
			out->params[i][1] = 1.0; /* a */
			out->params[i][2] = 0.0; /* b */
			out->params[i][3] = 1.0; /* c */
			out->params[i][4] = 0.0; /* d */
		}
		return true;
	}

	/* No other TF's have params. */
	weston_assert_uint32_eq(compositor, curve->tf->count_parameters, 0);

	if (!curve->tf->curve_params_valid)
		return false;

	if (curve->tf_direction == WESTON_FORWARD_TF)
		*out = curve->tf->curve;
	else
		*out = curve->tf->inverse_curve;

	return true;
}

static bool
curve_to_lut_has_good_precision(struct weston_color_curve *curve)
{
	struct weston_color_curve_enum *e = &curve->u.enumerated;
	struct weston_color_curve_parametric *p = &curve->u.parametric;
	float g;
	unsigned int i;

	if (curve->type == WESTON_COLOR_CURVE_TYPE_ENUM) {
		if (e->tf_direction == WESTON_INVERSE_TF) {
			if (e->tf->tf == WESTON_TF_ST2084_PQ ||
			    e->tf->tf == WESTON_TF_GAMMA22 ||
			    e->tf->tf == WESTON_TF_GAMMA28) {
				/**
				 * These have bad precision in the indirect
				 * direction.
				 */
				return false;
			}

			if (e->tf->tf == WESTON_TF_POWER) {
				/**
				 * Same as the above, but for parametric
				 * power-law transfer function. If g > 1.0
				 * it would result in bad precision.
				 */
				for (i = 0; i < 3; i++) {
					g = e->params[i][0];
					if (g > 1.0f)
						return false;
				}
			}
		} else {
			if (e->tf->tf == WESTON_TF_POWER) {
				/**
				 * For parametric power-law transfer function
				 * in the forward direction, g < 1.0 would
				 * result in bad precision.
				 */
				for (i = 0; i < 3; i++) {
					g = e->params[i][0];
					if (g < 1.0f)
						return false;
				}
			}
		}
	} else if (curve->type == WESTON_COLOR_CURVE_TYPE_PARAMETRIC) {
		switch(p->type) {
		case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW:
		case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN:
			/**
			 * Both LINPOW and POWLIN have bad precision if g < 1.0.
			 */
			for (i = 0; i < 3; i++) {
				g = p->params[i][0];
				if (g < 1.0f)
					return false;
			}
			break;
		}
	}

	return true;
}

/**
 * Given a xform and an enum corresponding to one of its curves (pre or post),
 * returns a 3x1D LUT that corresponds to such curve.
 *
 * The 3x1D LUT returned looks like this: the first lut_size elements compose
 * the LUT for the R channel, the next lut_size elements compose the LUT for the
 * G channel and the last lut_size elements compose the LUT for the B channel.
 *
 * @param compositor The Weston compositor.
 * @param xform The color transformation that owns the curve.
 * @param step The curve step (pre or post) from the xform.
 * @param lut_size The size of each LUT.
 * @param err_msg Set on failure, untouched otherwise. Must be free()'d by caller.
 * @return NULL on failure, the 3x1D LUT on success.
 */
WL_EXPORT float *
weston_color_curve_to_3x1D_LUT(struct weston_compositor *compositor,
			       struct weston_color_transform *xform,
			       enum weston_color_curve_step step,
			       uint32_t lut_size, char **err_msg)
{
	struct weston_color_curve *curve;
	float divider = lut_size - 1;
	float *in, *lut;
	unsigned int i, ch;
	bool ret;

	switch(step) {
	case WESTON_COLOR_CURVE_STEP_PRE:
		curve = &xform->pre_curve;
		break;
	case WESTON_COLOR_CURVE_STEP_POST:
		curve = &xform->post_curve;
		break;
	default:
		weston_assert_not_reached(compositor, "unknown curve step");
	}

	if (!curve_to_lut_has_good_precision(curve)) {
		str_printf(err_msg, "can't create color LUT from curve, it would " \
				    "result in bad precision");
		return NULL;
	}

	switch(curve->type) {
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		lut = xzalloc(3 * lut_size * sizeof(*lut));
		curve->u.lut_3x1d.fill_in(xform, lut, lut_size);
		return lut;
	case WESTON_COLOR_CURVE_TYPE_ENUM:
	case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
		lut = xzalloc(3 * lut_size * sizeof(*lut));
		in  = xzalloc(lut_size * sizeof(*lut));
		for (i = 0; i < lut_size; i++)
			in[i] = (float)i / divider;
		for (ch = 0; ch < 3; ch++) {
			ret = weston_color_curve_sample(compositor, curve, ch, lut_size,
							in, &lut[ch * lut_size]);
			if (!ret) {
				free(lut);
				lut = NULL;
				str_printf(err_msg, "can't create color LUT from " \
						    "curve, failed to sample color curve");
				break;
			}
		}
		free(in);
		return lut;
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		weston_assert_not_reached(compositor,
					  "no reason to create LUT for identity curve");
	}

	weston_assert_not_reached(compositor, "unkown color curve");
}

/**
 * Increase reference count of the color transform object
 *
 * \param xform The color transform. NULL is accepted too.
 * \return xform.
 */
WL_EXPORT struct weston_color_transform *
weston_color_transform_ref(struct weston_color_transform *xform)
{
	/* NULL is a valid color transform: identity */
	if (!xform)
		return NULL;

	assert(xform->ref_count > 0);
	xform->ref_count++;
	return xform;
}

/**
 * Decrease and potentially destroy the color transform object
 *
 * \param xform The color transform. NULL is accepted too.
 */
WL_EXPORT void
weston_color_transform_unref(struct weston_color_transform *xform)
{
	if (!xform)
		return;

	assert(xform->ref_count > 0);
	if (--xform->ref_count > 0)
		return;

	wl_signal_emit(&xform->destroy_signal, xform);
	weston_idalloc_put_id(xform->cm->compositor->color_transform_id_generator,
			      xform->id);
	xform->cm->destroy_color_transform(xform);
}

/**
 * Initializes a newly allocated color transform object
 *
 * This is used only by color managers. They sub-class weston_color_transform.
 *
 * The reference count starts at 1.
 *
 * To destroy a weston_color_transform, use weston_color_transfor_unref().
 */
WL_EXPORT void
weston_color_transform_init(struct weston_color_transform *xform,
			    struct weston_color_manager *cm)
{
	xform->cm = cm;
	xform->ref_count = 1;
	xform->id = weston_idalloc_get_id(cm->compositor->color_transform_id_generator);
	wl_signal_init(&xform->destroy_signal);
}

static const char *
param_curve_type_to_str(enum weston_color_curve_parametric_type type)
{
	switch(type) {
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_LINPOW:
		return "linpow";
	case WESTON_COLOR_CURVE_PARAMETRIC_TYPE_POWLIN:
		return "powlin";
	}
	return "???";
}

static const char *
curve_type_to_str(enum weston_color_curve_type curve_type)
{
	switch (curve_type) {
	case WESTON_COLOR_CURVE_TYPE_IDENTITY:
		return "identity";
	case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
		return "3x1D LUT";
	case WESTON_COLOR_CURVE_TYPE_ENUM:
		return "enumerated";
	case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
		return "parametric";
	}
	return "???";
}

static const char *
mapping_type_to_str(enum weston_color_mapping_type mapping_type)
{
	switch (mapping_type) {
	case WESTON_COLOR_MAPPING_TYPE_IDENTITY:
		return "identity";
	case WESTON_COLOR_MAPPING_TYPE_3D_LUT:
		return "3D LUT";
	case WESTON_COLOR_MAPPING_TYPE_MATRIX:
		return "matrix";
	}
	return "???";
}

/**
 * Print the color transform pipeline to a string
 *
 * \param xform The color transform.
 * \return The string in which the pipeline is printed.
 */
WL_EXPORT char *
weston_color_transform_string(const struct weston_color_transform *xform)
{
	enum weston_color_mapping_type mapping_type = xform->mapping.type;
	enum weston_color_curve_type pre_type = xform->pre_curve.type;
	enum weston_color_curve_type post_type = xform->post_curve.type;
	const char *empty = "";
	const char *sep = empty;
	FILE *fp;
	char *str = NULL;
	size_t size = 0;

	fp = open_memstream(&str, &size);
	abort_oom_if_null(fp);

	fprintf(fp, "pipeline: ");

	if (pre_type != WESTON_COLOR_CURVE_TYPE_IDENTITY) {
		fprintf(fp, "%spre %s", sep, curve_type_to_str(pre_type));
		if (pre_type == WESTON_COLOR_CURVE_TYPE_LUT_3x1D)
			fprintf(fp, " [%u]", xform->pre_curve.u.lut_3x1d.optimal_len);
		else if (pre_type == WESTON_COLOR_CURVE_TYPE_ENUM)
			fprintf(fp, " [%s%s]",
				(xform->pre_curve.u.enumerated.tf_direction == WESTON_INVERSE_TF) ? "inverse " : "",
				xform->pre_curve.u.enumerated.tf->desc);
		else if (pre_type == WESTON_COLOR_CURVE_TYPE_PARAMETRIC)
			fprintf(fp, " [%s]",
				param_curve_type_to_str(xform->pre_curve.u.parametric.type));
		sep = ", ";
	}

	if (mapping_type != WESTON_COLOR_MAPPING_TYPE_IDENTITY) {
		fprintf(fp, "%smapping %s", sep, mapping_type_to_str(mapping_type));
		if (mapping_type == WESTON_COLOR_MAPPING_TYPE_3D_LUT)
			fprintf(fp, " [%u]", xform->mapping.u.lut3d.optimal_len);
		sep = ", ";
	}

	if (post_type != WESTON_COLOR_CURVE_TYPE_IDENTITY) {
		fprintf(fp, "%spost %s", sep, curve_type_to_str(post_type));
		if (post_type == WESTON_COLOR_CURVE_TYPE_LUT_3x1D)
			fprintf(fp, " [%u]", xform->post_curve.u.lut_3x1d.optimal_len);
		else if (post_type == WESTON_COLOR_CURVE_TYPE_ENUM)
			fprintf(fp, " [%s%s]",
				(xform->post_curve.u.enumerated.tf_direction == WESTON_INVERSE_TF) ? "inverse " : "",
				xform->post_curve.u.enumerated.tf->desc);
		else if (post_type == WESTON_COLOR_CURVE_TYPE_PARAMETRIC)
			fprintf(fp, " [%s]",
				param_curve_type_to_str(xform->post_curve.u.parametric.type));
		sep = ", ";
	}

	if (sep == empty)
		fprintf(fp, "identity\n");
	else
		fprintf(fp, "\n");

	fclose(fp);
	abort_oom_if_null(str);

	return str;
}

/** Deep copy */
void
weston_surface_color_transform_copy(struct weston_surface_color_transform *dst,
				    const struct weston_surface_color_transform *src)
{
	*dst = *src;
	dst->transform = weston_color_transform_ref(src->transform);
}

/** Unref contents */
void
weston_surface_color_transform_fini(struct weston_surface_color_transform *surf_xform)
{
	weston_color_transform_unref(surf_xform->transform);
	surf_xform->transform = NULL;
	surf_xform->identity_pipeline = false;
}

/**
 * Ensure that the surface's color transformation for the given output is
 * populated in the paint nodes for all the views.
 *
 * Creates the color transformation description if necessary by calling
 * into the color manager.
 *
 * \param pnode Paint node defining the surface and the output. All
 * paint nodes with the same surface and output will be ensured.
 */
void
weston_paint_node_ensure_color_transform(struct weston_paint_node *pnode)
{
	struct weston_surface *surface = pnode->surface;
	struct weston_output *output = pnode->output;
	struct weston_color_manager *cm = surface->compositor->color_manager;
	struct weston_surface_color_transform surf_xform = {};
	struct weston_paint_node *it;
	bool ok;

	/*
	 * Invariant: all paint nodes with the same surface+output have the
	 * same surf_xform state.
	 */
	if (pnode->surf_xform_valid)
		return;

	ok = cm->get_surface_color_transform(cm, surface, output, &surf_xform);

	wl_list_for_each(it, &surface->paint_node_list, surface_link) {
		if (it->output == output) {
			assert(it->surf_xform_valid == false);
			assert(it->surf_xform.transform == NULL);
			weston_surface_color_transform_copy(&it->surf_xform,
							    &surf_xform);
			it->surf_xform_valid = ok;
		}
	}

	weston_surface_color_transform_fini(&surf_xform);

	if (!ok) {
		if (surface->resource)
			wl_resource_post_no_memory(surface->resource);
		weston_log("Failed to create color transformation for a surface.\n");
	}
}

/**
 * Load ICC profile file
 *
 * Loads an ICC profile file, ensures it is fit for use, and returns a
 * new reference to the weston_color_profile. Use weston_color_profile_unref()
 * to free it.
 *
 * \param compositor The compositor instance, identifies the color manager.
 * \param path Path to the ICC file to be open()'d.
 * \return A color profile reference, or NULL on failure.
 *
 * Error messages are printed to libweston log.
 *
 * This function is not meant for loading profiles on behalf of Wayland
 * clients.
 */
WL_EXPORT struct weston_color_profile *
weston_compositor_load_icc_file(struct weston_compositor *compositor,
				const char *path)
{
	struct weston_color_manager *cm = compositor->color_manager;
	struct weston_color_profile *cprof = NULL;
	int fd;
	struct stat icc_stat;
	void *icc_data;
	size_t len;
	char *errmsg = NULL;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		weston_log("Error: Cannot open ICC profile \"%s\" for reading: %s\n",
			   path, strerror(errno));
		return NULL;
	}

	if (fstat(fd, &icc_stat) != 0) {
		weston_log("Error: Cannot fstat ICC profile \"%s\": %s\n",
			   path, strerror(errno));
		goto out_close;
	}
	len = icc_stat.st_size;
	if (len < 1) {
		weston_log("Error: ICC profile \"%s\" has no size.\n", path);
		goto out_close;
	}

	icc_data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (icc_data == MAP_FAILED) {
		weston_log("Error: Cannot mmap ICC profile \"%s\": %s\n",
			   path, strerror(errno));
		goto out_close;
	}

	if (!cm->get_color_profile_from_icc(cm, icc_data, len,
					    path, &cprof, &errmsg)) {
		weston_log("Error: loading ICC profile \"%s\" failed: %s\n",
			   path, errmsg);
		free(errmsg);
	}

	munmap(icc_data, len);

out_close:
	close(fd);
	return cprof;
}

/** Get a string naming the EOTF mode for logs
 *
 * \return Static string. "???" for unknown mode.
 */
WL_EXPORT const char *
weston_eotf_mode_to_str(enum weston_eotf_mode e)
{
	switch (e) {
	case WESTON_EOTF_MODE_NONE:		return "(none)";
	case WESTON_EOTF_MODE_SDR:		return "SDR";
	case WESTON_EOTF_MODE_TRADITIONAL_HDR:	return "traditional gamma HDR";
	case WESTON_EOTF_MODE_ST2084:		return "ST2084";
	case WESTON_EOTF_MODE_HLG:		return "HLG";
	}
	return "???";
}

/** A list of EOTF modes as a string
 *
 * \param eotf_mask Bitwise-or'd enum weston_eotf_mode values.
 * \return Comma separated names of the listed EOTF modes. Must be free()'d by
 * the caller.
 */
WL_EXPORT char *
weston_eotf_mask_to_str(uint32_t eotf_mask)
{
	return bits_to_str(eotf_mask, weston_eotf_mode_to_str);
}

static const struct weston_colorimetry_mode_info colorimetry_mode_info_map[] = {
	{ WESTON_COLORIMETRY_MODE_NONE, "(none)", WDRM_COLORSPACE__COUNT },
	{ WESTON_COLORIMETRY_MODE_DEFAULT, "default", WDRM_COLORSPACE_DEFAULT },
	{ WESTON_COLORIMETRY_MODE_BT2020_CYCC, "BT.2020 (cYCC)", WDRM_COLORSPACE_BT2020_CYCC },
	{ WESTON_COLORIMETRY_MODE_BT2020_YCC, "BT.2020 (YCC)", WDRM_COLORSPACE_BT2020_YCC },
	{ WESTON_COLORIMETRY_MODE_BT2020_RGB, "BT.2020 (RGB)", WDRM_COLORSPACE_BT2020_RGB },
	{ WESTON_COLORIMETRY_MODE_P3D65, "DCI-P3 RGB D65", WDRM_COLORSPACE_DCI_P3_RGB_D65 },
	{ WESTON_COLORIMETRY_MODE_P3DCI, "DCI-P3 RGB Theatre", WDRM_COLORSPACE_DCI_P3_RGB_THEATER },
	{ WESTON_COLORIMETRY_MODE_ICTCP, "BT.2100 ICtCp", WDRM_COLORSPACE__COUNT },
};

/** Get information structure of colorimetry mode
 *
 * \internal
 */
WL_EXPORT const struct weston_colorimetry_mode_info *
weston_colorimetry_mode_info_get(enum weston_colorimetry_mode c)
{
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(colorimetry_mode_info_map); i++)
		if (colorimetry_mode_info_map[i].mode == c)
			return &colorimetry_mode_info_map[i];

	return NULL;
}

/** Get information structure of colorimetry mode from KMS "Colorspace" enum
 *
 * \internal
 */
WL_EXPORT const struct weston_colorimetry_mode_info *
weston_colorimetry_mode_info_get_by_wdrm(enum wdrm_colorspace cs)
{
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(colorimetry_mode_info_map); i++)
		if (colorimetry_mode_info_map[i].wdrm == cs)
			return &colorimetry_mode_info_map[i];

	return NULL;
}

/** Get a string naming the colorimetry mode for logs
 *
 * \return Static string. "???" for unknown mode.
 */
WL_EXPORT const char *
weston_colorimetry_mode_to_str(enum weston_colorimetry_mode c)
{
	const struct weston_colorimetry_mode_info *info;

	info = weston_colorimetry_mode_info_get(c);

	return info ? info->name : "???";
}

/** A list of colorimetry modes as a string
 *
 * \param colorimetry_mask Bitwise-or'd enum weston_colorimetry_mode values.
 * \return Comma separated names of the listed colorimetry modes.
 * Must be free()'d by the caller.
 */
WL_EXPORT char *
weston_colorimetry_mask_to_str(uint32_t colorimetry_mask)
{
	return bits_to_str(colorimetry_mask, weston_colorimetry_mode_to_str);
}

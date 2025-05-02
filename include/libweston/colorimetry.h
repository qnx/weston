/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2012-2025 Collabora, Ltd.
 * Copyright © 2017, 2018 General Electric Company
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

#pragma once

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <libweston/linalg-3.h>

struct weston_compositor;
struct weston_color_profile_param_builder;
struct weston_color_profile;
struct weston_color_transform;

/** Colorimetry mode for outputs and heads
 *
 * A list of colorimetry modes for driving displays, defined by ANSI/CTA-861-H.
 *
 * On heads, a bitmask of one or more entries shows which modes are claimed
 * supported.
 *
 * On outputs, the mode to be used for driving the video sink.
 *
 * Default (RGB) colorimetry differs from all the others in that the signal
 * colorimetry is not defined here. It is defined by the video sink, and it
 * may be described in e.g. EDID.
 */
 enum weston_colorimetry_mode {
	/** Invalid colorimetry mode, or none supported. */
	WESTON_COLORIMETRY_MODE_NONE			= 0,

	/** Default (RGB) colorimetry, video sink dependant */
	WESTON_COLORIMETRY_MODE_DEFAULT			= 0x01,

	/** Rec. ITU-R BT.2020 constant luminance YCbCr */
	WESTON_COLORIMETRY_MODE_BT2020_CYCC		= 0x02,

	/** Rec. ITU-R BT.2020 non-constant luminance YCbCr */
	WESTON_COLORIMETRY_MODE_BT2020_YCC		= 0x04,

	/** Rec. ITU-R BT.2020 RGB */
	WESTON_COLORIMETRY_MODE_BT2020_RGB		= 0x08,

	/** SMPTE ST 2113 DCI-P3 RGB D65 */
	WESTON_COLORIMETRY_MODE_P3D65			= 0x10,

	/** SMPTE ST 2113 DCI-P3 RGB Theater */
	WESTON_COLORIMETRY_MODE_P3DCI			= 0x20,

	/** Rec. ITU-R BT.2100 ICtCp HDR (with PQ and/or HLG)*/
	WESTON_COLORIMETRY_MODE_ICTCP			= 0x40,
};

/** Bitmask of all defined colorimetry modes */
#define WESTON_COLORIMETRY_MODE_ALL_MASK \
	((uint32_t)(WESTON_COLORIMETRY_MODE_DEFAULT | \
		    WESTON_COLORIMETRY_MODE_BT2020_CYCC | \
		    WESTON_COLORIMETRY_MODE_BT2020_YCC | \
		    WESTON_COLORIMETRY_MODE_BT2020_RGB | \
		    WESTON_COLORIMETRY_MODE_P3D65 | \
		    WESTON_COLORIMETRY_MODE_P3DCI | \
		    WESTON_COLORIMETRY_MODE_ICTCP))

const char *
weston_colorimetry_mode_to_str(enum weston_colorimetry_mode c);

/** EOTF mode for outputs and heads
 *
 * A list of EOTF modes for driving displays, defined by CTA-861-G for
 * Dynamic Range and Mastering InfoFrame.
 *
 * On heads, a bitmask of one or more entries shows which modes are claimed
 * supported.
 *
 * On outputs, the mode to be used for driving the video sink.
 *
 * For traditional non-HDR sRGB, use WESTON_EOTF_MODE_SDR.
 */
enum weston_eotf_mode {
	/** Invalid EOTF mode, or none supported. */
	WESTON_EOTF_MODE_NONE			= 0,

	/** Traditional gamma, SDR luminance range */
	WESTON_EOTF_MODE_SDR			= 0x01,

	/** Traditional gamma, HDR luminance range */
	WESTON_EOTF_MODE_TRADITIONAL_HDR	= 0x02,

	/** Preceptual quantizer, SMPTE ST 2084 */
	WESTON_EOTF_MODE_ST2084			= 0x04,

	/** Hybrid log-gamma, ITU-R BT.2100 */
	WESTON_EOTF_MODE_HLG			= 0x08,
};

/** Bitmask of all defined EOTF modes */
#define WESTON_EOTF_MODE_ALL_MASK \
	((uint32_t)(WESTON_EOTF_MODE_SDR | WESTON_EOTF_MODE_TRADITIONAL_HDR | \
		    WESTON_EOTF_MODE_ST2084 | WESTON_EOTF_MODE_HLG))

const char *
weston_eotf_mode_to_str(enum weston_eotf_mode e);

/** CIE 1931 xy chromaticity coordinates */
struct weston_CIExy {
	float x;
	float y;
};

/** Chromaticity coordinates and white point that defines the color gamut */
struct weston_color_gamut {
	struct weston_CIExy primary[3]; /* RGB order */
	struct weston_CIExy white_point;
};

enum weston_npm_direction {
	WESTON_NPM_FORWARD,
	WESTON_NPM_INVERSE
};

bool
weston_normalized_primary_matrix_init(struct weston_mat3f *npm,
				      const struct weston_color_gamut *gamut,
				      enum weston_npm_direction dir);

struct weston_mat3f
weston_bradford_adaptation(struct weston_CIExy from, struct weston_CIExy to);

/** Color primaries known by libweston */
enum weston_color_primaries {
	WESTON_PRIMARIES_CICP_SRGB = 0,
	WESTON_PRIMARIES_CICP_PAL_M,
	WESTON_PRIMARIES_CICP_PAL,
	WESTON_PRIMARIES_CICP_NTSC,
	WESTON_PRIMARIES_CICP_GENERIC_FILM,
	WESTON_PRIMARIES_CICP_BT2020,
	WESTON_PRIMARIES_CICP_CIE1931_XYZ,
	WESTON_PRIMARIES_CICP_DCI_P3,
	WESTON_PRIMARIES_CICP_DISPLAY_P3,
	WESTON_PRIMARIES_ADOBE_RGB,
};

/** Transfer functions known by libweston */
enum weston_transfer_function {
	WESTON_TF_BT1886 = 0,
	WESTON_TF_GAMMA22,
	WESTON_TF_GAMMA28,
	WESTON_TF_SRGB,
	WESTON_TF_EXT_SRGB,
	WESTON_TF_ST240,
	WESTON_TF_ST428,
	WESTON_TF_ST2084_PQ,
	WESTON_TF_EXT_LINEAR,
	WESTON_TF_LOG_100,
	WESTON_TF_LOG_316,
	WESTON_TF_XVYCC,
	WESTON_TF_HLG,
	WESTON_TF_POWER,
};

/** Error codes that the color profile parameters functions may return. */
enum weston_color_profile_param_builder_error {
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_TF = 0,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_PRIMARIES_NAMED,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_CIE_XY_OUT_OF_RANGE,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_CREATE_FAILED,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INVALID_LUMINANCE,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_INCOMPLETE_SET,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_ALREADY_SET,
	WESTON_COLOR_PROFILE_PARAM_BUILDER_ERROR_UNSUPPORTED,
};

struct weston_color_profile_param_builder *
weston_color_profile_param_builder_create(struct weston_compositor *compositor);

void
weston_color_profile_param_builder_destroy(struct weston_color_profile_param_builder *builder);

bool
weston_color_profile_param_builder_get_error(struct weston_color_profile_param_builder *builder,
                                             enum weston_color_profile_param_builder_error *err,
                                             char **err_msg);

bool
weston_color_profile_param_builder_set_primaries(struct weston_color_profile_param_builder *builder,
						 const struct weston_color_gamut *primaries);

bool
weston_color_profile_param_builder_set_primaries_named(struct weston_color_profile_param_builder *builder,
						       enum weston_color_primaries primaries);

bool
weston_color_profile_param_builder_set_tf_named(struct weston_color_profile_param_builder *builder,
						enum weston_transfer_function tf);

bool
weston_color_profile_param_builder_set_tf_power_exponent(struct weston_color_profile_param_builder *builder,
							 float power_exponent);

bool
weston_color_profile_param_builder_set_primary_luminance(struct weston_color_profile_param_builder *builder,
							 float ref_lum, float min_lum, float max_lum);

bool
weston_color_profile_param_builder_set_target_primaries(struct weston_color_profile_param_builder *builder,
							const struct weston_color_gamut *target_primaries);

bool
weston_color_profile_param_builder_set_target_luminance(struct weston_color_profile_param_builder *builder,
							float min_lum, float max_lum);

bool
weston_color_profile_param_builder_set_maxFALL(struct weston_color_profile_param_builder *builder,
					       float maxFALL);

bool
weston_color_profile_param_builder_set_maxCLL(struct weston_color_profile_param_builder *builder,
					      float maxCLL);

struct weston_color_profile *
weston_color_profile_param_builder_create_color_profile(struct weston_color_profile_param_builder *builder,
							const char *name_part,
							enum weston_color_profile_param_builder_error *err,
							char **err_msg);

enum weston_color_characteristics_groups {
	/** weston_color_characteristics::primary is set */
	WESTON_COLOR_CHARACTERISTICS_GROUP_PRIMARIES	= 0x01,

	/** weston_color_characteristics::white is set */
	WESTON_COLOR_CHARACTERISTICS_GROUP_WHITE	= 0x02,

	/** weston_color_characteristics::max_luminance is set */
	WESTON_COLOR_CHARACTERISTICS_GROUP_MAXL		= 0x04,

	/** weston_color_characteristics::min_luminance is set */
	WESTON_COLOR_CHARACTERISTICS_GROUP_MINL		= 0x08,

	/** weston_color_characteristics::maxFALL is set */
	WESTON_COLOR_CHARACTERISTICS_GROUP_MAXFALL	= 0x10,

	/** all valid bits */
	WESTON_COLOR_CHARACTERISTICS_GROUP_ALL_MASK	= 0x1f
};

/** Basic display color characteristics
 *
 * This is a simple description of a display or output (monitor) color
 * characteristics. The parameters can be found in EDID, with caveats. They
 * are particularly useful with HDR monitors.
 */
struct weston_color_characteristics {
	/** Which fields are valid
	 *
	 * A bitmask of values from enum weston_color_characteristics_groups.
	 */
	uint32_t group_mask;

	/* EOTF is tracked externally with enum weston_eotf_mode */

	/** Chromaticities of the primaries */
	struct weston_CIExy primary[3];

	/** White point chromaticity */
	struct weston_CIExy white;

	/** Display's desired maximum content peak luminance, cd/m² */
	float max_luminance;

	/** Display's desired minimum content luminance, cd/m² */
	float min_luminance;

	/** Display's desired maximum frame-average light level, cd/m² */
	float maxFALL;
};

struct weston_color_profile *
weston_color_profile_ref(struct weston_color_profile *cprof);

void
weston_color_profile_unref(struct weston_color_profile *cprof);

const char *
weston_color_profile_get_description(struct weston_color_profile *cprof);

struct weston_color_profile *
weston_compositor_load_icc_file(struct weston_compositor *compositor,
				const char *path);

#ifdef  __cplusplus
}
#endif

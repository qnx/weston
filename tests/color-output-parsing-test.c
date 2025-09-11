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

#include <libdisplay-info/info.h>

#include "weston-test-client-helper.h"
#include "weston-test-assert.h"

#include "weston-private.h"
#include "backend.h"
#include "color.h"
#include "color-properties.h"
#include "id-number-allocator.h"
#include "shared/string-helpers.h"
#include "shared/xalloc.h"

struct expected_params {
	struct weston_color_profile_params template;

	/* Cannot statically initialize these in the template: */
	enum weston_transfer_function tf;
	enum weston_color_primaries named_prim;
	bool use_named_prim;
};

struct config_testcase {
	enum weston_eotf_mode eotf_mode;
	enum weston_colorimetry_mode colorimetry_mode;
	const char *profile_name;
	const char *profile_string;

	const struct expected_params expected;
};

#define NO_VALUE -1.f
#define D65 { 0.3127f, 0.3290f }
#define prim_bt709 ((struct weston_color_gamut){		\
	.primary = {						\
		{ 0.640f, 0.330f },				\
		{ 0.300f, 0.600f },				\
		{ 0.150f, 0.060f },				\
	},							\
	.white_point = D65,					\
})
#define prim_bt2020 ((struct weston_color_gamut){		\
	.primary = {						\
		{ 0.708f, 0.292f },				\
		{ 0.170f, 0.797f },				\
		{ 0.131f, 0.046f },				\
	},							\
	.white_point = D65,					\
})
#define prim_display_p3 ((struct weston_color_gamut){		\
	.primary = {						\
		{ 0.680f, 0.320f },				\
		{ 0.265f, 0.690f },				\
		{ 0.150f, 0.060f },				\
	},							\
	.white_point = D65,					\
})
#define prim_hp_5dq99aa ((struct weston_color_gamut){		\
	.primary = {						\
		{ 0.6650f, 0.3261f },				\
		{ 0.2890f, 0.6435f },				\
		{ 0.1494f, 0.0507f },				\
	},							\
	.white_point = { 0.3134f, 0.3291f },			\
})

static const struct config_testcase config_cases[] = {
	{
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt709,
				.target_primaries = prim_bt709,
				.min_luminance = 0.2f,
				.max_luminance = 80.f,
				.reference_white_luminance = 80.f,
				.target_min_luminance = 0.2f,
				.target_max_luminance = 80.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_GAMMA22,
			.named_prim = WESTON_PRIMARIES_CICP_SRGB,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_TRADITIONAL_HDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt709,
				.target_primaries = prim_bt709,
				.min_luminance = 0.2f,
				.max_luminance = 80.f,
				.reference_white_luminance = 80.f,
				.target_min_luminance = 0.2f,
				.target_max_luminance = 80.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_GAMMA22,
			.named_prim = WESTON_PRIMARIES_CICP_SRGB,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_ST2084, WESTON_COLORIMETRY_MODE_DEFAULT,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt709,
				.target_primaries = prim_bt709,
				.min_luminance = 0.005f,
				.max_luminance = 10000.f,
				.reference_white_luminance = 203.f,
				.target_min_luminance = 0.005f,
				.target_max_luminance = 10000.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_ST2084_PQ,
			.named_prim = WESTON_PRIMARIES_CICP_SRGB,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_HLG, WESTON_COLORIMETRY_MODE_DEFAULT,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt709,
				.target_primaries = prim_bt709,
				.min_luminance = 0.005f,
				.max_luminance = 1000.f,
				.reference_white_luminance = 203.f,
				.target_min_luminance = 0.005f,
				.target_max_luminance = 1000.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_HLG,
			.named_prim = WESTON_PRIMARIES_CICP_SRGB,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_BT2020_RGB,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt2020,
				.target_primaries = prim_bt2020,
				.min_luminance = 0.010f,
				.max_luminance = 100.f,
				.reference_white_luminance = 100.f,
				.target_min_luminance = 0.010f,
				.target_max_luminance = 100.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_BT1886,
			.named_prim = WESTON_PRIMARIES_CICP_BT2020,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_ST2084, WESTON_COLORIMETRY_MODE_BT2020_RGB,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt2020,
				.target_primaries = prim_bt2020,
				.min_luminance = 0.005f,
				.max_luminance = 10000.f,
				.reference_white_luminance = 203.f,
				.target_min_luminance = 0.005f,
				.target_max_luminance = 10000.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_ST2084_PQ,
			.named_prim = WESTON_PRIMARIES_CICP_BT2020,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_HLG, WESTON_COLORIMETRY_MODE_BT2020_YCC,
		"auto:",
		"",
		{
			.template = {
				.primaries = prim_bt2020,
				.target_primaries = prim_bt2020,
				.min_luminance = 0.005f,
				.max_luminance = 1000.f,
				.reference_white_luminance = 203.f,
				.target_min_luminance = 0.005f,
				.target_max_luminance = 1000.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_HLG,
			.named_prim = WESTON_PRIMARIES_CICP_BT2020,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		"auto:edid-primaries edid-tf edid-dr",
		"",
		{
			.template = {
				.primaries = prim_hp_5dq99aa,
				.target_primaries = prim_hp_5dq99aa,
				.tf.params = { 2.2f, },
				.min_luminance = 0.2f,
				.max_luminance = 80.f,
				.reference_white_luminance = 80.f,
				.target_min_luminance = 0.2f,
				.target_max_luminance = 80.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_POWER,
		},
	},
	{
		WESTON_EOTF_MODE_ST2084, WESTON_COLORIMETRY_MODE_DEFAULT,
		"auto:edid-primaries edid-tf edid-dr",
		"",
		{
			.template = {
				.primaries = prim_hp_5dq99aa,
				.target_primaries = prim_hp_5dq99aa,
				.min_luminance = 0.005f,
				.max_luminance = 10000.f,
				.reference_white_luminance = 203.f,
				.target_min_luminance = 0.f,
				.target_max_luminance = 603.6657f,
				.maxCLL = NO_VALUE,
				.maxFALL = 351.2504f,
			},
			.tf = WESTON_TF_ST2084_PQ,
		},
	},
	{
		WESTON_EOTF_MODE_ST2084, WESTON_COLORIMETRY_MODE_BT2020_RGB,
		"auto:edid-primaries edid-tf edid-dr",
		"",
		{
			.template = {
				.primaries = prim_bt2020,
				.target_primaries = prim_bt2020,
				.min_luminance = 0.005f,
				.max_luminance = 10000.f,
				.reference_white_luminance = 203.f,
				.target_min_luminance = 0.f,
				.target_max_luminance = 603.6657f,
				.maxCLL = NO_VALUE,
				.maxFALL = 351.2504f,
			},
			.tf = WESTON_TF_ST2084_PQ,
			.named_prim = WESTON_PRIMARIES_CICP_BT2020,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_HLG, WESTON_COLORIMETRY_MODE_P3D65,
		"srgb:",
		"",
		{
			.template = {
				.primaries = prim_bt709,
				.target_primaries = prim_bt709,
				.min_luminance = 0.2f,
				.max_luminance = 80.f,
				.reference_white_luminance = 80.f,
				.target_min_luminance = 0.2f,
				.target_max_luminance = 80.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_GAMMA22,
			.named_prim = WESTON_PRIMARIES_CICP_SRGB,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		"mydisp",
		"prim_named=display_p3\n"
		"target_named=srgb\n"
		"tf_named=gamma22\n",
		{
			.template = {
				.primaries = prim_display_p3,
				.target_primaries = prim_bt709,
				.min_luminance = 0.2f,
				.max_luminance = 80.f,
				.reference_white_luminance = 80.f,
				.target_min_luminance = 0.2f,
				.target_max_luminance = 80.f,
				.maxCLL = NO_VALUE,
				.maxFALL = NO_VALUE,
			},
			.tf = WESTON_TF_GAMMA22,
			.named_prim = WESTON_PRIMARIES_CICP_DISPLAY_P3,
			.use_named_prim = true,
		},
	},
	{
		WESTON_EOTF_MODE_SDR, WESTON_COLORIMETRY_MODE_DEFAULT,
		"mydisp",
		"prim_red=1.0 0\n"
		"prim_green=0.0 1\n"
		"prim_blue=0 0\n"
		"prim_white=0.333333 0.333333\n"
		"min_lum=0\n"
		"ref_lum=150\n"
		"max_lum=860\n"
		"target_red=0.681 0.319\n"
		"target_green=24.3e-2 6.92e-1\n"
		"target_blue=   0.155\t0.07\n"
		"target_white= \t 0.310 \t 0.316   \t\n"
		"target_min_lum=1e-1\n"
		"target_max_lum=555.5\n"
		"max_fall=213\n"
		"max_cll=550\n"
		"tf_power=2.35\n",
		{
			.template = {
				.primaries = {
					.primary = {
						{ 1.f, 0.f },
						{ 0.f, 1.f },
						{ 0.f, 0.f },
					},
					.white_point = { 1.f / 3, 1.f / 3 },
				},
				.target_primaries = {
					.primary = {
						{ 0.681f, 0.319f },
						{ 0.243f, 0.692f },
						{ 0.155f, 0.070f },
					},
					.white_point = { 0.310f, 0.316f },
				},
				.tf.params = { 2.35f, },
				.min_luminance = 0.f,
				.max_luminance = 860.f,
				.reference_white_luminance = 150.f,
				.target_min_luminance = 0.1f,
				.target_max_luminance = 555.5f,
				.maxCLL = 550.f,
				.maxFALL = 213.f,
			},
			.tf = WESTON_TF_POWER,
		},
	},
};

static struct di_info *display_edid;

static int
logger(const char *fmt, va_list arg)
{
	return vfprintf(stderr, fmt, arg);
}

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	enum test_result_code ret;
	char *fname;
	size_t len;
	char *edid_data;

	str_printf(&fname, "%s/hp-5dq99aa-hdmi.edid", reference_path());
	abort_oom_if_null(fname);

	len = read_blob_from_file(fname, &edid_data);
	free(fname);
	if (!test_assert_u64_gt(len, 0))
		return RESULT_HARD_ERROR;

	display_edid = di_info_parse_edid(edid_data, len);
	free(edid_data);
	abort_oom_if_null(display_edid);

	ret = weston_test_harness_execute_standalone(harness);

	di_info_destroy(display_edid);
	display_edid = NULL;

	return ret;
}
DECLARE_FIXTURE_SETUP(fixture_setup)

static struct weston_config *
create_config(const struct config_testcase *t)
{
	struct compositor_setup setup;
	struct weston_config *wc;

	compositor_setup_defaults(&setup);
	weston_ini_setup(&setup,
			 cfgln("[color-profile]"),
			 cfgln("name=mydisp"),
			 cfgln("%s", t->profile_string));

	wc = weston_config_parse(setup.config_file);
	test_assert_ptr_not_null(wc);
	free(setup.config_file);

	return wc;
}

struct mock_color_manager {
	struct weston_color_manager base;
};

struct mock_color_profile {
	struct weston_color_profile base;
	struct weston_color_profile_params params;
};

static struct mock_color_profile *
to_mock_cprof(struct weston_color_profile *cprof)
{
	return container_of(cprof, struct mock_color_profile, base);
}

static struct weston_color_profile *
mock_cm_ref_stock_sRGB_color_profile(struct weston_color_manager *mock_cm)
{
	struct mock_color_profile *mock_cprof;

	mock_cprof = xzalloc(sizeof(*mock_cprof));

	weston_color_profile_init(&mock_cprof->base, mock_cm);
	str_printf(&mock_cprof->base.description, "Mock sRGB profile");

	return &mock_cprof->base;
}

static bool
mock_cm_get_color_profile_from_params(struct weston_color_manager *mock_cm,
				      const struct weston_color_profile_params *params,
				      const char *name_part,
				      struct weston_color_profile **cprof_out,
				      char **errmsg)
{
	struct mock_color_profile *mock_cprof;

	mock_cprof = xzalloc(sizeof(*mock_cprof));

	weston_color_profile_init(&mock_cprof->base, mock_cm);
	str_printf(&mock_cprof->base.description, "Mock profile %s", name_part);
	mock_cprof->params = *params;

	*cprof_out = &mock_cprof->base;
	return true;
}

static void
mock_cm_destroy_color_profile(struct weston_color_profile *cprof)
{
	struct mock_color_profile *mock_cprof = to_mock_cprof(cprof);

	free(mock_cprof->base.description);
	free(mock_cprof);
}

static bool
test_assert_CIExy_eq(const struct weston_CIExy *ref,
		     const struct weston_CIExy *tst,
		     float tolerance,
		     int indent,
		     const char *desc)
{
	bool r = true;

	r = test_assert_f32_absdiff_lt(ref->x, tst->x, tolerance) && r;
	r = test_assert_f32_absdiff_lt(ref->y, tst->y, tolerance) && r;

	if (!r)
		testlog("%*sin %s\n", indent, "", desc);

	return r;
}

static bool
test_assert_color_gamut_eq(const struct weston_color_gamut *ref,
			   const struct weston_color_gamut *tst,
			   float tolerance,
			   int indent,
			   const char *desc)
{
	static const char *chan[] = { "red", "green", "blue" };
	bool r = true;
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(tst->primary); i++) {
		r = test_assert_CIExy_eq(&ref->primary[i], &tst->primary[i],
					 tolerance, indent + 2, chan[i]) && r;
	}

	r = test_assert_CIExy_eq(&ref->white_point, &tst->white_point,
				 tolerance, indent + 2, "white point") && r;

	if (!r)
		testlog("%*sin %s\n", indent, "", desc);

	return r;
}

static void
assert_params_equal(const struct weston_color_profile_params *ref,
		    const struct weston_color_profile_params *tst)
{
	float tol = 0.0001;
	int indent = 4;
	unsigned i;

	test_assert_color_gamut_eq(&tst->primaries, &ref->primaries, tol, indent, "primaries");
	test_assert_ptr_eq(tst->primaries_info, ref->primaries_info);

	test_assert_ptr_eq(tst->tf.info, ref->tf.info);
	for (i = 0; i < ARRAY_LENGTH(tst->tf.params); i++) {
		if (!test_assert_f32_absdiff_lt(ref->tf.params[i], tst->tf.params[i], tol))
			testlog("%*sin tf.params[%d]\n", indent, "", i);
	}

	test_assert_f32_absdiff_lt(ref->min_luminance, tst->min_luminance, tol);
	test_assert_f32_absdiff_lt(ref->max_luminance, tst->max_luminance, tol);
	test_assert_f32_absdiff_lt(ref->reference_white_luminance, tst->reference_white_luminance, tol);

	test_assert_color_gamut_eq(&ref->target_primaries, &tst->target_primaries,
				   tol, indent, "target primaries");

	test_assert_f32_absdiff_lt(ref->target_min_luminance, tst->target_min_luminance, tol);
	test_assert_f32_absdiff_lt(ref->target_max_luminance, tst->target_max_luminance, tol);
	test_assert_f32_absdiff_lt(ref->maxCLL, tst->maxCLL, tol);
	test_assert_f32_absdiff_lt(ref->maxFALL, tst->maxFALL, tol);
}

static void
compare_results(struct weston_color_profile *tst,
		const struct expected_params *expected)
{
	const struct mock_color_profile *mock_cprof = to_mock_cprof(tst);
	struct weston_color_profile_params ref = expected->template;

	ref.tf.info = weston_color_tf_info_from(NULL, expected->tf);

	if (expected->use_named_prim) {
		ref.primaries_info = weston_color_primaries_info_from(NULL, expected->named_prim);
	}

	assert_params_equal(&ref, &mock_cprof->params);
}

/*
 * Manufacture various weston.ini and check what
 * wet_create_output_color_profile() says. Tests for the return value and
 * the error messages logged.
 */
TEST_P(parametric_color_profile_parsing, config_cases)
{
	const struct config_testcase *t = data;
	struct weston_color_profile *cprof;
	struct weston_config *wc;
	struct mock_color_manager mock_cm = {
		.base.ref_stock_sRGB_color_profile = mock_cm_ref_stock_sRGB_color_profile,
		.base.get_color_profile_from_params = mock_cm_get_color_profile_from_params,
		.base.destroy_color_profile = mock_cm_destroy_color_profile,
		.base.supported_color_features = 0xffffffff,
		.base.supported_primaries_named = 0xffffffff,
		.base.supported_tf_named = 0xffffffff,
	};
	struct weston_compositor mock_compositor = {
		.color_manager = &mock_cm.base,
		.color_profile_id_generator = weston_idalloc_create(&mock_compositor),
	};
	struct weston_output mock_output = {};
	struct weston_head mock_head = {};

	mock_cm.base.compositor = &mock_compositor;

	wl_list_init(&mock_compositor.plane_list);

	weston_log_set_handler(logger, logger);

	weston_head_init(&mock_head, "mock head");
	weston_head_set_supported_eotf_mask(&mock_head, WESTON_EOTF_MODE_ALL_MASK);
	weston_head_set_supported_colorimetry_mask(&mock_head, WESTON_COLORIMETRY_MODE_ALL_MASK);
	mock_head.display_info = display_edid; /* from fixture_setup() */

	weston_output_init(&mock_output, &mock_compositor, "mockoutput");
	weston_output_attach_head(&mock_output, &mock_head);
	weston_output_set_eotf_mode(&mock_output, t->eotf_mode);
	weston_output_set_colorimetry_mode(&mock_output, t->colorimetry_mode);

	wc = create_config(t);
	cprof = wet_create_output_color_profile(&mock_output, wc, t->profile_name);
	test_assert_ptr_not_null(cprof);

	compare_results(cprof, &t->expected);

	weston_color_profile_unref(cprof);

	weston_config_destroy(wc);
	weston_output_release(&mock_output);
	mock_head.display_info = NULL; /* freed in fixture_setup() */
	weston_head_release(&mock_head);
	weston_idalloc_destroy(mock_compositor.color_profile_id_generator);

	return RESULT_OK;
}

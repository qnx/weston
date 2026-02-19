/*
 * Copyright 2023 Collabora, Ltd.
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

#include <libweston/linalg-3.h>

#include "color-properties.h"
#include "color-manager-client.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"
#include "shared/xalloc.h"
#include "lcms_util.h"

#include "color-management-v1-client-protocol.h"

#include <fcntl.h>
#include <sys/stat.h>

static char srgb_icc_profile_path[500] = "\0";

const struct lcms_pipeline pipeline_sRGB = {
	.color_space = "sRGB",
	.prim_output = {
		.Red =   { 0.640, 0.330, 1.0 },
		.Green = { 0.300, 0.600, 1.0 },
		.Blue =  { 0.150, 0.060, 1.0 }
	},
	.pre_fn = TRANSFER_FN_SRGB,
	.mat = WESTON_MAT3F_IDENTITY,
	.post_fn = TRANSFER_FN_SRGB_INVERSE
};

static struct color_manager_client *
color_manager_get(struct client *client)
{
	struct color_manager_client *cm = client_get_color_manager(client, 1);

	/* Weston supports all color features. */
	test_assert_u32_eq(cm->supported_features,
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_ICC_V2_V4) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES) |
			   (1 << WP_COLOR_MANAGER_V1_FEATURE_EXTENDED_TARGET_VOLUME));

	/* Weston supports all rendering intents. */
	test_assert_u32_eq(cm->supported_rendering_intents,
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_RELATIVE) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_SATURATION) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_ABSOLUTE) |
			   (1 << WP_COLOR_MANAGER_V1_RENDER_INTENT_RELATIVE_BPC));

	test_assert_true(cm->init_done);

	return cm;
}

static void
build_sRGB_icc_profile(const char *filename)
{
	cmsHPROFILE profile;
	double vcgt_exponents[COLOR_CHAN_NUM] = { 0.0 };
	bool saved;

	profile = build_lcms_matrix_shaper_profile_output(NULL, &pipeline_sRGB,
							  vcgt_exponents);
	test_assert_ptr_not_null(profile);

	saved = cmsSaveProfileToFile(profile, filename);
	test_assert_true(saved);

	cmsCloseProfile(profile);
}

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
        setup.renderer = WESTON_RENDERER_GL;
	setup.shell = SHELL_TEST_DESKTOP;
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	/* Create the sRGB ICC profile. We do that only once for this test
	 * program. */
	if (strlen(srgb_icc_profile_path) == 0) {
		char *tmp;

		tmp = output_filename_for_test_program(THIS_TEST_NAME,
						       NULL, "icm");
		test_assert_int_lt(strlen(tmp), ARRAY_LENGTH(srgb_icc_profile_path));
		strcpy(srgb_icc_profile_path, tmp);
		free(tmp);

		build_sRGB_icc_profile(srgb_icc_profile_path);
	}

	weston_ini_setup(&setup,
			 cfgln("[core]"),
			 cfgln("color-management=true"));

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

TEST(smoke_test)
{
	struct client *client;

	client = create_client_and_test_surface(100, 100, 100, 100);
	color_manager_get(client);
	client_destroy(client);

	return RESULT_OK;
}

TEST(output_get_image_description)
{
	struct client *client;
	struct color_manager_client *cm;
	struct image_description *image_descr;
	struct image_description_info *info;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	/* Get image description from output */
	image_descr = image_description_create_for_output(cm, client->output);
	image_description_wait_until_ready(client, image_descr);

	/* Get output image description information */
	info = image_description_get_information(client, image_descr);

	image_description_info_destroy(info);
	image_description_destroy(image_descr);
	client_destroy(client);

	return RESULT_OK;
}

TEST(surface_get_preferred_image_description)
{
	struct client *client;
	struct color_manager_client *cm;
	struct image_description *image_descr;
	struct image_description_info *info;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	/* Get preferred image description from surface */
	image_descr = image_description_create_for_preferred(cm, client->surface);
	image_description_wait_until_ready(client, image_descr);

	/* Get surface image description information */
	info = image_description_get_information(client, image_descr);

	image_description_info_destroy(info);
	image_description_destroy(image_descr);
	client_destroy(client);

	return RESULT_OK;
}

TEST(create_image_description_before_setting_icc_file)
{
	struct client *client;
	struct color_manager_client *cm;
	struct wp_image_description_creator_icc_v1 *image_descr_creator_icc;
	struct wp_image_description_v1 *image_desc;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	image_descr_creator_icc =
		wp_color_manager_v1_create_icc_creator(cm->manager_proxy);

	/* Try creating image description based on ICC profile but without
	 * setting the ICC file, what should fail.
	 *
	 * We expect a protocol error from unknown object, because the
	 * image_descr_creator_icc wl_proxy will get destroyed with the create
	 * call below. It is a destructor request. */
	image_desc = wp_image_description_creator_icc_v1_create(image_descr_creator_icc);
	expect_protocol_error(client, NULL,
			      WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_INCOMPLETE_SET);

	wp_image_description_v1_destroy(image_desc);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_unreadable_icc_fd)
{
	struct client *client;
	struct color_manager_client *cm;
	struct wp_image_description_creator_icc_v1 *image_descr_creator_icc;
	int32_t icc_fd;
	struct stat st;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	image_descr_creator_icc =
		wp_color_manager_v1_create_icc_creator(cm->manager_proxy);

	/* The file is being open with WRITE, not READ permission. So the
	 * compositor should complain. */
	icc_fd = open(srgb_icc_profile_path, O_WRONLY);
	test_assert_s32_ge(icc_fd, 0);
	test_assert_int_eq(fstat(icc_fd, &st), 0);

	/* Try setting the bad ICC file fd, it should fail. */
	wp_image_description_creator_icc_v1_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	expect_protocol_error(client, &wp_image_description_creator_icc_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD);

	close(icc_fd);
	wp_image_description_creator_icc_v1_destroy(image_descr_creator_icc);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_bad_icc_size_zero)
{
	struct client *client;
	struct color_manager_client *cm;
	struct wp_image_description_creator_icc_v1 *image_descr_creator_icc;
	int32_t icc_fd;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	image_descr_creator_icc =
		wp_color_manager_v1_create_icc_creator(cm->manager_proxy);

	icc_fd = open(srgb_icc_profile_path, O_RDONLY);
	test_assert_s32_ge(icc_fd, 0);

	/* Try setting ICC file with a bad size, it should fail. */
	wp_image_description_creator_icc_v1_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, 0);
	expect_protocol_error(client, &wp_image_description_creator_icc_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_SIZE);

	close(icc_fd);
	wp_image_description_creator_icc_v1_destroy(image_descr_creator_icc);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_bad_icc_non_seekable)
{
	struct client *client;
	struct color_manager_client *cm;
	struct wp_image_description_creator_icc_v1 *image_descr_creator_icc;
	int32_t fds[2];

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	image_descr_creator_icc =
		wp_color_manager_v1_create_icc_creator(cm->manager_proxy);

	/* We need a non-seekable file, and pipes are non-seekable. */
	test_assert_int_ge(pipe(fds), 0);

	/* Pretend that it has a valid size of 1024 bytes. That still should
	 * fail because the fd is non-seekable. */
	wp_image_description_creator_icc_v1_set_icc_file(image_descr_creator_icc,
							 fds[0], 0, 1024);
	expect_protocol_error(client, &wp_image_description_creator_icc_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD);

	close(fds[0]);
	close(fds[1]);
	wp_image_description_creator_icc_v1_destroy(image_descr_creator_icc);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_icc_twice)
{
	struct client *client;
	struct color_manager_client *cm;
	struct wp_image_description_creator_icc_v1 *image_descr_creator_icc;
	int32_t icc_fd;
	struct stat st;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	image_descr_creator_icc =
		wp_color_manager_v1_create_icc_creator(cm->manager_proxy);

	icc_fd = open(srgb_icc_profile_path, O_RDONLY);
	test_assert_s32_ge(icc_fd, 0);
	test_assert_int_eq(fstat(icc_fd, &st), 0);

	wp_image_description_creator_icc_v1_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	client_roundtrip(client);

	/* Set the ICC again, what should fail. */
	wp_image_description_creator_icc_v1_set_icc_file(image_descr_creator_icc,
							 icc_fd, 0, st.st_size);
	expect_protocol_error(client, &wp_image_description_creator_icc_v1_interface,
			      WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_ALREADY_SET);

	close(icc_fd);
	wp_image_description_creator_icc_v1_destroy(image_descr_creator_icc);
	client_destroy(client);

	return RESULT_OK;
}

TEST(create_icc_image_description_no_info)
{
	struct client *client;
	struct color_manager_client *cm;
	struct image_description *image_descr;
	struct wp_image_description_info_v1 *proxy;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	/* Create image description based on ICC profile */
	image_descr = image_description_create_for_icc(cm, srgb_icc_profile_path);
	image_description_wait_until_ready(client, image_descr);

	/* Get image description information, and that should fail. Images
	 * descriptions that we create do not accept this request. */
	proxy = wp_image_description_v1_get_information(image_descr->proxy);
	expect_protocol_error(client, &wp_image_description_v1_interface,
			      WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION);

	wp_image_description_info_v1_destroy(proxy);
	image_description_destroy(image_descr);
	client_destroy(client);

	return RESULT_OK;
}

TEST(set_surface_image_description)
{
	struct client *client;
	struct color_manager_client *cm;
	struct image_description *image_descr;
	struct wp_color_management_surface_v1 *cm_surface;

	client = create_client_and_test_surface(100, 100, 100, 100);
	cm = color_manager_get(client);

	cm_surface = wp_color_manager_v1_get_surface(cm->manager_proxy, client->surface->wl_surface);

	/* Create image description based on ICC profile */
	image_descr = image_description_create_for_icc(cm, srgb_icc_profile_path);
	image_description_wait_until_ready(client, image_descr);

	/* Set surface image description */
	wp_color_management_surface_v1_set_image_description(cm_surface,
							     image_descr->proxy,
							     WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
	client_roundtrip(client);

	wp_color_management_surface_v1_destroy(cm_surface);
	image_description_destroy(image_descr);
	client_destroy(client);

	return RESULT_OK;
}

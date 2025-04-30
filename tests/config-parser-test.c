/*
 * Copyright © 2013 Intel Corporation
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

#include <stdio.h>

#include <libweston/config-parser.h>

#include "weston-test-client-helper.h"
#include "weston-test-assert.h"

static struct weston_config *
load_config(const char *text)
{
	struct weston_config *config = NULL;
	char *content = NULL;
	size_t file_len = 0;
	int write_len;
	FILE *file;

	file = open_memstream(&content, &file_len);
	test_assert_ptr_not_null(file);

	write_len = fwrite(text, 1, strlen(text), file);
	test_assert_int_eq((int)strlen(text), write_len);

	test_assert_int_eq(fflush(file), 0);
	fseek(file, 0L, SEEK_SET);

	config = weston_config_parse_fp(file);

	fclose(file);
	free(content);

	return config;
}

static struct weston_config *
assert_load_config(const char *text)
{
	struct weston_config *config = load_config(text);
	test_assert_ptr_not_null(config);

	return config;
}

static const char *comment_only_text =
	"# nothing in this file...\n";

TEST(comment_only)
{
	struct weston_config *config = assert_load_config(comment_only_text);

	weston_config_destroy(config);

	return RESULT_OK;
}

/** @todo legit tests should have more descriptive names. */

static const char *legit_text =
	"# comment line here...\n"
	"\n"
	"[foo]\n"
	"a=b\n"
	"name=  Roy Batty    \n"
	"\n"
	"\n"
	"[bar]\n"
	"# more comments\n"
	"number=5252\n"
	"zero=0\n"
	"negative=-42\n"
	"flag=false\n"
	"real=4.667\n"
	"negreal=-3.2\n"
	"expval=24.687E+15\n"
	"negexpval=-3e-2\n"
	"notanumber=nan\n"
	"empty=\n"
	"tiny=0.0000000000000000000000000000000000000063548\n"
	"\n"
	"[colors]\n"
	"none=0x00000000\n"
	"low=0x11223344\n"
	"high=0xff00ff00\n"
	"oct=01234567\n"
	"dec=12345670\n"
	"short=1234567\n"
	"\n"
	"[stuff]\n"
	"flag=     true \n"
	"\n"
	"[bucket]\n"
	"color=blue \n"
	"contents=live crabs\n"
	"pinchy=true\n"
	"\n"
	"[bucket]\n"
	"material=plastic \n"
	"color=red\n"
	"contents=sand\n";

TEST(legit_test01)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;

	section = weston_config_get_section(config,
					    "mollusc", NULL, NULL);
	test_assert_ptr_null(section);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test02)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config, "foo", NULL, NULL);
	r = weston_config_section_get_string(section, "a", &s, NULL);

	test_assert_int_eq(0, r);
	test_assert_str_eq("b", s);

	free(s);
	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test03)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config, "foo", NULL, NULL);
	r = weston_config_section_get_string(section, "b", &s, NULL);

	test_assert_int_eq(-1, r);
	test_assert_errno(ENOENT);
	test_assert_ptr_null(s);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test04)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config, "foo", NULL, NULL);
	r = weston_config_section_get_string(section, "name", &s, NULL);

	test_assert_int_eq(0, r);
	test_assert_str_eq("Roy Batty", s);

	free(s);
	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test05)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_string(section, "a", &s, "boo");

	test_assert_int_eq(-1, r);
	test_assert_errno(ENOENT);
	test_assert_str_eq("boo", s);

	free(s);
	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test06)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	int32_t n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_int(section, "number", &n, 600);

	test_assert_int_eq(0, r);
	test_assert_s32_eq(5252, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test07)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	int32_t n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_int(section, "+++", &n, 700);

	test_assert_int_eq(-1, r);
	test_assert_errno(ENOENT);
	test_assert_s32_eq(700, n);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test08)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t u;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_uint(section, "number", &u, 600);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(5252, u);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test09)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t u;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_uint(section, "+++", &u, 600);

	test_assert_int_eq(-1, r);
	test_assert_errno(ENOENT);
	test_assert_u32_eq(600, u);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test10)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	bool b;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_bool(section, "flag", &b, true);

	test_assert_int_eq(0, r);
	test_assert_false(b);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test11)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	bool b;

	section = weston_config_get_section(config, "stuff", NULL, NULL);
	r = weston_config_section_get_bool(section, "flag", &b, false);

	test_assert_int_eq(0, r);
	test_assert_true(b);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test12)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	bool b;

	section = weston_config_get_section(config, "stuff", NULL, NULL);
	r = weston_config_section_get_bool(section, "bonk", &b, false);

	test_assert_int_eq(-1, r);
	test_assert_errno(ENOENT);
	test_assert_false(b);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test13)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config,
					    "bucket", "color", "blue");
	r = weston_config_section_get_string(section, "contents", &s, NULL);

	test_assert_int_eq(0, r);
	test_assert_str_eq("live crabs", s);

	free(s);
	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test14)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config,
					    "bucket", "color", "red");
	r = weston_config_section_get_string(section, "contents", &s, NULL);

	test_assert_int_eq(0, r);
	test_assert_str_eq("sand", s);

	free(s);
	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test15)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	char *s;
	int r;

	section = weston_config_get_section(config,
					    "bucket", "color", "pink");
	test_assert_ptr_null(section);
	r = weston_config_section_get_string(section, "contents", &s, "eels");

	test_assert_int_eq(-1, r);
	test_assert_errno(ENOENT);
	test_assert_str_eq("eels", s);

	free(s);
	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test16)
{
	static const char *section_names[] = {
		"foo", "bar", "colors", "stuff", "bucket", "bucket"
	};
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	const char *name;
	int i;

	section = NULL;
	i = 0;
	while (weston_config_next_section(config, &section, &name))
		test_assert_str_eq(section_names[i++], name);

	test_assert_int_eq(6, i);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test17)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	int32_t n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_int(section, "zero", &n, 600);

	test_assert_int_eq(0, r);
	test_assert_s32_eq(0, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test18)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_uint(section, "zero", &n, 600);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(0, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test19)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	section = weston_config_get_section(config, "colors", NULL, NULL);
	r = weston_config_section_get_color(section, "none", &n, 0xff336699);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(0x000000, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test20)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	section = weston_config_get_section(config, "colors", NULL, NULL);
	r = weston_config_section_get_color(section, "low", &n, 0xff336699);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(0x11223344, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test21)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	section = weston_config_get_section(config, "colors", NULL, NULL);
	r = weston_config_section_get_color(section, "high", &n, 0xff336699);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(0xff00ff00, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test22)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	/* Treat colors as hex values even if missing the leading 0x */
	section = weston_config_get_section(config, "colors", NULL, NULL);
	r = weston_config_section_get_color(section, "oct", &n, 0xff336699);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(0x01234567, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test23)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	/* Treat colors as hex values even if missing the leading 0x */
	section = weston_config_get_section(config, "colors", NULL, NULL);
	r = weston_config_section_get_color(section, "dec", &n, 0xff336699);

	test_assert_int_eq(0, r);
	test_assert_u32_eq(0x12345670, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test24)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	/* 7-digit colors are not valid (most likely typos) */
	section = weston_config_get_section(config, "colors", NULL, NULL);
	r = weston_config_section_get_color(section, "short", &n, 0xff336699);

	test_assert_int_eq(-1, r);
	test_assert_u32_eq(0xff336699, n);
	test_assert_errno(EINVAL);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test25)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	/* String color names are unsupported */
	section = weston_config_get_section(config, "bucket", NULL, NULL);
	r = weston_config_section_get_color(section, "color", &n, 0xff336699);

	test_assert_int_eq(-1, r);
	test_assert_u32_eq(0xff336699, n);
	test_assert_errno(EINVAL);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test26)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	int32_t n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_int(section, "negative", &n, 600);

	test_assert_int_eq(0, r);
	test_assert_s32_eq(-42, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(legit_test27)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	uint32_t n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_uint(section, "negative", &n, 600);

	test_assert_int_eq(-1, r);
	test_assert_u32_eq(600, n);
	test_assert_errno(ERANGE);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_number)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "number", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(5252.0, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_missing)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "+++", &n, 600.0);

	test_assert_int_eq(-1, r);
	test_assert_f64_eq(600.0, n);
	test_assert_errno(ENOENT);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_zero)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "zero", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(n, 0.0);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_negative)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "negative", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(n, -42.0);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_flag)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "flag", &n, 600.0);

	test_assert_int_eq(-1, r);
	test_assert_f64_eq(n, 600.0);
	test_assert_errno(EINVAL);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_real)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "real", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(4.667, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_negreal)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "negreal", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(-3.2, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_expval)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "expval", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(24.687e+15, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_negexpval)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "negexpval", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(-3e-2, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_notanumber)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "notanumber", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_true(isnan(n));
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_empty)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "empty", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(0.0, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

TEST(get_double_tiny)
{
	struct weston_config *config = assert_load_config(legit_text);
	struct weston_config_section *section;
	int r;
	double n;

	errno = 0;
	section = weston_config_get_section(config, "bar", NULL, NULL);
	r = weston_config_section_get_double(section, "tiny", &n, 600.0);

	test_assert_int_eq(0, r);
	test_assert_f64_eq(6.3548e-39, n);
	test_assert_errno(0);

	weston_config_destroy(config);

	return RESULT_OK;
}

struct doesnt_parse_test { char *text; };

static const struct doesnt_parse_test doesnt_parse_test_data[] = {
	{
		"# invalid section...\n"
		"[this bracket isn't closed\n",
	}, {
		"# line without = ...\n"
		"[bambam]\n"
		"this line isn't any kind of valid\n",
	}, {
		"# starting with = ...\n"
		"[bambam]\n"
		"=not valid at all\n",
	},
};

TEST_P(doesnt_parse, doesnt_parse_test_data)
{
	struct doesnt_parse_test *test = (struct doesnt_parse_test *) data;
	struct weston_config *config = load_config(test->text);
	test_assert_ptr_null(config);

	return RESULT_OK;
}

TEST(destroy_null)
{
	weston_config_destroy(NULL);
	test_assert_int_eq(0, weston_config_next_section(NULL, NULL, NULL));

	return RESULT_OK;
}

TEST(section_from_null)
{
	struct weston_config_section *section;
	section = weston_config_get_section(NULL, "bucket", NULL, NULL);
	test_assert_ptr_null(section);

	return RESULT_OK;
}

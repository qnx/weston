/*
 * Copyright © 2016 Samsung Electronics Co., Ltd
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

#ifndef WESTON_STRING_HELPERS_H
#define WESTON_STRING_HELPERS_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "shared/helpers.h"

/* Convert string to integer
 *
 * Parses a base-10 number from the given string.  Checks that the
 * string is not blank, contains only numerical characters, and is
 * within the range of INT32_MIN to INT32_MAX.  If the validation is
 * successful the result is stored in *value; otherwise *value is
 * unchanged and errno is set appropriately.
 *
 * \return true if the number parsed successfully, false on error
 */
static inline bool
safe_strtoint(const char *str, int32_t *value)
{
	long ret;
	char *end;

	assert(str != NULL);

	errno = 0;
	ret = strtol(str, &end, 10);
	if (errno != 0) {
		return false;
	} else if (end == str || *end != '\0') {
		errno = EINVAL;
		return false;
	}

	if ((long)((int32_t)ret) != ret) {
		errno = ERANGE;
		return false;
	}
	*value = (int32_t)ret;

	return true;
}

/**
 * Exactly like asprintf(), but sets *str_out to NULL if it fails.
 *
 * If str_out is NULL, does nothing.
 */
static inline void __attribute__ ((format (printf, 2, 3)))
str_printf(char **str_out, const char *fmt, ...)
{
	char *msg;
	va_list ap;
	int ret;

	if (!str_out)
		return;

	va_start(ap, fmt);
	ret = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (ret >= 0)
		*str_out = msg;
	else
		*str_out = NULL;
}

/**
 * Utility to print combination of enum values as string
 *
 * Only works for enum whose values are defined as power of two. Given a bitmask
 * in which each bit represents an enum value and a function that maps each enum
 * value to a string, this function returns a string (comma separated) with all
 * the enum values that are present in the bitmask.
 *
 * \param bits The bitmask of enum values.
 * \param map Function that maps enum values to string.
 * \return A string combining all the enum values from the bitmask, comma
 * separated. Callers must free() it.
 */
static inline char *
bits_to_str(uint32_t bits, const char *(*map)(uint32_t))
{
	FILE *fp;
	char *str = NULL;
	size_t size = 0;
	unsigned i;
	const char *sep = "";

	fp = open_memstream(&str, &size);
	if (!fp)
		return NULL;

	for (i = 0; bits; i++) {
		uint32_t bitmask = 1u << i;

		if (bits & bitmask) {
			fprintf(fp, "%s%s", sep, map(bitmask));
			sep = ", ";
		}

		bits &= ~bitmask;
	}
	fclose(fp);

	return str;
}

static inline const char *
yesno(bool cond)
{
	return cond ? "yes" : "no";
}

struct weston_enum_map {
	const char *name;
	uint32_t value;
};

/** Find a name-value pair by name.
 *
 * \param map Array of enum mappings.
 * \param len Length of the array.
 * \param name The name string to look an exact match for.
 * \return Pointer to the array element or NULL if not found.
 */
static inline const struct weston_enum_map *
weston_enum_map_find_name_(const struct weston_enum_map *map, size_t map_len,
			   const char *name)
{
	size_t i;

	for (i = 0; i < map_len; i++) {
		if (strcmp(map[i].name, name) == 0)
			return &map[i];
	}

	return NULL;
}

/** Find a name-value pair by name.
 *
 * \param map Array of enum mappings. The length is determined by ARRAY_LENGTH(map).
 * \param name The name string to look an exact match for.
 * \return Pointer to the array element or NULL if not found.
 */
#define weston_enum_map_find_name(map, name) \
	weston_enum_map_find_name_((map), ARRAY_LENGTH(map), (name))

/** Find a name-value pair by value.
 *
 * \param map Array of enum mappings.
 * \param len Length of the array.
 * \param value The value to look for.
 * \return Pointer to the array element or NULL if not found.
 */
static inline const struct weston_enum_map *
weston_enum_map_find_value_(const struct weston_enum_map *map, size_t map_len,
			    uint32_t value)
{
	size_t i;

	for (i = 0; i < map_len; i++) {
		if (map[i].value == value)
			return &map[i];
	}

	return NULL;
}

/** Find a name-value pair by value.
 *
 * \param map Array of enum mappings. The length is determined by ARRAY_LENGTH(map).
 * \param value The value to look for.
 * \return Pointer to the array element or NULL if not found.
 */
#define weston_enum_map_find_value(map, value) \
	weston_enum_map_find_value_((map), ARRAY_LENGTH(map), (value))

#endif /* WESTON_STRING_HELPERS_H */

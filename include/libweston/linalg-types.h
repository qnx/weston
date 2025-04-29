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

#pragma once

#ifdef  __cplusplus
extern "C" {
#endif

/** Column 3-vector */
struct weston_vec3f {
	union {
		float el[3];
		struct {
			float x, y, z;
		};
		struct {
			float r, g, b;
		};
		struct {
			float Y, Cb, Cr;
		};
	};
};

/** 3x3 matrix, column-major */
struct weston_mat3f {
	union {
		struct weston_vec3f col[3];
		float colmaj[3 * 3];
	};
};

/** Column 4-vector */
struct weston_vec4f {
	union {
		float el[4];
		struct {
			float x, y, z, w;
		};
	};
};

/** 4x4 matrix, column-major */
struct weston_mat4f {
	union {
		struct weston_vec4f col[4];
		float colmaj[4 * 4];
	};
};

#ifdef  __cplusplus
}
#endif

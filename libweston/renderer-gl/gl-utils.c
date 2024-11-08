/*
 * Copyright © 2024 Collabora, Ltd.
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

#include "shared/xalloc.h"
#include "gl-renderer.h"
#include "gl-renderer-internal.h"

/*
 * Table 1: List of OpenGL ES 3 sized internal colour formats allowed for
 * texture and FBO creation. Built from Table 3.13 in the OpenGL ES 3.0 and 3.1
 * specs and from Table 8.10 in the OpenGL ES 3.2 spec.
 *
 * ┌─────────────────────┬─────┬─────┬─────┬─────────────────┬──────────────────────────────────┐
 * │ Internal fmt¹       │ T²  │ F³  │ R⁴  │ External fmt⁵   │ External type(s)⁵                │
 * ╞═════════════════════╪═════╪═════╪═════╪═════════════════╪══════════════════════════════════╡
 * │ GL_R8               │ 3.0 │ 3.0 │ 3.0 │ GL_RED          │ GL_UNSIGNED_BYTE                 │
 * │ GL_SR8_EXT          │ E⁶  │     │ E⁶  │ GL_RED          │ GL_UNSIGNED_BYTE                 │
 * │ GL_R8_SNORM         │ 3.0 │ 3.0 │     │ GL_RED          │ GL_BYTE                          │
 * │ GL_R16_EXT          │ E⁶  │ E⁶  │ E⁶  │ GL_RED          │ GL_UNSIGNED_SHORT                │
 * │ GL_R16_SNORM_EXT    │ E⁶  │ E⁶  │     │ GL_RED          │ GL_SHORT                         │
 * │ GL_R16F             │ 3.0 │ 3.0 │ 3.2 │ GL_RED          │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_R32F             │ 3.0 │     │ 3.2 │ GL_RED          │ GL_FLOAT                         │
 * │ GL_R8UI             │ 3.0 │     │ 3.0 │ GL_RED_INTEGER  │ GL_UNSIGNED_BYTE                 │
 * │ GL_R8I              │ 3.0 │     │ 3.0 │ GL_RED_INTEGER  │ GL_BYTE                          │
 * │ GL_R16UI            │ 3.0 │     │ 3.0 │ GL_RED_INTEGER  │ GL_UNSIGNED_SHORT                │
 * │ GL_R16I             │ 3.0 │     │ 3.0 │ GL_RED_INTEGER  │ GL_SHORT                         │
 * │ GL_R32UI            │ 3.0 │     │ 3.0 │ GL_RED_INTEGER  │ GL_UNSIGNED_INT                  │
 * │ GL_R32I             │ 3.0 │     │ 3.0 │ GL_RED_INTEGER  │ GL_INT                           │
 * │ GL_RG8              │ 3.0 │ 3.0 │ 3.0 │ GL_RG           │ GL_UNSIGNED_BYTE                 │
 * │ GL_SRG8_EXT         │ E⁶  │     │ E⁶  │ GL_RG           │ GL_UNSIGNED_BYTE                 │
 * │ GL_RG8_SNORM        │ 3.0 │ 3.0 │     │ GL_RG           │ GL_BYTE                          │
 * │ GL_RG16_EXT         │ E⁶  │ E⁶  │ E⁶  │ GL_RG           │ GL_UNSIGNED_SHORT                │
 * │ GL_RG16_SNORM_EXT   │ E⁶  │ E⁶  │     │ GL_RG           │ GL_SHORT                         │
 * │ GL_RG16F            │ 3.0 │ 3.0 │ 3.2 │ GL_RG           │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RG32F            │ 3.0 │     │ 3.2 │ GL_RG           │ GL_FLOAT                         │
 * │ GL_RG8UI            │ 3.0 │     │ 3.0 │ GL_RG_INTEGER   │ GL_UNSIGNED_BYTE                 │
 * │ GL_RG8I             │ 3.0 │     │ 3.0 │ GL_RG_INTEGER   │ GL_BYTE                          │
 * │ GL_RG16UI           │ 3.0 │     │ 3.0 │ GL_RG_INTEGER   │ GL_UNSIGNED_SHORT                │
 * │ GL_RG16I            │ 3.0 │     │ 3.0 │ GL_RG_INTEGER   │ GL_SHORT                         │
 * │ GL_RG32UI           │ 3.0 │     │ 3.0 │ GL_RG_INTEGER   │ GL_UNSIGNED_INT                  │
 * │ GL_RG32I            │ 3.0 │     │ 3.0 │ GL_RG_INTEGER   │ GL_INT                           │
 * │ GL_RGB8             │ 3.0 │ 3.0 │ 3.0 │ GL_RGB          │ GL_UNSIGNED_BYTE                 │
 * │ GL_SRGB8            │ 3.0 │ 3.0 │     │ GL_RGB          │ GL_UNSIGNED_BYTE                 │
 * │ GL_RGB565           │ 3.0 │ 3.0 │ 3.0 │ GL_RGB          │ GL_UNSIGNED_BYTE,                │
 * │                     │     │     │     │                 │ GL_UNSIGNED_SHORT_5_6_5          │
 * │ GL_RGB8_SNORM       │ 3.0 │ 3.0 │     │ GL_RGB          │ GL_BYTE                          │
 * │ GL_RGB16_EXT        │ E⁶  │ E⁶  │     │ GL_RGB          │ GL_UNSIGNED_SHORT                │
 * │ GL_RGB16_SNORM_EXT  │ E⁶  │ E⁶  │     │ GL_RGB          │ GL_SHORT                         │
 * │ GL_R11F_G11F_B10F   │ 3.0 │ 3.0 │ 3.2 │ GL_RGB          │ GL_UNSIGNED_INT_10F_11F_11F_REV, │
 * │                     │     │     │     │                 │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGB9_E5          │ 3.0 │ 3.0 │     │ GL_RGB          │ GL_UNSIGNED_INT_5_9_9_9_REV,     │
 * │                     │     │     │     │                 │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGB16F           │ 3.0 │ 3.0 │ E⁶  │ GL_RGB          │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGB32F           │ 3.0 │     │     │ GL_RGB          │ GL_FLOAT                         │
 * │ GL_RGB8UI           │ 3.0 │     │     │ GL_RGB_INTEGER  │ GL_UNSIGNED_BYTE                 │
 * │ GL_RGB8I            │ 3.0 │     │     │ GL_RGB_INTEGER  │ GL_BYTE                          │
 * │ GL_RGB16UI          │ 3.0 │     │     │ GL_RGB_INTEGER  │ GL_UNSIGNED_SHORT                │
 * │ GL_RGB16I           │ 3.0 │     │     │ GL_RGB_INTEGER  │ GL_SHORT                         │
 * │ GL_RGB32UI          │ 3.0 │     │     │ GL_RGB_INTEGER  │ GL_UNSIGNED_INT                  │
 * │ GL_RGB32I           │ 3.0 │     │     │ GL_RGB_INTEGER  │ GL_INT                           │
 * │ GL_RGBA8            │ 3.0 │ 3.0 │ 3.0 │ GL_RGBA         │ GL_UNSIGNED_BYTE                 │
 * │ GL_SRGB8_ALPHA8     │ 3.0 │ 3.0 │ 3.0 │ GL_RGBA         │ GL_UNSIGNED_BYTE                 │
 * │ GL_RGBA8_SNORM      │ 3.0 │ 3.0 │     │ GL_RGBA         │ GL_BYTE                          │
 * │ GL_RGB5_A1          │ 3.0 │ 3.0 │ 3.0 │ GL_RGBA         │ GL_UNSIGNED_BYTE,                │
 * │                     │     │     │     │                 │ GL_UNSIGNED_SHORT_5_5_5_1,       │
 * │                     │     │     │     │                 │ GL_UNSIGNED_INT_2_10_10_10_REV   │
 * │ GL_RGBA4            │ 3.0 │ 3.0 │ 3.0 │ GL_RGBA         │ GL_UNSIGNED_BYTE,                │
 * │                     │     │     │     │                 │ GL_UNSIGNED_SHORT_4_4_4_4        │
 * │ GL_RGB10_A2         │ 3.0 │ 3.0 │ 3.0 │ GL_RGBA         │ GL_UNSIGNED_INT_2_10_10_10_REV   │
 * │ GL_RGBA16_EXT       │ E⁶  │ E⁶  │ E⁶  │ GL_RGBA         │ GL_UNSIGNED_SHORT                │
 * │ GL_RGBA16_SNORM_EXT │ E⁶  │ E⁶  │     │ GL_RGBA         │ GL_SHORT                         │
 * │ GL_RGBA16F          │ 3.0 │ 3.0 │ 3.2 │ GL_RGBA         │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGBA32F          │ 3.0 │     │ 3.2 │ GL_RGBA         │ GL_FLOAT                         │
 * │ GL_BGRA8_EXT        │ E⁶  │ E⁶  │ E⁶  │ GL_BGRA_EXT     │ GL_UNSIGNED_BYTE                 │
 * │ GL_RGBA8UI          │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_UNSIGNED_BYTE                 │
 * │ GL_RGBA8I           │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_BYTE                          │
 * │ GL_RGB10_A2UI       │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_UNSIGNED_INT_2_10_10_10_REV   │
 * │ GL_RGBA16UI         │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_UNSIGNED_SHORT                │
 * │ GL_RGBA16I          │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_SHORT                         │
 * │ GL_RGBA32I          │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_INT                           │
 * │ GL_RGBA32UI         │ 3.0 │     │ 3.0 │ GL_RGBA_INTEGER │ GL_UNSIGNED_INT                  │
 * └─────────────────────┴─────┴─────┴─────┴─────────────────┴──────────────────────────────────┘
 *
 * ¹ Sized internal format.
 * ² Texturable since.
 * ³ Texture-filterable (GL_LINEAR support) since.
 * ⁴ Renderable (FBO support) since.
 * ⁵ External format and type combination(s).
 * ⁶ Supported via extensions.
 */

#if !defined(NDEBUG)

/* Validate an external format for a given OpenGL ES 3 sized internal colour
 * format. Based on Table 1 above.
 */
static bool
is_valid_format_es3(struct gl_renderer *gr,
		    GLenum internal_format,
		    GLenum external_format)
{
	assert(gr->gl_version >= gl_version(3, 0));

	switch (internal_format) {
	case GL_R8I:
	case GL_R8UI:
	case GL_R16I:
	case GL_R16UI:
	case GL_R32I:
	case GL_R32UI:
		return external_format == GL_RED_INTEGER;

	case GL_R8:
	case GL_R8_SNORM:
	case GL_R16F:
	case GL_R32F:
		return external_format == GL_RED;

	case GL_SR8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_SRGB_R8) &&
			external_format == GL_RED;

	case GL_R16_EXT:
	case GL_R16_SNORM_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16) &&
			external_format == GL_RED;

	case GL_RG8I:
	case GL_RG8UI:
	case GL_RG16I:
	case GL_RG16UI:
	case GL_RG32I:
	case GL_RG32UI:
		return external_format == GL_RG_INTEGER;

	case GL_RG8:
	case GL_RG8_SNORM:
	case GL_RG16F:
	case GL_RG32F:
		return external_format == GL_RG;

	case GL_SRG8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_SRGB_RG8) &&
			external_format == GL_RG;

	case GL_RG16_EXT:
	case GL_RG16_SNORM_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16) &&
			external_format == GL_RG;

	case GL_RGB8I:
	case GL_RGB8UI:
	case GL_RGB16I:
	case GL_RGB16UI:
	case GL_RGB32I:
	case GL_RGB32UI:
		return external_format == GL_RGB_INTEGER;

	case GL_RGB8:
	case GL_RGB8_SNORM:
	case GL_RGB16F:
	case GL_RGB32F:
	case GL_R11F_G11F_B10F:
	case GL_RGB9_E5:
	case GL_RGB565:
	case GL_SRGB8:
		return external_format == GL_RGB;

	case GL_RGB16_EXT:
	case GL_RGB16_SNORM_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16) &&
			external_format == GL_RGB;

	case GL_RGBA8I:
	case GL_RGBA8UI:
	case GL_RGBA16I:
	case GL_RGBA16UI:
	case GL_RGBA32I:
	case GL_RGBA32UI:
	case GL_RGB10_A2UI:
		return external_format == GL_RGBA_INTEGER;

	case GL_RGBA8:
	case GL_RGBA8_SNORM:
	case GL_RGBA16F:
	case GL_RGBA32F:
	case GL_RGB10_A2:
	case GL_SRGB8_ALPHA8:
	case GL_RGB5_A1:
	case GL_RGBA4:
		return external_format == GL_RGBA;

	case GL_RGBA16_EXT:
	case GL_RGBA16_SNORM_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16) &&
			external_format == GL_RGBA;

	/* GL_BGRA_EXT must be here even though it's not a proper sized internal
	 * format to crrectly support EXT_texture_format_BGRA8888. */
	case GL_BGRA8_EXT:
	case GL_BGRA_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888) &&
			external_format == GL_BGRA_EXT;

	default:
		return false;
	}
}

/* Validate an external type for a given OpenGL ES 3 sized internal colour
 * format. Based on Table 1 above.
 */
static bool
is_valid_type_es3(struct gl_renderer *gr,
		  GLenum internal_format,
		  GLenum type)
{
	assert(gr->gl_version >= gl_version(3, 0));

	switch (internal_format) {
	case GL_R8:
	case GL_R8UI:
	case GL_RG8:
	case GL_RG8UI:
	case GL_RGB8:
	case GL_RGB8UI:
	case GL_RGBA8:
	case GL_RGBA8UI:
	case GL_SRGB8:
	case GL_SRGB8_ALPHA8:
		return type == GL_UNSIGNED_BYTE;

	case GL_SR8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_SRGB_R8) &&
			type == GL_UNSIGNED_BYTE;

	case GL_SRG8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_SRGB_RG8) &&
			type == GL_UNSIGNED_BYTE;

	/* See comment in is_valid_format_es3(). */
	case GL_BGRA8_EXT:
	case GL_BGRA_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888) &&
			type == GL_UNSIGNED_BYTE;

	case GL_R8I:
	case GL_R8_SNORM:
	case GL_RG8I:
	case GL_RG8_SNORM:
	case GL_RGB8I:
	case GL_RGB8_SNORM:
	case GL_RGBA8I:
	case GL_RGBA8_SNORM:
		return type == GL_BYTE;

	case GL_R16UI:
	case GL_RG16UI:
	case GL_RGB16UI:
	case GL_RGBA16UI:
		return type == GL_UNSIGNED_SHORT;

	case GL_R16I:
	case GL_RG16I:
	case GL_RGB16I:
	case GL_RGBA16I:
		return type == GL_SHORT;

	case GL_R16_EXT:
	case GL_RG16_EXT:
	case GL_RGB16_EXT:
	case GL_RGBA16_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16) &&
			type == GL_UNSIGNED_SHORT;

	case GL_R16_SNORM_EXT:
	case GL_RG16_SNORM_EXT:
	case GL_RGB16_SNORM_EXT:
	case GL_RGBA16_SNORM_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16) &&
			type == GL_SHORT;

	case GL_R32UI:
	case GL_RG32UI:
	case GL_RGB32UI:
	case GL_RGBA32UI:
		return type == GL_UNSIGNED_INT;

	case GL_RGB10_A2UI:
		return type == GL_UNSIGNED_INT_2_10_10_10_REV;

	case GL_R32I:
	case GL_RG32I:
	case GL_RGB32I:
	case GL_RGBA32I:
		return type == GL_INT;

	case GL_R32F:
	case GL_RG32F:
	case GL_RGB32F:
	case GL_RGBA32F:
		return type == GL_FLOAT;

	case GL_R16F:
	case GL_RG16F:
	case GL_RGB16F:
	case GL_RGBA16F:
		return type == GL_HALF_FLOAT ||
			type == GL_FLOAT;

	case GL_RGB565:
		return type == GL_UNSIGNED_BYTE ||
			type == GL_UNSIGNED_SHORT_5_6_5;

	case GL_R11F_G11F_B10F:
		return type == GL_UNSIGNED_INT_10F_11F_11F_REV ||
			type == GL_HALF_FLOAT ||
			type == GL_FLOAT;

	case GL_RGB9_E5:
		return type == GL_UNSIGNED_INT_5_9_9_9_REV ||
			type == GL_HALF_FLOAT ||
			type == GL_FLOAT;

	case GL_RGB5_A1:
		return type == GL_UNSIGNED_BYTE ||
			type == GL_UNSIGNED_SHORT_5_5_5_1 ||
			type == GL_UNSIGNED_INT_2_10_10_10_REV;

	case GL_RGBA4:
		return type == GL_UNSIGNED_BYTE ||
			type == GL_UNSIGNED_SHORT_4_4_4_4;

	case GL_RGB10_A2:
		return type == GL_UNSIGNED_INT_2_10_10_10_REV;

	default:
		return false;
	}
}

/* Validate an external format and type combination for OpenGL ES 3.
 */
static bool
is_valid_combination_es3(struct gl_renderer *gr,
			 GLenum external_format,
			 GLenum type)
{
	assert(gr->gl_version >= gl_version(3, 0));

	switch (external_format) {
	case GL_RED:
	case GL_RG:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_BYTE:
		case GL_HALF_FLOAT:
		case GL_FLOAT:
			return true;

		case GL_UNSIGNED_SHORT:
		case GL_SHORT:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16);

		default:
			return false;
		}

	case GL_RED_INTEGER:
	case GL_RG_INTEGER:
	case GL_RGB_INTEGER:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_BYTE:
		case GL_UNSIGNED_SHORT:
		case GL_SHORT:
		case GL_UNSIGNED_INT:
		case GL_INT:
			return true;

		default:
			return false;
		}

	case GL_RGB:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_BYTE:
		case GL_UNSIGNED_SHORT_5_6_5:
		case GL_UNSIGNED_INT_10F_11F_11F_REV:
		case GL_UNSIGNED_INT_5_9_9_9_REV:
		case GL_HALF_FLOAT:
		case GL_FLOAT:
			return true;

		case GL_UNSIGNED_SHORT:
		case GL_SHORT:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16);

		default:
			return false;
		}

	case GL_RGBA:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_BYTE:
		case GL_UNSIGNED_SHORT_4_4_4_4:
		case GL_UNSIGNED_SHORT_5_5_5_1:
		case GL_UNSIGNED_INT_2_10_10_10_REV:
		case GL_HALF_FLOAT:
		case GL_FLOAT:
			return true;

		case GL_UNSIGNED_SHORT:
		case GL_SHORT:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16);

		default:
			return false;
		}

	case GL_RGBA_INTEGER:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_BYTE:
		case GL_UNSIGNED_SHORT:
		case GL_SHORT:
		case GL_UNSIGNED_INT:
		case GL_INT:
		case GL_UNSIGNED_INT_2_10_10_10_REV:
			return true;

		default:
			return false;
		}

	case GL_BGRA_EXT:
		switch (type) {
		case GL_UNSIGNED_BYTE:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888);
		default:
			return false;
		};

	default:
		return false;
	}
}

/* Validate an external format and type combination for OpenGL ES 2.
 */
static bool
is_valid_combination_es2(struct gl_renderer *gr,
			 GLenum external_format,
			 GLenum type)
{
	assert(gr->gl_version == gl_version(2, 0));

	switch (external_format) {
	case GL_ALPHA:
	case GL_LUMINANCE:
	case GL_LUMINANCE_ALPHA:
		switch (type) {
		case GL_UNSIGNED_BYTE:
			return true;

		case GL_HALF_FLOAT_OES:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_HALF_FLOAT);

		case GL_FLOAT:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_FLOAT);

		default:
			return false;
		}

	case GL_RED:
	case GL_RG:
		switch (type) {
		case GL_UNSIGNED_BYTE:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_RG);

		case GL_HALF_FLOAT_OES:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_HALF_FLOAT) &&
				gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_RG);

		case GL_FLOAT:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_FLOAT) &&
				gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_RG);

		default:
			return false;
		}

	case GL_RGB:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_UNSIGNED_SHORT_5_6_5:
			return true;

		case GL_UNSIGNED_INT_10F_11F_11F_REV:
			return gl_extensions_has(gr, EXTENSION_NV_PACKED_FLOAT) ||
				gl_extensions_has(gr, EXTENSION_APPLE_TEXTURE_PACKED_FLOAT);

		case GL_UNSIGNED_INT_5_9_9_9_REV:
			return gl_extensions_has(gr, EXTENSION_APPLE_TEXTURE_PACKED_FLOAT);

		case GL_HALF_FLOAT_OES:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_HALF_FLOAT);

		case GL_FLOAT:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_FLOAT);

		default:
			return false;
		}

	case GL_RGBA:
		switch (type) {
		case GL_UNSIGNED_BYTE:
		case GL_UNSIGNED_SHORT_4_4_4_4:
		case GL_UNSIGNED_SHORT_5_5_5_1:
			return true;

		case GL_UNSIGNED_INT_2_10_10_10_REV:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_TYPE_2_10_10_10_REV);

		case GL_HALF_FLOAT_OES:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_HALF_FLOAT);

		case GL_FLOAT:
			return gl_extensions_has(gr, EXTENSION_OES_TEXTURE_FLOAT);

		default:
			return false;
		}

	case GL_BGRA_EXT:
		switch (type) {
		case GL_UNSIGNED_BYTE:
			return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888);
		default:
			return false;
		};

	default:
		return false;
	}
}

/* Validate texture parameters.
 */
static bool
are_valid_texture_parameters(struct gl_renderer *gr,
			     struct gl_texture_parameters *parameters)
{
	GLint tex = 0;
	int i;

	if (parameters->target == GL_TEXTURE_2D)
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
	else if (parameters->target == GL_TEXTURE_3D)
		glGetIntegerv(GL_TEXTURE_BINDING_3D, &tex);
	else if (parameters->target == GL_TEXTURE_EXTERNAL_OES)
		glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &tex);
	if (tex == 0)
		return false;

	/* Filters. */
	for (i = 0; i < 2; i++) {
		switch (parameters->filters.array[i]) {
		case GL_NEAREST:
		case GL_LINEAR:
			break;

		case GL_NEAREST_MIPMAP_NEAREST:
		case GL_NEAREST_MIPMAP_LINEAR:
		case GL_LINEAR_MIPMAP_NEAREST:
		case GL_LINEAR_MIPMAP_LINEAR:
			/* Minification filter only. */
			if (parameters->target == GL_TEXTURE_EXTERNAL_OES ||
			    &parameters->filters.array[i] == &parameters->filters.mag)
				return false;
			break;

		default:
			return false;
		};
	}

	/* Wrap modes. OpenGL ES 3.2 (and extensions) has GL_CLAMP_TO_BORDER but
	 * Weston doesn't need it. */
	for (i = 0; i < 3; i++) {
		switch (parameters->wrap_modes.array[i]) {
		case GL_CLAMP_TO_EDGE:
		case GL_REPEAT:
		case GL_MIRRORED_REPEAT:
			break;

		default:
			return false;
		};
	}

	/* Swizzles. */
	if (parameters->target != GL_TEXTURE_EXTERNAL_OES) {
		for (i = 0; i < 4; i++) {
			switch (parameters->swizzles.array[i]) {
			case GL_RED:
			case GL_GREEN:
			case GL_BLUE:
			case GL_ALPHA:
			case GL_ZERO:
			case GL_ONE:
				break;

			default:
				return false;
			};
		}
	}

	return true;
}

#endif /* !defined(NDEBUG) */

/* Get the supported BGRA8 texture creation method. This is needed to correctly
 * handle the behaviour of different drivers. This function should only be used
 * at renderer setup once the extensions have been initialised.
 */
enum gl_bgra8_texture_support
gl_get_bgra8_texture_support(struct gl_renderer *gr)
{
	enum gl_bgra8_texture_support support;
	GLuint tex;

	if (!gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888))
		return BGRA8_TEXTURE_SUPPORT_NONE;

	/* Empty error queue. */
	while (glGetError() != GL_NO_ERROR);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	if (gl_features_has(gr, FEATURE_TEXTURE_IMMUTABILITY)) {
		gr->tex_storage_2d(GL_TEXTURE_2D, 1, GL_BGRA8_EXT, 16, 16);
		if (glGetError() == GL_NO_ERROR) {
			support = BGRA8_TEXTURE_SUPPORT_STORAGE;
			goto done;
		}
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA8_EXT, 16, 16, 0, GL_BGRA_EXT,
		     GL_UNSIGNED_BYTE, NULL);
	if (glGetError() == GL_NO_ERROR) {
		support = BGRA8_TEXTURE_SUPPORT_IMAGE_REVISED;
		goto done;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, 16, 16, 0, GL_BGRA_EXT,
		     GL_UNSIGNED_BYTE, NULL);
	if (glGetError() == GL_NO_ERROR) {
		support = BGRA8_TEXTURE_SUPPORT_IMAGE_ORIGINAL;
		goto done;
	}

	support = BGRA8_TEXTURE_SUPPORT_NONE;

 done:
	glDeleteTextures(1, &tex);

	return support;
}

/* Check whether the sized BGRA8 renderbuffer feature is available. This
 * function should only be used at renderer setup once the extensions have been
 * initialised.
 */
bool
gl_has_sized_bgra8_renderbuffer(struct gl_renderer *gr)
{
	bool available = false;
	GLuint rb;

	if (!gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888))
		return false;

	/* Empty error queue. */
	while (glGetError() != GL_NO_ERROR);

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_BGRA8_EXT, 16, 16);

	if (glGetError() == GL_NO_ERROR)
		available = true;

	glDeleteRenderbuffers(1, &rb);

	return available;
}

/* Check whether gl_texture_2d_init() supports texture creation for a given
 * coloured sized internal format or not.
 */
bool
gl_texture_is_format_supported(struct gl_renderer *gr,
			       GLenum format)
{
	switch (format) {
	case GL_R8:
	case GL_RG8:
	case GL_RGB8:
	case GL_RGB565:
	case GL_RGBA8:
	case GL_RGB5_A1:
	case GL_RGBA4:
		return true;

	case GL_SR8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_SRGB_R8);

	case GL_SRG8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_SRGB_RG8);

	case GL_BGRA8_EXT:
		return gr->bgra8_texture_support != BGRA8_TEXTURE_SUPPORT_NONE;

	case GL_R16F:
	case GL_RG16F:
	case GL_RGB16F:
	case GL_RGBA16F:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_OES_TEXTURE_HALF_FLOAT);

	case GL_R32F:
	case GL_RG32F:
	case GL_RGB32F:
	case GL_RGBA32F:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_OES_TEXTURE_FLOAT);

	case GL_R11F_G11F_B10F:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_NV_PACKED_FLOAT) ||
			gl_extensions_has(gr, EXTENSION_APPLE_TEXTURE_PACKED_FLOAT);

	case GL_RGB9_E5:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_APPLE_TEXTURE_PACKED_FLOAT);

	case GL_R16_EXT:
	case GL_RG16_EXT:
	case GL_RGB16_EXT:
	case GL_RGBA16_EXT:
	case GL_R16_SNORM_EXT:
	case GL_RG16_SNORM_EXT:
	case GL_RGB16_SNORM_EXT:
	case GL_RGBA16_SNORM_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16);

	case GL_RGB10_A2:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_TYPE_2_10_10_10_REV);

	case GL_R8I:
	case GL_R8UI:
	case GL_R8_SNORM:
	case GL_R16I:
	case GL_R16UI:
	case GL_R32I:
	case GL_R32UI:
	case GL_RG8I:
	case GL_RG8UI:
	case GL_RG8_SNORM:
	case GL_RG16I:
	case GL_RG16UI:
	case GL_RG32I:
	case GL_RG32UI:
	case GL_RGB8I:
	case GL_RGB8UI:
	case GL_RGB8_SNORM:
	case GL_RGB16I:
	case GL_RGB16UI:
	case GL_RGB32I:
	case GL_RGB32UI:
	case GL_SRGB8:
	case GL_RGBA8I:
	case GL_RGBA8UI:
	case GL_RGBA8_SNORM:
	case GL_RGBA16I:
	case GL_RGBA16UI:
	case GL_RGBA32I:
	case GL_RGBA32UI:
	case GL_RGB10_A2UI:
	case GL_SRGB8_ALPHA8:
		return gr->gl_version >= gl_version(3, 0);

	default:
		unreachable("Unsupported sized internal format!");
		return false;
	}
}

static void
texture_init(struct gl_renderer *gr,
	     GLenum target,
	     int levels,
	     GLenum format,
	     int width,
	     int height,
	     int depth,
	     GLuint *tex_out)
{
	bool bgra_fallback;
	GLuint tex;

	assert(width > 0);
	assert(height > 0);
	assert(levels <= ((int) log2(MAX(width, height)) + 1));
	assert(target == GL_TEXTURE_2D ||
	       target == GL_TEXTURE_3D);

	glGenTextures(1, &tex);
	glBindTexture(target, tex);

	/* Fallback to TexImage*D() when GL_BGRA8_EXT isn't supported by
	 * TexStorage*D(). */
	bgra_fallback = format == GL_BGRA8_EXT &&
		gr->bgra8_texture_support != BGRA8_TEXTURE_SUPPORT_STORAGE;

	if (gl_features_has(gr, FEATURE_TEXTURE_IMMUTABILITY) &&
	    !bgra_fallback) {
		if (!gl_features_has(gr, FEATURE_TEXTURE_RG)) {
			switch (format) {
			case GL_R8:
				format = GL_LUMINANCE8_EXT;
				break;

			case GL_R16F:
				format = GL_LUMINANCE16F_EXT;
				break;

			case GL_R32F:
				format = GL_LUMINANCE32F_EXT;
				break;

			case GL_RG8:
				format = GL_LUMINANCE8_ALPHA8_EXT;
				break;

			case GL_RG16F:
				format = GL_LUMINANCE_ALPHA16F_EXT;
				break;

			case GL_RG32F:
				format = GL_LUMINANCE_ALPHA32F_EXT;
				break;

			default:
				break;
			}
		}

		if (target == GL_TEXTURE_2D)
			gr->tex_storage_2d(GL_TEXTURE_2D, levels, format, width,
					   height);
		else
			gr->tex_storage_3d(GL_TEXTURE_3D, levels, format, width,
					   height, depth);
	} else {
		GLenum external_format, type;
		int i;

		/* Implicit conversion to external format for supported
		 * subset. */
		switch (format) {
		case GL_R8:
			if (gl_features_has(gr, FEATURE_TEXTURE_RG)) {
				format = external_format = GL_RED;
			} else if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT)) {
				format = external_format = GL_LUMINANCE;
			} else {
				format = GL_LUMINANCE8_OES;
				external_format = GL_LUMINANCE;
			}
			type = GL_UNSIGNED_BYTE;
			break;

		case GL_R16F:
			format = external_format = gl_features_has(gr, FEATURE_TEXTURE_RG) ?
				GL_RED : GL_LUMINANCE;
			type = GL_HALF_FLOAT_OES;
			break;

		case GL_R32F:
			format = external_format = gl_features_has(gr, FEATURE_TEXTURE_RG) ?
				GL_RED : GL_LUMINANCE;
			type = GL_FLOAT;
			break;

		case GL_RG8:
			if (gl_features_has(gr, FEATURE_TEXTURE_RG)) {
				format = external_format = GL_RG;
			} else if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT)) {
				format = external_format = GL_LUMINANCE_ALPHA;
			} else {
				format = GL_LUMINANCE8_ALPHA8_OES;
				external_format = GL_LUMINANCE_ALPHA;
			}
			type = GL_UNSIGNED_BYTE;
			break;

		case GL_RG16F:
			format = external_format = gl_features_has(gr, FEATURE_TEXTURE_RG) ?
				GL_RG : GL_LUMINANCE_ALPHA;
			type = GL_HALF_FLOAT_OES;
			break;

		case GL_RG32F:
			format = external_format = gl_features_has(gr, FEATURE_TEXTURE_RG) ?
				GL_RG : GL_LUMINANCE_ALPHA;
			type = GL_FLOAT;
			break;

		case GL_RGB8:
			if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT))
				format = GL_RGB;
			external_format = GL_RGB;
			type = GL_UNSIGNED_BYTE;
			break;

		case GL_RGB565:
			if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT))
				format = GL_RGB;
			external_format = GL_RGB;
			type = GL_UNSIGNED_SHORT_5_6_5;
			break;

		case GL_RGB16F:
			format = external_format = GL_RGB;
			type = GL_HALF_FLOAT_OES;
			break;

		case GL_RGB32F:
			format = external_format = GL_RGB;
			type = GL_FLOAT;
			break;

		case GL_R11F_G11F_B10F:
			format = external_format = GL_RGB;
			type = GL_UNSIGNED_INT_10F_11F_11F_REV;
			break;

		case GL_RGB9_E5:
			format = external_format = GL_RGB;
			type = GL_UNSIGNED_INT_5_9_9_9_REV;
			break;

		case GL_RGBA8:
			if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT))
				format = GL_RGBA;
			external_format = GL_RGBA;
			type = GL_UNSIGNED_BYTE;
			break;

		case GL_BGRA8_EXT:
			if (gr->bgra8_texture_support == BGRA8_TEXTURE_SUPPORT_IMAGE_ORIGINAL)
				format = GL_BGRA_EXT;
			external_format = GL_BGRA_EXT;
			type = GL_UNSIGNED_BYTE;
			break;

		case GL_RGBA4:
			if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT))
				format = GL_RGBA;
			external_format = GL_RGBA;
			type = GL_UNSIGNED_SHORT_4_4_4_4;
			break;

		case GL_RGB5_A1:
			if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT))
				format = GL_RGBA;
			external_format = GL_RGBA;
			type = GL_UNSIGNED_SHORT_5_5_5_1;
			break;

		case GL_RGB10_A2:
			if (!gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT))
				format = GL_RGBA;
			external_format = GL_RGBA;
			type = GL_UNSIGNED_INT_2_10_10_10_REV;
			break;

		case GL_RGBA16F:
			format = external_format = GL_RGBA;
			type = GL_HALF_FLOAT_OES;
			break;

		case GL_RGBA32F:
			format = external_format = GL_RGBA;
			type = GL_FLOAT;
			break;

		default:
			unreachable("Missing conversion to external format!");
			return;
		}

		/* Allocate storage. */
		if (target == GL_TEXTURE_2D) {
			for (i = 0; i < levels; i++) {
				glTexImage2D(GL_TEXTURE_2D, i, format, width,
					     height, 0, external_format, type,
					     NULL);
				width = MAX(width / 2, 1);
				height = MAX(height / 2, 1);
			}
		} else {
			for (i = 0; i < levels; i++) {
				gr->tex_image_3d(GL_TEXTURE_3D, i, format,
						 width, height, depth, 0,
						 external_format, type, NULL);
				width = MAX(width / 2, 1);
				height = MAX(height / 2, 1);
				depth = MAX(depth / 2, 1);
			}
		}
	}

	*tex_out = tex;
}

/* Initialise a 2D texture object. 'format' is a coloured sized internal format
 * listed in Table 1 above with the Texturable column filled. The texture object
 * is left bound on the 2D texture target of the current texture unit on
 * success. No texture parameters are set. Use gl_texture_fini() to finalise.
 *
 * OpenGL ES 2 notes:
 *
 * Implementations support at least this subset of formats: GL_R8, GL_RG8,
 * GL_RGB8, GL_RGB565, GL_RGBA8, GL_RGBA4 and GL_RGB5_A1. Additional formats are
 * supported depending on extensions: GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F,
 * GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F, GL_R11F_G11F_B10F, GL_RGB9_E5,
 * GL_RGB10_A2 and GL_BGRA8_EXT.
 *
 * This is implemented by implicitly converting 'format' into an external
 * format. If the red and red-green texture formats aren't supported
 * (FEATURE_TEXTURE_RG flag not set), GL_R8 is converted into a luminance format
 * and GL_RG8 into a luminance alpha format. Care must be taken in the latter
 * case in order to access the green component in the shader: "c.a" (or "c[3]")
 * must be used instead of "c.g" (or "c[1]").
 *
 * See gl_texture_is_format_supported().
 */
bool
gl_texture_2d_init(struct gl_renderer *gr,
		   int levels,
		   GLenum format,
		   int width,
		   int height,
		   GLuint *tex_out)
{
	if (!gl_texture_is_format_supported(gr, format)) {
		weston_log("Error: texture format not supported.\n");
		return false;
	}

	texture_init(gr, GL_TEXTURE_2D, levels, format, width, height, 1,
		     tex_out);

	return true;
}

/* Initialise a 3D texture object. The texture object is left bound on the 3D
 * texture target of the current texture unit on success. The accepted formats
 * and OpenGL ES 2 notes are exactly the same as for the 2D init function.
 *
 * See gl_texture_2d_init().
 */
bool
gl_texture_3d_init(struct gl_renderer *gr,
		   int levels,
		   GLenum format,
		   int width,
		   int height,
		   int depth,
		   GLuint *tex_out)
{
	if (!gl_features_has(gr, FEATURE_TEXTURE_3D)) {
		weston_log("Error: texture 3D not supported.\n");
		return false;
	}

	if (!gl_texture_is_format_supported(gr, format)) {
		weston_log("Error: texture format not supported.\n");
		return false;
	}

	texture_init(gr, GL_TEXTURE_3D, levels, format, width, height, depth,
		     tex_out);

	return true;
}

static void
texture_store(struct gl_renderer *gr,
	      GLenum target,
	      int level,
	      int x,
	      int y,
	      int z,
	      int width,
	      int height,
	      int depth,
	      GLenum format,
	      GLenum type,
	      const void *data)
{
#if !defined(NDEBUG)
	GLint tex, tex_width, tex_height, tex_depth, tex_internal_format;
#endif

	if (!gl_features_has(gr, FEATURE_TEXTURE_RG)) {
		if (format == GL_RED)
			format = GL_LUMINANCE;
		else if (format == GL_RG)
			format = GL_LUMINANCE_ALPHA;
	}

	if (type == GL_HALF_FLOAT && gr->gl_version == gl_version(2, 0))
		type = GL_HALF_FLOAT_OES;

#if !defined(NDEBUG)
	assert(target == GL_TEXTURE_2D ||
	       target == GL_TEXTURE_3D);
	glGetIntegerv(target == GL_TEXTURE_2D ?
		      GL_TEXTURE_BINDING_2D :
		      GL_TEXTURE_BINDING_3D, &tex);
	assert(tex != 0);

	if (gr->gl_version == gl_version(2, 0)) {
		assert(is_valid_combination_es2(gr, format, type));
	} else if (gr->gl_version == gl_version(3, 0)) {
		assert(is_valid_combination_es3(gr, format, type));
	} else if (gr->gl_version >= gl_version(3, 1)) {
		glGetTexLevelParameteriv(target, level,
					 GL_TEXTURE_WIDTH, &tex_width);
		glGetTexLevelParameteriv(target, level,
					 GL_TEXTURE_HEIGHT, &tex_height);
		if (target == GL_TEXTURE_3D)
			glGetTexLevelParameteriv(target, level,
						 GL_TEXTURE_DEPTH, &tex_depth);
		glGetTexLevelParameteriv(target, level,
					 GL_TEXTURE_INTERNAL_FORMAT,
					 &tex_internal_format);
		assert(level >= 0);
		assert(x >= 0);
		assert(y >= 0);
		assert(z >= 0);
		assert(x + width <= tex_width);
		assert(y + height <= tex_height);
		if (target == GL_TEXTURE_3D)
			assert(z + depth <= tex_depth);
		assert(is_valid_format_es3(gr, tex_internal_format, format));
		assert(is_valid_type_es3(gr, tex_internal_format, type));
	}
#endif

	if (target == GL_TEXTURE_3D)
		gr->tex_sub_image_3d(target, level, x, y, z, width, height,
				     depth, format, type, data);
	else
		glTexSubImage2D(target, level, x, y, width, height, format,
				type, data);
}

/* Store data into the texture object bound to the 2D texture target of the
 * current texture unit. 'format' and 'type' must be a valid external format and
 * type combination for the internal format of the texture object as listed in
 * Table 1 above. The texture object is left bound. No texture parameters are
 * set.
 *
 * OpenGL ES 2 notes:
 *
 * Table 2: List of invalid external format and type combinations from Table 1
 * for the supported subset of formats.
 *
 * ┌───────────────────────┬─────────────────┬────────────────────────────────┐
 * │ Sized internal format │ External format │ Type(s)                        │
 * ╞═══════════════════════╪═════════════════╪════════════════════════════════╡
 * │ GL_RGB565             │ GL_RGB          │ GL_UNSIGNED_BYTE               │
 * │ GL_R11F_G11F_B10F     │ GL_RGB          │ GL_HALF_FLOAT,                 │
 * │                       │                 │ GL_FLOAT                       │
 * │ GL_RGB9_E5            │ GL_RGB          │ GL_HALF_FLOAT,                 │
 * │                       │                 │ GL_FLOAT                       │
 * │ GL_RGBA4              │ GL_RGBA         │ GL_UNSIGNED_BYTE               │
 * │ GL_RGB5_A1            │ GL_RGBA         │ GL_UNSIGNED_BYTE,              │
 * │                       │                 │ GL_UNSIGNED_INT_2_10_10_10_REV │
 * └───────────────────────┴─────────────────┴────────────────────────────────┘
 *
 * See gl_texture_2d_init().
 */
void
gl_texture_2d_store(struct gl_renderer *gr,
		    int level,
		    int x,
		    int y,
		    int width,
		    int height,
		    GLenum format,
		    GLenum type,
		    const void *data)
{
	texture_store(gr, GL_TEXTURE_2D, level, x, y, 0, width, height, 1,
		      format, type, data);
}

/* Store data into the texture object bound to the 3D texture target of the
 * current texture unit. The texture object is left bound. No texture parameters
 * are set. The accepted external format and type combination and the OpenGL ES
 * 2 notes are exactly the same as for the 2D store function.
 *
 * See gl_texture_store_2d() and gl_texture_3d_init().
 */
bool
gl_texture_3d_store(struct gl_renderer *gr,
		    int level,
		    int x,
		    int y,
		    int z,
		    int width,
		    int height,
		    int depth,
		    GLenum format,
		    GLenum type,
		    const void *data)
{
	if (!gl_features_has(gr, FEATURE_TEXTURE_3D)) {
		weston_log("Error: texture 3D not supported.\n");
		return false;
	}

	texture_store(gr, GL_TEXTURE_3D, level, x, y, z, width, height, depth,
		      format, type, data);

	return true;
}

/* Finalise a texture object.
 */
void
gl_texture_fini(GLuint *tex)
{
	glDeleteTextures(1, tex);
	*tex = 0;
}

/* Initialise texture parameters. 'target' is either a 2D, a 3D or an external
 * texture target. 'filters' points to an array of 2 values for respectively the
 * texture minification and magnification filters. 'wrap_modes' points to an
 * array of 3 values for the S, T and R texture wrap modes. 'swizzles' points to
 * an array of 4 values for the R, G, B and A texture swizzles. The texture
 * object bound to the given texture target (of the active texture) is updated
 * if 'flush' is true, make sure it's properly bound in that case. The
 * parameters and the flags bitfield can then directly be set and flushed when
 * needed.
 *
 * filters are set to GL_NEAREST if 'filters' is NULL, wrap modes are set to
 * GL_CLAMP_TO_EDGE if 'wrap_modes' is NULL and swizzles are set to their
 * default components if 'swizzles' is NULL.
 *
 * See gl_texture_parameters_flush().
 */
void
gl_texture_parameters_init(struct gl_renderer *gr,
			   struct gl_texture_parameters *parameters,
			   GLenum target,
			   const GLint *filters,
			   const GLint *wrap_modes,
			   const GLint *swizzles,
			   bool flush)
{
	GLint default_filters[] = { GL_NEAREST, GL_NEAREST };
	GLint default_wrap_modes[] = { GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
				       GL_CLAMP_TO_EDGE };
	GLint default_swizzles[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };

	assert(target == GL_TEXTURE_2D ||
	       target == GL_TEXTURE_3D ||
	       target == GL_TEXTURE_EXTERNAL_OES);
	assert(target != GL_TEXTURE_3D ||
	       gl_features_has(gr, FEATURE_TEXTURE_3D));
	assert(target != GL_TEXTURE_EXTERNAL_OES ||
	       gl_extensions_has(gr, EXTENSION_OES_EGL_IMAGE_EXTERNAL));

	parameters->target = target;
	memcpy(&parameters->filters, filters ? filters : default_filters,
	       sizeof default_filters);
	memcpy(&parameters->wrap_modes, wrap_modes ? wrap_modes :
	       default_wrap_modes, sizeof default_wrap_modes);
	memcpy(&parameters->swizzles, swizzles ? swizzles : default_swizzles,
	       sizeof default_swizzles);
	parameters->flags = TEXTURE_ALL_DIRTY;

	if (flush)
		gl_texture_parameters_flush(gr, parameters);
}

/* Flush texture parameters to the texture object currently bound to the texture
 * target (of the active texture) set at initialisation.
 *
 * See gl_texture_parameters_init().
 */
void
gl_texture_parameters_flush(struct gl_renderer *gr,
			    struct gl_texture_parameters *parameters)
{
	assert(are_valid_texture_parameters(gr, parameters));

	if (parameters->flags & TEXTURE_FILTERS_DIRTY) {
		glTexParameteri(parameters->target, GL_TEXTURE_MIN_FILTER,
				parameters->filters.min);
		glTexParameteri(parameters->target, GL_TEXTURE_MAG_FILTER,
				parameters->filters.mag);
	}

	if (parameters->flags & TEXTURE_WRAP_MODES_DIRTY) {
		glTexParameteri(parameters->target, GL_TEXTURE_WRAP_S,
				parameters->wrap_modes.s);
		glTexParameteri(parameters->target, GL_TEXTURE_WRAP_T,
				parameters->wrap_modes.t);
		if (parameters->target == GL_TEXTURE_3D)
			glTexParameteri(parameters->target, GL_TEXTURE_WRAP_R,
					parameters->wrap_modes.r);
	}

	if (parameters->flags & TEXTURE_SWIZZLES_DIRTY &&
	    parameters->target != GL_TEXTURE_EXTERNAL_OES &&
	    gr->gl_version >= gl_version(3, 0)) {
		glTexParameteri(parameters->target, GL_TEXTURE_SWIZZLE_R,
				parameters->swizzles.r);
		glTexParameteri(parameters->target, GL_TEXTURE_SWIZZLE_G,
				parameters->swizzles.g);
		glTexParameteri(parameters->target, GL_TEXTURE_SWIZZLE_B,
				parameters->swizzles.b);
		glTexParameteri(parameters->target, GL_TEXTURE_SWIZZLE_A,
				parameters->swizzles.a);
	}

	parameters->flags = 0;
}

/* Check whether gl_fbo_init() supports FBO creation for a given
 * colour-renderable sized internal 'format' or not.
 */
bool
gl_fbo_is_format_supported(struct gl_renderer *gr,
			   GLenum format)
{
	switch (format) {
	case GL_RGBA4:
	case GL_RGB5_A1:
	case GL_RGB565:
		return true; /* From OpenGL ES 2.0 (Table 4.5 in spec). */

	case GL_R8:
	case GL_RG8:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_RG);

	case GL_SR8_EXT:
	case GL_SRG8_EXT:
		return gl_extensions_has(gr, EXTENSION_QCOM_RENDER_SRGB_R8_RG8);

	case GL_RGB8:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_OES_RGB8_RGBA8);

	case GL_RGBA8:
		return gr->gl_version >= gl_version(3, 0) ||
			gl_extensions_has(gr, EXTENSION_ARM_RGBA8) ||
			gl_extensions_has(gr, EXTENSION_OES_RGB8_RGBA8) ||
			gl_extensions_has(gr, EXTENSION_OES_REQUIRED_INTERNALFORMAT);

	case GL_BGRA8_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888);

	case GL_SRGB8_ALPHA8:
	case GL_R8I:
	case GL_R8UI:
	case GL_R16I:
	case GL_R16UI:
	case GL_R32I:
	case GL_R32UI:
	case GL_RG8I:
	case GL_RG8UI:
	case GL_RG16I:
	case GL_RG16UI:
	case GL_RG32I:
	case GL_RG32UI:
	case GL_RGBA8I:
	case GL_RGBA8UI:
	case GL_RGBA16I:
	case GL_RGBA16UI:
	case GL_RGBA32I:
	case GL_RGBA32UI:
	case GL_RGB10_A2:
	case GL_RGB10_A2UI:
		return gr->gl_version >= gl_version(3, 0);

	case GL_R16F:
	case GL_RG16F:
	case GL_RGBA16F:
		return gr->gl_version >= gl_version(3, 2) ||
			gl_extensions_has(gr, EXTENSION_EXT_COLOR_BUFFER_FLOAT) ||
			gl_extensions_has(gr, EXTENSION_EXT_COLOR_BUFFER_HALF_FLOAT);

	case GL_RGB16F:
		return gl_extensions_has(gr, EXTENSION_EXT_COLOR_BUFFER_HALF_FLOAT);

	case GL_R32F:
	case GL_RG32F:
	case GL_RGBA32F:
		return gr->gl_version >= gl_version(3, 2) ||
			gl_extensions_has(gr, EXTENSION_EXT_COLOR_BUFFER_FLOAT);

	case GL_R11F_G11F_B10F:
		return gr->gl_version >= gl_version(3, 2) ||
			gl_extensions_has(gr, EXTENSION_EXT_COLOR_BUFFER_FLOAT) ||
			(gl_extensions_has(gr, EXTENSION_NV_PACKED_FLOAT) &&
			 gl_extensions_has(gr, EXTENSION_EXT_COLOR_BUFFER_HALF_FLOAT));

	case GL_R16_EXT:
	case GL_RG16_EXT:
	case GL_RGBA16_EXT:
		return gl_extensions_has(gr, EXTENSION_EXT_TEXTURE_NORM16);

	case GL_R8_SNORM:
	case GL_R16_SNORM_EXT:
	case GL_RG8_SNORM:
	case GL_RG16_SNORM_EXT:
	case GL_SRGB8:
	case GL_RGB9_E5:
	case GL_RGB32F:
	case GL_RGB8_SNORM:
	case GL_RGB16_EXT:
	case GL_RGB16_SNORM_EXT:
	case GL_RGB8I:
	case GL_RGB8UI:
	case GL_RGB16I:
	case GL_RGB16UI:
	case GL_RGB32I:
	case GL_RGB32UI:
	case GL_RGBA8_SNORM:
	case GL_RGBA16_SNORM_EXT:
		return false;

	default:
		unreachable("Unsupported sized internal format!");
		return false;
	}
}

/* Initialise a pair of framebuffer and renderbuffer objects. 'format' is a
 * colour-renderable sized internal format listed in Table 1 above with the
 * Renderable column filled. The framebuffer object is left bound on success.
 * Use gl_fbo_fini() to finalise.
 *
 * OpenGL ES 2 notes:
 *
 * Implementations support at least these formats: GL_RGBA4, GL_RGB5_A1 and
 * GL_RGB565. Additional formats are supported depending on extensions: GL_R8,
 * GL_RG8, GL_RGB8, GL_RGBA8, GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F,
 * GL_R11F_G11F_B10F and GL_BGRA8_EXT.
 *
 * See gl_fbo_is_format_supported().
 */
bool
gl_fbo_init(struct gl_renderer *gr,
	    GLenum format,
	    int width,
	    int height,
	    GLuint *fb_out,
	    GLuint *rb_out)
{
	GLuint fb, rb;
	GLenum status;

	if (!gl_fbo_is_format_supported(gr, format)) {
		weston_log("Error: FBO format not supported.\n");
		return false;
	}

	if (format == GL_BGRA8_EXT &&
	    !gl_features_has(gr, FEATURE_SIZED_BGRA8_RENDERBUFFER))
		format = GL_BGRA_EXT;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		weston_log("Error: FBO incomplete.\n");
		goto error;
	}

	*fb_out = fb;
	*rb_out = rb;
	return true;

 error:
	glDeleteFramebuffers(1, &fb);
	glDeleteRenderbuffers(1, &rb);
	return false;
}

/* Finalise a pair of framebuffer and renderbuffer objects.
 */
void
gl_fbo_fini(GLuint *fb,
	    GLuint *rb)
{
	glDeleteFramebuffers(1, fb);
	glDeleteRenderbuffers(1, rb);
	*fb = 0;
	*rb = 0;
}

/* Initialise a pair of framebuffer and renderbuffer objects to render into an
 * EGL image. The framebuffer object is left bound on success. Use gl_fbo_fini()
 * to finalise.
 */
bool
gl_fbo_image_init(struct gl_renderer *gr,
		  EGLImageKHR image,
		  GLuint *fb_out,
		  GLuint *rb_out)
{
	GLuint fb, rb;
	GLenum status;

	if (!gl_extensions_has(gr, EXTENSION_OES_EGL_IMAGE)) {
		weston_log("Error: FBO from EGLImage not supported.\n");
		return false;
	}

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	gr->image_target_renderbuffer_storage(GL_RENDERBUFFER, image);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		weston_log("Error: FBO incomplete.\n");
		goto error;
	}

	*fb_out = fb;
	*rb_out = rb;
	return true;

 error:
	glDeleteFramebuffers(1, &fb);
	glDeleteRenderbuffers(1, &rb);
	return false;
}

/* Initialise a pair of framebuffer and texture objects to render into a
 * texture. 'format' is a colour-renderable sized internal format listed in
 * Table 1 above with the Renderable column filled. The framebuffer object is
 * left bound on the framebuffer target and the texture object is left bound on
 * the 2D texture target of the current texture unit on success. Use
 * gl_fbo_texture_fini() to finalise.
 */
bool
gl_fbo_texture_init(struct gl_renderer *gr,
		    GLenum format,
		    int width,
		    int height,
		    GLuint *fb_out,
		    GLuint *tex_out)
{
	GLenum status;
	GLuint fb, tex;

	if (!gl_fbo_is_format_supported(gr, format)) {
		weston_log("Error: FBO format not supported.\n");
		return false;
	}

	if (format == GL_BGRA8_EXT &&
	    !gl_features_has(gr, FEATURE_SIZED_BGRA8_RENDERBUFFER))
		format = GL_BGRA_EXT;

	texture_init(gr, GL_TEXTURE_2D, 1, format, width, height, 1, &tex);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		weston_log("Error: FBO incomplete.\n");
		goto error;
	}

	*fb_out = fb;
	*tex_out = tex;
	return true;

 error:
	glDeleteFramebuffers(1, &fb);
	glDeleteTextures(1, &tex);
	return false;
}

/* Finalise a pair of framebuffer and texture objects.
 */
void
gl_fbo_texture_fini(GLuint *fb,
		    GLuint *tex)
{
	glDeleteFramebuffers(1, fb);
	glDeleteTextures(1, tex);
	*fb = 0;
	*tex = 0;
}

/* Add extension flags to the bitfield that 'flags_out' points to. 'table'
 * stores extension names and flags to check for and 'extensions' is the list
 * usually returned by the EGL or GL implementation. New flags are stored using
 * a binary OR in order to keep flags set from a previous call. Caller must
 * ensure the bitfield is set to 0 at first call.
 */
void
gl_extensions_add(const struct gl_extension_table *table,
		  const char *extensions,
		  uint64_t *flags_out)
{
	struct { const char *str; size_t len; } *map;
	size_t i = 0, n = 0;
	uint64_t flags = 0;
	char prev_char = ' ';

	/* Get number of extensions. */
	while (extensions[i]) {
		if (prev_char == ' ' && extensions[i] != ' ')
			n++;
		prev_char = extensions[i++];
	}

	if (n == 0)
		return;

	/* Allocate data structure mapping each extension with their length. */
	map = xmalloc(n * sizeof *map);
	prev_char = ' ';
	i = n = 0;
	while (prev_char) {
		if (extensions[i] != ' ' && extensions[i] != '\0') {
			if (prev_char == ' ')
				map[n].str = &extensions[i];
		} else if (prev_char != ' ') {
			map[n].len = &extensions[i] - map[n].str;
			n++;
		}
		prev_char = extensions[i++];
	}

	/* Match extensions with table. */
	for (; table->str; table++) {
		for (i = 0; i < n; i++) {
			if (table->len == map[i].len &&
			    !strncmp(table->str, map[i].str, table->len)) {
				flags |= table->flag;
				break;
			}
		}
	}

	*flags_out |= flags;
	free(map);
}

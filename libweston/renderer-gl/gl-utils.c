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
 * │ GL_R8_SNORM         │ 3.0 │ 3.0 │     │ GL_RED          │ GL_BYTE                          │
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
 * │ GL_RG8_SNORM        │ 3.0 │ 3.0 │     │ GL_RG           │ GL_BYTE                          │
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
 * │ GL_R11F_G11F_B10F   │ 3.0 │ 3.0 │ 3.2 │ GL_RGB          │ GL_UNSIGNED_INT_10F_11F_11F_REV, │
 * │                     │     │     │     │                 │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGB9_E5          │ 3.0 │ 3.0 │     │ GL_RGB          │ GL_UNSIGNED_INT_5_9_9_9_REV,     │
 * │                     │     │     │     │                 │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGB16F           │ 3.0 │ 3.0 │     │ GL_RGB          │ GL_HALF_FLOAT,                   │
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
 * │ GL_RGBA16F          │ 3.0 │ 3.0 │ 3.2 │ GL_RGBA         │ GL_HALF_FLOAT,                   │
 * │                     │     │     │     │                 │ GL_FLOAT                         │
 * │ GL_RGBA32F          │ 3.0 │     │ 3.2 │ GL_RGBA         │ GL_FLOAT                         │
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
 */

/* Initialise a pair of framebuffer and renderbuffer objects. The framebuffer
 * object is left bound on success. Use gl_fbo_fini() to finalise.
 */
bool
gl_fbo_init(GLenum internal_format,
	    int width,
	    int height,
	    GLuint *fb_out,
	    GLuint *rb_out)
{
	GLuint fb, rb;
	GLenum status;

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, internal_format, width, height);
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
 * texture. The framebuffer object is left bound on success. Use
 * gl_fbo_texture_fini() to finalise.
 */
bool
gl_fbo_texture_init(GLenum internal_format,
		    int width,
		    int height,
		    GLenum format,
		    GLenum type,
		    GLuint *fb_out,
		    GLuint *tex_out)
{
	GLenum status;
	GLuint fb, tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0,
		     format, type, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
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

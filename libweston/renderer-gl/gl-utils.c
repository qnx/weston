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

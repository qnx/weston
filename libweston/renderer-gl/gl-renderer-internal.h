/*
 * Copyright © 2019 Collabora, Ltd.
 * Copyright © 2019 Harish Krupo
 * Copyright © 2019 Intel Corporation
 * Copyright 2021 Advanced Micro Devices, Inc.
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

/*
 * GL renderer best practices:
 *
 * 1. Extensions and features
 *
 *    1. An extension flag ensures the availability of an EGL or OpenGL ES
 *       extension at run-time, independently of the version.
 *    2. A feature flag ensures the availability of a minimal OpenGL ES version
 *       and/or extensions at run-time in order to enable the use of a specific
 *       feature.
 *    3. Any function pointers declared in the gl_renderer structure must be
 *       loaded at setup so that an extension availability check can ensure
 *       valid pointers.
 *    4. OpenGL ES 3 functions must be loaded at run-time after having checked
 *       for EGL_KHR_get_all_proc_addresses extension availability in order to
 *       correctly link against OpenGL ES 2 only implementations.
 *
 * 2. Pixel storage modes
 *
 *    1. Any functions changing modes must restore them to their default values
 *       before return so that other functions can assume default values.
 *
 * 3. Texture units
 *
 *    1. Fixed allocation using the gl_tex_unit enumeration.
 *    2. Any functions changing the active unit must restore it to 0 before
 *       return so that other functions can assume a default value.
 */

#ifndef GL_RENDERER_INTERNAL_H
#define GL_RENDERER_INTERNAL_H

#include <stdbool.h>
#include <time.h>

#include <wayland-util.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl32.h>

#include "shared/weston-egl-ext.h"  /* for PFN* stuff */
#include "shared/helpers.h"

/* Max number of images per buffer. */
#define SHADER_INPUT_TEX_MAX 3

#define GET_PROC_ADDRESS(dest, proc) do { \
	dest = (void *) eglGetProcAddress(proc); \
	assert(dest); \
} while (0)

#define EXT(string, flag) { string, ARRAY_LENGTH(string) - 1, (uint64_t) flag }

/* Keep in sync with egl-glue.c. */
enum egl_client_extension_flag {
	EXTENSION_EXT_DEVICE_QUERY          = 1ull << 0,
	EXTENSION_EXT_PLATFORM_BASE         = 1ull << 1,
	EXTENSION_EXT_PLATFORM_WAYLAND      = 1ull << 2,
	EXTENSION_EXT_PLATFORM_X11          = 1ull << 3,
	EXTENSION_KHR_PLATFORM_GBM          = 1ull << 4,
	EXTENSION_KHR_PLATFORM_WAYLAND      = 1ull << 5,
	EXTENSION_KHR_PLATFORM_X11          = 1ull << 6,
	EXTENSION_MESA_PLATFORM_GBM         = 1ull << 7,
	EXTENSION_MESA_PLATFORM_SURFACELESS = 1ull << 8,
};

/* Keep in sync with egl-glue.c. */
enum egl_device_extension_flag {
	EXTENSION_EXT_DEVICE_DRM             = 1ull << 0,
	EXTENSION_EXT_DEVICE_DRM_RENDER_NODE = 1ull << 1,
};

/* Keep in sync with egl-glue.c. */
enum egl_display_extension_flag {
	EXTENSION_ANDROID_NATIVE_FENCE_SYNC          = 1ull << 0,
	EXTENSION_EXT_BUFFER_AGE                     = 1ull << 1,
	EXTENSION_EXT_IMAGE_DMA_BUF_IMPORT           = 1ull << 2,
	EXTENSION_EXT_IMAGE_DMA_BUF_IMPORT_MODIFIERS = 1ull << 3,
	EXTENSION_EXT_SWAP_BUFFERS_WITH_DAMAGE       = 1ull << 4,
	EXTENSION_IMG_CONTEXT_PRIORITY               = 1ull << 5,
	EXTENSION_KHR_FENCE_SYNC                     = 1ull << 6,
	EXTENSION_KHR_GET_ALL_PROC_ADDRESSES         = 1ull << 7,
	EXTENSION_KHR_IMAGE_BASE                     = 1ull << 8,
	EXTENSION_KHR_NO_CONFIG_CONTEXT              = 1ull << 9,
	EXTENSION_KHR_PARTIAL_UPDATE                 = 1ull << 10,
	EXTENSION_KHR_SURFACELESS_CONTEXT            = 1ull << 11,
	EXTENSION_KHR_SWAP_BUFFERS_WITH_DAMAGE       = 1ull << 12,
	EXTENSION_KHR_WAIT_SYNC                      = 1ull << 13,
	EXTENSION_MESA_CONFIGLESS_CONTEXT            = 1ull << 14,
	EXTENSION_WL_BIND_WAYLAND_DISPLAY            = 1ull << 15,
};

/* Keep in sync with gl-renderer.c. */
enum gl_extension_flag {
	EXTENSION_ANGLE_PACK_REVERSE_ROW_ORDER    = 1ull << 1,
	EXTENSION_APPLE_TEXTURE_PACKED_FLOAT      = 1ull << 3,
	EXTENSION_EXT_COLOR_BUFFER_FLOAT          = 1ull << 5,
	EXTENSION_EXT_COLOR_BUFFER_HALF_FLOAT     = 1ull << 6,
	EXTENSION_EXT_DISJOINT_TIMER_QUERY        = 1ull << 7,
	EXTENSION_EXT_MAP_BUFFER_RANGE            = 1ull << 9,
	EXTENSION_EXT_READ_FORMAT_BGRA            = 1ull << 10,
	EXTENSION_EXT_TEXTURE_FORMAT_BGRA8888     = 1ull << 11,
	EXTENSION_EXT_TEXTURE_NORM16              = 1ull << 12,
	EXTENSION_EXT_TEXTURE_RG                  = 1ull << 13,
	EXTENSION_EXT_TEXTURE_STORAGE             = 1ull << 16,
	EXTENSION_EXT_TEXTURE_TYPE_2_10_10_10_REV = 1ull << 17,
	EXTENSION_EXT_UNPACK_SUBIMAGE             = 1ull << 18,
	EXTENSION_NV_PACKED_FLOAT                 = 1ull << 20,
	EXTENSION_NV_PIXEL_BUFFER_OBJECT          = 1ull << 21,
	EXTENSION_OES_EGL_IMAGE                   = 1ull << 22,
	EXTENSION_OES_EGL_IMAGE_EXTERNAL          = 1ull << 23,
	EXTENSION_OES_MAPBUFFER                   = 1ull << 24,
	EXTENSION_OES_RGB8_RGBA8                  = 1ull << 26,
	EXTENSION_OES_TEXTURE_FLOAT               = 1ull << 28,
	EXTENSION_OES_TEXTURE_FLOAT_LINEAR        = 1ull << 29,
	EXTENSION_OES_TEXTURE_HALF_FLOAT          = 1ull << 30,
};

enum gl_feature_flag {
	/* GL renderer can create contexts without specifying an EGLConfig. */
	FEATURE_NO_CONFIG_CONTEXT = 1ull << 0,

	/* GL renderer can pass a list of damage rectangles at buffer swap in
	 * order to reduce recomposition costs. */
	FEATURE_SWAP_BUFFERS_WITH_DAMAGE = 1ull << 1,

	/* GL renderer can create native sync objects and wait on them. This
	 * enables support for the Linux explicit sync Wayland protocol. */
	FEATURE_EXPLICIT_SYNC = 1ull << 2,

	/* GL renderer can asynchronously map the framebuffer into CPU memory
	 * for reading. This is exposed by binding a Pixel Buffer Object (PBO)
	 * to the GL_PIXEL_PACK_BUFFER target before read-back with
	 * glReadPixels(). map_buffer_range() is then called to sync and map and
	 * unmap_buffer() to unmap once read. A fence sync can be used to signal
	 * pixel transfer completion, this is flagged as another feature. */
	FEATURE_ASYNC_READBACK = 1ull << 3,

	/* GL renderer can create 16-bit floating-point framebuffers and
	 * transform colours using linearly interpolated 3D look-up tables. */
	FEATURE_COLOR_TRANSFORMS = 1ull << 4,

	/* GL renderer can instrument output repaint time and report it through
	 * the timeline logging scope. */
	FEATURE_GPU_TIMELINE = 1ull << 5,

	/* GL renderer can specify the entire structure of a texture in a single
	 * call. Once specified, format and dimensions can't be changed. */
	FEATURE_TEXTURE_IMMUTABILITY = 1ull << 6,

	/* GL renderer can create two-component red-green textures. */
	FEATURE_TEXTURE_RG = 1ull << 7,
};

/* Keep the following in sync with vertex.glsl. */
enum gl_shader_texcoord_input {
	SHADER_TEXCOORD_INPUT_ATTRIB = 0,
	SHADER_TEXCOORD_INPUT_SURFACE,
};

enum gl_shader_texture_variant {
	SHADER_VARIANT_NONE = 0,
/* Keep the following in sync with fragment.glsl. */
	SHADER_VARIANT_RGBX,
	SHADER_VARIANT_RGBA,
	SHADER_VARIANT_Y_U_V,
	SHADER_VARIANT_Y_UV,
	SHADER_VARIANT_Y_XUXV,
	SHADER_VARIANT_XYUV,
	SHADER_VARIANT_SOLID,
	SHADER_VARIANT_EXTERNAL,
};

/* Keep the following in sync with fragment.glsl. */
enum gl_shader_color_curve {
	SHADER_COLOR_CURVE_IDENTITY = 0,
	SHADER_COLOR_CURVE_LUT_3x1D,
	SHADER_COLOR_CURVE_LINPOW,
	SHADER_COLOR_CURVE_POWLIN,
};

/* Keep the following in sync with fragment.glsl. */
enum gl_shader_color_mapping {
	SHADER_COLOR_MAPPING_IDENTITY = 0,
	SHADER_COLOR_MAPPING_3DLUT,
	SHADER_COLOR_MAPPING_MATRIX,
};

enum gl_shader_attrib_loc {
	SHADER_ATTRIB_LOC_POSITION = 0,
	SHADER_ATTRIB_LOC_TEXCOORD,
	SHADER_ATTRIB_LOC_BARYCENTRIC,
};

enum gl_tex_unit {
	TEX_UNIT_IMAGES = 0,
	TEX_UNIT_COLOR_PRE_CURVE = SHADER_INPUT_TEX_MAX,
	TEX_UNIT_COLOR_MAPPING,
	TEX_UNIT_COLOR_POST_CURVE,
	TEX_UNIT_WIREFRAME,
	TEX_UNIT_LAST,
};
static_assert(TEX_UNIT_LAST < 8, "OpenGL ES 2.0 requires at least 8 texture "
	      "units. Consider replacing this assert with a "
	      "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS check at display creation "
	      "to require more.");

struct gl_extension_table {
	const char *str;
	size_t len;
	uint64_t flag;
};

/** GL shader requirements key
 *
 * This structure is used as a binary blob key for building and searching
 * shaders. Therefore it must not contain any bytes or bits the C compiler
 * would be free to leave undefined e.g. after struct initialization,
 * struct assignment, or member operations.
 *
 * Use 'pahole' from package 'dwarves' to inspect this structure.
 */
struct gl_shader_requirements
{
	unsigned texcoord_input:1; /* enum gl_shader_texcoord_input */

	unsigned variant:4; /* enum gl_shader_texture_variant */
	bool input_is_premult:1;
	bool tint:1;
	bool wireframe:1;

	unsigned color_pre_curve:2; /* enum gl_shader_color_curve */
	unsigned color_mapping:2; /* enum gl_shader_color_mapping */
	unsigned color_post_curve:2; /* enum gl_shader_color_curve */
	unsigned color_channel_order:2; /* enum gl_channel_order */

	/*
	 * The total size of all bitfields plus pad_bits_ must fill up exactly
	 * how many bytes the compiler allocates for them together.
	 */
	unsigned pad_bits_:16;
};
static_assert(sizeof(struct gl_shader_requirements) ==
	      4 /* total bitfield size in bytes */,
	      "struct gl_shader_requirements must not contain implicit padding");

struct gl_shader;
struct weston_color_transform;
struct dmabuf_allocator;

struct gl_shader_config {
	struct gl_shader_requirements req;

	struct weston_matrix projection;
	struct weston_matrix surface_to_buffer;
	float view_alpha;
	GLfloat unicolor[4];
	GLfloat tint[4];
	GLint input_tex_filter; /* GL_NEAREST or GL_LINEAR */
	GLuint input_tex[SHADER_INPUT_TEX_MAX];
	GLuint wireframe_tex;

	union {
		struct {
			GLuint tex;
			GLfloat scale_offset[2];
		} lut_3x1d;
		struct {
			GLfloat params[3][10];
			GLboolean clamped_input;
		} parametric;
	} color_pre_curve;

	union {
		struct {
			GLuint tex;
			GLfloat scale_offset[2];
		} lut3d;
		GLfloat matrix[9];
	} color_mapping;

	union {
		struct {
			GLuint tex;
			GLfloat scale_offset[2];
		} lut_3x1d;
		struct {
			GLfloat params[3][10];
			GLboolean clamped_input;
		} parametric;
	} color_post_curve;
};

struct gl_renderer {
	struct weston_renderer base;
	struct weston_compositor *compositor;
	struct weston_log_scope *renderer_scope;

	/* Debug modes. */
	struct weston_binding *debug_mode_binding;
	int debug_mode;
	bool debug_clear;
	bool wireframe_dirty;
	GLuint wireframe_tex;
	int wireframe_size;
	int nbatches;

	EGLenum platform;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLConfig egl_config;

	uint32_t gl_version;

	/* Vertex streams. */
	struct wl_array position_stream;
	struct wl_array barycentric_stream;
	struct wl_array indices;

	EGLDeviceEXT egl_device;
	const char *drm_device;

	struct weston_drm_format_array supported_formats;

	uint64_t egl_client_extensions;
	uint64_t egl_device_extensions;
	uint64_t egl_display_extensions;

	/* EGL_EXT_device_query */
	PFNEGLQUERYDISPLAYATTRIBEXTPROC query_display_attrib;
	PFNEGLQUERYDEVICESTRINGEXTPROC query_device_string;

	/* EGL_EXT_platform_base */
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
	PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window;

	/* EGL_KHR_image_base */
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;

	/* EGL_WL_bind_wayland_display */
	PFNEGLBINDWAYLANDDISPLAYWL bind_display;
	PFNEGLUNBINDWAYLANDDISPLAYWL unbind_display;
	PFNEGLQUERYWAYLANDBUFFERWL query_buffer;
	bool display_bound;

	/* EGL_KHR_partial_update */
	PFNEGLSETDAMAGEREGIONKHRPROC set_damage_region;

	/* EGL_KHR_swap_buffers_with_damage
	 * EGL_EXT_swap_buffers_with_damage */
	PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;

	/* EGL_EXT_image_dma_buf_import_modifiers */
	PFNEGLQUERYDMABUFFORMATSEXTPROC query_dmabuf_formats;
	PFNEGLQUERYDMABUFMODIFIERSEXTPROC query_dmabuf_modifiers;

	/* EGL_KHR_fence_sync */
	PFNEGLCREATESYNCKHRPROC create_sync;
	PFNEGLDESTROYSYNCKHRPROC destroy_sync;

	/* EGL_ANDROID_native_fence_sync */
	PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_native_fence_fd;

	/* EGL_KHR_wait_sync */
	PFNEGLWAITSYNCKHRPROC wait_sync;

	uint64_t gl_extensions;

	/* GL_OES_EGL_image */
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC image_target_renderbuffer_storage;

	/* GL_OES_mapbuffer */
	PFNGLUNMAPBUFFEROESPROC unmap_buffer;

	/* GL_EXT_map_buffer_range */
	PFNGLMAPBUFFERRANGEEXTPROC map_buffer_range;

	/* GL_OES_texture_3d */
	PFNGLTEXIMAGE3DOESPROC tex_image_3d;

	/* GL_EXT_disjoint_timer_query */
	PFNGLGENQUERIESEXTPROC gen_queries;
	PFNGLDELETEQUERIESEXTPROC delete_queries;
	PFNGLBEGINQUERYEXTPROC begin_query;
	PFNGLENDQUERYEXTPROC end_query;
#if !defined(NDEBUG)
	PFNGLGETQUERYOBJECTIVEXTPROC get_query_object_iv;
#endif
	PFNGLGETQUERYOBJECTUI64VEXTPROC get_query_object_ui64v;

	/* GL_EXT_texture_storage */
	PFNGLTEXSTORAGE2DEXTPROC tex_storage_2d;

	uint64_t features;

	GLenum pbo_usage;

	struct wl_list dmabuf_images;
	struct wl_list dmabuf_formats;
	struct wl_list pending_capture_list;

	struct gl_shader *current_shader;
	struct gl_shader *fallback_shader;

	struct wl_signal destroy_signal;

	/** Shader program cache in most recently used order
	 *
	 * Uses struct gl_shader::link.
	 */
	struct wl_list shader_list;
	struct weston_log_scope *shader_scope;

	struct dmabuf_allocator *allocator;
};

static inline uint32_t
gl_version(uint16_t major, uint16_t minor)
{
	return ((uint32_t)major << 16) | minor;
}

static inline int
gl_version_major(uint32_t ver)
{
	return ver >> 16;
}

static inline int
gl_version_minor(uint32_t ver)
{
	return ver & 0xffff;
}

void
gl_extensions_add(const struct gl_extension_table *table,
		  const char *extensions,
		  uint64_t *flags_out);

static inline bool
egl_client_has(struct gl_renderer *gr,
	       enum egl_client_extension_flag flag)
{
	return (bool) (gr->egl_client_extensions & ((uint64_t) flag));
}

static inline bool
egl_device_has(struct gl_renderer *gr,
	       enum egl_device_extension_flag flag)
{
	return (bool) (gr->egl_device_extensions & ((uint64_t) flag));
}

static inline bool
egl_display_has(struct gl_renderer *gr,
		enum egl_display_extension_flag flag)
{
	return (bool) (gr->egl_display_extensions & ((uint64_t) flag));
}

static inline bool
gl_extensions_has(struct gl_renderer *gr,
		  enum gl_extension_flag flag)
{
	return (bool) (gr->gl_extensions & ((uint64_t) flag));
}

static inline bool
gl_features_has(struct gl_renderer *gr,
		enum gl_feature_flag flag)
{
	return (bool) (gr->features & ((uint64_t) flag));
}

bool
gl_texture_is_format_supported(struct gl_renderer *gr,
			       GLenum format);

bool
gl_texture_2d_init(struct gl_renderer *gr,
		   int levels,
		   GLenum format,
		   int width,
		   int height,
		   GLuint *tex_out);

void
gl_texture_2d_store(struct gl_renderer *gr,
		    int level,
		    int x,
		    int y,
		    int width,
		    int height,
		    GLenum format,
		    GLenum type,
		    const void *data);

void
gl_texture_fini(GLuint *tex);

bool
gl_fbo_is_format_supported(struct gl_renderer *gr,
			   GLenum format);

bool
gl_fbo_init(struct gl_renderer *gr,
	    GLenum format,
	    int width,
	    int height,
	    GLuint *fb_out,
	    GLuint *rb_out);

void
gl_fbo_fini(GLuint *fb,
	    GLuint *rb);

bool
gl_fbo_image_init(struct gl_renderer *gr,
		  EGLImageKHR image,
		  GLuint *fb_out,
		  GLuint *rb_out);

bool
gl_fbo_texture_init(GLenum internal_format,
		    int width,
		    int height,
		    GLenum format,
		    GLenum type,
		    GLuint *fb_out,
		    GLuint *tex_out);

void
gl_fbo_texture_fini(GLuint *fb,
		    GLuint *tex);

static inline struct gl_renderer *
get_renderer(struct weston_compositor *ec)
{
	return (struct gl_renderer *)ec->renderer;
}

void
gl_renderer_print_egl_error_state(void);

void
gl_renderer_log_extensions(struct gl_renderer *gr,
			   const char *name, const char *extensions);

void
log_egl_config_info(EGLDisplay egldpy, EGLConfig eglconfig);

EGLConfig
gl_renderer_get_egl_config(struct gl_renderer *gr,
			   EGLint egl_surface_type,
			   const struct pixel_format_info *const *formats,
			   unsigned formats_count);

int
gl_renderer_setup_egl_display(struct gl_renderer *gr, void *native_display);

int
gl_renderer_setup_egl_client_extensions(struct gl_renderer *gr);

int
gl_renderer_setup_egl_extensions(struct weston_compositor *ec);

GLenum
gl_shader_texture_variant_get_target(enum gl_shader_texture_variant v);

bool
gl_shader_texture_variant_can_be_premult(enum gl_shader_texture_variant v);

void
gl_shader_destroy(struct gl_renderer *gr, struct gl_shader *shader);

void
gl_renderer_shader_list_destroy(struct gl_renderer *gr);

struct gl_shader *
gl_renderer_create_fallback_shader(struct gl_renderer *gr);

void
gl_renderer_garbage_collect_programs(struct gl_renderer *gr);

bool
gl_renderer_use_program(struct gl_renderer *gr,
			const struct gl_shader_config *sconf);

struct weston_log_scope *
gl_shader_scope_create(struct gl_renderer *gr);

bool
gl_shader_config_set_color_transform(struct gl_renderer *gr,
				     struct gl_shader_config *sconf,
				     struct weston_color_transform *xform);

#endif /* GL_RENDERER_INTERNAL_H */

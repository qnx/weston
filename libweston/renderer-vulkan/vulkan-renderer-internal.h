/*
 * Copyright © 2025 Erico Nunes
 *
 * based on gl-renderer-internal.h:
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

#ifndef VULKAN_RENDERER_INTERNAL_H
#define VULKAN_RENDERER_INTERNAL_H

#include <stdbool.h>
#include <time.h>

#include <wayland-util.h>
#include <vulkan/vulkan.h>
#include "shared/helpers.h"
#include "libweston/libweston.h"
#include "libweston/libweston-internal.h"
#include <xcb/xcb.h>

#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xcb.h>

#define MAX_NUM_IMAGES 5
#define MAX_CONCURRENT_FRAMES 2

enum vulkan_pipeline_texture_variant {
	PIPELINE_VARIANT_NONE = 0,
/* Keep the following in sync with Vulkan shader.frag. */
	PIPELINE_VARIANT_RGBA     = 1,
	PIPELINE_VARIANT_RGBX     = 2,
	PIPELINE_VARIANT_SOLID    = 3,
	PIPELINE_VARIANT_EXTERNAL = 4,
};

struct vulkan_pipeline_requirements
{
	unsigned texcoord_input:1; /* enum vulkan_shader_texcoord_input */
	unsigned variant:4; /* enum vulkan_pipeline_texture_variant */
	bool input_is_premult:1;
	bool blend:1;
	VkRenderPass renderpass;
};

struct vulkan_pipeline_config {
	struct vulkan_pipeline_requirements req;

	struct weston_matrix projection;
	struct weston_matrix surface_to_buffer;
	float view_alpha;
	float unicolor[4];
};


/* Keep the following in sync with vertex.glsl. */
enum vulkan_shader_texcoord_input {
	SHADER_TEXCOORD_INPUT_ATTRIB = 0,
	SHADER_TEXCOORD_INPUT_SURFACE,
};

struct vulkan_pipeline {
	struct vulkan_pipeline_requirements key;

	struct wl_list link; /* vulkan_renderer::pipeline_list */
	struct timespec last_used;

	VkDescriptorSetLayout descriptor_set_layout;

	VkPipeline pipeline;
	VkPipelineLayout pipeline_layout;
};

struct vulkan_renderer_texture_image {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView image_view;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	void *staging_map;

	VkCommandBuffer upload_cmd;
	VkFence upload_fence;
};

struct vulkan_renderer {
	struct weston_renderer base;
	struct weston_compositor *compositor;

	bool has_wayland_surface;
	bool has_xcb_surface;
	VkInstance inst;

	VkPhysicalDevice phys_dev;
	VkQueue queue;
	uint32_t queue_family;

	bool has_incremental_present;
	bool has_image_drm_format_modifier;
	bool has_external_semaphore_fd;
	bool has_physical_device_drm;
	bool has_external_memory_dma_buf;
	bool has_queue_family_foreign;
	bool semaphore_import_export;
	VkDevice dev;

	VkCommandPool cmd_pool;

	int drm_fd; /* drm device fd */
	struct weston_drm_format_array supported_formats;
	struct wl_list dmabuf_images;
	struct wl_list dmabuf_formats;

	struct wl_signal destroy_signal;
	struct wl_list pipeline_list;
	struct dmabuf_allocator *allocator;

	PFN_vkCreateWaylandSurfaceKHR create_wayland_surface;
	PFN_vkCreateXcbSurfaceKHR create_xcb_surface;
	PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR get_wayland_presentation_support;
	PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR get_xcb_presentation_support;

	PFN_vkGetImageMemoryRequirements2KHR get_image_memory_requirements2;
	PFN_vkGetMemoryFdPropertiesKHR get_memory_fd_properties;
	PFN_vkGetSemaphoreFdKHR get_semaphore_fd;
	PFN_vkImportSemaphoreFdKHR import_semaphore_fd;

	/* This can be removed if a different shader is defined
	 * to avoid requiring a valid sampler descriptor to run
	 * for solids */
	struct {
		struct vulkan_renderer_texture_image image;
		VkSampler sampler;
	} dummy;
};

static inline struct vulkan_renderer *
get_renderer(struct weston_compositor *ec)
{
	return (struct vulkan_renderer *)ec->renderer;
}

static inline void pnext(void *base, void *next)
{
	VkBaseOutStructure *b = base;
	VkBaseOutStructure *n = next;
	n->pNext = b->pNext;
	b->pNext = n;
}

static inline void _check_vk_success(const char *file, int line, const char *func,
				     VkResult result, const char *vk_func)
{
	if (result == VK_SUCCESS)
		return;

	weston_log("%s %d %s Error: %s failed with VkResult %d\n", file, line, func, vk_func, result);
	abort();
}
#define check_vk_success(result, vk_func) \
	_check_vk_success(__FILE__, __LINE__, __func__, (result), (vk_func))

void
vulkan_pipeline_destroy(struct vulkan_renderer *vr, struct vulkan_pipeline *pipeline);

void
vulkan_renderer_pipeline_list_destroy(struct vulkan_renderer *vr);

struct vulkan_pipeline *
vulkan_renderer_get_pipeline(struct vulkan_renderer *vr,
			     const struct vulkan_pipeline_requirements *reqs);

bool
vulkan_renderer_query_dmabuf_format(struct vulkan_renderer *vr,
				    const struct pixel_format_info *format);

#endif /* VULKAN_RENDERER_INTERNAL_H */

/*
 * Copyright © 2025 Erico Nunes
 *
 * based on simple-dmabug-egl.c:
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
 * Copyright © 2014,2018 Collabora Ltd.
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

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <xf86drm.h>
#include <gbm.h>

#include <wayland-client.h>
#include "shared/helpers.h"
#include "shared/platform.h"
#include "shared/weston-drm-fourcc.h"
#include "shared/xalloc.h"
#include "xdg-shell-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "weston-direct-display-client-protocol.h"
#include "linux-explicit-synchronization-unstable-v1-client-protocol.h"
#include "pixel-formats.h"

#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_PROTOTYPES
#include <vulkan/vulkan.h>

#include <libweston/matrix.h>

/* Possible options that affect the displayed image */
#define OPT_IMMEDIATE     (1 << 0)  /* create wl_buffer immediately */
#define OPT_IMPLICIT_SYNC (1 << 1)  /* force implicit sync */
#define OPT_DIRECT_DISPLAY     (1 << 3)  /* direct-display */

#define MAX_BUFFER_PLANES 4

/* const uint32_t simple_dmabuf_vulkan_vertex_shader[]; simple_dmabuf_vulkan_vertex_shader.frag */
#include "simple_dmabuf_vulkan_vertex_shader.spv.h"

/* const uint32_t simple_dmabuf_vulkan_fragment_shader[]; simple_dmabuf_vulkan_fragment_shader.frag */
#include "simple_dmabuf_vulkan_fragment_shader.spv.h"

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct zwp_linux_dmabuf_v1 *dmabuf;
	struct weston_direct_display_v1 *direct_display;
	struct zwp_linux_explicit_synchronization_v1 *explicit_sync;
	uint32_t format;
	bool format_supported;
	uint64_t *modifiers;
	int modifiers_count;
	int req_dmabuf_immediate;
	bool use_explicit_sync;
	struct {
		int drm_fd;
		struct gbm_device *device;
	} gbm;

	struct {
		VkInstance inst;
		VkPhysicalDevice phys_dev;
		VkDevice dev;

		VkQueue queue;
		uint32_t queue_family;

		VkRenderPass renderpass;
		VkDescriptorPool descriptor_pool;
		VkCommandPool cmd_pool;

		VkDescriptorSetLayout descriptor_set_layout;
		VkPipeline pipeline;

		VkPipelineLayout pipeline_layout;

		VkFormat format;

		struct {
			VkBuffer buffer;
			VkDeviceMemory mem;
			void *map;
		} vertex_buffer;

		PFN_vkGetImageMemoryRequirements2KHR get_image_memory_requirements2;
		PFN_vkGetMemoryFdPropertiesKHR get_memory_fd_properties;
		PFN_vkGetSemaphoreFdKHR get_semaphore_fd;
		PFN_vkImportSemaphoreFdKHR import_semaphore_fd;
	} vk;
};

struct {
	float reflection[16];
	float offset;
} ubo;

struct buffer {
	struct display *display;
	struct wl_buffer *buffer;
	int busy;

	struct gbm_bo *bo;

	int width;
	int height;
	int format;
	uint64_t modifier;
	int plane_count;
	int dmabuf_fds[MAX_BUFFER_PLANES];
	uint32_t strides[MAX_BUFFER_PLANES];
	uint32_t offsets[MAX_BUFFER_PLANES];

	VkImage image;
	VkDeviceMemory image_memory;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	VkFence fence;
	VkCommandBuffer cmd_buffer;

	VkSemaphore render_done;

	struct zwp_linux_buffer_release_v1 *buffer_release;
	/* The buffer owns the release_fence_fd, until it passes ownership
	 * to it to Vulkan (see wait_for_buffer_release_fence). */
	int release_fence_fd;
	/* This is the release semaphore that is waited on by the next
	 * submitted frame */
	VkSemaphore release_semaphore;
	/* The release_semaphore object cannot be safely destroyed at every
	 * buffer_release event as it might still be waited on by a previous
	 * submit. It is saved to be destroyed safely at the next event. */
	VkSemaphore prev_release_semaphore;

	struct {
		VkBuffer buffer;
		VkDeviceMemory mem;
		void *map;
	} ubo_buffer;
	VkDescriptorSet descriptor_set;
};

#define NUM_BUFFERS 4

struct window {
	struct display *display;
	int width, height;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zwp_linux_surface_synchronization_v1 *surface_sync;
	struct buffer buffers[NUM_BUFFERS];
	struct wl_callback *callback;
	bool initialized;
	bool wait_for_configure;

	bool needs_buffer_geometry_update;
};

static void _check_vk_success(const char *file, int line, const char *func,
			       VkResult result, const char *vk_func)
{
	if (result == VK_SUCCESS)
		return;

	fprintf(stderr, "Error: %s failed with VkResult %d ", vk_func, result);
	abort();
}
#define check_vk_success(result, vk_func) \
	_check_vk_success(__FILE__, __LINE__, __func__, (result), (vk_func))

static void pnext(void *base, void *next)
{
	VkBaseOutStructure *b = base;
	VkBaseOutStructure *n = next;
	n->pNext = b->pNext;
	b->pNext = n;
}

static sig_atomic_t running = 1;

static void
redraw(void *data, struct wl_callback *callback, uint32_t time);

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	struct buffer *mybuf = data;

	mybuf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static void
buffer_free(struct buffer *buf)
{
	if (buf->release_fence_fd >= 0)
		close(buf->release_fence_fd);

	if (buf->buffer_release)
		zwp_linux_buffer_release_v1_destroy(buf->buffer_release);

	struct display *display = buf->display;

	vkUnmapMemory(display->vk.dev, buf->ubo_buffer.mem);
	vkDestroyBuffer(display->vk.dev, buf->ubo_buffer.buffer, NULL);
	vkFreeMemory(display->vk.dev, buf->ubo_buffer.mem, NULL);

	vkDestroySemaphore(display->vk.dev, buf->render_done, NULL);
	vkDestroyFence(display->vk.dev, buf->fence, NULL);

	vkFreeCommandBuffers(display->vk.dev, display->vk.cmd_pool, 1, &buf->cmd_buffer);

	vkDestroyImageView(display->vk.dev, buf->image_view, NULL);
	vkFreeMemory(display->vk.dev, buf->image_memory, NULL);
	vkDestroyImage(display->vk.dev, buf->image, NULL);
	vkDestroyFramebuffer(display->vk.dev, buf->framebuffer, NULL);

	if (buf->prev_release_semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(buf->display->vk.dev, buf->prev_release_semaphore, NULL);

	if (buf->release_semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(buf->display->vk.dev, buf->release_semaphore, NULL);

	if (buf->buffer)
		wl_buffer_destroy(buf->buffer);

	if (buf->bo)
		gbm_bo_destroy(buf->bo);

	for (int i = 0; i < buf->plane_count; ++i) {
		if (buf->dmabuf_fds[i] >= 0)
			close(buf->dmabuf_fds[i]);
	}
}

static void
create_succeeded(void *data,
		 struct zwp_linux_buffer_params_v1 *params,
		 struct wl_buffer *new_buffer)
{
	struct buffer *buffer = data;

	buffer->buffer = new_buffer;
	/* When not using explicit synchronization listen to wl_buffer.release
	 * for release notifications, otherwise we are going to use
	 * zwp_linux_buffer_release_v1. */
	if (!buffer->display->use_explicit_sync) {
		wl_buffer_add_listener(buffer->buffer, &buffer_listener,
				       buffer);
	}

	zwp_linux_buffer_params_v1_destroy(params);
}

static void
create_failed(void *data, struct zwp_linux_buffer_params_v1 *params)
{
	struct buffer *buffer = data;

	buffer->buffer = NULL;
	running = 0;

	zwp_linux_buffer_params_v1_destroy(params);

	fprintf(stderr, "Error: zwp_linux_buffer_params.create failed.\n");
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
	create_succeeded,
	create_failed
};

static void create_dmabuf_image(struct display *display, struct buffer *buffer,
		VkFormat format, VkImageUsageFlags usage, VkImage *image)
{
	VkResult result;

	int width = buffer->width;
	int height = buffer->height;

	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImageDrmFormatModifierExplicitCreateInfoEXT mod_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
	};
	VkSubresourceLayout plane_layouts[buffer->plane_count];
	memset(plane_layouts, 0, sizeof(plane_layouts));
	for (int i = 0; i < buffer->plane_count; i++) {
		plane_layouts[i].offset = buffer->offsets[i];
		plane_layouts[i].size = 0;
		plane_layouts[i].rowPitch = buffer->strides[i];
	}

	mod_create_info.drmFormatModifier = buffer->modifier;
	mod_create_info.drmFormatModifierPlaneCount = buffer->plane_count;
	mod_create_info.pPlaneLayouts = plane_layouts;
	pnext(&image_create_info, &mod_create_info);

	VkExternalMemoryImageCreateInfo external_create_info = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	pnext(&image_create_info, &external_create_info);

	result = vkCreateImage(display->vk.dev, &image_create_info, NULL, image);
	check_vk_success(result, "vkCreateImage");
}

static void
create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageView *image_view)
{
	VkResult result;

	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = 1,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
	};

	result = vkCreateImageView(device, &view_info, NULL, image_view);
	check_vk_success(result, "vkCreateImageView");
}

static bool
create_image_for_buffer(struct display *display, struct buffer *buffer)
{
	VkResult result;
	int fd0 = buffer->dmabuf_fds[0];

	const struct pixel_format_info *pixel_format = pixel_format_get_info(buffer->format);

	create_dmabuf_image(display, buffer, pixel_format->vulkan_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &buffer->image);

	VkMemoryFdPropertiesKHR fd_props = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
	};
	result = display->vk.get_memory_fd_properties(display->vk.dev,
			VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
			fd0, &fd_props);
	check_vk_success(result, "vkGetMemoryFdPropertiesKHR");

	const VkImageMemoryRequirementsInfo2 mem_reqs_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.image = buffer->image,
	};
	VkMemoryRequirements2 mem_reqs = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
	};
	display->vk.get_image_memory_requirements2(display->vk.dev, &mem_reqs_info, &mem_reqs);

	const uint32_t memory_type_bits = fd_props.memoryTypeBits & mem_reqs.memoryRequirements.memoryTypeBits;
	assert(memory_type_bits > 0);

	VkMemoryAllocateInfo memory_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.memoryRequirements.size,
		.memoryTypeIndex = ffs(memory_type_bits) - 1,
	};

	VkImportMemoryFdInfoKHR memory_fd_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
		.fd = fd0,
	};
	pnext(&memory_allocate_info, &memory_fd_info);

	VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.image = buffer->image,
	};
	pnext(&memory_allocate_info, &memory_dedicated_info);

	result = vkAllocateMemory(display->vk.dev, &memory_allocate_info, NULL, &buffer->image_memory);
	check_vk_success(result, "vkAllocateMemory");

	result = vkBindImageMemory(display->vk.dev, buffer->image, buffer->image_memory, 0);
	check_vk_success(result, "vkBindImageMemory");

	create_image_view(display->vk.dev, buffer->image, pixel_format->vulkan_format, &buffer->image_view);

	const VkFramebufferCreateInfo framebuffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = display->vk.renderpass,
		.attachmentCount = 1,
		.pAttachments = &buffer->image_view,
		.width = buffer->width,
		.height = buffer->height,
		.layers = 1
	};
	result = vkCreateFramebuffer(display->vk.dev, &framebuffer_create_info, NULL, &buffer->framebuffer);
	check_vk_success(result, "vkCreateFramebuffer");

	return true;
}

static int
find_memory_type(struct display *display, uint32_t allowed, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties mem_properties;
	vkGetPhysicalDeviceMemoryProperties(display->vk.phys_dev, &mem_properties);

	for (unsigned i = 0; (1u << i) <= allowed && i <= mem_properties.memoryTypeCount; ++i) {
		if ((allowed & (1u << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties))
			return i;
	}
	return -1;
}

static void
create_buffer(struct display *display, VkDeviceSize size,
	      VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	      VkBuffer *buffer, VkDeviceMemory *buffer_memory)
{
	VkResult result;

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	result = vkCreateBuffer(display->vk.dev, &buffer_info, NULL, buffer);
	check_vk_success(result, "vkCreateBuffer");

	VkMemoryRequirements mem_requirements;
	vkGetBufferMemoryRequirements(display->vk.dev, *buffer, &mem_requirements);

	int memory_type = find_memory_type(display, mem_requirements.memoryTypeBits, properties);
	assert(memory_type >= 0);

	const VkMemoryAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_requirements.size,
		.memoryTypeIndex = memory_type,
	};

	result = vkAllocateMemory(display->vk.dev, &alloc_info, NULL, buffer_memory);
	check_vk_success(result, "vkAllocateMemory");

	result = vkBindBufferMemory(display->vk.dev, *buffer, *buffer_memory, 0);
	check_vk_success(result, "vkBindBufferMemory");
}

static void create_descriptor_set(struct display *display, struct buffer *buffer)
{
	VkResult result;

	const VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = display->vk.descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &display->vk.descriptor_set_layout,
	};
	result = vkAllocateDescriptorSets(display->vk.dev, &descriptor_set_allocate_info, &buffer->descriptor_set);
	check_vk_success(result, "vkAllocateDescriptorSets");

	const VkDescriptorBufferInfo descriptor_buffer_info = {
		.buffer = buffer->ubo_buffer.buffer,
		.range = VK_WHOLE_SIZE,
	};
	const VkWriteDescriptorSet descriptor_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = buffer->descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &descriptor_buffer_info,
		},
	};

	vkUpdateDescriptorSets(display->vk.dev, ARRAY_LENGTH(descriptor_writes), descriptor_writes, 0, NULL);
}

static int
create_dmabuf_buffer(struct display *display, struct buffer *buffer,
		     int width, int height, uint32_t opts)
{
	VkResult result;

	static uint32_t flags = 0;
	struct zwp_linux_buffer_params_v1 *params;

	buffer->display = display;
	buffer->width = width;
	buffer->height = height;
	buffer->format = display->format;
	buffer->release_fence_fd = -1;

	if (display->modifiers_count > 0) {
#ifdef HAVE_GBM_BO_CREATE_WITH_MODIFIERS2
		buffer->bo = gbm_bo_create_with_modifiers2(display->gbm.device,
							   buffer->width,
							   buffer->height,
							   buffer->format,
							   display->modifiers,
							   display->modifiers_count,
							   GBM_BO_USE_RENDERING);
#else
		buffer->bo = gbm_bo_create_with_modifiers(display->gbm.device,
							  buffer->width,
							  buffer->height,
							  buffer->format,
							  display->modifiers,
							  display->modifiers_count);
#endif
		if (buffer->bo)
			buffer->modifier = gbm_bo_get_modifier(buffer->bo);
	}

	if (!buffer->bo) {
		buffer->bo = gbm_bo_create(display->gbm.device,
					   buffer->width,
					   buffer->height,
					   buffer->format,
					   GBM_BO_USE_RENDERING);
		buffer->modifier = DRM_FORMAT_MOD_INVALID;
	}

	if (!buffer->bo) {
		fprintf(stderr, "create_bo failed\n");
		goto error;
	}

	buffer->plane_count = gbm_bo_get_plane_count(buffer->bo);
	for (int i = 0; i < buffer->plane_count; ++i) {
		int ret;
		union gbm_bo_handle handle;

		handle = gbm_bo_get_handle_for_plane(buffer->bo, i);
		if (handle.s32 == -1) {
			fprintf(stderr, "error: failed to get gbm_bo_handle\n");
			goto error;
		}

		ret = drmPrimeHandleToFD(display->gbm.drm_fd, handle.u32, 0,
					 &buffer->dmabuf_fds[i]);
		if (ret < 0 || buffer->dmabuf_fds[i] < 0) {
			fprintf(stderr, "error: failed to get dmabuf_fd\n");
			goto error;
		}
		buffer->strides[i] = gbm_bo_get_stride_for_plane(buffer->bo, i);
		buffer->offsets[i] = gbm_bo_get_offset(buffer->bo, i);
	}

	params = zwp_linux_dmabuf_v1_create_params(display->dmabuf);

	if ((opts & OPT_DIRECT_DISPLAY) && display->direct_display)
		weston_direct_display_v1_enable(display->direct_display, params);

	for (int i = 0; i < buffer->plane_count; ++i) {
		zwp_linux_buffer_params_v1_add(params,
					       buffer->dmabuf_fds[i],
					       i,
					       buffer->offsets[i],
					       buffer->strides[i],
					       buffer->modifier >> 32,
					       buffer->modifier & 0xffffffff);
	}

	zwp_linux_buffer_params_v1_add_listener(params, &params_listener, buffer);
	if (display->req_dmabuf_immediate) {
		buffer->buffer =
			zwp_linux_buffer_params_v1_create_immed(params,
								buffer->width,
								buffer->height,
								buffer->format,
								flags);
		/* When not using explicit synchronization listen to
		 * wl_buffer.release for release notifications, otherwise we
		 * are going to use zwp_linux_buffer_release_v1. */
		if (!buffer->display->use_explicit_sync) {
			wl_buffer_add_listener(buffer->buffer,
					       &buffer_listener,
					       buffer);
		}
	} else {
		zwp_linux_buffer_params_v1_create(params,
						  buffer->width,
						  buffer->height,
						  buffer->format,
						  flags);
	}

	if (!create_image_for_buffer(display, buffer))
		goto error;

	const VkFenceCreateInfo fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	result = vkCreateFence(display->vk.dev, &fence_info, NULL, &buffer->fence);
	check_vk_success(result, "vkCreateFence");

	VkSemaphoreCreateInfo semaphore_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	VkExportSemaphoreCreateInfo export_info = {
		.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
	};
	pnext(&semaphore_info, &export_info);

	result = vkCreateSemaphore(display->vk.dev, &semaphore_info, NULL, &buffer->render_done);
	check_vk_success(result, "vkCreateSemaphore");

	const VkCommandBufferAllocateInfo cmd_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = display->vk.cmd_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	result = vkAllocateCommandBuffers(display->vk.dev, &cmd_alloc_info, &buffer->cmd_buffer);
	check_vk_success(result, "vkAllocateCommandBuffers");

	uint32_t ubo_size = sizeof(ubo);
	create_buffer(display, ubo_size,
		      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      &buffer->ubo_buffer.buffer, &buffer->ubo_buffer.mem);

	result = vkMapMemory(display->vk.dev, buffer->ubo_buffer.mem, 0, ubo_size, 0, &buffer->ubo_buffer.map);
	check_vk_success(result, "vkMapMemory");

	create_descriptor_set(display, buffer);

	return 0;

error:
	buffer_free(buffer);
	return -1;
}

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(surface, serial);

	if (window->initialized && window->wait_for_configure)
		redraw(window, NULL, 0);
	window->wait_for_configure = false;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	xdg_toplevel_handle_configure,
	xdg_toplevel_handle_close,
};

typedef float vec3[3];

static void create_renderpass(struct display *display)
{
	VkResult result;

	const VkAttachmentDescription attachment_description = {
		.format = display->vk.format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	const VkAttachmentReference attachment_reference = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};
	const VkSubpassDescription subpass_description = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &attachment_reference,
	};
	const VkRenderPassCreateInfo renderpass_create_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment_description,
		.subpassCount = 1,
		.pSubpasses = &subpass_description,
	};

	result = vkCreateRenderPass(display->vk.dev, &renderpass_create_info, NULL, &display->vk.renderpass);
	check_vk_success(result, "vkCreateRenderPass");
}

static void
create_descriptor_set_layout(struct display *display)
{
	VkResult result;

	const VkDescriptorSetLayoutBinding vs_ubo_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};
	const VkDescriptorSetLayoutBinding bindings[] = {
		vs_ubo_layout_binding,
	};
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = ARRAY_LENGTH(bindings),
		.pBindings = bindings,
	};

	result = vkCreateDescriptorSetLayout(display->vk.dev, &layout_info, NULL, &display->vk.descriptor_set_layout);
	check_vk_success(result, "vkCreateDescriptorSetLayout");
}

static void
create_pipeline(struct display *display)
{
	VkResult result;

	VkShaderModule vs_module;
	const VkShaderModuleCreateInfo vs_shader_module_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(simple_dmabuf_vulkan_vertex_shader),
		.pCode = (uint32_t *)simple_dmabuf_vulkan_vertex_shader,
	};
	vkCreateShaderModule(display->vk.dev, &vs_shader_module_create_info, NULL, &vs_module);

	VkShaderModule fs_module;
	const VkShaderModuleCreateInfo fs_shader_module_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(simple_dmabuf_vulkan_fragment_shader),
		.pCode = (uint32_t *)simple_dmabuf_vulkan_fragment_shader,
	};
	vkCreateShaderModule(display->vk.dev, &fs_shader_module_create_info, NULL, &fs_module);

	const VkPipelineVertexInputStateCreateInfo pipeline_vertex_input_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 2,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
			{
				.binding = 0,
				.stride = 3 * sizeof(float),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			},
			{
				.binding = 1,
				.stride = 3 * sizeof(float),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			},
		},
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
			{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = 0
			},
			{
				.location = 1,
				.binding = 1,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = 0
			},
		}
	};
	const VkPipelineInputAssemblyStateCreateInfo pipeline_input_assembly_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = false,
	};
	const VkPipelineViewportStateCreateInfo pipeline_viewport_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo pipeline_rasterization_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.rasterizerDiscardEnable = false,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthClampEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = 1,
	};
	const VkPipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = (VkPipelineColorBlendAttachmentState []) {
			{ .colorWriteMask = VK_COLOR_COMPONENT_A_BIT |
				VK_COLOR_COMPONENT_R_BIT |
					VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT },
		}
	};
	const VkPipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = (VkDynamicState[]) {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		},
	};

	const VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &display->vk.descriptor_set_layout,
	};
	result = vkCreatePipelineLayout(display->vk.dev, &pipeline_layout_create_info, NULL, &display->vk.pipeline_layout);
	check_vk_success(result, "vkCreatePipelineLayout");

	const VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = (VkPipelineShaderStageCreateInfo[]) {
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = vs_module,
				.pName = "main",
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = fs_module,
				.pName = "main",
			},
		},
		.pVertexInputState = &pipeline_vertex_input_state_create_info,
		.pInputAssemblyState = &pipeline_input_assembly_state_create_info,
		.pViewportState = &pipeline_viewport_state_create_info,
		.pRasterizationState = &pipeline_rasterization_state_create_info,
		.pMultisampleState = &pipeline_multisample_state_create_info,
		.pColorBlendState = &pipeline_color_blend_state_create_info,
		.pDynamicState = &pipeline_dynamic_state_create_info,

		.flags = 0,
		.layout = display->vk.pipeline_layout,
		.renderPass = display->vk.renderpass,
		.subpass = 0,
	};
	result = vkCreateGraphicsPipelines(display->vk.dev, VK_NULL_HANDLE, 1, &graphics_pipeline_create_info, NULL, &display->vk.pipeline);
	check_vk_success(result, "vkCreateGraphicsPipelines");

	vkDestroyShaderModule(display->vk.dev, fs_module, NULL);
	vkDestroyShaderModule(display->vk.dev, vs_module, NULL);
}

static void create_vertex_buffer(struct display *display)
{
	VkResult result;

	/* This can be created statically and shared across
	 * frames since it doesn't change at all */
	const vec3 verts[] = {
		{ -0.5, -0.5, 0 },
		{ -0.5,  0.5, 0 },
		{  0.5, -0.5, 0 },

		{  0.5, -0.5, 0 },
		{ -0.5,  0.5, 0 },
		{  0.5,  0.5, 0 },
	};

	const vec3 colors[] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 },

		{ 0, 0, 1 },
		{ 0, 1, 0 },
		{ 1, 1, 0 },
	};

	uint32_t vertex_buffer_size = sizeof(verts) + sizeof(colors);

	create_buffer(display, vertex_buffer_size,
		      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      &display->vk.vertex_buffer.buffer, &display->vk.vertex_buffer.mem);

	result = vkMapMemory(display->vk.dev, display->vk.vertex_buffer.mem, 0, vertex_buffer_size, 0, &display->vk.vertex_buffer.map);
	check_vk_success(result, "vkMapMemory");

	memcpy(display->vk.vertex_buffer.map, verts, sizeof(verts));
	memcpy(display->vk.vertex_buffer.map + sizeof(verts), colors, sizeof(colors));
}

static void
create_descriptor_pool(struct display *display, VkDescriptorPool *descriptor_pool,
		       uint32_t base_count, uint32_t maxsets)
{
	VkResult result;

	const VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1 * base_count,
		},
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = ARRAY_LENGTH(pool_sizes),
		.pPoolSizes = pool_sizes,
		.maxSets = maxsets,
	};

	result = vkCreateDescriptorPool(display->vk.dev, &pool_info, NULL, descriptor_pool);
	check_vk_success(result, "vkCreateDescriptorPool");
}

static bool
window_set_up_vulkan(struct window *window)
{
	VkResult result;
	struct display *display = window->display;

	const struct pixel_format_info *pixel_format = pixel_format_get_info(display->format);
	display->vk.format = pixel_format->vulkan_format;

	create_renderpass(display);

	create_descriptor_set_layout(display);

	create_pipeline(display);

	create_vertex_buffer(display);

	create_descriptor_pool(display, &display->vk.descriptor_pool, NUM_BUFFERS, NUM_BUFFERS);

	const VkCommandPoolCreateInfo cmd_pool_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = display->vk.queue_family,
	};
	result = vkCreateCommandPool(display->vk.dev, &cmd_pool_create_info, NULL, &display->vk.cmd_pool);
	check_vk_success(result, "vkCreateCommandPool");

	return true;
}

static void
destroy_window(struct window *window)
{
	struct display *display = window->display;
	VkResult result;

	result = vkDeviceWaitIdle(display->vk.dev);
	check_vk_success(result, "vkDeviceWaitIdle");

	if (window->callback)
		wl_callback_destroy(window->callback);

	for (int i = 0; i < NUM_BUFFERS; i++) {
		if (window->buffers[i].buffer)
			buffer_free(&window->buffers[i]);
	}

	if (window->xdg_toplevel)
		xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		xdg_surface_destroy(window->xdg_surface);
	if (window->surface_sync)
		zwp_linux_surface_synchronization_v1_destroy(window->surface_sync);
	wl_surface_destroy(window->surface);
	free(window);
}

static struct window *
create_window(struct display *display, int width, int height, int opts)
{
	struct window *window;
	int ret;

	window = xzalloc(sizeof *window);

	window->callback = NULL;
	window->display = display;
	window->width = width;
	window->height = height;
	window->surface = wl_compositor_create_surface(display->compositor);

	if (!display->wm_base)
		abort();

	window->xdg_surface =
		xdg_wm_base_get_xdg_surface(display->wm_base,
				window->surface);

	assert(window->xdg_surface);

	xdg_surface_add_listener(window->xdg_surface,
			&xdg_surface_listener, window);

	window->xdg_toplevel =
		xdg_surface_get_toplevel(window->xdg_surface);

	assert(window->xdg_toplevel);

	xdg_toplevel_add_listener(window->xdg_toplevel,
			&xdg_toplevel_listener, window);

	xdg_toplevel_set_title(window->xdg_toplevel, "simple-dmabuf-vulkan");
	xdg_toplevel_set_app_id(window->xdg_toplevel,
			"org.freedesktop.weston.simple-dmabuf-vulkan");

	window->wait_for_configure = true;
	wl_surface_commit(window->surface);

	if (display->explicit_sync) {
		window->surface_sync =
			zwp_linux_explicit_synchronization_v1_get_synchronization(
					display->explicit_sync, window->surface);
		assert(window->surface_sync);
	}

	for (int i = 0; i < NUM_BUFFERS; ++i)
		for (int j = 0; j < MAX_BUFFER_PLANES; ++j)
			window->buffers[i].dmabuf_fds[j] = -1;

	if (!window_set_up_vulkan(window))
		goto error;

	for (int i = 0; i < NUM_BUFFERS; ++i) {
		ret = create_dmabuf_buffer(display, &window->buffers[i],
					   width, height, opts);
		if (ret < 0)
			goto error;
	}

	return window;

error:
	if (window)
		destroy_window(window);

	return NULL;
}

static int
create_vulkan_fence_fd(struct window *window, struct buffer *buffer)
{
	struct display *display = window->display;
	VkResult result;
	int fd;

	/* Export semaphore into fence for acquire_fence */
	VkSemaphoreGetFdInfoKHR semaphore_fd_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
		.semaphore = buffer->render_done,
		.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
	};
	result = display->vk.get_semaphore_fd(display->vk.dev, &semaphore_fd_info, &fd);
	check_vk_success(result, "vkGetSemaphoreFdKHR");

	return fd;
}

static struct buffer *
window_next_buffer(struct window *window)
{
	for (int i = 0; i < NUM_BUFFERS; i++)
		if (!window->buffers[i].busy)
			return &window->buffers[i];

	return NULL;
}

static void
transfer_image_queue_family(VkCommandBuffer cmd_buffer, VkImage image,
			    uint32_t src_index, uint32_t dst_index)
{
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.srcAccessMask = 0,
		.dstAccessMask = 0,
		.image = image,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
		.srcQueueFamilyIndex = src_index,
		.dstQueueFamilyIndex = dst_index,
	};

	vkCmdPipelineBarrier(cmd_buffer,
			     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			     0, 0, NULL, 0, NULL, 1, &barrier);
}

static const struct wl_callback_listener frame_listener;

/* Renders a square moving from the lower left corner to the
 * upper right corner of the window. The square's vertices have
 * the following colors:
 *
 *  green +-----+ yellow
 *        |     |
 *        |     |
 *    red +-----+ blue
 */
static void
render(struct window *window, struct buffer *buffer)
{
	struct display *display = window->display;
	/* Complete a movement iteration in 5000 ms. */
	static const uint64_t iteration_ms = 5000;
	float offset;
	struct timeval tv;
	uint64_t time_ms;
	struct weston_matrix reflection;
	VkResult result;

	gettimeofday(&tv, NULL);
	time_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	/* Split time_ms in repeating windows of [0, iteration_ms) and map them
	 * to offsets in the [-0.5, 0.5) range. */
	offset = (time_ms % iteration_ms) / (float) iteration_ms - 0.5;

	weston_matrix_init(&reflection);
	/* perform a reflection about x-axis to keep the same orientation of
	 * the vertices colors,  as outlined in the comment at the beginning
	 * of this function.
	 *
	 * We need to render upside-down, because rendering through an FBO
	 * causes the bottom of the image to be written to the top pixel row of
	 * the buffer, y-flipping the image.
	 *
	 * Reflection is a specialized version of scaling with the
	 * following matrix:
	 *
	 * [1,  0,  0]
	 * [0, -1,  0]
	 * [0,  0,  1]
	 */
	weston_matrix_scale(&reflection, 1, -1, 1);

	memcpy(buffer->ubo_buffer.map, &reflection.M.colmaj, sizeof(reflection.M.colmaj));
	memcpy(buffer->ubo_buffer.map + sizeof(reflection.M.colmaj), &offset, sizeof(offset));

	vkWaitForFences(display->vk.dev, 1, &buffer->fence, VK_TRUE, UINT64_MAX);
	vkResetFences(display->vk.dev, 1, &buffer->fence);

	const VkCommandBufferBeginInfo command_buffer_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = 0
	};
	result = vkBeginCommandBuffer(buffer->cmd_buffer, &command_buffer_begin_info);
	check_vk_success(result, "vkCreateCommandPool");

	transfer_image_queue_family(buffer->cmd_buffer, buffer->image,
				    VK_QUEUE_FAMILY_FOREIGN_EXT,
				    display->vk.queue_family);

	const VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	const VkRenderPassBeginInfo renderpass_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = display->vk.renderpass,
		.framebuffer = buffer->framebuffer,
		.renderArea.offset = {0, 0},
		.renderArea.extent = {window->width, window->height},
		.clearValueCount = 1,
		.pClearValues = &clear_color,
	};
	vkCmdBeginRenderPass(buffer->cmd_buffer, &renderpass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	const VkBuffer buffers[] = {
		display->vk.vertex_buffer.buffer,
		display->vk.vertex_buffer.buffer,
	};
	const VkDeviceSize offsets[] = {
		0,
		6 * sizeof(float[3]),
	};
	vkCmdBindVertexBuffers(buffer->cmd_buffer, 0, ARRAY_LENGTH(buffers), buffers, offsets);

	vkCmdBindPipeline(buffer->cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, display->vk.pipeline);

	vkCmdBindDescriptorSets(buffer->cmd_buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			display->vk.pipeline_layout,
			0, 1,
			&buffer->descriptor_set, 0, NULL);

	const VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = window->width,
		.height = window->height,
		.minDepth = 0,
		.maxDepth = 1,
	};
	vkCmdSetViewport(buffer->cmd_buffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = { window->width, window->height, },
	};
	vkCmdSetScissor(buffer->cmd_buffer, 0, 1, &scissor);

	vkCmdDraw(buffer->cmd_buffer, 6, 1, 0, 0);

	vkCmdEndRenderPass(buffer->cmd_buffer);

	transfer_image_queue_family(buffer->cmd_buffer, buffer->image,
				    display->vk.queue_family,
				    VK_QUEUE_FAMILY_FOREIGN_EXT);

	result = vkEndCommandBuffer(buffer->cmd_buffer);
	check_vk_success(result, "vkEndCommandBuffer");

	const VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &buffer->cmd_buffer,
	};
	/* Get semaphore from submit to be exported */
	if (window->display->use_explicit_sync) {
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &buffer->render_done;
		if (buffer->release_semaphore != VK_NULL_HANDLE) {
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = &buffer->release_semaphore;
			submit_info.pWaitDstStageMask = wait_stages;
		}
	}

	result = vkQueueSubmit(display->vk.queue, 1, &submit_info, buffer->fence);
	check_vk_success(result, "vkQueueSubmit");
}

static void
buffer_fenced_release(void *data,
		      struct zwp_linux_buffer_release_v1 *release,
		      int32_t fence)
{
	struct buffer *buffer = data;

	assert(release == buffer->buffer_release);
	assert(buffer->release_fence_fd == -1);

	if (buffer->prev_release_semaphore != VK_NULL_HANDLE) {
		vkDestroySemaphore(buffer->display->vk.dev, buffer->prev_release_semaphore, NULL);
		buffer->prev_release_semaphore = VK_NULL_HANDLE;
	}

	if (buffer->release_semaphore != VK_NULL_HANDLE) {
		buffer->prev_release_semaphore = buffer->release_semaphore;
	}

	buffer->busy = 0;
	buffer->release_fence_fd = fence;
	zwp_linux_buffer_release_v1_destroy(buffer->buffer_release);
	buffer->buffer_release = NULL;
}

static void
buffer_immediate_release(void *data,
			 struct zwp_linux_buffer_release_v1 *release)
{
	struct buffer *buffer = data;

	assert(release == buffer->buffer_release);
	assert(buffer->release_fence_fd == -1);

	if (buffer->prev_release_semaphore != VK_NULL_HANDLE) {
		vkDestroySemaphore(buffer->display->vk.dev, buffer->prev_release_semaphore, NULL);
		buffer->prev_release_semaphore = VK_NULL_HANDLE;
	}

	if (buffer->release_semaphore != VK_NULL_HANDLE) {
		buffer->prev_release_semaphore = buffer->release_semaphore;
	}

	buffer->busy = 0;
	zwp_linux_buffer_release_v1_destroy(buffer->buffer_release);
	buffer->buffer_release = NULL;
}

static const struct zwp_linux_buffer_release_v1_listener buffer_release_listener = {
	buffer_fenced_release,
	buffer_immediate_release,
};

static void
wait_for_buffer_release_fence(struct window *window, struct buffer *buffer)
{
	struct display *display = window->display;
	VkResult result;

	const VkSemaphoreCreateInfo semaphore_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	result = vkCreateSemaphore(display->vk.dev, &semaphore_info, NULL, &buffer->release_semaphore);
	check_vk_success(result, "vkCreateSemaphore");

	/* Import fence fd into Vulkan sempahore */
	const VkImportSemaphoreFdInfoKHR import_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
		.semaphore = buffer->release_semaphore,
		.fd = buffer->release_fence_fd,
	};
	result = display->vk.import_semaphore_fd(display->vk.dev, &import_info);
	check_vk_success(result, "vkImportSemaphoreFdKHR");

	/* Vulkan takes ownership of the fence fd. */
	buffer->release_fence_fd = -1;
}

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct buffer *buffer;

	buffer = window_next_buffer(window);
	if (!buffer) {
		fprintf(stderr,
			!callback ? "Failed to create the first buffer.\n" :
			"All buffers busy at redraw(). Server bug?\n");
		abort();
	}

	if (buffer->release_fence_fd >= 0)
		wait_for_buffer_release_fence(window, buffer);
	else
		buffer->release_semaphore = VK_NULL_HANDLE;

	render(window, buffer);

	if (window->display->use_explicit_sync) {
		int fence_fd = create_vulkan_fence_fd(window, buffer);
		zwp_linux_surface_synchronization_v1_set_acquire_fence(
			window->surface_sync, fence_fd);
		close(fence_fd);

		buffer->buffer_release =
			zwp_linux_surface_synchronization_v1_get_release(window->surface_sync);
		zwp_linux_buffer_release_v1_add_listener(
			buffer->buffer_release, &buffer_release_listener, buffer);
	}

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	if (callback)
		wl_callback_destroy(callback);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);
	wl_surface_commit(window->surface);
	buffer->busy = 1;
}

static const struct wl_callback_listener frame_listener = {
	redraw
};

static void
dmabuf_modifiers(void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
		 uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
	struct display *d = data;
	uint64_t modifier = u64_from_u32s(modifier_hi, modifier_lo);

	if (format != d->format) {
		return;
	}

	d->format_supported = true;

	if (modifier != DRM_FORMAT_MOD_INVALID) {
		++d->modifiers_count;
		d->modifiers = realloc(d->modifiers,
				       d->modifiers_count * sizeof(*d->modifiers));
		d->modifiers[d->modifiers_count - 1] = modifier;
	}
}

static void
dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf, uint32_t format)
{
	/* XXX: deprecated */
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
	dmabuf_format,
	dmabuf_modifiers
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t id, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry,
					 id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		d->wm_base = wl_registry_bind(registry,
					      id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(d->wm_base, &xdg_wm_base_listener, d);
	} else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
		if (version < 3)
			return;
		d->dmabuf = wl_registry_bind(registry,
					     id, &zwp_linux_dmabuf_v1_interface, 3);
		zwp_linux_dmabuf_v1_add_listener(d->dmabuf, &dmabuf_listener, d);
	} else if (strcmp(interface, "zwp_linux_explicit_synchronization_v1") == 0) {
		d->explicit_sync = wl_registry_bind(
			registry, id,
			&zwp_linux_explicit_synchronization_v1_interface, 1);
	} else if (strcmp(interface, "weston_direct_display_v1") == 0) {
		d->direct_display = wl_registry_bind(registry,
						     id, &weston_direct_display_v1_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
destroy_display(struct display *display)
{
	if (display->gbm.device)
		gbm_device_destroy(display->gbm.device);

	if (display->gbm.drm_fd >= 0)
		close(display->gbm.drm_fd);

	vkUnmapMemory(display->vk.dev, display->vk.vertex_buffer.mem);
	vkDestroyBuffer(display->vk.dev, display->vk.vertex_buffer.buffer, NULL);
	vkFreeMemory(display->vk.dev, display->vk.vertex_buffer.mem, NULL);

	vkDestroyPipelineLayout(display->vk.dev, display->vk.pipeline_layout, NULL);
	vkDestroyPipeline(display->vk.dev, display->vk.pipeline, NULL);
	vkDestroyDescriptorSetLayout(display->vk.dev, display->vk.descriptor_set_layout, NULL);
	vkDestroyRenderPass(display->vk.dev, display->vk.renderpass, NULL);

	vkDestroyDescriptorPool(display->vk.dev, display->vk.descriptor_pool, NULL);

	vkDestroyCommandPool(display->vk.dev, display->vk.cmd_pool, NULL);
	vkDestroyDevice(display->vk.dev, NULL);
	vkDestroyInstance(display->vk.inst, NULL);

	free(display->modifiers);

	if (display->direct_display)
		weston_direct_display_v1_destroy(display->direct_display);

	if (display->explicit_sync)
		zwp_linux_explicit_synchronization_v1_destroy(display->explicit_sync);

	if (display->dmabuf)
		zwp_linux_dmabuf_v1_destroy(display->dmabuf);

	if (display->wm_base)
		xdg_wm_base_destroy(display->wm_base);

	if (display->compositor)
		wl_compositor_destroy(display->compositor);

	if (display->registry)
		wl_registry_destroy(display->registry);

	if (display->display) {
		wl_display_flush(display->display);
		wl_display_disconnect(display->display);
	}

	free(display);
}

static void
load_device_proc(struct display *display, const char *func, void *proc_ptr)
{
	void *proc = (void *)vkGetDeviceProcAddr(display->vk.dev, func);
	if (proc == NULL) {
		char err[256];
		snprintf(err, sizeof(err), "Failed to vkGetDeviceProcAddr %s\n", func);
		err[sizeof(err)-1] = '\0';
		fprintf(stderr, "%s", err);
		abort();
	}

	*(void **)proc_ptr = proc;
}

static void
create_instance(struct display *display)
{
	uint32_t num_avail_inst_extns;
	uint32_t num_inst_extns = 0;
	VkResult result;

	result = vkEnumerateInstanceExtensionProperties(NULL, &num_avail_inst_extns, NULL);
	check_vk_success(result, "vkEnumerateInstanceExtensionProperties");
	assert(num_avail_inst_extns > 0);
	VkExtensionProperties *avail_inst_extns = xmalloc(num_avail_inst_extns * sizeof(VkExtensionProperties));
	result = vkEnumerateInstanceExtensionProperties(NULL, &num_avail_inst_extns, avail_inst_extns);
	check_vk_success(result, "vkEnumerateInstanceExtensionProperties");

	const char **inst_extns = xmalloc(num_avail_inst_extns * sizeof(*inst_extns));
	inst_extns[num_inst_extns++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	inst_extns[num_inst_extns++] = VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME;
	inst_extns[num_inst_extns++] = VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME;
	inst_extns[num_inst_extns++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

	for (uint32_t i = 0; i < num_inst_extns; i++) {
		uint32_t j;
		for (j = 0; j < num_avail_inst_extns; j++) {
			if (strcmp(inst_extns[i], avail_inst_extns[j].extensionName) == 0) {
				break;
			}
		}
		if (j == num_avail_inst_extns) {
			fprintf(stderr, "Unsupported instance extension: %s\n", inst_extns[i]);
			abort();
		}
	}

	const VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "simple-dmabuf-vulkan",
		.apiVersion = VK_MAKE_VERSION(1, 0, 0),
	};

	const VkInstanceCreateInfo inst_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.ppEnabledExtensionNames = inst_extns,
		.enabledExtensionCount = num_inst_extns,
	};

	result = vkCreateInstance(&inst_create_info, NULL, &display->vk.inst);
	check_vk_success(result, "vkCreateInstance");

	free(avail_inst_extns);
	free(inst_extns);
}

static void
choose_physical_device(struct display *display)
{
	uint32_t n_phys_devs;
	VkPhysicalDevice *phys_devs = NULL;
	VkResult result;

	result = vkEnumeratePhysicalDevices(display->vk.inst, &n_phys_devs, NULL);
	check_vk_success(result, "vkEnumeratePhysicalDevices");
	assert(n_phys_devs != 0);
	phys_devs = xmalloc(n_phys_devs * sizeof(*phys_devs));
	result = vkEnumeratePhysicalDevices(display->vk.inst, &n_phys_devs, phys_devs);
	check_vk_success(result, "vkEnumeratePhysicalDevices");

	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	/* Pick the first one */
	for (uint32_t i = 0; i < n_phys_devs; ++i) {
		VkPhysicalDeviceProperties props;

		vkGetPhysicalDeviceProperties(phys_devs[i], &props);

		if (physical_device == VK_NULL_HANDLE) {
			physical_device = phys_devs[i];
			break;
		}
	}

	if (physical_device == VK_NULL_HANDLE) {
		fprintf(stderr, "Unable to find a suitable physical device\n");
		abort();
	}

	display->vk.phys_dev = physical_device;

	free(phys_devs);
}

static void
choose_queue_family(struct display *display)
{
	uint32_t n_props = 0;
	VkQueueFamilyProperties *props = NULL;

	vkGetPhysicalDeviceQueueFamilyProperties(display->vk.phys_dev, &n_props, NULL);
	props = xmalloc(n_props * sizeof(*props));
	vkGetPhysicalDeviceQueueFamilyProperties(display->vk.phys_dev, &n_props, props);

	uint32_t family_idx = UINT32_MAX;
	/* Pick the first graphics queue */
	for (uint32_t i = 0; i < n_props; ++i) {
		if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && props[i].queueCount > 0) {
			family_idx = i;
			break;
		}
	}

	if (family_idx == UINT32_MAX) {
		fprintf(stderr, "Physical device exposes no queue with graphics\n");
		abort();
	}

	display->vk.queue_family = family_idx;

	free(props);
}

static void
create_device(struct display *display)
{
	uint32_t num_avail_device_extns;
	uint32_t num_device_extns = 0;
	VkResult result;

	result = vkEnumerateDeviceExtensionProperties(display->vk.phys_dev, NULL, &num_avail_device_extns, NULL);
	check_vk_success(result, "vkEnumerateDeviceExtensionProperties");
	VkExtensionProperties *avail_device_extns = xmalloc(num_avail_device_extns * sizeof(VkExtensionProperties));
	result = vkEnumerateDeviceExtensionProperties(display->vk.phys_dev, NULL, &num_avail_device_extns, avail_device_extns);
	check_vk_success(result, "vkEnumerateDeviceExtensionProperties");

	const char **device_extns = xmalloc(num_avail_device_extns * sizeof(*device_extns));
	device_extns[num_device_extns++] = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_BIND_MEMORY_2_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_MAINTENANCE_1_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME;

	for (uint32_t i = 0; i < num_device_extns; i++) {
		uint32_t j;
		for (j = 0; j < num_avail_device_extns; j++) {
			if (strcmp(device_extns[i], avail_device_extns[j].extensionName) == 0) {
				break;
			}
		}
		if (j == num_avail_device_extns) {
			fprintf(stderr, "Unsupported device extension: %s\n", device_extns[i]);
			abort();
		}
	}

	const VkDeviceQueueCreateInfo device_queue_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = display->vk.queue_family,
		.queueCount = 1,
		.pQueuePriorities = (float[]){ 1.0f },
	};

	const VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &device_queue_info,
		.enabledExtensionCount = num_device_extns,
		.ppEnabledExtensionNames = device_extns,
	};

	result = vkCreateDevice(display->vk.phys_dev, &device_create_info, NULL, &display->vk.dev);
	check_vk_success(result, "vkCreateDevice");

	load_device_proc(display, "vkGetImageMemoryRequirements2KHR", &display->vk.get_image_memory_requirements2);
	load_device_proc(display, "vkGetMemoryFdPropertiesKHR", &display->vk.get_memory_fd_properties);
	load_device_proc(display, "vkGetSemaphoreFdKHR", &display->vk.get_semaphore_fd);
	load_device_proc(display, "vkImportSemaphoreFdKHR", &display->vk.import_semaphore_fd);

	free(avail_device_extns);
	free(device_extns);
}

static bool
display_set_up_vulkan(struct display *display)
{
	create_instance(display);

	choose_physical_device(display);

	choose_queue_family(display);

	create_device(display);

	vkGetDeviceQueue(display->vk.dev, 0, 0, &display->vk.queue);

	return true;
}

static bool query_modifier_usage_support(struct display *d, VkFormat vk_format,
		VkImageUsageFlags usage, const VkDrmFormatModifierPropertiesEXT *m)
{
	VkResult result;

	VkPhysicalDeviceImageFormatInfo2 pdev_image_format_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
		.type = VK_IMAGE_TYPE_2D,
		.format = vk_format,
		.usage = usage,
		.flags = 0,
		.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
	};

	VkPhysicalDeviceExternalImageFormatInfo pdev_ext_image_format_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	pnext(&pdev_image_format_info, &pdev_ext_image_format_info);

	VkPhysicalDeviceImageDrmFormatModifierInfoEXT pdev_image_drm_format_mod_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
		.drmFormatModifier = m->drmFormatModifier,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	pnext(&pdev_image_format_info, &pdev_image_drm_format_mod_info);

	VkImageFormatListCreateInfoKHR image_format_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR,
		.pViewFormats = &vk_format,
		.viewFormatCount = 1,
	};
	pnext(&pdev_image_format_info, &image_format_info);

	VkImageFormatProperties2 image_format_props = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	};

	VkExternalImageFormatProperties ext_image_format_props = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
	};
	pnext(&image_format_props, &ext_image_format_props);

	const VkExternalMemoryProperties *ext_mem_props = &ext_image_format_props.externalMemoryProperties;

	result = vkGetPhysicalDeviceImageFormatProperties2(d->vk.phys_dev, &pdev_image_format_info, &image_format_props);
	if (result != VK_SUCCESS && result != VK_ERROR_FORMAT_NOT_SUPPORTED)
		return false;

	if (!(ext_mem_props->externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
		return false;

	return true;
}

static bool
query_modifier_support(struct display *d, size_t max_modifiers, VkFormat vulkan_format, int *num_modifiers, uint64_t *vulkan_modifiers)
{
	unsigned int num_supported = 0;
	const VkImageUsageFlags vulkan_render_usage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	static const VkFormatFeatureFlags render_features =
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;

	VkDrmFormatModifierPropertiesListEXT drm_format_mod_props = {
		.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
		.drmFormatModifierCount = max_modifiers,
	};
	VkFormatProperties2 format_props = {
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	pnext(&format_props, &drm_format_mod_props);

	VkDrmFormatModifierPropertiesEXT* mod_props = xzalloc(max_modifiers * sizeof(*drm_format_mod_props.pDrmFormatModifierProperties));
	drm_format_mod_props.pDrmFormatModifierProperties = mod_props;

	vkGetPhysicalDeviceFormatProperties2(d->vk.phys_dev, vulkan_format, &format_props);

	for (uint32_t i = 0; i < drm_format_mod_props.drmFormatModifierCount; ++i) {
		VkDrmFormatModifierPropertiesEXT m = drm_format_mod_props.pDrmFormatModifierProperties[i];
		if ((m.drmFormatModifierTilingFeatures & render_features) == render_features) {
			bool supported = query_modifier_usage_support(d, vulkan_format,
					vulkan_render_usage, &m);

			if (supported) {
				if (vulkan_modifiers && num_supported < max_modifiers)
					vulkan_modifiers[num_supported] = m.drmFormatModifier;
				num_supported++;
			}
		}
	}

	*num_modifiers = num_supported;

	free(mod_props);
	return true;
}

static void
query_dma_buf_modifiers(struct display *d, uint32_t drm_format, VkFormat vulkan_format, int *num_modifiers, uint64_t *vulkan_modifiers)
{
	*num_modifiers = 0;

	VkDrmFormatModifierPropertiesListEXT drm_format_mod_props = {
		drm_format_mod_props.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
	};
	VkFormatProperties2 format_props = {
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	pnext(&format_props, &drm_format_mod_props);
	vkGetPhysicalDeviceFormatProperties2(d->vk.phys_dev, vulkan_format, &format_props);

	if (drm_format_mod_props.drmFormatModifierCount > 0) {
		query_modifier_support(d, drm_format_mod_props.drmFormatModifierCount, vulkan_format, num_modifiers, vulkan_modifiers);
	}
}

static bool
display_update_supported_modifiers_for_vulkan(struct display *d)
{
	uint64_t *vulkan_modifiers = NULL;
	int num_vulkan_modifiers = 0;
	const struct pixel_format_info *pixel_format = pixel_format_get_info(d->format);
	VkFormat vulkan_format = pixel_format->vulkan_format;

	query_dma_buf_modifiers(d, d->format, vulkan_format, &num_vulkan_modifiers, NULL);
	if (num_vulkan_modifiers == 0)
		return true;

	vulkan_modifiers = xzalloc(num_vulkan_modifiers * sizeof(*vulkan_modifiers));

	query_dma_buf_modifiers(d, d->format, vulkan_format, &num_vulkan_modifiers, vulkan_modifiers);

	/* Poor person's set intersection: d->modifiers INTERSECT
	 * vulkan_modifiers.  If a modifier is not supported, replace it with
	 * DRM_FORMAT_MOD_INVALID in the d->modifiers array.
	 */
	for (int i = 0; i < d->modifiers_count; ++i) {
		uint64_t mod = d->modifiers[i];
		bool vulkan_supported = false;

		for (int j = 0; j < num_vulkan_modifiers; ++j) {
			if (vulkan_modifiers[j] == mod) {
				vulkan_supported = true;
				break;
			}
		}

		if (!vulkan_supported)
			d->modifiers[i] = DRM_FORMAT_MOD_INVALID;
	}

	free(vulkan_modifiers);

	return true;
}

static bool
display_set_up_gbm(struct display *display, char const* drm_render_node)
{
	display->gbm.drm_fd = open(drm_render_node, O_RDWR);
	if (display->gbm.drm_fd < 0) {
		fprintf(stderr, "Failed to open drm render node %s\n",
			drm_render_node);
		return false;
	}

	display->gbm.device = gbm_create_device(display->gbm.drm_fd);
	if (display->gbm.device == NULL) {
		fprintf(stderr, "Failed to create gbm device\n");
		return false;
	}

	return true;
}

static struct display *
create_display(char const *drm_render_node, uint32_t format, int opts)
{
	struct display *display = NULL;

	display = xzalloc(sizeof *display);

	display->gbm.drm_fd = -1;

	display->display = wl_display_connect(NULL);
	assert(display->display);

	display->format = format;
	display->req_dmabuf_immediate = opts & OPT_IMMEDIATE;

	display->registry = wl_display_get_registry(display->display);
	wl_registry_add_listener(display->registry,
				 &registry_listener, display);
	wl_display_roundtrip(display->display);
	if (display->dmabuf == NULL) {
		fprintf(stderr, "No zwp_linux_dmabuf global\n");
		goto error;
	}

	wl_display_roundtrip(display->display);

	if (!display->format_supported) {
		fprintf(stderr, "format 0x%"PRIX32" is not available\n",
			display->format);
		goto error;
	}

	/* GBM needs to be initialized before Vulkan, so that we have a valid
	 * render node gbm_device to create the Vulkan display from. */
	if (!display_set_up_gbm(display, drm_render_node))
		goto error;

	if (!display_set_up_vulkan(display))
		goto error;

	if (!display_update_supported_modifiers_for_vulkan(display))
		goto error;

	/* We use explicit synchronization only if the user hasn't disabled it,
	 * the compositor supports it, we can handle fence fds. */
	display->use_explicit_sync =
		!(opts & OPT_IMPLICIT_SYNC) &&
		display->explicit_sync;

	if (opts & OPT_IMPLICIT_SYNC) {
		fprintf(stderr, "Warning: Not using explicit sync, disabled by user\n");
	} else if (!display->explicit_sync) {
		fprintf(stderr,
			"Warning: zwp_linux_explicit_synchronization_v1 not supported,\n"
			"         will not use explicit synchronization\n");
	}

	return display;

error:
	if (display != NULL)
		destroy_display(display);
	return NULL;
}

static void
signal_int(int signum)
{
	running = 0;
}

static void
print_usage_and_exit(void)
{
	printf("usage flags:\n"
		"\t'-i,--import-immediate=<>'"
		"\n\t\t0 to import dmabuf via roundtrip, "
		"\n\t\t1 to enable import without roundtrip\n"
		"\t'-d,--drm-render-node=<>'"
		"\n\t\tthe full path to the drm render node to use\n"
		"\t'-s,--size=<>'"
		"\n\t\tthe window size in pixels (default: 256)\n"
		"\t'-e,--explicit-sync=<>'"
		"\n\t\t0 to disable explicit sync, "
		"\n\t\t1 to enable explicit sync (default: 1)\n"
		"\t'-f,--format=0x<>'"
		"\n\t\tthe DRM format code to use\n"
		"\t'-g,--direct-display'"
		"\n\t\tenables weston-direct-display extension to attempt "
		"direct scan-out;\n\t\tnote this will cause the image to be "
		"displayed inverted as GL uses a\n\t\tdifferent texture "
		"coordinate system\n");
	exit(0);
}

static int
is_true(const char* c)
{
	if (!strcmp(c, "1"))
		return 1;
	else if (!strcmp(c, "0"))
		return 0;
	else
		print_usage_and_exit();

	return 0;
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display *display;
	struct window *window;
	uint32_t format = DRM_FORMAT_XRGB8888;
	int opts = 0;
	char const *drm_render_node = "/dev/dri/renderD128";
	int c, option_index, ret = 0;
	int window_size = 256;

	static struct option long_options[] = {
		{"import-immediate", required_argument, 0,  'i' },
		{"drm-render-node",  required_argument, 0,  'd' },
		{"size",	     required_argument, 0,  's' },
		{"explicit-sync",    required_argument, 0,  'e' },
		{"format",           required_argument, 0,  'f' },
		{"direct-display",   no_argument,	0,  'g' },
		{"help",             no_argument      , 0,  'h' },
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "hi:d:s:e:f:mg",
				long_options, &option_index)) != -1) {
		switch (c) {
		case 'i':
			if (is_true(optarg))
				opts |= OPT_IMMEDIATE;
			break;
		case 'd':
			drm_render_node = optarg;
			break;
		case 's':
			window_size = strtol(optarg, NULL, 10);
			break;
		case 'e':
			if (!is_true(optarg))
				opts |= OPT_IMPLICIT_SYNC;
			break;
		case 'g':
			opts |= OPT_DIRECT_DISPLAY;
			break;
		case 'f':
			format = strtoul(optarg, NULL, 0);
			break;
		default:
			print_usage_and_exit();
		}
	}

	display = create_display(drm_render_node, format, opts);
	if (!display)
		return 1;
	window = create_window(display, window_size, window_size, opts);
	if (!window)
		return 1;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	/* Here we retrieve the linux-dmabuf objects if executed without immed,
	 * or error */
	wl_display_roundtrip(display->display);

	if (!running)
		return 1;

	window->initialized = true;

	if (!window->wait_for_configure)
		redraw(window, NULL, 0);

	while (running && ret != -1)
		ret = wl_display_dispatch(display->display);

	fprintf(stderr, "simple-dmabuf-vulkan exiting\n");
	destroy_window(window);
	destroy_display(display);

	return 0;
}

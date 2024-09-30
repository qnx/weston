/*
 * Copyright © 2025 Erico Nunes
 *
 * based on gl-renderer:
 * Copyright © 2012 Intel Corporation
 * Copyright © 2015,2019,2021 Collabora, Ltd.
 * Copyright © 2016 NVIDIA Corporation
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

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <gbm.h>

#include "linux-sync-file.h"

#include <vulkan/vulkan.h>
#include "vulkan-renderer.h"
#include "vulkan-renderer-internal.h"
#include "vertex-clipping.h"
#include "linux-dmabuf.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "linux-explicit-synchronization.h"
#include "output-capture.h"
#include "pixel-formats.h"

#include "shared/fd-util.h"
#include "shared/helpers.h"
#include "shared/platform.h"
#include "shared/string-helpers.h"
#include "shared/weston-drm-fourcc.h"
#include "shared/xalloc.h"
#include "libweston/weston-log.h"

#include <xf86drm.h> /* Physical device drm */

enum vulkan_border_status {
	BORDER_STATUS_CLEAN = 0,
	BORDER_TOP_DIRTY = 1 << WESTON_RENDERER_BORDER_TOP,
	BORDER_LEFT_DIRTY = 1 << WESTON_RENDERER_BORDER_LEFT,
	BORDER_RIGHT_DIRTY = 1 << WESTON_RENDERER_BORDER_RIGHT,
	BORDER_BOTTOM_DIRTY = 1 << WESTON_RENDERER_BORDER_BOTTOM,
	BORDER_ALL_DIRTY = 0xf,
};

struct vulkan_border_image {
	int32_t width, height;
	int32_t tex_width;
	void *data;

	struct vulkan_renderer_texture_image texture;
	VkSampler sampler;

	VkDescriptorSet descriptor_set;

	VkBuffer vs_ubo_buffer;
	VkDeviceMemory vs_ubo_memory;
	void *vs_ubo_map;

	/* these are not really used as of now */
	VkBuffer fs_ubo_buffer;
	VkDeviceMemory fs_ubo_memory;
	void *fs_ubo_map;
};

struct vulkan_renderbuffer_dmabuf {
	struct vulkan_renderer *vr;
	struct linux_dmabuf_memory *memory;
};

struct vulkan_renderbuffer {
	struct weston_output *output;
	pixman_region32_t damage;
	enum vulkan_border_status border_status;
	bool stale;

	struct vulkan_renderbuffer_dmabuf dmabuf;

	void *buffer;
	int stride;
	weston_renderbuffer_discarded_func discarded_cb;
	void *user_data;

	/* Unused by drm and swapchain outputs */
	struct vulkan_renderer_image *image;

	struct wl_list link;
};

struct vulkan_renderer_image {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView image_view;
	VkFramebuffer framebuffer;

	VkSemaphore render_done;
	struct vulkan_renderbuffer *renderbuffer;
	struct gbm_bo *bo;

};

struct vulkan_renderer_frame_acquire_fence {
	VkSemaphore semaphore;

	struct wl_list link;
};

struct vulkan_renderer_frame_vbuf {
	VkBuffer buffer;
	VkDeviceMemory memory;
	void *map;
	uint64_t offset;
	uint64_t size;

	struct wl_list link;
};

struct vulkan_renderer_frame_dspool {
	VkDescriptorPool pool;
	uint32_t count;
	uint32_t maxsets;

	struct wl_list link;
};

struct vulkan_renderer_frame {
	VkCommandBuffer cmd_buffer;

	VkSemaphore image_acquired;
	VkFence fence;

	struct wl_list acquire_fence_list;

	struct wl_list vbuf_list;
	struct wl_list dspool_list;
};

enum vulkan_output_type {
	VULKAN_OUTPUT_HEADLESS,
	VULKAN_OUTPUT_DRM,
	VULKAN_OUTPUT_SWAPCHAIN,
};

struct vulkan_output_state {
	struct weston_size fb_size; /**< in pixels, including borders */
	struct weston_geometry area; /**< composited area in pixels inside fb */

	struct vulkan_border_image borders[4];
	enum vulkan_border_status border_status;

	struct weston_matrix output_matrix;

	/* struct vulkan_renderbuffer::link */
	struct wl_list renderbuffer_list;

	const struct pixel_format_info *pixel_format;
	VkRenderPass renderpass;
	enum vulkan_output_type output_type;
	struct {
		VkSwapchainKHR swapchain;
		VkPresentModeKHR present_mode;
		VkSurfaceKHR surface;
	} swapchain;
	struct {
		uint32_t image_index;
	} drm;

	/* For drm and swapchain outputs only */
	uint32_t image_count;
	struct vulkan_renderer_image images[MAX_NUM_IMAGES];

	uint32_t frame_index;
	uint32_t num_frames;
	struct vulkan_renderer_frame frames[MAX_CONCURRENT_FRAMES];

	int render_fence_fd; /* exported render_done from last submitted image */
};

struct vulkan_buffer_state {
	struct vulkan_renderer *vr;

	float color[4];

	bool needs_full_upload;
	pixman_region32_t texture_damage;

	/* Only needed between attach() and flush_damage() */
	uint32_t vulkan_format[3];
	uint32_t pitch; /* plane 0 pitch in pixels */
	uint32_t offset[3]; /* per-plane pitch in bytes */

	enum vulkan_pipeline_texture_variant pipeline_variant;

	unsigned int textures[3];
	int num_textures;

	struct wl_listener destroy_listener;

	struct vulkan_renderer_texture_image texture;
	VkSampler sampler_linear;
	VkSampler sampler_nearest;

	VkDescriptorSet descriptor_set;

	VkBuffer vs_ubo_buffer;
	VkDeviceMemory vs_ubo_memory;
	void *vs_ubo_map;

	VkBuffer fs_ubo_buffer;
	VkDeviceMemory fs_ubo_memory;
	void *fs_ubo_map;
};

struct vulkan_surface_state {
	struct weston_surface *surface;

	struct vulkan_buffer_state *buffer;

	/* These buffer references should really be attached to paint nodes
	 * rather than either buffer or surface state */
	struct weston_buffer_reference buffer_ref;
	struct weston_buffer_release_reference buffer_release_ref;

	/* Whether this surface was used in the current output repaint.
	   Used only in the context of a vulkan_renderer_repaint_output call. */
	bool used_in_output_repaint;

	struct wl_listener surface_destroy_listener;
	struct wl_listener renderer_destroy_listener;
};

struct vs_ubo {
	float proj[16];
	float surface_to_buffer[16];
};

struct fs_ubo {
	float unicolor[4];
	float view_alpha;
};

struct dmabuf_allocator {
	struct gbm_device *gbm_device;
};

struct vulkan_renderer_dmabuf_memory {
	struct linux_dmabuf_memory base;
	struct dmabuf_allocator *allocator;
	struct gbm_bo *bo;
};

struct dmabuf_format {
	uint32_t format;
	struct wl_list link;

	uint64_t *modifiers;
	unsigned *external_only;
	int num_modifiers;
};

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

static void
transition_image_layout(VkCommandBuffer cmd_buffer, VkImage image,
			VkImageLayout old_layout, VkImageLayout new_layout,
			VkPipelineStageFlags srcs, VkPipelineStageFlags dsts,
			VkAccessFlags src_access, VkAccessFlags dst_access)
{
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.image = image,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	};

	vkCmdPipelineBarrier(cmd_buffer, srcs, dsts, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static void
destroy_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
	if (memory)
		vkUnmapMemory(device, memory);
	vkDestroyBuffer(device, buffer, NULL);
	vkFreeMemory(device, memory, NULL);
}

static void
destroy_sampler(VkDevice device, VkSampler sampler)
{
	vkDestroySampler(device, sampler, NULL);
}

static void
destroy_image(VkDevice device, VkImage image, VkImageView image_view, VkDeviceMemory memory)
{
	if (image_view)
		vkDestroyImageView(device, image_view, NULL);
	vkDestroyImage(device, image, NULL);
	vkFreeMemory(device, memory, NULL);
}

static void
destroy_texture_image(struct vulkan_renderer *vr, struct vulkan_renderer_texture_image *texture)
{
	vkDestroyFence(vr->dev, texture->upload_fence, NULL);
	vkFreeCommandBuffers(vr->dev, vr->cmd_pool, 1, &texture->upload_cmd);

	destroy_buffer(vr->dev, texture->staging_buffer, texture->staging_memory);

	destroy_image(vr->dev, texture->image, texture->image_view, texture->memory);
}

static void
destroy_buffer_state(struct vulkan_buffer_state *vb)
{
	struct vulkan_renderer *vr = vb->vr;

	// TODO: how to refcount this buffer properly so that it is not
	// destroyed in the middle of a frame?
	VkResult result;
	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	destroy_sampler(vr->dev, vb->sampler_linear);
	destroy_sampler(vr->dev, vb->sampler_nearest);
	destroy_texture_image(vr, &vb->texture);

	destroy_buffer(vr->dev, vb->fs_ubo_buffer, vb->fs_ubo_memory);
	destroy_buffer(vr->dev, vb->vs_ubo_buffer, vb->vs_ubo_memory);

	pixman_region32_fini(&vb->texture_damage);

	wl_list_remove(&vb->destroy_listener.link);

	free(vb);
}

static void
surface_state_destroy(struct vulkan_surface_state *vs, struct vulkan_renderer *vr)
{
	wl_list_remove(&vs->surface_destroy_listener.link);
	wl_list_remove(&vs->renderer_destroy_listener.link);

	vs->surface->renderer_state = NULL;

	if (vs->buffer && vs->buffer_ref.buffer->type == WESTON_BUFFER_SHM)
		destroy_buffer_state(vs->buffer);
	vs->buffer = NULL;

	weston_buffer_reference(&vs->buffer_ref, NULL,
				BUFFER_WILL_NOT_BE_ACCESSED);
	weston_buffer_release_reference(&vs->buffer_release_ref, NULL);

	free(vs);
}

static void
surface_state_handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct vulkan_surface_state *vs;
	struct vulkan_renderer *vr;

	vs = container_of(listener, struct vulkan_surface_state,
			  surface_destroy_listener);

	vr = get_renderer(vs->surface->compositor);

	surface_state_destroy(vs, vr);
}

static void
surface_state_handle_renderer_destroy(struct wl_listener *listener, void *data)
{
	struct vulkan_surface_state *vs;
	struct vulkan_renderer *vr;

	vr = data;

	vs = container_of(listener, struct vulkan_surface_state,
			  renderer_destroy_listener);

	surface_state_destroy(vs, vr);
}

static inline struct vulkan_output_state *
get_output_state(struct weston_output *output)
{
	return (struct vulkan_output_state *)output->renderer_state;
}

static void
vulkan_renderbuffer_fini(struct vulkan_renderbuffer *renderbuffer)
{
	assert(!renderbuffer->stale);

	pixman_region32_fini(&renderbuffer->damage);

	renderbuffer->stale = true;
}

static void
vulkan_renderer_destroy_image(struct vulkan_renderer *vr,
			      struct vulkan_renderer_image *image)
{
	vkDestroySemaphore(vr->dev, image->render_done, NULL);
	vkDestroyFramebuffer(vr->dev, image->framebuffer, NULL);
	vkDestroyImageView(vr->dev, image->image_view, NULL);
	vkDestroyImage(vr->dev, image->image, NULL);
	vkFreeMemory(vr->dev, image->memory, NULL);
}

static void
vulkan_renderer_destroy_renderbuffer(weston_renderbuffer_t weston_renderbuffer)
{
	struct vulkan_renderbuffer *rb =
		(struct vulkan_renderbuffer *) weston_renderbuffer;
	struct vulkan_renderer *vr = get_renderer(rb->output->compositor);

	wl_list_remove(&rb->link);

	if (!rb->stale)
		vulkan_renderbuffer_fini(rb);

	// this wait idle is only on renderbuffer destroy
	VkResult result;
	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	if (rb->image) {
		vulkan_renderer_destroy_image(vr, rb->image);
		free(rb->image);
	}

	if (rb->dmabuf.memory)
		rb->dmabuf.memory->destroy(rb->dmabuf.memory);

	free(rb);
}

static bool
vulkan_renderer_discard_renderbuffers(struct vulkan_output_state *vo,
				      bool destroy)
{
	struct vulkan_renderbuffer *rb, *tmp;
	bool success = true;

	/* A renderbuffer goes stale after being discarded. Most resources are
	 * released. It's kept in the output states' renderbuffer list waiting
	 * for the backend to destroy it. */
	wl_list_for_each_safe(rb, tmp, &vo->renderbuffer_list, link) {
		if (destroy) {
			vulkan_renderer_destroy_renderbuffer((weston_renderbuffer_t) rb);
		} else if (!rb->stale) {
			vulkan_renderbuffer_fini(rb);
			if (rb->discarded_cb)
				success = rb->discarded_cb((weston_renderbuffer_t) rb,
							   rb->user_data);
		}
	}

	return success;
}

static void
vulkan_renderer_output_destroy_images(struct weston_output *output)
{
	struct vulkan_output_state *vo = get_output_state(output);
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);

	// this wait idle is only on output destroy
	VkResult result;
	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	for (uint32_t i = 0; i < vo->image_count; i++) {
		struct vulkan_renderer_image *im = &vo->images[i];
		vulkan_renderer_destroy_image(vr, im);
	}
}

static void
vulkan_renderer_destroy_swapchain(struct weston_output *output)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_output_state *vo = get_output_state(output);

	// Wait idle here is bad, but this is only swapchain recreation
	// and not on drm-backend
	VkResult result;
	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	for (uint32_t i = 0; i < vo->image_count; i++) {
		struct vulkan_renderer_image *im = &vo->images[i];

		vkDestroySemaphore(vr->dev, im->render_done, NULL);
		vkDestroyFramebuffer(vr->dev, im->framebuffer, NULL);
		vkDestroyImageView(vr->dev, im->image_view, NULL);
	}

	vkDestroySwapchainKHR(vr->dev, vo->swapchain.swapchain, NULL);
}

static void
vulkan_renderer_output_destroy(struct weston_output *output)
{
	struct vulkan_output_state *vo = get_output_state(output);
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);

	// this wait idle is only on output destroy
	VkResult result;
	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	vkDestroyRenderPass(vr->dev, vo->renderpass, NULL);

	for (unsigned int i = 0; i < vo->num_frames; ++i) {
		struct vulkan_renderer_frame *fr = &vo->frames[i];

		vkDestroyFence(vr->dev, fr->fence, NULL);
		vkDestroySemaphore(vr->dev, fr->image_acquired, NULL);
		vkFreeCommandBuffers(vr->dev, vr->cmd_pool, 1, &fr->cmd_buffer);

		struct vulkan_renderer_frame_acquire_fence *acquire_fence, *ftmp;
		wl_list_for_each_safe(acquire_fence, ftmp, &fr->acquire_fence_list, link) {
			vkDestroySemaphore(vr->dev, acquire_fence->semaphore, NULL);
			wl_list_remove(&acquire_fence->link);
			free(acquire_fence);
		}

		struct vulkan_renderer_frame_vbuf *vbuf, *vtmp;
		wl_list_for_each_safe(vbuf, vtmp, &fr->vbuf_list, link) {
			destroy_buffer(vr->dev, vbuf->buffer, vbuf->memory);
			wl_list_remove(&vbuf->link);
			free(vbuf);
		}

		struct vulkan_renderer_frame_dspool *dspool, *dtmp;
		wl_list_for_each_safe(dspool, dtmp, &fr->dspool_list, link) {
			vkDestroyDescriptorPool(vr->dev, dspool->pool, NULL);
			wl_list_remove(&dspool->link),
			free(dspool);
		}
	}

	if (vo->output_type == VULKAN_OUTPUT_SWAPCHAIN) {
		vulkan_renderer_destroy_swapchain(output);
		vkDestroySurfaceKHR(vr->inst, vo->swapchain.surface, NULL);
	} else {
		vulkan_renderer_output_destroy_images(output);
	}

	vulkan_renderer_discard_renderbuffers(vo, true);

	free(vo);
}

static void
vulkan_renderer_dmabuf_destroy(struct linux_dmabuf_memory *dmabuf)
{
	struct vulkan_renderer_dmabuf_memory *vulkan_renderer_dmabuf;
	struct dmabuf_attributes *attributes;
	int i;

	vulkan_renderer_dmabuf = (struct vulkan_renderer_dmabuf_memory *)dmabuf;

	attributes = dmabuf->attributes;
	for (i = 0; i < attributes->n_planes; ++i)
		close(attributes->fd[i]);
	free(dmabuf->attributes);

	gbm_bo_destroy(vulkan_renderer_dmabuf->bo);
	free(vulkan_renderer_dmabuf);
}

static struct linux_dmabuf_memory *
vulkan_renderer_dmabuf_alloc(struct weston_renderer *renderer,
			     unsigned int width, unsigned int height,
			     uint32_t format,
			     const uint64_t *modifiers, const unsigned int count)
{
	struct vulkan_renderer *vr = (struct vulkan_renderer *)renderer;
	struct dmabuf_allocator *allocator = vr->allocator;
	struct linux_dmabuf_memory *dmabuf = NULL;

	if (!allocator)
		return NULL;

	struct vulkan_renderer_dmabuf_memory *vulkan_renderer_dmabuf;
	struct dmabuf_attributes *attributes;
	struct gbm_bo *bo;
	int i;
#ifdef HAVE_GBM_BO_CREATE_WITH_MODIFIERS2
	bo = gbm_bo_create_with_modifiers2(allocator->gbm_device,
					   width, height, format,
					   modifiers, count,
					   GBM_BO_USE_RENDERING);
#else
	bo = gbm_bo_create_with_modifiers(allocator->gbm_device,
					  width, height, format,
					  modifiers, count);
#endif
	if (!bo)
		bo = gbm_bo_create(allocator->gbm_device,
				   width, height, format,
				   GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
	if (!bo) {
		weston_log("failed to create gbm_bo\n");
		return NULL;
	}

	vulkan_renderer_dmabuf = xzalloc(sizeof(*vulkan_renderer_dmabuf));
	vulkan_renderer_dmabuf->bo = bo;
	vulkan_renderer_dmabuf->allocator = allocator;

	attributes = xzalloc(sizeof(*attributes));
	attributes->width = width;
	attributes->height = height;
	attributes->format = format;
	attributes->n_planes = gbm_bo_get_plane_count(bo);
	for (i = 0; i < attributes->n_planes; ++i) {
		attributes->fd[i] = gbm_bo_get_fd(bo);
		attributes->stride[i] = gbm_bo_get_stride_for_plane(bo, i);
		attributes->offset[i] = gbm_bo_get_offset(bo, i);
	}
	attributes->modifier = gbm_bo_get_modifier(bo);

	dmabuf = &vulkan_renderer_dmabuf->base;
	dmabuf->attributes = attributes;
	dmabuf->destroy = vulkan_renderer_dmabuf_destroy;

	return dmabuf;
}

static void
create_descriptor_pool(struct vulkan_renderer *vr, VkDescriptorPool *descriptor_pool,
		       uint32_t base_count, uint32_t maxsets)
{
	VkResult result;

	const VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 2 * base_count,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1 * base_count,
		},
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = ARRAY_LENGTH(pool_sizes),
		.pPoolSizes = pool_sizes,
		.maxSets = maxsets,
	};

	result = vkCreateDescriptorPool(vr->dev, &pool_info, NULL, descriptor_pool);
	check_vk_success(result, "vkCreateDescriptorPool");
}

static bool
try_allocate_descriptor_set(struct vulkan_renderer *vr,
			    VkDescriptorPool descriptor_pool,
			    VkDescriptorSetLayout *descriptor_set_layout,
			    VkDescriptorSet *descriptor_set)
{
	VkResult result;

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = descriptor_set_layout,
	};

	result = vkAllocateDescriptorSets(vr->dev, &alloc_info, descriptor_set);
	return (result == VK_SUCCESS);
}

static void
get_descriptor_set(struct vulkan_renderer *vr,
		   struct vulkan_renderer_frame *fr,
		   VkDescriptorSetLayout *descriptor_set_layout,
		   VkDescriptorSet *descriptor_set)
{
	const uint32_t base_count = 1024;
	const uint32_t maxsets = 4096;

	struct vulkan_renderer_frame_dspool *dspool;

	wl_list_for_each(dspool, &fr->dspool_list, link) {
		VkDescriptorPool pool = dspool->pool;
		bool success = try_allocate_descriptor_set(vr, pool, descriptor_set_layout,
							   descriptor_set);
		if (success)
			return;
	}

	struct vulkan_renderer_frame_dspool *new_dspool = xzalloc(sizeof(*new_dspool));
	new_dspool->count = base_count;
	new_dspool->maxsets = maxsets;
	create_descriptor_pool(vr, &new_dspool->pool, base_count, maxsets);
	wl_list_insert(&fr->dspool_list, &new_dspool->link);

	bool success = try_allocate_descriptor_set(vr, new_dspool->pool, descriptor_set_layout,
						   descriptor_set);
	assert(success);
}

static void
create_descriptor_set(struct vulkan_renderer *vr,
		      struct vulkan_renderer_frame *fr,
		      VkDescriptorSetLayout *descriptor_set_layout,
		      VkBuffer vs_ubo_buffer,
		      VkBuffer fs_ubo_buffer,
		      VkImageView image_view,
		      VkSampler sampler,
		      VkDescriptorSet *descriptor_set)
{
	const VkDescriptorBufferInfo vs_ubo_info = {
		.buffer = vs_ubo_buffer,
		.offset = 0,
		.range = sizeof(struct vs_ubo),
	};

	const VkDescriptorBufferInfo fs_ubo_info = {
		.buffer = fs_ubo_buffer,
		.offset = 0,
		.range = sizeof(struct fs_ubo),
	};

	const VkDescriptorImageInfo image_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView = image_view,
		.sampler = sampler,
	};

	get_descriptor_set(vr, fr, descriptor_set_layout, descriptor_set);
	assert(descriptor_set);

	const VkWriteDescriptorSet descriptor_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = *descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &vs_ubo_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = *descriptor_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &fs_ubo_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = *descriptor_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo = &image_info,
		},
	};

	vkUpdateDescriptorSets(vr->dev, ARRAY_LENGTH(descriptor_writes), descriptor_writes, 0, NULL);
}

static void
reset_descriptor_pool(struct vulkan_renderer *vr, struct vulkan_renderer_frame *fr)
{
	if (wl_list_empty(&fr->dspool_list))
		return;

	if (wl_list_length(&fr->dspool_list) == 1) {
		struct vulkan_renderer_frame_dspool *first = wl_container_of(fr->dspool_list.next, first, link);
		vkResetDescriptorPool(vr->dev, first->pool, 0);
		return;
	}

	struct vulkan_renderer_frame_dspool *dspool, *tmp;
	uint32_t total_count = 0;
	uint32_t total_maxsets = 0;
	wl_list_for_each_safe(dspool, tmp, &fr->dspool_list, link) {
		total_count += dspool->count;
		total_maxsets += dspool->maxsets;
		wl_list_remove(&dspool->link),
		vkDestroyDescriptorPool(vr->dev, dspool->pool, NULL);
		free(dspool);
	}

	total_count = round_up_pow2_32(total_count);
	total_maxsets = round_up_pow2_32(total_maxsets);

	struct vulkan_renderer_frame_dspool *new_dspool = xzalloc(sizeof(*new_dspool));
	new_dspool->count = total_count;
	new_dspool->maxsets = total_maxsets;
	create_descriptor_pool(vr, &new_dspool->pool, total_count, total_maxsets);
	wl_list_insert(&fr->dspool_list, &new_dspool->link);
}

static int
find_memory_type(struct vulkan_renderer *vr, uint32_t allowed, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties mem_properties;
	vkGetPhysicalDeviceMemoryProperties(vr->phys_dev, &mem_properties);

	for (unsigned i = 0; (1u << i) <= allowed && i <= mem_properties.memoryTypeCount; ++i) {
		if ((allowed & (1u << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties))
			return i;
	}
	return -1;
}

static void
create_buffer(struct vulkan_renderer *vr, VkDeviceSize size,
	      VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	      VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkResult result;

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	result = vkCreateBuffer(vr->dev, &buffer_info, NULL, buffer);
	check_vk_success(result, "vkCreateBuffer");

	VkMemoryRequirements mem_requirements;
	vkGetBufferMemoryRequirements(vr->dev, *buffer, &mem_requirements);

	int memory_type = find_memory_type(vr, mem_requirements.memoryTypeBits, properties);
	assert(memory_type >= 0);

	const VkMemoryAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_requirements.size,
		.memoryTypeIndex = memory_type,
	};

	result = vkAllocateMemory(vr->dev, &alloc_info, NULL, memory);
	check_vk_success(result, "vkAllocateMemory");

	result = vkBindBufferMemory(vr->dev, *buffer, *memory, 0);
	check_vk_success(result, "vkBindBufferMemory");
}

static void
create_vs_ubo_buffer(struct vulkan_renderer *vr, VkBuffer *vs_ubo_buffer,
		     VkDeviceMemory *vs_ubo_memory, void **vs_ubo_map)
{
	VkResult result;
	VkDeviceSize buffer_size = sizeof(struct vs_ubo);

	create_buffer(vr, buffer_size,
		      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      vs_ubo_buffer, vs_ubo_memory);

	result = vkMapMemory(vr->dev, *vs_ubo_memory, 0, VK_WHOLE_SIZE, 0, vs_ubo_map);
	check_vk_success(result, "vkMapMemory");
}

static void
create_fs_ubo_buffer(struct vulkan_renderer *vr, VkBuffer *fs_ubo_buffer,
		     VkDeviceMemory *fs_ubo_memory, void **fs_ubo_map)
{
	VkResult result;
	VkDeviceSize buffer_size = sizeof(struct fs_ubo);

	create_buffer(vr, buffer_size,
		      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      fs_ubo_buffer, fs_ubo_memory);

	result = vkMapMemory(vr->dev, *fs_ubo_memory, 0, VK_WHOLE_SIZE, 0, fs_ubo_map);
	check_vk_success(result, "vkMapMemory");
}

/*
 * Allocates new vertex buffers on demand or reuse current buffers if there
 * is still space available
 */
static struct vulkan_renderer_frame_vbuf *
get_vertex_buffer(struct vulkan_renderer *vr, struct vulkan_renderer_frame *fr, uint64_t size)
{
	const uint32_t base_size = 4096;
	VkResult result;

	if (!wl_list_empty(&fr->vbuf_list)) {
		struct vulkan_renderer_frame_vbuf *first = wl_container_of(fr->vbuf_list.next, first, link);
		if (first->size >= first->offset + size)
			return first;
	}

	struct vulkan_renderer_frame_vbuf *new_vbuf = xzalloc(sizeof(*new_vbuf));

	VkDeviceSize buffer_size = MAX(base_size, round_up_pow2_32(size));
	new_vbuf->size = buffer_size;

	create_buffer(vr, new_vbuf->size,
		      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      &new_vbuf->buffer, &new_vbuf->memory);

	result = vkMapMemory(vr->dev, new_vbuf->memory, 0, VK_WHOLE_SIZE, 0, &new_vbuf->map);
	check_vk_success(result, "vkMapMemory");

	wl_list_insert(&fr->vbuf_list, &new_vbuf->link);

	return new_vbuf;
}

/*
 * Resets vertex buffer offset so it can be reused; or coalesces multiple
 * vertex buffers into a single larger new one if multiple were dynamically
 * allocated in the previous use of this frame
 */
static void
reset_vertex_buffers(struct vulkan_renderer *vr, struct vulkan_renderer_frame *fr)
{
	if (wl_list_empty(&fr->vbuf_list))
		return;

	if (wl_list_length(&fr->vbuf_list) == 1) {
		struct vulkan_renderer_frame_vbuf *first = wl_container_of(fr->vbuf_list.next, first, link);
		first->offset = 0;
		return;
	}

	struct vulkan_renderer_frame_vbuf *vbuf, *tmp;
	uint64_t total_size = 0;
	wl_list_for_each_safe(vbuf, tmp, &fr->vbuf_list, link) {
		total_size += vbuf->size;
		wl_list_remove(&vbuf->link);
		destroy_buffer(vr->dev, vbuf->buffer, vbuf->memory);
		free(vbuf);
	}

	total_size = round_up_pow2_32(total_size);

	get_vertex_buffer(vr, fr, total_size);
}

static int
vulkan_renderer_create_surface(struct weston_surface *surface)
{
	struct vulkan_surface_state *vs;
	struct vulkan_renderer *vr = get_renderer(surface->compositor);

	vs = xzalloc(sizeof(*vs));

	vs->surface = surface;

	surface->renderer_state = vs;

	vs->surface_destroy_listener.notify =
		surface_state_handle_surface_destroy;
	wl_signal_add(&surface->destroy_signal,
		      &vs->surface_destroy_listener);

	vs->renderer_destroy_listener.notify =
		surface_state_handle_renderer_destroy;
	wl_signal_add(&vr->destroy_signal,
		      &vs->renderer_destroy_listener);

	return 0;
}

static inline struct vulkan_surface_state *
get_surface_state(struct weston_surface *surface)
{
	if (!surface->renderer_state)
		vulkan_renderer_create_surface(surface);

	return (struct vulkan_surface_state *)surface->renderer_state;
}

static void
create_image(struct vulkan_renderer *vr,
	     uint32_t width, uint32_t height,
	     VkFormat format, VkImageTiling tiling,
	     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
	     VkImage *image, VkDeviceMemory *memory)
{
	VkResult result;

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	result = vkCreateImage(vr->dev, &image_info, NULL, image);
	check_vk_success(result, "vkCreateImage");

	VkMemoryRequirements mem_requirements;
	vkGetImageMemoryRequirements(vr->dev, *image, &mem_requirements);

	int memory_type = find_memory_type(vr, mem_requirements.memoryTypeBits, properties);
	assert(memory_type >= 0);

	const VkMemoryAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_requirements.size,
		.memoryTypeIndex = memory_type,
	};

	result = vkAllocateMemory(vr->dev, &alloc_info, NULL, memory);
	check_vk_success(result, "vkAllocateMemory");

	result = vkBindImageMemory(vr->dev, *image, *memory, 0);
	check_vk_success(result, "vkBindImageMemory");
}

static void
create_framebuffer(VkDevice device, VkRenderPass renderpass, VkImageView image_view,
		   uint32_t width, uint32_t height, VkFramebuffer *framebuffer)
{
	VkResult result;

	const VkFramebufferCreateInfo framebuffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = renderpass,
		.attachmentCount = 1,
		.pAttachments = &image_view,
		.width = width,
		.height = height,
		.layers = 1,
	};

	result = vkCreateFramebuffer(device, &framebuffer_create_info, NULL, framebuffer);
	check_vk_success(result, "vkCreateFramebuffer");
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

static void
copy_sub_image_to_buffer(VkCommandBuffer cmd_buffer,
			 VkBuffer buffer, VkImage image,
			 uint32_t buffer_width, uint32_t buffer_height,
			 uint32_t pitch,
			 uint32_t bpp,
			 uint32_t xoff, uint32_t yoff,
			 uint32_t xcopy, uint32_t ycopy)
{
	const VkOffset3D image_offset = { xoff, yoff };
	const VkExtent3D image_extent = { xcopy, ycopy, 1 };

	const VkBufferImageCopy region = {
		.bufferOffset = ((buffer_width * yoff) + xoff) * (bpp/8),
		.bufferRowLength = pitch,
		.bufferImageHeight = buffer_height,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = image_offset,
		.imageExtent = image_extent,
	};

	vkCmdCopyImageToBuffer(cmd_buffer,
			       image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			       buffer, 1, &region);
}

static void
vulkan_renderer_cmd_begin(struct vulkan_renderer *vr,
			       VkCommandBuffer *cmd_buffer)
{
	VkResult result;

	const VkCommandBufferAllocateInfo cmd_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = vr->cmd_pool,
		.commandBufferCount = 1,
	};

	result = vkAllocateCommandBuffers(vr->dev, &cmd_alloc_info, cmd_buffer);
	check_vk_success(result, "vkAllocateCommandBuffers");

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	result = vkBeginCommandBuffer(*cmd_buffer, &begin_info);
	check_vk_success(result, "vkBeginCommandBuffer");
}

static void
vulkan_renderer_cmd_end_wait(struct vulkan_renderer *vr,
			     VkCommandBuffer *cmd_buffer)
{
	VkResult result;

	result = vkEndCommandBuffer(*cmd_buffer);
	check_vk_success(result, "vkEndCommandBuffer");

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = cmd_buffer,
	};

	result = vkQueueSubmit(vr->queue, 1, &submit_info, VK_NULL_HANDLE);
	check_vk_success(result, "vkQueueSubmit");

	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	vkFreeCommandBuffers(vr->dev, vr->cmd_pool, 1, cmd_buffer);
}

static bool
vulkan_renderer_do_read_pixels(struct vulkan_renderer *vr,
			       VkImage color_attachment,
			       struct vulkan_output_state *vo,
			       const struct pixel_format_info *pixel_format,
			       void *pixels, int stride,
			       const struct weston_geometry *rect)
{
	VkBuffer dst_buffer;
	VkDeviceMemory dst_memory;
	VkDeviceSize buffer_size = stride * vo->fb_size.height;
	VkResult result;

	create_buffer(vr, buffer_size,
		      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      &dst_buffer, &dst_memory);

	// TODO: async implementation of this
	VkCommandBuffer cmd_buffer;
	vulkan_renderer_cmd_begin(vr, &cmd_buffer);

	transition_image_layout(cmd_buffer, color_attachment,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, VK_ACCESS_TRANSFER_WRITE_BIT);

	copy_sub_image_to_buffer(cmd_buffer,
				 dst_buffer, color_attachment,
				 vo->fb_size.width, vo->fb_size.height,
				 (stride / (pixel_format->bpp/8)),
				 pixel_format->bpp,
				 rect->x, rect->y,
				 rect->width, rect->height);

	transition_image_layout(cmd_buffer, color_attachment,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, VK_ACCESS_TRANSFER_WRITE_BIT);

	// TODO: async implementation of this, remove wait
	vulkan_renderer_cmd_end_wait(vr, &cmd_buffer);

	/* Map image memory so we can start copying from it */
	void* buffer_map;
	result = vkMapMemory(vr->dev, dst_memory, 0, VK_WHOLE_SIZE, 0, &buffer_map);
	check_vk_success(result, "vkMapMemory");

	/* The captured buffer cannot be just memcpy'ed to the destination as
	 * it might overwrite existing pixels outside of the capture region,
	 * so use a pixman composition. */
	pixman_image_t *image_src;
	image_src = pixman_image_create_bits_no_clear(pixel_format->pixman_format,
						      vo->fb_size.width, vo->fb_size.height,
						      buffer_map, stride);

	pixman_image_t *image_dst;
	image_dst = pixman_image_create_bits_no_clear(pixel_format->pixman_format,
						      vo->fb_size.width, vo->fb_size.height,
						      pixels, stride);

	pixman_image_composite32(PIXMAN_OP_SRC,
				 image_src,        /* src */
				 NULL,             /* mask */
				 image_dst,        /* dest */
				 rect->x, rect->y, /* src x,y */
				 0, 0,             /* mask x,y */
				 rect->x, rect->y, /* dest x,y */
				 rect->width, rect->height);

	pixman_image_unref(image_src);
	pixman_image_unref(image_dst);

	destroy_buffer(vr->dev, dst_buffer, dst_memory);

	return true;
}

static bool
vulkan_renderer_do_capture(struct vulkan_renderer *vr,
			   VkImage color_attachment,
			   struct vulkan_output_state *vo,
			   struct weston_buffer *into,
			   const struct weston_geometry *rect)
{
	struct wl_shm_buffer *shm = into->shm_buffer;
	const struct pixel_format_info *pixel_format = into->pixel_format;
	bool ret;

	assert(into->type == WESTON_BUFFER_SHM);
	assert(shm);

	wl_shm_buffer_begin_access(shm);

	ret = vulkan_renderer_do_read_pixels(vr, color_attachment, vo, pixel_format,
					     wl_shm_buffer_get_data(shm), into->stride, rect);

	wl_shm_buffer_end_access(shm);

	return ret;
}

static void
vulkan_renderer_do_capture_tasks(struct vulkan_renderer *vr,
				 VkImage color_attachment,
				 struct weston_output *output,
				 enum weston_output_capture_source source)
{
	struct vulkan_output_state *vo = get_output_state(output);
	const struct pixel_format_info *pixel_format;
	struct weston_capture_task *ct;
	struct weston_geometry rect;

	switch (source) {
	case WESTON_OUTPUT_CAPTURE_SOURCE_FRAMEBUFFER:
		pixel_format = output->compositor->read_format;
		rect = vo->area;
		break;
	case WESTON_OUTPUT_CAPTURE_SOURCE_FULL_FRAMEBUFFER:
		pixel_format = output->compositor->read_format;
		rect.x = 0;
		rect.y = 0;
		rect.width = vo->fb_size.width;
		rect.height = vo->fb_size.height;
		break;
	default:
		assert(0);
		return;
	}

	while ((ct = weston_output_pull_capture_task(output, source, rect.width,
						     rect.height, pixel_format))) {
		struct weston_buffer *buffer = weston_capture_task_get_buffer(ct);

		assert(buffer->width == rect.width);
		assert(buffer->height == rect.height);
		assert(buffer->pixel_format->format == pixel_format->format);

		if (buffer->type != WESTON_BUFFER_SHM ||
		    buffer->buffer_origin != ORIGIN_TOP_LEFT) {
			weston_capture_task_retire_failed(ct, "Vulkan: unsupported buffer");
			continue;
		}

		if (buffer->stride % 4 != 0) {
			weston_capture_task_retire_failed(ct, "Vulkan: buffer stride not multiple of 4");
			continue;
		}

		if (vulkan_renderer_do_capture(vr, color_attachment, vo, buffer, &rect))
			weston_capture_task_retire_complete(ct);
		else
			weston_capture_task_retire_failed(ct, "Vulkan: capture failed");
	}
}

static bool
vulkan_pipeline_texture_variant_can_be_premult(enum vulkan_pipeline_texture_variant v)
{
	switch (v) {
	case PIPELINE_VARIANT_SOLID:
	case PIPELINE_VARIANT_RGBA:
	case PIPELINE_VARIANT_EXTERNAL:
		return true;
	case PIPELINE_VARIANT_RGBX:
		return false;
	case PIPELINE_VARIANT_NONE:
	default:
		abort();
	}
	return true;
}

static bool
vulkan_pipeline_config_init_for_paint_node(struct vulkan_pipeline_config *pconf,
					   struct weston_paint_node *pnode)
{
	struct vulkan_output_state *vo = get_output_state(pnode->output);
	struct vulkan_surface_state *vs = get_surface_state(pnode->surface);
	struct vulkan_buffer_state *vb = vs->buffer;
	struct weston_buffer *buffer = vs->buffer_ref.buffer;

	if (!pnode->surf_xform_valid)
		return false;

	*pconf = (struct vulkan_pipeline_config) {
		.req = {
			.texcoord_input = SHADER_TEXCOORD_INPUT_SURFACE,
			.renderpass = vo->renderpass,
		},
		.projection = pnode->view->transform.matrix,
		.surface_to_buffer =
			pnode->view->surface->surface_to_buffer_matrix,
		.view_alpha = pnode->view->alpha,
	};

	weston_matrix_multiply(&pconf->projection, &vo->output_matrix);

	if (buffer->buffer_origin == ORIGIN_TOP_LEFT) {
		weston_matrix_scale(&pconf->surface_to_buffer,
				    1.0f / buffer->width,
				    1.0f / buffer->height, 1);
	} else {
		weston_matrix_scale(&pconf->surface_to_buffer,
				    1.0f / buffer->width,
				    -1.0f / buffer->height, 1);
		weston_matrix_translate(&pconf->surface_to_buffer, 0, 1, 0);
	}

	pconf->req.variant = vb->pipeline_variant;
	pconf->req.input_is_premult =
		vulkan_pipeline_texture_variant_can_be_premult(vb->pipeline_variant);

	for (int i = 0; i < 4; i++)
		pconf->unicolor[i] = vb->color[i];

	return true;
}

static void
rect_to_quad(pixman_box32_t *rect,
	     struct weston_view *ev,
	     struct clipper_quad *quad)
{
	struct weston_coord_global rect_g[4] = {
		{ .c = weston_coord(rect->x1, rect->y1) },
		{ .c = weston_coord(rect->x2, rect->y1) },
		{ .c = weston_coord(rect->x2, rect->y2) },
		{ .c = weston_coord(rect->x1, rect->y2) },
	};
	struct weston_coord rect_s;

	/* Transform rect to surface space. */
	for (int i = 0; i < 4; i++) {
		rect_s = weston_coord_global_to_surface(ev, rect_g[i]).c;
		quad->polygon[i].x = (float)rect_s.x;
		quad->polygon[i].y = (float)rect_s.y;
	}

	quad->axis_aligned = !ev->transform.enabled ||
		(ev->transform.matrix.type < WESTON_MATRIX_TRANSFORM_ROTATE);

	// TODO handle !axis_aligned ?
	assert(quad->axis_aligned);
}

static uint32_t
generate_fans(struct weston_paint_node *pnode,
	      pixman_region32_t *region,
	      pixman_region32_t *surf_region,
	      struct wl_array *vertices,
	      struct wl_array *vtxcnt)
{
	struct weston_view *ev = pnode->view;
	struct clipper_vertex *v;
	uint32_t *cnt;
	uint32_t nvtx = 0;
	pixman_box32_t *rects;
	pixman_box32_t *surf_rects;
	int nrects;
	int nsurf;
	struct clipper_quad quad;

	rects = pixman_region32_rectangles(region, &nrects);
	surf_rects = pixman_region32_rectangles(surf_region, &nsurf);

	/* worst case we can have 8 vertices per rect (ie. clipped into
	 * an octagon) */
	v = wl_array_add(vertices, nrects * nsurf * 8 * sizeof(struct clipper_vertex));
	cnt = wl_array_add(vtxcnt, nrects * nsurf * sizeof(uint32_t));

	for (int i = 0; i < nrects; i++) {
		rect_to_quad(&rects[i], ev, &quad);
		for (int j = 0; j < nsurf; j++) {
			uint32_t n;

			/* The transformed quad, after clipping to the surface rect, can
			 * have as many as eight sides, emitted as a triangle-fan. The
			 * first vertex in the triangle fan can be chosen arbitrarily,
			 * since the area is guaranteed to be convex.
			 *
			 * If a corner of the transformed quad falls outside of the
			 * surface rect, instead of emitting one vertex, up to two are
			 * emitted for two corresponding intersection point(s) between the
			 * edges.
			 *
			 * To do this, we first calculate the (up to eight) points at the
			 * intersection of the edges of the quad and the surface rect.
			 */
			n = clipper_quad_clip_box32(&quad, &surf_rects[j], v);
			if (n >= 3) {
				v += n;
				cnt[nvtx++] = n;
			}
		}
	}

	return nvtx;
}

static void
repaint_region(struct vulkan_renderer *vr,
	       struct weston_paint_node *pnode,
	       pixman_region32_t *region,
	       pixman_region32_t *surf_region,
	       const struct vulkan_pipeline_config *pconf,
	       struct vulkan_renderer_frame *fr)
{
	struct vulkan_surface_state *vs = get_surface_state(pnode->surface);
	struct vulkan_buffer_state *vb = vs->buffer;
	struct vulkan_pipeline *pipeline;
	VkCommandBuffer cmd_buffer = fr->cmd_buffer;
	uint32_t nfans;

	struct wl_array vertices;
	struct wl_array vtxcnt;
	wl_array_init(&vertices);
	wl_array_init(&vtxcnt);

	/* The final region to be painted is the intersection of 'region' and
	 * 'surf_region'. However, 'region' is in the global coordinates, and
	 * 'surf_region' is in the surface-local coordinates.
	 * generate_fans() will iterate over all pairs of rectangles from both
	 * regions, compute the intersection polygon for each pair, and store
	 * it as a triangle fan if it has a non-zero area (at least 3 vertices,
	 * actually).
	 */
	nfans = generate_fans(pnode, region, surf_region, &vertices, &vtxcnt);

	struct vulkan_renderer_frame_vbuf *vbuf = get_vertex_buffer(vr, fr, vertices.size);

	pipeline = vulkan_renderer_get_pipeline(vr, &pconf->req);
	assert(pipeline);

	vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
	memcpy(vbuf->map + vbuf->offset, vertices.data, vertices.size);

	vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vbuf->buffer, &vbuf->offset);

	memcpy(vb->vs_ubo_map + offsetof(struct vs_ubo, proj),
	       pconf->projection.M.colmaj, sizeof(pconf->projection.M.colmaj));
	memcpy(vb->vs_ubo_map + offsetof(struct vs_ubo, surface_to_buffer),
	       pconf->surface_to_buffer.M.colmaj, sizeof(pconf->surface_to_buffer.M.colmaj));
	memcpy(vb->fs_ubo_map + offsetof(struct fs_ubo, unicolor),
	       pconf->unicolor, sizeof(pconf->unicolor));
	memcpy(vb->fs_ubo_map + offsetof(struct fs_ubo, view_alpha),
	       &pconf->view_alpha, sizeof(pconf->view_alpha));

	vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline->pipeline_layout, 0, 1, &vb->descriptor_set, 0, NULL);

	for (uint32_t i = 0, first = 0; i < nfans; i++) {
		const uint32_t *vtxcntp = vtxcnt.data;
		vkCmdDraw(cmd_buffer, vtxcntp[i], 1, first, 0);
		first += vtxcntp[i];
	}

	vbuf->offset += vertices.size;

	wl_array_release(&vertices);
	wl_array_release(&vtxcnt);
}

static int
ensure_surface_buffer_is_ready(struct vulkan_renderer *vr,
			       struct vulkan_surface_state *vs,
			       struct vulkan_renderer_frame *fr)
{
	struct weston_surface *surface = vs->surface;
	struct weston_buffer *buffer = vs->buffer_ref.buffer;
	int acquire_fence_fd;
	VkResult result;

	if (!buffer)
		return 0;

	if (surface->acquire_fence_fd < 0)
		return 0;

	/* We should only get a fence for non-SHM buffers, since surface
	 * commit would have failed otherwise. */
	assert(buffer->type != WESTON_BUFFER_SHM);

	acquire_fence_fd = dup(surface->acquire_fence_fd);
	if (acquire_fence_fd == -1) {
		linux_explicit_synchronization_send_server_error(
			vs->surface->synchronization_resource,
			"Failed to dup acquire fence");
		return -1;
	}

	struct vulkan_renderer_frame_acquire_fence *acquire_fence;
	acquire_fence = xzalloc(sizeof(*acquire_fence));

	const VkSemaphoreCreateInfo semaphore_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	result = vkCreateSemaphore(vr->dev, &semaphore_info, NULL,
				   &acquire_fence->semaphore);
	check_vk_success(result, "vkCreateSemaphore");
	if (result != VK_SUCCESS) {
		linux_explicit_synchronization_send_server_error(
			vs->surface->synchronization_resource,
			"vkCreateSemaphore");
		close(acquire_fence_fd);
		return -1;
	}

	const VkImportSemaphoreFdInfoKHR import_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
		.semaphore = acquire_fence->semaphore,
		.fd = acquire_fence_fd,
	};
	result = vr->import_semaphore_fd(vr->dev, &import_info);
	check_vk_success(result, "vkImportSemaphoreFdKHR");
	if (result != VK_SUCCESS) {
		linux_explicit_synchronization_send_server_error(
			vs->surface->synchronization_resource,
			"vkImportSemaphoreFdKHR");
		close(acquire_fence_fd);
		return -1;
	}

	wl_list_insert(&fr->acquire_fence_list, &acquire_fence->link);

	return 0;
}

static void
draw_paint_node(struct weston_paint_node *pnode,
		pixman_region32_t *damage, /* in global coordinates */
		struct vulkan_renderer_frame *fr)
{
	struct vulkan_renderer *vr = get_renderer(pnode->surface->compositor);
	struct vulkan_surface_state *vs = get_surface_state(pnode->surface);
	struct vulkan_buffer_state *vb = vs->buffer;
	struct weston_buffer *buffer = vs->buffer_ref.buffer;
	/* repaint bounding region in global coordinates: */
	pixman_region32_t repaint;
	/* opaque region in surface coordinates: */
	pixman_region32_t surface_opaque;
	/* non-opaque region in surface coordinates: */
	pixman_region32_t surface_blend;
	struct vulkan_pipeline_config pconf;
	struct vulkan_pipeline *pipeline;

	if (vb->pipeline_variant == PIPELINE_VARIANT_NONE &&
	    !buffer->direct_display)
		return;

	pixman_region32_init(&repaint);
	pixman_region32_intersect(&repaint, &pnode->visible, damage);

	if (!pixman_region32_not_empty(&repaint))
		goto out;

	if (!pnode->draw_solid && ensure_surface_buffer_is_ready(vr, vs, fr) < 0)
		goto out;

	if (!vulkan_pipeline_config_init_for_paint_node(&pconf, pnode)) {
		goto out;
	}

	pipeline = vulkan_renderer_get_pipeline(vr, &pconf.req);
	assert(pipeline);

	VkSampler sampler;
	VkImageView image_view;
	if (vb->texture.image_view) {
		image_view = vb->texture.image_view;
		sampler = pnode->needs_filtering ? vb->sampler_linear : vb->sampler_nearest;
	} else {
		image_view = vr->dummy.image.image_view;
		sampler = vr->dummy.sampler;
	}
	create_descriptor_set(vr, fr, &pipeline->descriptor_set_layout,
			      vb->vs_ubo_buffer, vb->fs_ubo_buffer,
			      image_view, sampler,
			      &vb->descriptor_set);

	/* XXX: Should we be using ev->transform.opaque here? */
	if (pnode->is_fully_opaque) {
		pixman_region32_init_rect(&surface_opaque, 0, 0,
					  pnode->surface->width,
					  pnode->surface->height);
	} else {
		pixman_region32_init(&surface_opaque);
		pixman_region32_copy(&surface_opaque, &pnode->surface->opaque);
	}

	if (pnode->view->geometry.scissor_enabled)
		pixman_region32_intersect(&surface_opaque,
					  &surface_opaque,
					  &pnode->view->geometry.scissor);

	/* blended region is whole surface minus opaque region: */
	pixman_region32_init_rect(&surface_blend, 0, 0,
				  pnode->surface->width, pnode->surface->height);
	if (pnode->view->geometry.scissor_enabled)
		pixman_region32_intersect(&surface_blend, &surface_blend,
					  &pnode->view->geometry.scissor);
	pixman_region32_subtract(&surface_blend, &surface_blend,
				 &surface_opaque);

	if (pixman_region32_not_empty(&surface_opaque)) {
		struct vulkan_pipeline_config alt = pconf;

		if (alt.req.variant == PIPELINE_VARIANT_RGBA)
			alt.req.variant = PIPELINE_VARIANT_RGBX;

		alt.req.blend = (pnode->view->alpha < 1.0);

		repaint_region(vr, pnode, &repaint, &surface_opaque, &alt, fr);
		vs->used_in_output_repaint = true;
	}

	pconf.req.blend = true;
	if (pixman_region32_not_empty(&surface_blend)) {
		repaint_region(vr, pnode, &repaint, &surface_blend, &pconf, fr);
		vs->used_in_output_repaint = true;
	}

	pixman_region32_fini(&surface_blend);
	pixman_region32_fini(&surface_opaque);

out:
	pixman_region32_fini(&repaint);
}

static void
repaint_views(struct weston_output *output, pixman_region32_t *damage,
	      struct vulkan_renderer_frame *fr)
{
	struct weston_paint_node *pnode;

	wl_list_for_each_reverse(pnode, &output->paint_node_z_order_list,
				 z_order_link) {
		if (pnode->plane == &output->primary_plane)
			draw_paint_node(pnode, damage, fr);
	}
}

static void
vulkan_renderbuffer_init(struct vulkan_renderbuffer *renderbuffer,
			 struct vulkan_renderer_image *image,
			 weston_renderbuffer_discarded_func discarded_cb,
			 void *user_data,
			 struct weston_output *output)
{
	struct vulkan_output_state *vo = get_output_state(output);

	renderbuffer->output = output;
	pixman_region32_init(&renderbuffer->damage);
	pixman_region32_copy(&renderbuffer->damage, &output->region);
	renderbuffer->border_status = BORDER_ALL_DIRTY;
	renderbuffer->discarded_cb = discarded_cb;
	renderbuffer->user_data = user_data;
	renderbuffer->image = image;

	wl_list_insert(&vo->renderbuffer_list, &renderbuffer->link);
}

static void
vulkan_renderer_update_renderbuffers(struct weston_output *output,
				     pixman_region32_t *damage)
{
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderbuffer *rb;

	/* Accumulate changes in non-stale renderbuffers. */
	wl_list_for_each(rb, &vo->renderbuffer_list, link) {
		if (rb->stale)
			continue;

		pixman_region32_union(&rb->damage, &rb->damage, damage);
		rb->border_status |= vo->border_status;
	}
}

static struct weston_geometry
output_get_border_area(const struct vulkan_output_state *vo,
		       enum weston_renderer_border_side side)
{
	const struct weston_size *fb_size = &vo->fb_size;
	const struct weston_geometry *area = &vo->area;

	switch (side) {
	case WESTON_RENDERER_BORDER_TOP:
		return (struct weston_geometry){
			.x = 0,
			.y = 0,
			.width = fb_size->width,
			.height = area->y
		};
	case WESTON_RENDERER_BORDER_LEFT:
		return (struct weston_geometry){
			.x = 0,
			.y = area->y,
			.width = area->x,
			.height = area->height
		};
	case WESTON_RENDERER_BORDER_RIGHT:
		return (struct weston_geometry){
			.x = area->x + area->width,
			.y = area->y,
			.width = fb_size->width - area->x - area->width,
			.height = area->height
		};
	case WESTON_RENDERER_BORDER_BOTTOM:
		return (struct weston_geometry){
			.x = 0,
			.y = area->y + area->height,
			.width = fb_size->width,
			.height = fb_size->height - area->y - area->height
		};
	}

	abort();
	return (struct weston_geometry){};
}

static int
vulkan_renderer_create_fence_fd(struct weston_output *output)
{
	struct vulkan_output_state *vo = get_output_state(output);

	return dup(vo->render_fence_fd);
}

static void
vulkan_renderer_allocator_destroy(struct dmabuf_allocator *allocator)
{
	if (!allocator)
		return;

	if (allocator->gbm_device)
		gbm_device_destroy(allocator->gbm_device);

	free(allocator);
}

static struct dmabuf_allocator *
vulkan_renderer_allocator_create(struct vulkan_renderer *vr,
			     const struct vulkan_renderer_display_options * options)
{
	struct dmabuf_allocator *allocator;
	struct gbm_device *gbm = NULL;

	if (vr->drm_fd)
		gbm = gbm_create_device(vr->drm_fd);

	if (!gbm)
		return NULL;

	allocator = xzalloc(sizeof(*allocator));
	allocator->gbm_device = gbm;

	return allocator;
}

/* Updates the release fences of surfaces that were used in the current output
 * repaint. Should only be used from vulkan_renderer_repaint_output, so that the
 * information in vulkan_surface_state.used_in_output_repaint is accurate.
 */
static void
update_buffer_release_fences(struct weston_compositor *compositor,
			     struct weston_output *output)
{
	struct weston_paint_node *pnode;

	wl_list_for_each_reverse(pnode, &output->paint_node_z_order_list,
				 z_order_link) {
		struct vulkan_surface_state *vs;
		struct weston_buffer_release *buffer_release;
		int fence_fd;

		if (pnode->plane != &output->primary_plane)
			continue;

		if (pnode->draw_solid)
			continue;

		vs = get_surface_state(pnode->surface);
		buffer_release = vs->buffer_release_ref.buffer_release;

		if (!vs->used_in_output_repaint || !buffer_release)
			continue;

		fence_fd = vulkan_renderer_create_fence_fd(output);

		/* If we have a buffer_release then it means we support fences,
		 * and we should be able to create the release fence. If we
		 * can't, something has gone horribly wrong, so disconnect the
		 * client.
		 */
		if (fence_fd == -1) {
			linux_explicit_synchronization_send_server_error(
				buffer_release->resource,
				"Failed to create release fence");
			fd_clear(&buffer_release->fence_fd);
			continue;
		}

		/* At the moment it is safe to just replace the fence_fd,
		 * discarding the previous one:
		 *
		 * 1. If the previous fence fd represents a sync fence from
		 *    a previous repaint cycle, that fence fd is now not
		 *    sufficient to provide the release guarantee and should
		 *    be replaced.
		 *
		 * 2. If the fence fd represents a sync fence from another
		 *    output in the same repaint cycle, it's fine to replace
		 *    it since we are rendering to all outputs using the same
		 *    EGL context, so a fence issued for a later output rendering
		 *    is guaranteed to signal after fences for previous output
		 *    renderings.
		 *
		 * Note that the above is only valid if the buffer_release
		 * fences only originate from the GL renderer, which guarantees
		 * a total order of operations and fences.  If we introduce
		 * fences from other sources (e.g., plane out-fences), we will
		 * need to merge fences instead.
		 */
		fd_update(&buffer_release->fence_fd, fence_fd);
	}
}

static void
draw_output_border_texture(struct vulkan_renderer *vr,
			   struct vulkan_output_state *vo,
			   struct vulkan_pipeline_config *pconf,
			   enum weston_renderer_border_side side,
			   int32_t x, int32_t y,
			   int32_t width, int32_t height,
			   VkCommandBuffer cmd_buffer,
			   struct vulkan_renderer_frame *fr)
{
	struct vulkan_border_image *border = &vo->borders[side];
	struct vulkan_pipeline *pipeline;

	if (!border->data)
		return;

	float position[] = {
		x, y, 0.0f, 0.0f,
		x + width, y, (float)border->width / (float)border->tex_width, 0.0f,
		x + width, y + height, (float)border->width / (float)border->tex_width, 1.0f,
		x, y + height, 0.0f, 1.0f,
	};

	struct vulkan_renderer_frame_vbuf *vbuf = get_vertex_buffer(vr, fr, sizeof(position));

	pipeline = vulkan_renderer_get_pipeline(vr, &pconf->req);
	assert(pipeline);

	create_descriptor_set(vr, fr, &pipeline->descriptor_set_layout,
			      border->vs_ubo_buffer, border->fs_ubo_buffer,
			      border->texture.image_view, border->sampler,
			      &border->descriptor_set);

	vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
	memcpy(vbuf->map + vbuf->offset, position, sizeof(position));

	vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vbuf->buffer, &vbuf->offset);

	memcpy(border->vs_ubo_map + offsetof(struct vs_ubo, proj),
	       pconf->projection.M.colmaj, sizeof(pconf->projection.M.colmaj));
	memset(border->vs_ubo_map + offsetof(struct vs_ubo, surface_to_buffer),
	       0, sizeof(pconf->surface_to_buffer.M.colmaj));
	memcpy(border->fs_ubo_map + offsetof(struct fs_ubo, unicolor),
	       pconf->unicolor, sizeof(pconf->unicolor));
	memcpy(border->fs_ubo_map + offsetof(struct fs_ubo, view_alpha),
	       &pconf->view_alpha, sizeof(pconf->view_alpha));

	vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline->pipeline_layout, 0, 1, &border->descriptor_set, 0, NULL);

	vkCmdDraw(cmd_buffer, 4, 1, 0, 0);

	vbuf->offset += sizeof(position);
}

static void
draw_output_borders(struct weston_output *output,
		    enum vulkan_border_status border_status,
		    VkCommandBuffer cmd_buffer,
		    struct vulkan_renderer_frame *fr)
{
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(output->compositor);
	const struct weston_size *fb_size = &vo->fb_size;
	enum vulkan_pipeline_texture_variant pipeline_variant;

	if (pixel_format_is_opaque(vo->pixel_format))
		pipeline_variant = PIPELINE_VARIANT_RGBX;
	else
		pipeline_variant = PIPELINE_VARIANT_RGBA;

	struct vulkan_pipeline_config pconf = {
		.req = {
			.texcoord_input = SHADER_TEXCOORD_INPUT_ATTRIB,
			.renderpass = vo->renderpass,
			.variant = pipeline_variant,
			.input_is_premult = true,
		},
		.view_alpha = 1.0f,
	};

	if (border_status == BORDER_STATUS_CLEAN)
		return; /* Clean. Nothing to do. */

	weston_matrix_init(&pconf.projection);

	weston_matrix_translate(&pconf.projection,
				-fb_size->width / 2.0, -fb_size->height / 2.0, 0);
	weston_matrix_scale(&pconf.projection,
			    2.0 / (float)fb_size->width, 2.0 / (float)fb_size->height, 1);

	const VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = fb_size->width,
		.height = fb_size->height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = { fb_size->width, fb_size->height },
	};
	vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

	for (unsigned side = 0; side < 4; side++) {
		struct weston_geometry g;

		if (!(border_status & (1 << side)))
			continue;

		g = output_get_border_area(vo, side);
		draw_output_border_texture(vr, vo, &pconf, side,
					   g.x, g.y, g.width, g.height, cmd_buffer, fr);
	}
}

static void
output_get_border_damage(struct weston_output *output,
			 enum vulkan_border_status border_status,
			 pixman_region32_t *damage)
{
	struct vulkan_output_state *vo = get_output_state(output);
	unsigned side;

	for (side = 0; side < 4; side++) {
		struct weston_geometry g;

		if (!(border_status & (1 << side)))
			continue;

		g = output_get_border_area(vo, side);
		pixman_region32_union_rect(damage, damage,
				g.x, g.y, g.width, g.height);
	}
}

static int
output_has_borders(struct weston_output *output)
{
	struct vulkan_output_state *vo = get_output_state(output);

	return vo->borders[WESTON_RENDERER_BORDER_TOP].data ||
		vo->borders[WESTON_RENDERER_BORDER_RIGHT].data ||
		vo->borders[WESTON_RENDERER_BORDER_BOTTOM].data ||
		vo->borders[WESTON_RENDERER_BORDER_LEFT].data;
}

static void
pixman_region_to_scissor(struct weston_output *output,
			 struct pixman_region32 *global_region,
			 enum vulkan_border_status border_status,
			 VkRect2D *scissor)
{
	struct vulkan_output_state *vo = get_output_state(output);
	pixman_region32_t transformed;
	struct pixman_box32 *box;

	/* Translate from global to output coordinate space. */
	pixman_region32_init(&transformed);
	weston_region_global_to_output(&transformed,
				       output,
				       global_region);

	/* If we have borders drawn around the output, shift our output damage
	 * to account for borders being drawn around the outside, adding any
	 * damage resulting from borders being redrawn. */
	if (output_has_borders(output)) {
		pixman_region32_translate(&transformed,
					  vo->area.x, vo->area.y);
		output_get_border_damage(output, border_status, &transformed);
	}

	/* Convert from a Pixman region into a VkRect2D */
	box = pixman_region32_extents(&transformed);

	const VkRect2D s = {
		.offset = { box->x1, box->y1 },
		.extent = { box->x2 - box->x1, box->y2 - box->y1 },
	};

	*scissor = s;
	pixman_region32_fini(&transformed);
}

static void
pixman_region_to_present_region(struct weston_output *output,
				struct pixman_region32 *global_region,
				enum vulkan_border_status border_status,
				uint32_t *nrects,
				VkRectLayerKHR **rects)
{
	struct vulkan_output_state *vo = get_output_state(output);
	pixman_region32_t transformed;

	/* Translate from global to output coordinate space. */
	pixman_region32_init(&transformed);
	weston_region_global_to_output(&transformed,
				       output,
				       global_region);

	/* If we have borders drawn around the output, shift our output damage
	 * to account for borders being drawn around the outside, adding any
	 * damage resulting from borders being redrawn. */
	if (output_has_borders(output)) {
		pixman_region32_translate(&transformed,
					  vo->area.x, vo->area.y);
		output_get_border_damage(output, border_status, &transformed);
	}

	int n;
	pixman_box32_t *r;
	r = pixman_region32_rectangles(&transformed, &n);
	VkRectLayerKHR *rect_layers = xmalloc(n * sizeof(*rect_layers));

	for (int i = 0; i < n; i++) {
		const pixman_box32_t *b = &r[i];
		const VkRectLayerKHR l = {
			.offset = { b->x1, b->y1 },
			.extent = { b->x2 - b->x1, b->y2 - b->y1 },
		};

		rect_layers[i] = l;
	}

	*nrects = (uint32_t)n;
	*rects = rect_layers;

	pixman_region32_fini(&transformed);
}

static void
create_image_semaphores(struct vulkan_renderer *vr,
			struct vulkan_output_state *vo,
			struct vulkan_renderer_image *image)
{
	VkResult result;

	VkSemaphoreCreateInfo semaphore_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	VkExportSemaphoreCreateInfo export_info = {
		.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
	};
	if (vr->semaphore_import_export && vo->output_type != VULKAN_OUTPUT_SWAPCHAIN)
		pnext(&semaphore_info, &export_info);

	result = vkCreateSemaphore(vr->dev, &semaphore_info, NULL, &image->render_done);
	check_vk_success(result, "vkCreateSemaphore render_done");
}

static void
vulkan_renderer_create_swapchain(struct weston_output *output,
				 struct weston_size fb_size)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_output_state *vo = get_output_state(output);
	const struct pixel_format_info *pixel_format = vo->pixel_format;
	const VkFormat format = pixel_format->vulkan_format;

	VkSurfaceCapabilitiesKHR surface_caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vr->phys_dev, vo->swapchain.surface, &surface_caps);

	uint32_t min_image_count = 2;
	if (min_image_count < surface_caps.minImageCount)
		min_image_count = surface_caps.minImageCount;

	if (surface_caps.maxImageCount > 0 && min_image_count > surface_caps.maxImageCount)
		min_image_count = surface_caps.maxImageCount;

	const VkExtent2D swapchain_extent = { fb_size.width, fb_size.height };
	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.flags = 0,
		.surface = vo->swapchain.surface,
		.minImageCount = min_image_count,
		.imageFormat = format,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = swapchain_extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &vr->queue_family,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.presentMode = vo->swapchain.present_mode,
	};
	if (surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
		swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
	else
		swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	vkCreateSwapchainKHR(vr->dev, &swapchain_create_info, NULL, &vo->swapchain.swapchain);

	vkGetSwapchainImagesKHR(vr->dev, vo->swapchain.swapchain, &vo->image_count, NULL);
	assert(vo->image_count > 0);
	VkImage swapchain_images[vo->image_count];
	vkGetSwapchainImagesKHR(vr->dev, vo->swapchain.swapchain, &vo->image_count, swapchain_images);

	// Command here only for the layout transitions
	VkCommandBuffer cmd_buffer;
	vulkan_renderer_cmd_begin(vr, &cmd_buffer);

	for (uint32_t i = 0; i < vo->image_count; i++) {
		struct vulkan_renderer_image *im = &vo->images[i];

		create_image_view(vr->dev, swapchain_images[i], format, &im->image_view);
		create_framebuffer(vr->dev, vo->renderpass, im->image_view,
				   fb_size.width, fb_size.height, &im->framebuffer);

		transition_image_layout(cmd_buffer, swapchain_images[i],
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, VK_ACCESS_TRANSFER_WRITE_BIT);

		create_image_semaphores(vr, vo, im);

		im->renderbuffer = xzalloc(sizeof(*im->renderbuffer));
		vulkan_renderbuffer_init(im->renderbuffer, NULL, NULL, NULL, output);
	}

	// Wait here is bad, but this is only on swapchain recreation
	vulkan_renderer_cmd_end_wait(vr, &cmd_buffer);
}

static void
vulkan_renderer_recreate_swapchain(struct weston_output *output,
				   struct weston_size fb_size)
{
	vulkan_renderer_destroy_swapchain(output);
	vulkan_renderer_create_swapchain(output, fb_size);
}

static void
vulkan_renderer_repaint_output(struct weston_output *output,
			       pixman_region32_t *output_damage,
			       weston_renderbuffer_t renderbuffer)
{
	struct weston_compositor *compositor = output->compositor;
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(compositor);
	struct weston_paint_node *pnode;
	VkResult result;
	uint32_t swapchain_index;

	assert(vo);
	assert(!renderbuffer ||
	       ((struct vulkan_renderbuffer *) renderbuffer)->output == output);

	struct vulkan_renderer_frame *fr = &vo->frames[vo->frame_index];

	assert(vo->frame_index < vo->num_frames);
	vkWaitForFences(vr->dev, 1, &vo->frames[vo->frame_index].fence, VK_TRUE, UINT64_MAX);
	vkResetFences(vr->dev, 1, &vo->frames[vo->frame_index].fence);

	struct vulkan_renderer_frame_acquire_fence *acquire_fence, *ftmp;
	wl_list_for_each_safe(acquire_fence, ftmp, &fr->acquire_fence_list, link) {
		vkDestroySemaphore(vr->dev, acquire_fence->semaphore, NULL);
		wl_list_remove(&acquire_fence->link);
		free(acquire_fence);
	}

	reset_vertex_buffers(vr, fr);

	reset_descriptor_pool(vr, fr);

	/* Clear the used_in_output_repaint flag, so that we can properly track
	 * which surfaces were used in this output repaint. */
	wl_list_for_each_reverse(pnode, &output->paint_node_z_order_list,
				 z_order_link) {
		if (pnode->plane == &output->primary_plane) {
			struct vulkan_surface_state *vs =
				get_surface_state(pnode->view->surface);
			vs->used_in_output_repaint = false;
		}
	}

	/* Calculate the global matrix */
	vo->output_matrix = output->matrix;
	weston_matrix_translate(&vo->output_matrix,
				-(vo->area.width / 2.0),
				-(vo->area.height / 2.0), 0);
	weston_matrix_scale(&vo->output_matrix,
			    2.0 / vo->area.width,
			    2.0 / vo->area.height, 1);

	struct vulkan_renderer_image *im;
	struct vulkan_renderbuffer *rb;
	switch(vo->output_type) {
	case VULKAN_OUTPUT_SWAPCHAIN:
		result = vkAcquireNextImageKHR(vr->dev, vo->swapchain.swapchain, UINT64_MAX,
					       fr->image_acquired, VK_NULL_HANDLE, &swapchain_index);
		if (result == VK_SUBOPTIMAL_KHR) {
			vulkan_renderer_recreate_swapchain(output, vo->fb_size);
		} else if (result != VK_SUCCESS) {
			abort();
		}

		im = &vo->images[swapchain_index];
		rb = im->renderbuffer;
		break;
	case VULKAN_OUTPUT_HEADLESS:
		assert(renderbuffer);
		rb = renderbuffer;
		im = rb->image;
		break;
	case VULKAN_OUTPUT_DRM:
		im = &vo->images[vo->drm.image_index];
		rb = im->renderbuffer;
		break;
	default:
		abort();
	}
	assert(rb && im);

	vulkan_renderer_update_renderbuffers(output, output_damage);

	VkCommandBuffer cmd_buffer = fr->cmd_buffer;
	VkFramebuffer framebuffer = im->framebuffer;

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
	check_vk_success(result, "vkBeginCommandBuffer");

	if (vo->output_type == VULKAN_OUTPUT_DRM) {
		// Transfer ownership of the dmabuf to Vulkan
		if (!vr->has_queue_family_foreign)
			abort();
		transfer_image_queue_family(cmd_buffer, im->image,
					    VK_QUEUE_FAMILY_FOREIGN_EXT,
					    vr->queue_family);
	}

	const struct weston_size *fb = &vo->fb_size;
	const VkRect2D render_area = {
		.offset = { 0, 0 },
		.extent = { fb->width, fb->height },
	};
	const VkRenderPassBeginInfo renderpass_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = vo->renderpass,
		.framebuffer = framebuffer,
		.renderArea = render_area,
	};
	vkCmdBeginRenderPass(cmd_buffer, &renderpass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = vo->area.x,
		.y = vo->area.y,
		.width = vo->area.width,
		.height = vo->area.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

	VkRect2D scissor;
	pixman_region_to_scissor(output, &rb->damage, rb->border_status, &scissor);
	vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

	repaint_views(output, &rb->damage, fr);

	draw_output_borders(output, rb->border_status, cmd_buffer, fr);

	wl_signal_emit(&output->frame_signal, output_damage);

	vkCmdEndRenderPass(cmd_buffer);

	if (vo->output_type == VULKAN_OUTPUT_DRM) {
		// Transfer ownership of the dmabuf to DRM
		if (!vr->has_queue_family_foreign)
			abort();
		transfer_image_queue_family(cmd_buffer, im->image,
					    vr->queue_family,
					    VK_QUEUE_FAMILY_FOREIGN_EXT);
	}

	result = vkEndCommandBuffer(cmd_buffer);
	check_vk_success(result, "vkEndCommandBuffer");

	uint32_t semaphore_count = wl_list_length(&fr->acquire_fence_list);
	VkPipelineStageFlags wait_stages[1+semaphore_count];
	VkSemaphore wait_semaphores[1+semaphore_count];

	uint32_t wait_count = 0;
	if (vo->output_type == VULKAN_OUTPUT_SWAPCHAIN) {
		wait_stages[wait_count] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		wait_semaphores[wait_count++] = fr->image_acquired;
	}
	wl_list_for_each(acquire_fence, &fr->acquire_fence_list, link) {
		wait_stages[wait_count] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		wait_semaphores[wait_count++] = acquire_fence->semaphore;
	}

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = wait_count,
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buffer,
	};

	/* Either use this semaphore for the swapchain present,
	 * or to export for render_fence_fd */
	if (vo->output_type == VULKAN_OUTPUT_SWAPCHAIN || vr->semaphore_import_export) {
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &im->render_done;
	}

	result = vkQueueSubmit(vr->queue, 1, &submit_info, fr->fence);
	check_vk_success(result, "vkQueueSubmit");

	if (vo->output_type == VULKAN_OUTPUT_SWAPCHAIN) {
		VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &im->render_done,
			.swapchainCount = 1,
			.pSwapchains = &vo->swapchain.swapchain,
			.pImageIndices = &swapchain_index,
			.pResults = NULL,
		};

		if (vr->has_incremental_present) {
			uint32_t nrects;
			VkRectLayerKHR *rects;
			pixman_region_to_present_region(output, output_damage,
							rb->border_status, &nrects, &rects);

			const VkPresentRegionKHR region = {
				.rectangleCount = nrects,
				.pRectangles = rects,
			};
			VkPresentRegionsKHR present_regions = {
				.sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
				.swapchainCount = 1,
				.pRegions = &region,
			};
			pnext(&present_info, &present_regions);

			result = vkQueuePresentKHR(vr->queue, &present_info);
			free(rects);
		} else {
			result = vkQueuePresentKHR(vr->queue, &present_info);
		}

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			abort();
		} else if (result != VK_SUCCESS) {
			abort();
		}
	} else if (vr->semaphore_import_export) {
		int fd;
		const VkSemaphoreGetFdInfoKHR semaphore_fd_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
			.semaphore = im->render_done,
			.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		};
		result = vr->get_semaphore_fd(vr->dev, &semaphore_fd_info, &fd);
		check_vk_success(result, "vkGetSemaphoreFdKHR");

		fd_update(&vo->render_fence_fd, fd);
	}

	vulkan_renderer_do_capture_tasks(vr, im->image, output,
					 WESTON_OUTPUT_CAPTURE_SOURCE_FRAMEBUFFER);
	vulkan_renderer_do_capture_tasks(vr, im->image, output,
					 WESTON_OUTPUT_CAPTURE_SOURCE_FULL_FRAMEBUFFER);

	rb->border_status = BORDER_STATUS_CLEAN;
	vo->border_status = BORDER_STATUS_CLEAN;

	update_buffer_release_fences(compositor, output);

	if (rb->buffer) {
		uint32_t *pixels = rb->buffer;
		int width = vo->fb_size.width;
		int stride = width * (compositor->read_format->bpp >> 3);
		pixman_box32_t extents;

		assert(rb->stride == stride);

		extents = weston_matrix_transform_rect(&output->matrix,
						       rb->damage.extents);

		const struct weston_geometry rect = {
			.x = vo->area.x + extents.x1,
			.y = vo->area.y + extents.y1,
			.width = extents.x2 - extents.x1,
			.height = extents.y2 - extents.y1,
		};

		vulkan_renderer_do_read_pixels(vr, im->image, vo,
					       compositor->read_format,
					       pixels, stride, &rect);
	}

	pixman_region32_clear(&rb->damage);

	vo->frame_index = (vo->frame_index + 1) % vo->num_frames;

	if (vo->output_type == VULKAN_OUTPUT_DRM)
		vo->drm.image_index = (vo->drm.image_index + 1) % vo->image_count;
}

static void
create_texture_sampler(struct vulkan_renderer *vr, VkSampler *texture_sampler, VkFilter filter)
{
	VkResult result;

	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = filter,
		.minFilter = filter,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.anisotropyEnable = VK_FALSE,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
	};

	result = vkCreateSampler(vr->dev, &sampler_info, NULL, texture_sampler);
	check_vk_success(result, "vkCreateSampler");
}

static void
copy_buffer_to_sub_image(VkCommandBuffer cmd_buffer,
			 VkBuffer buffer, VkImage image,
			 uint32_t buffer_width, uint32_t buffer_height,
			 uint32_t pitch,
			 uint32_t bpp,
			 uint32_t xoff, uint32_t yoff,
			 uint32_t xcopy, uint32_t ycopy)
{
	const VkOffset3D image_offset = { xoff, yoff };
	const VkExtent3D image_extent = { xcopy, ycopy, 1 };

	const VkBufferImageCopy region = {
		.bufferOffset = ((buffer_width * yoff) + xoff) * (bpp/8),
		.bufferRowLength = pitch,
		.bufferImageHeight = buffer_height,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = image_offset,
		.imageExtent = image_extent,
	};

	vkCmdCopyBufferToImage(cmd_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

static void
update_texture_image(struct vulkan_renderer *vr,
		     struct vulkan_renderer_texture_image *texture,
		     VkImageLayout expected_layout,
		     const struct pixel_format_info *pixel_format,
		     uint32_t buffer_width, uint32_t buffer_height,
		     uint32_t pitch, const void * const pixels,
		     uint32_t xoff, uint32_t yoff,
		     uint32_t xcopy, uint32_t ycopy)
{
	VkDeviceSize image_size = pitch * buffer_height * (pixel_format->bpp/8);
	VkResult result;

	assert(pixels);

	memcpy(texture->staging_map, pixels, (size_t)image_size);

	vkWaitForFences(vr->dev, 1, &texture->upload_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(vr->dev, 1, &texture->upload_fence);

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	VkCommandBuffer cmd_buffer = texture->upload_cmd;

	result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
	check_vk_success(result, "vkBeginCommandBuffer");

	transition_image_layout(cmd_buffer, texture->image,
				expected_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	copy_buffer_to_sub_image(cmd_buffer, texture->staging_buffer, texture->image,
				 buffer_width, buffer_height, pitch, pixel_format->bpp,
				 xoff, yoff, xcopy, ycopy);

	transition_image_layout(cmd_buffer, texture->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	result = vkEndCommandBuffer(cmd_buffer);
	check_vk_success(result, "vkEndCommandBuffer");

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buffer,
	};

	result = vkQueueSubmit(vr->queue, 1, &submit_info, texture->upload_fence);
	check_vk_success(result, "vkQueueSubmit");
}

static void
update_texture_image_all(struct vulkan_renderer *vr,
			 struct vulkan_renderer_texture_image *texture,
			 VkImageLayout expected_layout,
			 const struct pixel_format_info *pixel_format,
			 uint32_t buffer_width, uint32_t buffer_height,
			 uint32_t pitch, const void * const pixels)
{
	update_texture_image(vr, texture, expected_layout, pixel_format,
			     buffer_width, buffer_height, pitch, pixels,
			     0, 0, buffer_width, buffer_height);
}

static void
create_texture_image(struct vulkan_renderer *vr,
		     struct vulkan_renderer_texture_image *texture,
		     const struct pixel_format_info *pixel_format,
		     uint32_t buffer_width, uint32_t buffer_height, uint32_t pitch)
{
	VkDeviceSize image_size = pitch * buffer_height * (pixel_format->bpp/8);
	VkResult result;

	const VkFenceCreateInfo fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	result = vkCreateFence(vr->dev, &fence_info, NULL, &texture->upload_fence);
	check_vk_success(result, "vkCreateFence");

	const VkCommandBufferAllocateInfo cmd_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = vr->cmd_pool,
		.commandBufferCount = 1,
	};
	result = vkAllocateCommandBuffers(vr->dev, &cmd_alloc_info, &texture->upload_cmd);
	check_vk_success(result, "vkAllocateCommandBuffers");

	create_buffer(vr, image_size,
		      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		      &texture->staging_buffer, &texture->staging_memory);

	result = vkMapMemory(vr->dev, texture->staging_memory, 0, image_size, 0, &texture->staging_map);
	check_vk_success(result, "vkMapMemory");

	create_image(vr, buffer_width, buffer_height, pixel_format->vulkan_format, VK_IMAGE_TILING_OPTIMAL,
		     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &texture->image, &texture->memory);

	create_image_view(vr->dev, texture->image, pixel_format->vulkan_format, &texture->image_view);
}

static void
vulkan_renderer_flush_damage(struct weston_paint_node *pnode)
{
	struct weston_surface *es = pnode->surface;
	struct weston_compositor *ec = es->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);

	struct weston_surface *surface = pnode->surface;
	const struct weston_testsuite_quirks *quirks =
		&surface->compositor->test_data.test_quirks;
	struct weston_buffer *buffer = surface->buffer_ref.buffer;
	struct vulkan_surface_state *vs = get_surface_state(surface);
	struct vulkan_buffer_state *vb = vs->buffer;
	pixman_box32_t *rectangles;
	uint8_t *data;
	int n;

	assert(buffer && vb);

	pixman_region32_union(&vb->texture_damage,
			      &vb->texture_damage, &surface->damage);

	if (pnode->plane != &pnode->output->primary_plane) {
		return;
	}

	/* This can happen if a SHM wl_buffer gets destroyed before we flush
	 * damage, because wayland-server just nukes the wl_shm_buffer from
	 * underneath us */
	if (!buffer->shm_buffer) {
		return;
	}

	if (!pixman_region32_not_empty(&vb->texture_damage) &&
	    !vb->needs_full_upload) {
		return;
	}

	data = wl_shm_buffer_get_data(buffer->shm_buffer);

	if (vb->needs_full_upload || quirks->force_full_upload) {
		wl_shm_buffer_begin_access(buffer->shm_buffer);

		for (int j = 0; j < vb->num_textures; j++) {
			int hsub = pixel_format_hsub(buffer->pixel_format, j);
			int vsub = pixel_format_vsub(buffer->pixel_format, j);
			void *pixels = data + vb->offset[j];
			uint32_t buffer_width = buffer->width / hsub;
			uint32_t buffer_height = buffer->height / vsub;

			update_texture_image_all(vr, &vb->texture, VK_IMAGE_LAYOUT_UNDEFINED,
						 buffer->pixel_format, buffer_width, buffer_height,
						 vb->pitch, pixels);
		}
		wl_shm_buffer_end_access(buffer->shm_buffer);
		goto done;
	}

	rectangles = pixman_region32_rectangles(&vb->texture_damage, &n);
	wl_shm_buffer_begin_access(buffer->shm_buffer);
	for (int i = 0; i < n; i++) {
		pixman_box32_t r;

		r = weston_surface_to_buffer_rect(surface, rectangles[i]);

		for (int j = 0; j < vb->num_textures; j++) {
			int hsub = pixel_format_hsub(buffer->pixel_format, j);
			int vsub = pixel_format_vsub(buffer->pixel_format, j);
			uint32_t xoff = r.x1 / hsub;
			uint32_t yoff = r.y1 / vsub;
			uint32_t xcopy = (r.x2 - r.x1) / hsub;
			uint32_t ycopy = (r.y2 - r.y1) / vsub;
			void *pixels = data + vb->offset[j];

			update_texture_image(vr, &vb->texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					     buffer->pixel_format, buffer->width / hsub, buffer->height / vsub,
					     vb->pitch, pixels, xoff, yoff, xcopy, ycopy);
		}
	}
	wl_shm_buffer_end_access(buffer->shm_buffer);

done:
	pixman_region32_fini(&vb->texture_damage);
	pixman_region32_init(&vb->texture_damage);
	vb->needs_full_upload = false;

	weston_buffer_reference(&vs->buffer_ref, buffer,
				BUFFER_WILL_NOT_BE_ACCESSED);
	weston_buffer_release_reference(&vs->buffer_release_ref, NULL);
}

static void
handle_buffer_destroy(struct wl_listener *listener, void *data)
{
	struct weston_buffer *buffer = data;
	struct vulkan_buffer_state *vb =
		container_of(listener, struct vulkan_buffer_state, destroy_listener);

	assert(vb == buffer->renderer_private);
	buffer->renderer_private = NULL;

	destroy_buffer_state(vb);
}

static void
vulkan_renderer_attach_shm(struct weston_surface *surface, struct weston_buffer *buffer)
{
	struct weston_compositor *ec = surface->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_surface_state *vs = get_surface_state(surface);
	struct vulkan_buffer_state *vb;
	struct weston_buffer *old_buffer = vs->buffer_ref.buffer;
	unsigned int vulkan_format[3] = { 0, 0, 0 };
	enum vulkan_pipeline_texture_variant pipeline_variant;
	uint32_t pitch;
	int offset[3] = { 0, 0, 0 };
	unsigned int num_planes;

	int bpp = buffer->pixel_format->bpp;

	assert(pixel_format_get_plane_count(buffer->pixel_format) == 1);
	num_planes = 1;

	if (pixel_format_is_opaque(buffer->pixel_format))
		pipeline_variant = PIPELINE_VARIANT_RGBX;
	else
		pipeline_variant = PIPELINE_VARIANT_RGBA;

	assert(bpp > 0 && !(bpp & 7));
	pitch = buffer->stride / (bpp / 8);

	vulkan_format[0] = buffer->pixel_format->vulkan_format;
	vulkan_format[1] = buffer->pixel_format->vulkan_format;
	vulkan_format[2] = buffer->pixel_format->vulkan_format;
	/* If this surface previously had a SHM buffer, its vulkan_buffer_state will
	 * be speculatively retained. Check to see if we can reuse it rather
	 * than allocating a new one. */
	assert(!vs->buffer ||
	      (old_buffer && old_buffer->type == WESTON_BUFFER_SHM));
	if (vs->buffer &&
	    buffer->width == old_buffer->width &&
	    buffer->height == old_buffer->height &&
	    buffer->pixel_format == old_buffer->pixel_format) {
		vs->buffer->pitch = pitch;
		memcpy(vs->buffer->offset, offset, sizeof(offset));
		return;
	}

	if (vs->buffer)
		destroy_buffer_state(vs->buffer);
	vs->buffer = NULL;

	vb = xzalloc(sizeof(*vb));
	vb->vr = vr;

	wl_list_init(&vb->destroy_listener.link);
	pixman_region32_init(&vb->texture_damage);

	vb->pitch = pitch;
	vb->pipeline_variant = pipeline_variant;
	ARRAY_COPY(vb->offset, offset);
	ARRAY_COPY(vb->vulkan_format, vulkan_format);
	vb->needs_full_upload = true;
	vb->num_textures = num_planes;

	vs->buffer = vb;
	vs->surface = surface;

	for (uint32_t i = 0; i < num_planes; i++) {
		int hsub = pixel_format_hsub(buffer->pixel_format, i);
		int vsub = pixel_format_vsub(buffer->pixel_format, i);
		uint32_t buffer_width = buffer->width / hsub;
		uint32_t buffer_height = buffer->height / vsub;

		create_texture_image(vr, &vb->texture, buffer->pixel_format, buffer_width, buffer_height, pitch);
		create_texture_sampler(vr, &vb->sampler_nearest, VK_FILTER_NEAREST);
		create_texture_sampler(vr, &vb->sampler_linear, VK_FILTER_LINEAR);
	}
	create_vs_ubo_buffer(vr, &vb->vs_ubo_buffer, &vb->vs_ubo_memory, &vb->vs_ubo_map);
	create_fs_ubo_buffer(vr, &vb->fs_ubo_buffer, &vb->fs_ubo_memory, &vb->fs_ubo_map);
}

static void
create_texture_image_dummy(struct vulkan_renderer *vr)
{
	const struct pixel_format_info *dummy_pixel_format = pixel_format_get_info(DRM_FORMAT_ARGB8888);
	const uint32_t dummy_pixels[1] = { 0 };
	create_texture_image(vr, &vr->dummy.image, dummy_pixel_format, 1, 1, 1);
	create_texture_sampler(vr, &vr->dummy.sampler, VK_FILTER_NEAREST);
	update_texture_image_all(vr, &vr->dummy.image, VK_IMAGE_LAYOUT_UNDEFINED,
				 dummy_pixel_format, 1, 1, 1, dummy_pixels);
}

static struct vulkan_buffer_state *
ensure_renderer_vulkan_buffer_state(struct weston_surface *surface,
				    struct weston_buffer *buffer)
{
	struct vulkan_renderer *vr = get_renderer(surface->compositor);
	struct vulkan_surface_state *vs = get_surface_state(surface);
	struct vulkan_buffer_state *vb = buffer->renderer_private;

	if (vb) {
		vs->buffer = vb;
		return vb;
	}

	vb = xzalloc(sizeof(*vb));
	vb->vr = vr;
	pixman_region32_init(&vb->texture_damage);
	buffer->renderer_private = vb;
	vb->destroy_listener.notify = handle_buffer_destroy;
	wl_signal_add(&buffer->destroy_signal, &vb->destroy_listener);

	vs->buffer = vb;

	create_vs_ubo_buffer(vr, &vb->vs_ubo_buffer, &vb->vs_ubo_memory, &vb->vs_ubo_map);
	create_fs_ubo_buffer(vr, &vb->fs_ubo_buffer, &vb->fs_ubo_memory, &vb->fs_ubo_map);

	return vb;
}

static void
attach_direct_display_placeholder(struct weston_paint_node *pnode)
{
	struct weston_surface *surface = pnode->surface;
	struct weston_buffer *buffer = surface->buffer_ref.buffer;
	struct vulkan_buffer_state *vb;

	vb = ensure_renderer_vulkan_buffer_state(surface, buffer);

	/* uses the same color as the content-protection placeholder */
	vb->color[0] = pnode->solid.r;
	vb->color[1] = pnode->solid.g;
	vb->color[2] = pnode->solid.b;
	vb->color[3] = pnode->solid.a;

	vb->pipeline_variant = PIPELINE_VARIANT_SOLID;
}

static void
vulkan_renderer_attach_buffer(struct weston_surface *surface,
			      struct weston_buffer *buffer)
{
	struct vulkan_surface_state *vs = get_surface_state(surface);
	struct vulkan_buffer_state *vb;

	assert(buffer->renderer_private);
	vb = buffer->renderer_private;

	if (pixel_format_is_opaque(buffer->pixel_format))
		vb->pipeline_variant = PIPELINE_VARIANT_RGBX;
	else
		vb->pipeline_variant = PIPELINE_VARIANT_RGBA;

	vs->buffer = vb;
}

static void
vulkan_renderer_attach_solid(struct weston_surface *surface,
			     struct weston_buffer *buffer)
{
	struct vulkan_buffer_state *vb;

	vb = ensure_renderer_vulkan_buffer_state(surface, buffer);

	vb->color[0] = buffer->solid.r;
	vb->color[1] = buffer->solid.g;
	vb->color[2] = buffer->solid.b;
	vb->color[3] = buffer->solid.a;

	vb->pipeline_variant = PIPELINE_VARIANT_SOLID;
}

static void
vulkan_renderer_attach(struct weston_paint_node *pnode)
{
	struct weston_surface *es = pnode->surface;
	struct weston_buffer *buffer = es->buffer_ref.buffer;
	struct vulkan_surface_state *vs = get_surface_state(es);

	if (vs->buffer_ref.buffer == buffer)
		return;

	/* SHM buffers are a little special in that they are allocated
	 * per-surface rather than per-buffer, because we keep a shadow
	 * copy of the SHM data in a GL texture; for these we need to
	 * destroy the buffer state when we're switching to another
	 * buffer type. For all the others, the vulkan_buffer_state comes
	 * from the weston_buffer itself, and will only be destroyed
	 * along with it. */
	if (vs->buffer && vs->buffer_ref.buffer->type == WESTON_BUFFER_SHM) {
		if (!buffer || buffer->type != WESTON_BUFFER_SHM) {
			destroy_buffer_state(vs->buffer);
			vs->buffer = NULL;
		}
	} else {
		vs->buffer = NULL;
	}

	if (!buffer)
		goto out;

	if (pnode->is_direct) {
		attach_direct_display_placeholder(pnode);
		goto success;
	}

	switch (buffer->type) {
	case WESTON_BUFFER_SHM:
		vulkan_renderer_attach_shm(es, buffer);
		break;
	case WESTON_BUFFER_DMABUF:
	case WESTON_BUFFER_RENDERER_OPAQUE:
		vulkan_renderer_attach_buffer(es, buffer);
		break;
	case WESTON_BUFFER_SOLID:
		vulkan_renderer_attach_solid(es, buffer);
		break;
	default:
		weston_log("unhandled buffer type!\n");
		weston_buffer_send_server_error(buffer,
			"disconnecting due to unhandled buffer type");
		goto out;
	}

success:
	weston_buffer_reference(&vs->buffer_ref, buffer,
				BUFFER_MAY_BE_ACCESSED);
	weston_buffer_release_reference(&vs->buffer_release_ref,
					es->buffer_release_ref.buffer_release);
	return;

out:
	assert(!vs->buffer);
	weston_buffer_reference(&vs->buffer_ref, NULL,
				BUFFER_WILL_NOT_BE_ACCESSED);
	weston_buffer_release_reference(&vs->buffer_release_ref, NULL);
}

static void
vulkan_renderer_buffer_init(struct weston_compositor *ec,
			struct weston_buffer *buffer)
{
	struct vulkan_buffer_state *vb;

	if (buffer->type != WESTON_BUFFER_DMABUF)
		return;

	/* Thanks to linux-dmabuf being totally independent of libweston,
	 * the vulkan_buffer_state willonly be set as userdata on the dmabuf,
	 * not on the weston_buffer. Steal it away into the weston_buffer. */
	assert(!buffer->renderer_private);
	vb = linux_dmabuf_buffer_get_user_data(buffer->dmabuf);
	assert(vb);
	linux_dmabuf_buffer_set_user_data(buffer->dmabuf, NULL, NULL);
	buffer->renderer_private = vb;
	vb->destroy_listener.notify = handle_buffer_destroy;
	wl_signal_add(&buffer->destroy_signal, &vb->destroy_listener);
}

static void
vulkan_renderer_output_destroy_border(struct weston_output *output,
				      enum weston_renderer_border_side side)
{
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(output->compositor);

	// Wait idle here is bad, but this is only resize/refocus
	// and not on drm-backend
	VkResult result;
	result = vkQueueWaitIdle(vr->queue);
	check_vk_success(result, "vkQueueWaitIdle");

	struct vulkan_border_image *border = &vo->borders[side];

	destroy_buffer(vr->dev, border->fs_ubo_buffer, border->fs_ubo_memory);
	destroy_buffer(vr->dev, border->vs_ubo_buffer, border->vs_ubo_memory);

	destroy_sampler(vr->dev, border->sampler);
	destroy_texture_image(vr, &border->texture);
}

static void
vulkan_renderer_output_set_border(struct weston_output *output,
				  enum weston_renderer_border_side side,
				  int32_t width, int32_t height,
				  int32_t tex_width, unsigned char *data)
{
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(output->compositor);

	if (vo->borders[side].width != width ||
	    vo->borders[side].height != height)
		/* In this case, we have to blow everything and do a full
		 * repaint. */
		vo->border_status |= BORDER_ALL_DIRTY;

	struct vulkan_border_image *border = &vo->borders[side];

	if (border->data != NULL)
		vulkan_renderer_output_destroy_border(output, side);

	if (data == NULL) {
		width = 0;
		height = 0;
	}

	border->width = width;
	border->height = height;
	border->tex_width = tex_width;
	border->data = data;
	vo->border_status |= 1 << side;

	if (data == NULL)
		return;

	const uint32_t drm_format = DRM_FORMAT_ARGB8888;
	const struct pixel_format_info *pixel_format = pixel_format_get_info(drm_format);
	uint32_t pitch = tex_width;

	create_texture_image(vr, &border->texture, pixel_format, tex_width, height, pitch);
	create_texture_sampler(vr, &border->sampler, VK_FILTER_NEAREST);
	update_texture_image_all(vr, &border->texture, VK_IMAGE_LAYOUT_UNDEFINED,
				 pixel_format, tex_width, height, pitch, data);

	create_vs_ubo_buffer(vr, &border->vs_ubo_buffer, &border->vs_ubo_memory, &border->vs_ubo_map);
	create_fs_ubo_buffer(vr, &border->fs_ubo_buffer, &border->fs_ubo_memory, &border->fs_ubo_map);
}

static bool
vulkan_renderer_resize_output(struct weston_output *output,
			      const struct weston_size *fb_size,
			      const struct weston_geometry *area)
{
	struct vulkan_output_state *vo = get_output_state(output);
	bool ret = true;

	assert(vo->output_type == VULKAN_OUTPUT_SWAPCHAIN ||
	       vo->output_type == VULKAN_OUTPUT_HEADLESS);

	check_compositing_area(fb_size, area);

	vo->fb_size = *fb_size;
	vo->area = *area;

	weston_output_update_capture_info(output,
					  WESTON_OUTPUT_CAPTURE_SOURCE_FRAMEBUFFER,
					  area->width, area->height,
					  output->compositor->read_format);

	weston_output_update_capture_info(output,
					  WESTON_OUTPUT_CAPTURE_SOURCE_FULL_FRAMEBUFFER,
					  fb_size->width, fb_size->height,
					  output->compositor->read_format);

	if (!vulkan_renderer_discard_renderbuffers(vo, false))
		return false;

	if (vo->output_type == VULKAN_OUTPUT_SWAPCHAIN)
		vulkan_renderer_recreate_swapchain(output, *fb_size);

	return ret;
}

static int
import_dmabuf(struct vulkan_renderer *vr,
	      VkImage image,
	      VkDeviceMemory *memory,
	      const struct dmabuf_attributes *attributes)
{
	VkResult result;

	int fd0 = attributes->fd[0];

	if (!vr->has_external_memory_dma_buf)
		abort();

	VkMemoryFdPropertiesKHR fd_props = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
	};
	result = vr->get_memory_fd_properties(vr->dev,
					      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
					      fd0, &fd_props);
	check_vk_success(result, "vkGetMemoryFdPropertiesKHR");

	VkImageMemoryRequirementsInfo2 mem_reqs_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.image = image,
	};
	VkMemoryRequirements2 mem_reqs = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
	};
	vr->get_image_memory_requirements2(vr->dev, &mem_reqs_info, &mem_reqs);

	const uint32_t memory_type_bits = fd_props.memoryTypeBits &
		mem_reqs.memoryRequirements.memoryTypeBits;
	if (!memory_type_bits) {
		weston_log("No valid memory type\n");
		return false;
	}

	int dfd = fcntl(fd0, F_DUPFD_CLOEXEC, 0);
	if (dfd < 0) {
		weston_log("fcntl(F_DUPFD_CLOEXEC) failed\n");
		abort();
	}

	VkMemoryAllocateInfo memory_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.memoryRequirements.size,
		.memoryTypeIndex = ffs(memory_type_bits) - 1,
	};

	VkImportMemoryFdInfoKHR memory_fd_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
		.fd = dfd,
	};
	pnext(&memory_allocate_info, &memory_fd_info);

	VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.image = image,
	};
	pnext(&memory_allocate_info, &memory_dedicated_info);

	result = vkAllocateMemory(vr->dev, &memory_allocate_info, NULL, memory);
	check_vk_success(result, "vkAllocateMemory");

	result = vkBindImageMemory(vr->dev, image, *memory, 0);
	check_vk_success(result, "vkBindImageMemory");

	return true;
}

static void
create_dmabuf_image(struct vulkan_renderer *vr,
		    const struct dmabuf_attributes *attributes,
		    VkImageUsageFlags usage, VkImage *image)
{
	VkResult result;
	int width = attributes->width;
	int height = attributes->height;
	uint64_t modifier = attributes->modifier;
	int n_planes = attributes->n_planes;
	VkFormat format = 0;

	const struct pixel_format_info *pixel_format = pixel_format_get_info(attributes->format);
	assert(pixel_format);

	format = pixel_format->vulkan_format;

	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImageDrmFormatModifierExplicitCreateInfoEXT mod_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
	};
	VkSubresourceLayout plane_layouts[n_planes];
	if (vr->has_image_drm_format_modifier) {
		image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

		memset(plane_layouts, 0, sizeof(plane_layouts));
		for (int i = 0; i < n_planes; i++) {
			plane_layouts[i].offset = attributes->offset[i];
			plane_layouts[i].size = 0;
			plane_layouts[i].rowPitch = attributes->stride[i];
		}

		mod_create_info.drmFormatModifier = modifier;
		mod_create_info.drmFormatModifierPlaneCount = n_planes;
		mod_create_info.pPlaneLayouts = plane_layouts;
		pnext(&image_info, &mod_create_info);
	} else {
		image_info.tiling = VK_IMAGE_TILING_LINEAR;
	}

	VkExternalMemoryImageCreateInfo external_create_info = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	pnext(&image_info, &external_create_info);

	result = vkCreateImage(vr->dev, &image_info, NULL, image);
	check_vk_success(result, "vkCreateImage");
}

static int
vulkan_renderer_output_window_create_gbm(struct weston_output *output,
					 const struct vulkan_renderer_output_options *options)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(ec);
	const struct pixel_format_info *pixel_format = vo->pixel_format;
	const VkFormat format = pixel_format->vulkan_format;

	vo->image_count = options->num_gbm_bos;

	for (uint32_t i = 0; i < vo->image_count; i++) {
		struct vulkan_renderer_image *im = &vo->images[i];
		struct gbm_bo *bo = options->gbm_bos[i];

		im->bo = bo;

		struct dmabuf_attributes attributes;
		attributes.fd[0] = gbm_bo_get_fd(bo);
		attributes.width = gbm_bo_get_width(bo);
		attributes.height = gbm_bo_get_height(bo);
		attributes.modifier = gbm_bo_get_modifier(bo);
		attributes.n_planes = gbm_bo_get_plane_count(bo);
		attributes.format = pixel_format->format;

		for (int i = 0; i < attributes.n_planes; i++) {
			attributes.offset[i] = gbm_bo_get_offset(bo, i);
			attributes.stride[i] = gbm_bo_get_stride_for_plane(bo, i);
		}

		create_dmabuf_image(vr, &attributes,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				&im->image);

		import_dmabuf(vr, im->image, &im->memory, &attributes);
		close(attributes.fd[0]); /* fd is duped */

		create_image_view(vr->dev, im->image, format, &im->image_view);
		create_framebuffer(vr->dev, vo->renderpass, im->image_view,
				   options->fb_size.width, options->fb_size.height, &im->framebuffer);

		create_image_semaphores(vr, vo, im);

		im->renderbuffer = xzalloc(sizeof(*im->renderbuffer));
		vulkan_renderbuffer_init(im->renderbuffer, NULL, NULL, NULL, output);
	}

	return 0;
}

static int
vulkan_renderer_output_window_create_swapchain(struct weston_output *output,
					       const struct vulkan_renderer_output_options *options)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_output_state *vo = get_output_state(output);
	VkResult result;
	VkBool32 supported;

	if (options->wayland_display && options->wayland_surface) {
		assert(vr->has_wayland_surface);

		supported = vr->get_wayland_presentation_support(vr->phys_dev, 0, options->wayland_display);
		assert(supported);

		const VkWaylandSurfaceCreateInfoKHR wayland_surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
			.display = options->wayland_display,
			.surface = options->wayland_surface,
		};
		result = vr->create_wayland_surface(vr->inst, &wayland_surface_create_info, NULL,
						    &vo->swapchain.surface);
		check_vk_success(result, "vkCreateWaylandSurfaceKHR");
	} else if (options->xcb_connection && options->xcb_window) {
		assert(vr->has_xcb_surface);

		supported = vr->get_xcb_presentation_support(vr->phys_dev, 0, options->xcb_connection, options->xcb_visualid);
		assert(supported);

		const VkXcbSurfaceCreateInfoKHR xcb_surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = options->xcb_connection,
			.window = options->xcb_window,
		};
		result = vr->create_xcb_surface(vr->inst, &xcb_surface_create_info, NULL,
						&vo->swapchain.surface);
		check_vk_success(result, "vkCreateXcbSurfaceKHR");
	} else {
		assert(0);
	}

	vkGetPhysicalDeviceSurfaceSupportKHR(vr->phys_dev, 0, vo->swapchain.surface, &supported);
	assert(supported);

	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(vr->phys_dev, vo->swapchain.surface,
						  &present_mode_count, NULL);
	VkPresentModeKHR present_modes[present_mode_count];
	vkGetPhysicalDeviceSurfacePresentModesKHR(vr->phys_dev, vo->swapchain.surface,
						  &present_mode_count, present_modes);

	vo->swapchain.present_mode = VK_PRESENT_MODE_FIFO_KHR;
	assert(vo->swapchain.present_mode >= 0 && vo->swapchain.present_mode < 4);
	supported = false;
	for (size_t i = 0; i < present_mode_count; ++i) {
		if (present_modes[i] == vo->swapchain.present_mode) {
			supported = true;
			break;
		}
	}

	if (!supported) {
		weston_log("Present mode %d unsupported\n", vo->swapchain.present_mode);
		abort();
	}

	vulkan_renderer_create_swapchain(output, options->fb_size);

	return 0;
}

static int
vulkan_renderer_create_output_state(struct weston_output *output,
				    const struct weston_size *fb_size,
				    const struct weston_geometry *area)
{
	struct vulkan_output_state *vo;

	vo = xzalloc(sizeof(*vo));

	wl_list_init(&vo->renderbuffer_list);

	output->renderer_state = vo;

	check_compositing_area(fb_size, area);

	vo->fb_size = *fb_size;
	vo->area = *area;

	vo->render_fence_fd = -1;

	return 0;
}

static int
vulkan_renderer_create_output_frames(struct weston_output *output,
				     const struct weston_size *fb_size,
				     const struct weston_geometry *area,
				     uint32_t num_frames)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_output_state *vo = get_output_state(output);

	vo->num_frames = num_frames;

	for (unsigned int i = 0; i < vo->num_frames; ++i) {
		struct vulkan_renderer_frame *fr = &vo->frames[i];
		VkResult result;

		const VkCommandBufferAllocateInfo cmd_alloc_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = vr->cmd_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		result = vkAllocateCommandBuffers(vr->dev, &cmd_alloc_info, &fr->cmd_buffer);
		check_vk_success(result, "vkAllocateCommandBuffers");

		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		result = vkCreateSemaphore(vr->dev, &semaphore_info, NULL, &fr->image_acquired);
		check_vk_success(result, "vkCreateSemaphore image_acquired");

		const VkFenceCreateInfo fence_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		result = vkCreateFence(vr->dev, &fence_info, NULL, &fr->fence);
		check_vk_success(result, "vkCreateFence");

		wl_list_init(&fr->dspool_list);
		wl_list_init(&fr->vbuf_list);
		wl_list_init(&fr->acquire_fence_list);
	}

	return 0;
}

static int
create_renderpass(struct weston_output *output, VkFormat format, VkImageLayout attachment_layout)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_output_state *vo = get_output_state(output);
	VkResult result;

	const VkAttachmentDescription attachment_description = {
		.format = format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout = attachment_layout,
		.finalLayout = attachment_layout,
	};
	const VkAttachmentReference attachment_reference = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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

	result = vkCreateRenderPass(vr->dev, &renderpass_create_info, NULL, &vo->renderpass);
	check_vk_success(result, "vkCreateRenderPass");

	return 0;
}

static int
vulkan_renderer_output_window_create(struct weston_output *output,
				     const struct vulkan_renderer_output_options *options)
{
	int ret;
	const struct weston_size *fb_size = &options->fb_size;
	const struct weston_geometry *area = &options->area;
	const struct pixel_format_info *pixel_format = options->formats[0];

	ret = vulkan_renderer_create_output_state(output, fb_size, area);
	assert(ret == 0);

	struct vulkan_output_state *vo = get_output_state(output);
	if ((options->wayland_display && options->wayland_surface) ||
	    (options->xcb_connection && options->xcb_window)) {
		vo->output_type = VULKAN_OUTPUT_SWAPCHAIN;
	} else {
		vo->output_type = VULKAN_OUTPUT_DRM;
	}
	vo->pixel_format = pixel_format;

	if (vo->output_type == VULKAN_OUTPUT_SWAPCHAIN) {
		create_renderpass(output, pixel_format->vulkan_format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		vulkan_renderer_output_window_create_swapchain(output, options);
	} else {
		create_renderpass(output, pixel_format->vulkan_format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		vulkan_renderer_output_window_create_gbm(output, options);
	}

	weston_output_update_capture_info(output,
					  WESTON_OUTPUT_CAPTURE_SOURCE_FRAMEBUFFER,
					  area->width, area->height,
					  output->compositor->read_format);

	weston_output_update_capture_info(output,
					  WESTON_OUTPUT_CAPTURE_SOURCE_FULL_FRAMEBUFFER,
					  fb_size->width, fb_size->height,
					  output->compositor->read_format);

	vulkan_renderer_create_output_frames(output, fb_size, area, MAX_CONCURRENT_FRAMES);

	return 0;
}

static int
vulkan_renderer_output_fbo_create(struct weston_output *output,
				  const struct vulkan_renderer_fbo_options *options)
{
	/* TODO: a format is needed here because right now a renderpass object
	 * is created per output.
	 * It should probably be independent of output (at least for renderbuffers),
	 * should probably be moved to a renderpass allocator to avoid creating
	 * a large number of renderpass objects (and exploding the number of
	 * pipelines) ? */
	const struct pixel_format_info *pixel_format = pixel_format_get_info(DRM_FORMAT_XRGB8888);
	const VkFormat format = pixel_format->vulkan_format;
	int ret;
	const struct weston_size *fb_size = &options->fb_size;
	const struct weston_geometry *area = &options->area;

	ret = vulkan_renderer_create_output_state(output, &options->fb_size, &options->area);
	assert(ret == 0);

	struct vulkan_output_state *vo = get_output_state(output);
	vo->output_type = VULKAN_OUTPUT_HEADLESS;
	vo->pixel_format = pixel_format;

	create_renderpass(output, format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	weston_output_update_capture_info(output,
					  WESTON_OUTPUT_CAPTURE_SOURCE_FRAMEBUFFER,
					  area->width, area->height,
					  output->compositor->read_format);

	weston_output_update_capture_info(output,
					  WESTON_OUTPUT_CAPTURE_SOURCE_FULL_FRAMEBUFFER,
					  fb_size->width, fb_size->height,
					  output->compositor->read_format);

	vulkan_renderer_create_output_frames(output, &options->fb_size, &options->area, 1);

	return 0;
}

static void
vulkan_renderer_destroy(struct weston_compositor *ec)
{
	struct vulkan_renderer *vr = get_renderer(ec);

	wl_signal_emit(&vr->destroy_signal, vr);

	VkResult result;
	result = vkDeviceWaitIdle(vr->dev);
	check_vk_success(result, "vkDeviceWaitIdle");

	vulkan_renderer_pipeline_list_destroy(vr);

	destroy_sampler(vr->dev, vr->dummy.sampler);
	destroy_texture_image(vr, &vr->dummy.image);

	vkDestroyCommandPool(vr->dev, vr->cmd_pool, NULL);

	vkDestroyDevice(vr->dev, NULL);

	vkDestroyInstance(vr->inst, NULL);

	vulkan_renderer_allocator_destroy(vr->allocator);

	if (vr->drm_fd > 0)
		close(vr->drm_fd);

	weston_drm_format_array_fini(&vr->supported_formats);

	free(vr);
	ec->renderer = NULL;
}

static void
log_vulkan_phys_dev(VkPhysicalDevice phys_dev)
{
	VkPhysicalDeviceProperties props;

	vkGetPhysicalDeviceProperties(phys_dev, &props);

	uint32_t api_major = VK_VERSION_MAJOR(props.apiVersion);
	uint32_t api_minor = VK_VERSION_MINOR(props.apiVersion);
	uint32_t api_patch = VK_VERSION_PATCH(props.apiVersion);

	uint32_t driver_major = VK_VERSION_MAJOR(props.driverVersion);
	uint32_t driver_minor = VK_VERSION_MINOR(props.driverVersion);
	uint32_t driver_patch = VK_VERSION_PATCH(props.driverVersion);

	const char *dev_type = "unknown";
	switch (props.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			dev_type = "integrated";
			break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			dev_type = "discrete";
			break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			dev_type = "cpu";
			break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			dev_type = "vgpu";
			break;
		default:
			break;
	}

	weston_log("Vulkan device: '%s'\n", props.deviceName);
	weston_log(" Device type: '%s'\n", dev_type);
	weston_log(" Supported API version: %u.%u.%u\n", api_major, api_minor, api_patch);
	weston_log(" Driver version: %u.%u.%u\n", driver_major, driver_minor, driver_patch);
}

static void
vulkan_renderer_choose_physical_device(struct vulkan_renderer *vr)
{
	uint32_t n_phys_devs;
	VkPhysicalDevice *phys_devs = NULL;
	VkResult result;

	result = vkEnumeratePhysicalDevices(vr->inst, &n_phys_devs, NULL);
	check_vk_success(result, "vkEnumeratePhysicalDevices");
	assert(n_phys_devs != 0);
	phys_devs = xmalloc(n_phys_devs * sizeof(*phys_devs));
	result = vkEnumeratePhysicalDevices(vr->inst, &n_phys_devs, phys_devs);
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
		weston_log("Unable to find a suitable physical device\n");
		abort();
	}

	vr->phys_dev = physical_device;

	free(phys_devs);

	log_vulkan_phys_dev(physical_device);
}

static void
vulkan_renderer_choose_queue_family(struct vulkan_renderer *vr)
{
	uint32_t n_props = 0;
	VkQueueFamilyProperties *props = NULL;

	vkGetPhysicalDeviceQueueFamilyProperties(vr->phys_dev, &n_props, NULL);
	props = xmalloc(n_props * sizeof(*props));
	vkGetPhysicalDeviceQueueFamilyProperties(vr->phys_dev, &n_props, props);

	uint32_t family_idx = UINT32_MAX;
	/* Pick the first graphics queue */
	for (uint32_t i = 0; i < n_props; ++i) {
		if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && props[i].queueCount > 0) {
			family_idx = i;
			break;
		}
	}

	if (family_idx == UINT32_MAX) {
		weston_log("Unable to find graphics queue\n");
		abort();
	}

	vr->queue_family = family_idx;

	free(props);
}

static weston_renderbuffer_t
vulkan_renderer_create_renderbuffer(struct weston_output *output,
				    const struct pixel_format_info *pixel_format,
				    void *buffer, int stride,
				    weston_renderbuffer_discarded_func discarded_cb,
				    void *user_data)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(ec);

	struct vulkan_renderbuffer *renderbuffer;

	const struct weston_size *fb_size = &vo->fb_size;
	VkFormat format = pixel_format->vulkan_format;

	renderbuffer = xzalloc(sizeof(*renderbuffer));
	renderbuffer->buffer = buffer;
	renderbuffer->stride = stride;

	struct vulkan_renderer_image *im = xzalloc(sizeof(*im));

	// Command here only for the layout transition
	VkCommandBuffer cmd_buffer;
	vulkan_renderer_cmd_begin(vr, &cmd_buffer);

	create_image(vr, fb_size->width, fb_size->height, format, VK_IMAGE_TILING_OPTIMAL,
		     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &im->image, &im->memory);

	create_image_view(vr->dev, im->image, format, &im->image_view);

	create_framebuffer(vr->dev, vo->renderpass, im->image_view,
			   fb_size->width, fb_size->height, &im->framebuffer);

	transition_image_layout(cmd_buffer, im->image,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, VK_ACCESS_TRANSFER_WRITE_BIT);

	// Wait here is bad, but this is only on renderbuffer creation
	vulkan_renderer_cmd_end_wait(vr, &cmd_buffer);

	create_image_semaphores(vr, vo, im);

	vulkan_renderbuffer_init(renderbuffer, im, discarded_cb, user_data, output);

	return (weston_renderbuffer_t) renderbuffer;
}

static weston_renderbuffer_t
vulkan_renderer_create_renderbuffer_dmabuf(struct weston_output *output,
					   struct linux_dmabuf_memory *dmabuf,
					   weston_renderbuffer_discarded_func discarded_cb,
					   void *user_data)
{
	struct weston_compositor *ec = output->compositor;
	struct vulkan_output_state *vo = get_output_state(output);
	struct vulkan_renderer *vr = get_renderer(ec);
	struct dmabuf_attributes *attributes = dmabuf->attributes;
	struct vulkan_buffer_state *vb;
	const struct weston_size *fb_size = &vo->fb_size;
	struct vulkan_renderbuffer *renderbuffer;
	const uint32_t drm_format = attributes->format;
	const struct pixel_format_info *pixel_format = pixel_format_get_info(drm_format);
	assert(pixel_format);

	vb = xzalloc(sizeof(*vb));

	vb->vr = vr;
	pixman_region32_init(&vb->texture_damage);
	wl_list_init(&vb->destroy_listener.link);

	renderbuffer = xzalloc(sizeof(*renderbuffer));

	struct vulkan_renderer_image *im = xzalloc(sizeof(*im));

	create_dmabuf_image(vr, attributes,
			    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			    &im->image);

	import_dmabuf(vr, im->image, &im->memory, attributes);

	VkFormat format = pixel_format->vulkan_format;
	create_image_view(vr->dev, im->image, format, &im->image_view);

	create_framebuffer(vr->dev, vo->renderpass, im->image_view,
			   fb_size->width, fb_size->height, &im->framebuffer);

	create_image_semaphores(vr, vo, im);

	vulkan_renderbuffer_init(renderbuffer, im, discarded_cb, user_data, output);

	renderbuffer->dmabuf.vr = vr;
	renderbuffer->dmabuf.memory = dmabuf;

	return (weston_renderbuffer_t) renderbuffer;
}

static void
vulkan_renderer_destroy_dmabuf(struct linux_dmabuf_buffer *dmabuf)
{
	struct vulkan_buffer_state *vb =
		linux_dmabuf_buffer_get_user_data(dmabuf);

	linux_dmabuf_buffer_set_user_data(dmabuf, NULL, NULL);
	destroy_buffer_state(vb);
}

static bool
vulkan_renderer_import_dmabuf(struct weston_compositor *ec,
			      struct linux_dmabuf_buffer *dmabuf)
{
	struct vulkan_renderer *vr = get_renderer(ec);
	struct vulkan_buffer_state *vb;
	struct dmabuf_attributes *attributes = &dmabuf->attributes;

	/* reject all flags we do not recognize or handle */
	if (attributes->flags & ~ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT)
		return false;

	const uint32_t drm_format = attributes->format;
	const struct pixel_format_info *pixel_format = pixel_format_get_info(drm_format);
	assert(pixel_format);

	vb = xzalloc(sizeof(*vb));

	vb->vr = vr;
	pixman_region32_init(&vb->texture_damage);
	wl_list_init(&vb->destroy_listener.link);

	VkFormat format = pixel_format->vulkan_format;

	create_dmabuf_image(vr, &dmabuf->attributes,
			    VK_IMAGE_USAGE_SAMPLED_BIT,
			    &vb->texture.image);

	import_dmabuf(vr, vb->texture.image, &vb->texture.memory, &dmabuf->attributes);

	create_texture_sampler(vr, &vb->sampler_linear, VK_FILTER_LINEAR);
	create_texture_sampler(vr, &vb->sampler_nearest, VK_FILTER_NEAREST);
	create_image_view(vr->dev, vb->texture.image, format, &vb->texture.image_view);

	assert(vb->num_textures == 0);
	vb->num_textures = 1;

	create_vs_ubo_buffer(vr, &vb->vs_ubo_buffer, &vb->vs_ubo_memory, &vb->vs_ubo_map);
	create_fs_ubo_buffer(vr, &vb->fs_ubo_buffer, &vb->fs_ubo_memory, &vb->fs_ubo_map);

	linux_dmabuf_buffer_set_user_data(dmabuf, vb,
		vulkan_renderer_destroy_dmabuf);

	return true;
}

static const struct weston_drm_format_array *
vulkan_renderer_get_supported_dmabuf_formats(struct weston_compositor *ec)
{
	struct vulkan_renderer *vr = get_renderer(ec);

	return &vr->supported_formats;
}

static int
populate_supported_formats(struct weston_compositor *ec,
			   struct weston_drm_format_array *supported_formats)
{
	struct vulkan_renderer *vr = get_renderer(ec);

	for (unsigned int i = 0; i < pixel_format_get_info_count(); i++) {
		const struct pixel_format_info *format = pixel_format_get_info_by_index(i);

		if (format->vulkan_format == VK_FORMAT_UNDEFINED)
			continue;

		vulkan_renderer_query_dmabuf_format(vr, format);
	}

	return 0;
}

static int
create_default_dmabuf_feedback(struct weston_compositor *ec,
			       struct vulkan_renderer *vr)
{
	struct stat dev_stat;
	struct weston_dmabuf_feedback_tranche *tranche;
	uint32_t flags = 0;

	if (fstat(vr->drm_fd, &dev_stat) != 0) {
		weston_log("%s: device disappeared, so we can't recover\n", __func__);
		abort();
	}

	ec->default_dmabuf_feedback =
		weston_dmabuf_feedback_create(dev_stat.st_rdev);
	if (!ec->default_dmabuf_feedback)
		return -1;

	tranche =
		weston_dmabuf_feedback_tranche_create(ec->default_dmabuf_feedback,
						      ec->dmabuf_feedback_format_table,
						      dev_stat.st_rdev, flags,
						      RENDERER_PREF);
	if (!tranche) {
		weston_dmabuf_feedback_destroy(ec->default_dmabuf_feedback);
		ec->default_dmabuf_feedback = NULL;
		return -1;
	}

	return 0;
}

static int
open_drm_device_node(struct vulkan_renderer *vr)
{
	assert(vr->has_physical_device_drm);

	VkPhysicalDeviceProperties2 phys_dev_props = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
	};

	VkPhysicalDeviceDrmPropertiesEXT drm_props = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
	};
	pnext(&phys_dev_props, &drm_props);

	vkGetPhysicalDeviceProperties2(vr->phys_dev, &phys_dev_props);

	dev_t devid;
	if (drm_props.hasRender) {
		devid = makedev(drm_props.renderMajor, drm_props.renderMinor);
	} else if (drm_props.hasPrimary) {
		devid = makedev(drm_props.primaryMajor, drm_props.primaryMinor);
	} else {
		weston_log("Physical device is missing both render and primary nodes\n");
		return -1;
	}

	drmDevice *device = NULL;
	if (drmGetDeviceFromDevId(devid, 0, &device) != 0) {
		weston_log("drmGetDeviceFromDevId failed\n");
		return -1;
	}

	const char *name = NULL;
	if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
		name = device->nodes[DRM_NODE_RENDER];
	} else {
		assert(device->available_nodes & (1 << DRM_NODE_PRIMARY));
		name = device->nodes[DRM_NODE_PRIMARY];
		weston_log("DRM device %s has no render node, falling back to primary node\n", name);
	}

	int drm_fd = open(name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (drm_fd < 0) {
		weston_log("Failed to open DRM node %s\n", name);
	}
	drmFreeDevice(&device);
	return drm_fd;
}

static bool
check_extension(const VkExtensionProperties *avail, uint32_t avail_len, const char *name)
{
	for (size_t i = 0; i < avail_len; i++) {
		if (strcmp(avail[i].extensionName, name) == 0) {
			return true;
		}
	}
	return false;
}

static void
load_instance_proc(struct vulkan_renderer *vr, const char *func, void *proc_ptr)
{
	void *proc = (void *)vkGetInstanceProcAddr(vr->inst, func);
	if (proc == NULL) {
		char err[256];
		snprintf(err, sizeof(err), "Failed to vkGetInstanceProcAddr %s\n", func);
		err[sizeof(err)-1] = '\0';
		weston_log("%s", err);
		abort();
	}

	*(void **)proc_ptr = proc;
}

static void
vulkan_renderer_setup_instance_extensions(struct vulkan_renderer *vr)
{
	if (vr->has_wayland_surface) {
		load_instance_proc(vr, "vkCreateWaylandSurfaceKHR", &vr->create_wayland_surface);
		load_instance_proc(vr, "vkGetPhysicalDeviceWaylandPresentationSupportKHR", &vr->get_wayland_presentation_support);
	}

	if (vr->has_xcb_surface) {
		load_instance_proc(vr, "vkCreateXcbSurfaceKHR", &vr->create_xcb_surface);
		load_instance_proc(vr, "vkGetPhysicalDeviceXcbPresentationSupportKHR", &vr->get_xcb_presentation_support);
	}
}

static void
vulkan_renderer_create_instance(struct vulkan_renderer *vr)
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

	if (check_extension(avail_inst_extns, num_avail_inst_extns, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
		inst_extns[num_inst_extns++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
		vr->has_wayland_surface = true;
	}

	if (check_extension(avail_inst_extns, num_avail_inst_extns, VK_KHR_XCB_SURFACE_EXTENSION_NAME)) {
		inst_extns[num_inst_extns++] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
		vr->has_xcb_surface = true;
	}

	if (vr->has_wayland_surface || vr->has_xcb_surface)
		inst_extns[num_inst_extns++] = VK_KHR_SURFACE_EXTENSION_NAME;

	for (uint32_t i = 0; i < num_inst_extns; i++) {
		uint32_t j;
		for (j = 0; j < num_avail_inst_extns; j++) {
			if (strcmp(inst_extns[i], avail_inst_extns[j].extensionName) == 0) {
				break;
			}
		}
		if (j == num_avail_inst_extns) {
			weston_log("Unsupported instance extension: %s\n", inst_extns[i]);
			abort();
		}
	}

	const VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "weston",
		.apiVersion = VK_MAKE_VERSION(1, 0, 0),
	};

	const VkInstanceCreateInfo inst_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.ppEnabledExtensionNames = inst_extns,
		.enabledExtensionCount = num_inst_extns,
	};

	result = vkCreateInstance(&inst_create_info, NULL, &vr->inst);
	check_vk_success(result, "vkCreateInstance");

	vulkan_renderer_setup_instance_extensions(vr);

	free(avail_inst_extns);
	free(inst_extns);
}

static void
load_device_proc(struct vulkan_renderer *vr, const char *func, void *proc_ptr)
{
	void *proc = (void *)vkGetDeviceProcAddr(vr->dev, func);
	if (proc == NULL) {
		char err[256];
		snprintf(err, sizeof(err), "Failed to vkGetDeviceProcAddr %s\n", func);
		err[sizeof(err)-1] = '\0';
		weston_log("%s", err);
		abort();
	}

	*(void **)proc_ptr = proc;
}

static void
vulkan_renderer_setup_device_extensions(struct vulkan_renderer *vr)
{
	// VK_KHR_get_memory_requirements2
	load_device_proc(vr, "vkGetImageMemoryRequirements2KHR", &vr->get_image_memory_requirements2);

	// VK_KHR_external_memory_fd
	load_device_proc(vr, "vkGetMemoryFdPropertiesKHR", &vr->get_memory_fd_properties);

	// VK_KHR_external_semaphore_fd
	if (vr->has_external_semaphore_fd) {
		load_device_proc(vr, "vkGetSemaphoreFdKHR", &vr->get_semaphore_fd);
		load_device_proc(vr, "vkImportSemaphoreFdKHR", &vr->import_semaphore_fd);
	}
}

static void
vulkan_renderer_create_device(struct vulkan_renderer *vr)
{
	uint32_t num_avail_device_extns;
	uint32_t num_device_extns = 0;
	VkResult result;

	result = vkEnumerateDeviceExtensionProperties(vr->phys_dev, NULL, &num_avail_device_extns, NULL);
	check_vk_success(result, "vkEnumerateDeviceExtensionProperties");
	VkExtensionProperties *avail_device_extns = xmalloc(num_avail_device_extns * sizeof(VkExtensionProperties));
	result = vkEnumerateDeviceExtensionProperties(vr->phys_dev, NULL, &num_avail_device_extns, avail_device_extns);
	check_vk_success(result, "vkEnumerateDeviceExtensionProperties");

	const char **device_extns = xmalloc(num_avail_device_extns * sizeof(*device_extns));
	device_extns[num_device_extns++] = VK_KHR_BIND_MEMORY_2_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
	device_extns[num_device_extns++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

	if (check_extension(avail_device_extns, num_avail_device_extns, VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME)) {
		device_extns[num_device_extns++] = VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME;
		vr->has_incremental_present = true;
	}

	if (check_extension(avail_device_extns, num_avail_device_extns, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
		device_extns[num_device_extns++] = VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME;
		vr->has_physical_device_drm = true;
	}

	if (check_extension(avail_device_extns, num_avail_device_extns, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME) &&
	    check_extension(avail_device_extns, num_avail_device_extns, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME)) {
		device_extns[num_device_extns++] = VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
		/* Extension dependencies */
		device_extns[num_device_extns++] = VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME;
		device_extns[num_device_extns++] = VK_KHR_MAINTENANCE_1_EXTENSION_NAME;
		vr->has_image_drm_format_modifier = true;
	}

	if (check_extension(avail_device_extns, num_avail_device_extns, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) {
		device_extns[num_device_extns++] = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
		vr->has_external_semaphore_fd = true;
	}

	/* These are really not optional for DRM backend, but are not used by
	 * e.g. headless, software renderer, so make them optional for tests */
	if (check_extension(avail_device_extns, num_avail_device_extns, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME)) {
		device_extns[num_device_extns++] = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
		vr->has_external_memory_dma_buf = true;
	}
	if (check_extension(avail_device_extns, num_avail_device_extns, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
		device_extns[num_device_extns++] = VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;
		vr->has_queue_family_foreign = true;
	}

	for (uint32_t i = 0; i < num_device_extns; i++) {
		uint32_t j;
		for (j = 0; j < num_avail_device_extns; j++) {
			if (strcmp(device_extns[i], avail_device_extns[j].extensionName) == 0) {
				break;
			}
		}
		if (j == num_avail_device_extns) {
			weston_log("Unsupported device extension: %s\n", device_extns[i]);
			abort();
		}
	}

	const VkDeviceQueueCreateInfo device_queue_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = vr->queue_family,
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

	result = vkCreateDevice(vr->phys_dev, &device_create_info, NULL, &vr->dev);
	check_vk_success(result, "vkCreateDevice");

	bool exportable_semaphore = false, importable_semaphore = false;
	if (vr->has_external_semaphore_fd) {
		const VkPhysicalDeviceExternalSemaphoreInfo ext_semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
			.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		};
		VkExternalSemaphoreProperties ext_semaphore_props = {
			.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
		};
		vkGetPhysicalDeviceExternalSemaphoreProperties(vr->phys_dev, &ext_semaphore_info, &ext_semaphore_props);

		exportable_semaphore = ext_semaphore_props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
		importable_semaphore = ext_semaphore_props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
	}
	if (!vr->has_image_drm_format_modifier)
		weston_log("DRM format modifiers not supported\n");
	if (!exportable_semaphore)
		weston_log("VkSemaphore is not exportable\n");
	if (!importable_semaphore)
		weston_log("VkSemaphore is not importable\n");

	vr->semaphore_import_export = exportable_semaphore && importable_semaphore;

	vulkan_renderer_setup_device_extensions(vr);

	free(avail_device_extns);
	free(device_extns);
}

static int
vulkan_renderer_display_create(struct weston_compositor *ec,
			       const struct vulkan_renderer_display_options *options)
{
	struct vulkan_renderer *vr;
	VkResult result;

	vr = xzalloc(sizeof(*vr));

	vr->compositor = ec;
	wl_list_init(&vr->pipeline_list);
	vr->base.repaint_output = vulkan_renderer_repaint_output;
	vr->base.resize_output = vulkan_renderer_resize_output;
	vr->base.create_renderbuffer = vulkan_renderer_create_renderbuffer;
	vr->base.destroy_renderbuffer = vulkan_renderer_destroy_renderbuffer;
	vr->base.flush_damage = vulkan_renderer_flush_damage;
	vr->base.attach = vulkan_renderer_attach;
	vr->base.destroy = vulkan_renderer_destroy;
	vr->base.buffer_init = vulkan_renderer_buffer_init;
	vr->base.output_set_border = vulkan_renderer_output_set_border,
	vr->base.type = WESTON_RENDERER_VULKAN;

	weston_drm_format_array_init(&vr->supported_formats);

	ec->renderer = &vr->base;

	wl_list_init(&vr->dmabuf_formats);
	wl_signal_init(&vr->destroy_signal);

	// TODO: probe and register remaining shm formats
	wl_display_add_shm_format(ec->wl_display, WL_SHM_FORMAT_XRGB8888);
	wl_display_add_shm_format(ec->wl_display, WL_SHM_FORMAT_ARGB8888);
	wl_display_add_shm_format(ec->wl_display, WL_SHM_FORMAT_ABGR2101010);

	vulkan_renderer_create_instance(vr);

	vulkan_renderer_choose_physical_device(vr);

	vulkan_renderer_choose_queue_family(vr);

	vulkan_renderer_create_device(vr);

	vr->drm_fd = -1;
	if (vr->has_physical_device_drm)
		vr->drm_fd = open_drm_device_node(vr);

	ec->capabilities |= WESTON_CAP_ROTATION_ANY;
	ec->capabilities |= WESTON_CAP_CAPTURE_YFLIP;
	ec->capabilities |= WESTON_CAP_VIEW_CLIP_MASK;

	if (vr->semaphore_import_export)
		ec->capabilities |= WESTON_CAP_EXPLICIT_SYNC;

	vr->allocator = vulkan_renderer_allocator_create(vr, options);
	if (!vr->allocator)
		weston_log("failed to initialize allocator\n");

	if (vr->allocator)
		vr->base.dmabuf_alloc = vulkan_renderer_dmabuf_alloc;

	if (vr->has_external_memory_dma_buf) {
		int ret;
		vr->base.import_dmabuf = vulkan_renderer_import_dmabuf;
		vr->base.get_supported_dmabuf_formats = vulkan_renderer_get_supported_dmabuf_formats;
		vr->base.create_renderbuffer_dmabuf =
			vulkan_renderer_create_renderbuffer_dmabuf;

		ret = populate_supported_formats(ec, &vr->supported_formats);
		if (ret < 0)
			abort();

		if (vr->drm_fd > 0) {
			/* We support dmabuf feedback only when the renderer
			 * exposes a DRM-device */
			ec->dmabuf_feedback_format_table =
				weston_dmabuf_feedback_format_table_create(&vr->supported_formats);
			assert(ec->dmabuf_feedback_format_table);
			ret = create_default_dmabuf_feedback(ec, vr);
			if (ret < 0)
				abort();
		}
	}

	vkGetDeviceQueue(vr->dev, vr->queue_family, 0, &vr->queue);

	const VkCommandPoolCreateInfo cmd_pool_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = vr->queue_family,
	};
	result = vkCreateCommandPool(vr->dev, &cmd_pool_create_info, NULL, &vr->cmd_pool);
	check_vk_success(result, "vkCreateCommandPool");

	ec->read_format = pixel_format_get_info(DRM_FORMAT_ARGB8888);

	create_texture_image_dummy(vr); /* Workaround for solids */

	weston_log("Vulkan features:\n");
	weston_log_continue(STAMP_SPACE "wayland_surface: %s\n", yesno(vr->has_wayland_surface));
	weston_log_continue(STAMP_SPACE "xcb_surface: %s\n", yesno(vr->has_xcb_surface));
	weston_log_continue(STAMP_SPACE "incremental_present: %s\n", yesno(vr->has_incremental_present));
	weston_log_continue(STAMP_SPACE "image_drm_format_modifier: %s\n", yesno(vr->has_image_drm_format_modifier));
	weston_log_continue(STAMP_SPACE "external_semaphore_fd: %s\n", yesno(vr->has_external_semaphore_fd));
	weston_log_continue(STAMP_SPACE "physical_device_drm: %s\n", yesno(vr->has_physical_device_drm));
	weston_log_continue(STAMP_SPACE "external_memory_dma_buf: %s\n", yesno(vr->has_external_memory_dma_buf));
	weston_log_continue(STAMP_SPACE "queue_family_foreign: %s\n", yesno(vr->has_queue_family_foreign));
	weston_log_continue(STAMP_SPACE "semaphore_import_export: %s\n", yesno(vr->semaphore_import_export));

	return 0;
}

WL_EXPORT struct vulkan_renderer_interface vulkan_renderer_interface = {
	.display_create = vulkan_renderer_display_create,
	.output_window_create = vulkan_renderer_output_window_create,
	.output_fbo_create = vulkan_renderer_output_fbo_create,
	.output_destroy = vulkan_renderer_output_destroy,
	.create_fence_fd = vulkan_renderer_create_fence_fd,
};

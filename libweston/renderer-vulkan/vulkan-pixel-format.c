/*
 * Copyright © 2025 Erico Nunes
 *
 * Based on wlroots' vulkan pixel_format.c
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

#include <vulkan/vulkan.h>

#include <stdint.h>
#include "pixel-formats.h"
#include "shared/xalloc.h"
#include "vulkan-renderer-internal.h"

#include <xf86drm.h>
#include <drm_fourcc.h>

static const VkImageUsageFlags image_tex_usage =
	VK_IMAGE_USAGE_SAMPLED_BIT |
	VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

static const VkFormatFeatureFlags format_tex_features =
	VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
	VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

static bool
query_modifier_usage_support(struct vulkan_renderer *vr, VkFormat vk_format,
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

	result = vkGetPhysicalDeviceImageFormatProperties2(vr->phys_dev, &pdev_image_format_info, &image_format_props);
	if (result != VK_SUCCESS && result != VK_ERROR_FORMAT_NOT_SUPPORTED)
		return false;

	if (!(ext_mem_props->externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
		return false;

	return true;
}

static bool
query_dmabuf_support(struct vulkan_renderer *vr, VkFormat vk_format,
		     VkImageFormatProperties *out)
{
	VkResult result;

	VkPhysicalDeviceImageFormatInfo2 pdev_image_format_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
		.type = VK_IMAGE_TYPE_2D,
		.format = vk_format,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = image_tex_usage,
		.flags = 0,
	};

	VkImageFormatListCreateInfoKHR image_format_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR,
		.pViewFormats = &vk_format,
		.viewFormatCount = 1,
	};
	pnext(&pdev_image_format_info, &image_format_info);

	VkImageFormatProperties2 image_format_props = {
		image_format_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	};

	result = vkGetPhysicalDeviceImageFormatProperties2(vr->phys_dev, &pdev_image_format_info, &image_format_props);
	if (result != VK_SUCCESS) {
		if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
			weston_log("unsupported format\n");
		} else {
			weston_log("failed to get format properties\n");
		}
		return false;
	}

	*out = image_format_props.imageFormatProperties;
	return true;
}

static void
query_dmabuf_modifier_support(struct vulkan_renderer *vr, const struct pixel_format_info *format,
			      struct weston_drm_format *fmt)
{
	if (!vr->has_image_drm_format_modifier) {
		uint64_t modifier = DRM_FORMAT_MOD_INVALID;

		int ret = weston_drm_format_add_modifier(fmt, modifier);
		assert(ret == 0);

		char *modifier_name = drmGetFormatModifierName(modifier);
		weston_log("DRM dmabuf format %s (0x%08x) modifier %s (0x%016lx)\n",
			   format->drm_format_name ? format->drm_format_name : "<unknown>",
			   format->format,
			   modifier_name ? modifier_name : "<unknown>",
			   modifier);
		free(modifier_name);
		return;
	}

	VkDrmFormatModifierPropertiesListEXT drm_format_mod_props = {
		drm_format_mod_props.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
	};
	VkFormatProperties2 format_props = {
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	pnext(&format_props, &drm_format_mod_props);
	vkGetPhysicalDeviceFormatProperties2(vr->phys_dev, format->vulkan_format, &format_props);

	size_t modifier_count = drm_format_mod_props.drmFormatModifierCount;

	drm_format_mod_props.drmFormatModifierCount = modifier_count;
	drm_format_mod_props.pDrmFormatModifierProperties =
		xzalloc(modifier_count * sizeof(*drm_format_mod_props.pDrmFormatModifierProperties));

	vkGetPhysicalDeviceFormatProperties2(vr->phys_dev, format->vulkan_format, &format_props);

	for (uint32_t i = 0; i < drm_format_mod_props.drmFormatModifierCount; ++i) {
		VkDrmFormatModifierPropertiesEXT m = drm_format_mod_props.pDrmFormatModifierProperties[i];

		// check that specific modifier for texture usage
		if ((m.drmFormatModifierTilingFeatures & format_tex_features) != format_tex_features)
			continue;

		if (!query_modifier_usage_support(vr, format->vulkan_format, image_tex_usage, &m))
			continue;

		int ret = weston_drm_format_add_modifier(fmt, m.drmFormatModifier);
		assert(ret == 0);

		char *modifier_name = drmGetFormatModifierName(m.drmFormatModifier);
		weston_log("DRM dmabuf format %s (0x%08x) modifier %s (0x%016lx) %d planes\n",
			   format->drm_format_name ? format->drm_format_name : "<unknown>",
			   format->format,
			   modifier_name ? modifier_name : "<unknown>",
			   m.drmFormatModifier,
			   m.drmFormatModifierPlaneCount);
		free(modifier_name);

	}

	free(drm_format_mod_props.pDrmFormatModifierProperties);
}

bool
vulkan_renderer_query_dmabuf_format(struct vulkan_renderer *vr, const struct pixel_format_info *format)
{
	VkFormatProperties2 format_props = {
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};

	vkGetPhysicalDeviceFormatProperties2(vr->phys_dev, format->vulkan_format, &format_props);

	struct weston_drm_format *fmt = NULL;

	// dmabuf texture properties
	if ((format_props.formatProperties.optimalTilingFeatures & format_tex_features) != format_tex_features)
		return false;

	VkImageFormatProperties iformat_props;
	if (!query_dmabuf_support(vr, format->vulkan_format, &iformat_props))
		return false;

	fmt = weston_drm_format_array_add_format(&vr->supported_formats, format->format);
	assert(fmt);

	weston_log("DRM dmabuf format %s (0x%08x)\n",
		   format->drm_format_name ? format->drm_format_name : "<unknown>",
		   format->format);

	query_dmabuf_modifier_support(vr, format, fmt);

	return true;
}

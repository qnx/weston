/*
 * Copyright © 2025 Erico Nunes
 *
 * based on gl-shaders.c:
 * Copyright 2012 Intel Corporation
 * Copyright 2015,2019,2021 Collabora, Ltd.
 * Copyright 2016 NVIDIA Corporation
 * Copyright 2019 Harish Krupo
 * Copyright 2019 Intel Corporation
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

#include "config.h"

#include <vulkan/vulkan.h>

#include <stdint.h>
#include <string.h>

#include "vulkan-renderer.h"
#include "vulkan-renderer-internal.h"

/* const uint32_t vulkan_vertex_shader_surface[]; vulkan_vertex_shader_surface.vert */
#include "vulkan_vertex_shader_surface.spv.h"

/* const uint32_t vulkan_vertex_shader_texcoord[]; vulkan_vertex_shader_texcoord.vert */
#include "vulkan_vertex_shader_texcoord.spv.h"

/* const uint32_t vulkan_fragment_shader[]; vulkan_fragment_shader.frag */
#include "vulkan_fragment_shader.spv.h"

struct vertex {
	float pos[2];
};

struct vertex_tc {
	float pos[2];
	float texcoord[2];
};

struct fs_specialization_consts {
	uint32_t c_variant;
	uint32_t c_input_is_premult;
};

static void create_graphics_pipeline(struct vulkan_renderer *vr,
				      const struct vulkan_pipeline_requirements *req,
				      struct vulkan_pipeline *pipeline)
{
	VkResult result;

	VkShaderModule vs_module;
	VkShaderModuleCreateInfo vs_shader_module_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	};

	switch (req->texcoord_input) {
	case SHADER_TEXCOORD_INPUT_ATTRIB:
		vs_shader_module_create_info.codeSize = sizeof(vulkan_vertex_shader_texcoord),
		vs_shader_module_create_info.pCode = (uint32_t *)vulkan_vertex_shader_texcoord;
		break;
	case SHADER_TEXCOORD_INPUT_SURFACE:
		vs_shader_module_create_info.codeSize = sizeof(vulkan_vertex_shader_surface);
		vs_shader_module_create_info.pCode = (uint32_t *)vulkan_vertex_shader_surface;
		break;
	default:
		weston_log("Invalid req->texcoord_input\n");
		abort();
	}

	vkCreateShaderModule(vr->dev, &vs_shader_module_create_info, NULL, &vs_module);

	const struct fs_specialization_consts fsc = {
		req->variant,
		req->input_is_premult
	};
	const VkSpecializationMapEntry fsc_entries[] = {
		{ 0, 0, sizeof(fsc.c_variant) },
		{ 1, 0, sizeof(fsc.c_input_is_premult) },
	};
	const VkSpecializationInfo fs_specialization = {
		.mapEntryCount = ARRAY_LENGTH(fsc_entries),
		.pMapEntries = fsc_entries,
		.dataSize = sizeof(fsc),
		.pData = &fsc,
	};
	VkShaderModule fs_module;
	const VkShaderModuleCreateInfo fs_shader_module_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(vulkan_fragment_shader),
		.pCode = (uint32_t *)vulkan_fragment_shader,
	};
	vkCreateShaderModule(vr->dev, &fs_shader_module_create_info, NULL, &fs_module);

	const VkPipelineShaderStageCreateInfo vert_shader_stage_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vs_module,
		.pName = "main",
	};

	const VkPipelineShaderStageCreateInfo frag_shader_stage_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = fs_module,
		.pSpecializationInfo = &fs_specialization,
		.pName = "main",
	};

	const VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};

	// SHADER_TEXCOORD_INPUT_ATTRIB
	const VkPipelineVertexInputStateCreateInfo pipeline_vertex_input_attrib = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
			{
				.binding = 0,
				.stride = sizeof(struct vertex_tc),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			},
		},
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
			{
				.binding = 0,
				.location = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(struct vertex_tc, pos),
			},
			{
				.binding = 0,
				.location = 1,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(struct vertex_tc, texcoord),
			},
		}
	};

	// SHADER_TEXCOORD_INPUT_SURFACE
	const VkPipelineVertexInputStateCreateInfo pipeline_vertex_input_surface = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
			{
				.binding = 0,
				.stride = sizeof(struct vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			},
		},
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
			{
				.binding = 0,
				.location = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(struct vertex, pos),
			},
		}
	};

	const VkPipelineVertexInputStateCreateInfo *pipeline_vertex_input_state_create_info;

	switch (req->texcoord_input) {
	case SHADER_TEXCOORD_INPUT_ATTRIB:
		pipeline_vertex_input_state_create_info = &pipeline_vertex_input_attrib;
		break;
	case SHADER_TEXCOORD_INPUT_SURFACE:
		pipeline_vertex_input_state_create_info = &pipeline_vertex_input_surface;
		break;
	default:
		weston_log("Invalid req->texcoord_input\n");
		abort();
	}


	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
		.primitiveRestartEnable = VK_FALSE,
	};

	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterizer = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.lineWidth = 1.0f,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
	};

	const VkPipelineMultisampleStateCreateInfo multisampling = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.sampleShadingEnable = VK_FALSE,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};

	if (req->blend) {
		color_blend_attachment.blendEnable = VK_TRUE;
		color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}

	const VkPipelineColorBlendStateCreateInfo color_blending = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ARRAY_LENGTH(dynamic_states),
		.pDynamicStates = dynamic_states,
	};

	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &pipeline->descriptor_set_layout,
	};

	result = vkCreatePipelineLayout(vr->dev, &pipeline_layout_info, NULL, &pipeline->pipeline_layout);
	check_vk_success(result, "vkCreatePipelineLayout");

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shader_stages,
		.pVertexInputState = pipeline_vertex_input_state_create_info,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pColorBlendState = &color_blending,
		.pDynamicState = &dynamic_state,
		.layout = pipeline->pipeline_layout,
		.renderPass = req->renderpass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
	};

	result = vkCreateGraphicsPipelines(vr->dev, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline->pipeline);
	check_vk_success(result, "vkCreateGraphicsPipelines");

	vkDestroyShaderModule(vr->dev, fs_module, NULL);
	vkDestroyShaderModule(vr->dev, vs_module, NULL);
}

static void
create_descriptor_set_layout(struct vulkan_renderer *vr, struct vulkan_pipeline *pipeline)
{
	VkResult result;

	const VkDescriptorSetLayoutBinding vs_ubo_layout_binding = {
		.binding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutBinding fs_ubo_layout_binding = {
		.binding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutBinding fs_sampler_layout_binding = {
		.binding = 2,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutBinding bindings[] = {
		vs_ubo_layout_binding,
		fs_ubo_layout_binding,
		fs_sampler_layout_binding,
	};
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = ARRAY_LENGTH(bindings),
		.pBindings = bindings,
	};

	result = vkCreateDescriptorSetLayout(vr->dev, &layout_info, NULL, &pipeline->descriptor_set_layout);
	check_vk_success(result, "vkCreateDescriptorSetLayout");
}

static struct vulkan_pipeline *
vulkan_pipeline_create(struct vulkan_renderer *vr,
		 const struct vulkan_pipeline_requirements *reqs)
{
	struct vulkan_pipeline *pipeline = NULL;

	pipeline = zalloc(sizeof *pipeline);
	if (!pipeline) {
		weston_log("could not create pipeline\n");
		abort();
	}

	wl_list_init(&pipeline->link);
	pipeline->key = *reqs;

	create_descriptor_set_layout(vr, pipeline);

	create_graphics_pipeline(vr, reqs, pipeline);

	wl_list_insert(&vr->pipeline_list, &pipeline->link);

	return pipeline;
}

void
vulkan_pipeline_destroy(struct vulkan_renderer *vr, struct vulkan_pipeline *pipeline)
{
	vkDestroyPipelineLayout(vr->dev, pipeline->pipeline_layout, NULL);
	vkDestroyPipeline(vr->dev, pipeline->pipeline, NULL);
	vkDestroyDescriptorSetLayout(vr->dev, pipeline->descriptor_set_layout, NULL);
	wl_list_remove(&pipeline->link);
	free(pipeline);
}

void
vulkan_renderer_pipeline_list_destroy(struct vulkan_renderer *vr)
{
	struct vulkan_pipeline *pipeline, *next_pipeline;

	wl_list_for_each_safe(pipeline, next_pipeline, &vr->pipeline_list, link)
		vulkan_pipeline_destroy(vr, pipeline);
}

static int
vulkan_pipeline_requirements_cmp(const struct vulkan_pipeline_requirements *a,
				 const struct vulkan_pipeline_requirements *b)
{
	return memcmp(a, b, sizeof(*a));
}

struct vulkan_pipeline *
vulkan_renderer_get_pipeline(struct vulkan_renderer *vr,
			     const struct vulkan_pipeline_requirements *reqs)
{
	struct vulkan_pipeline *pipeline;

	wl_list_for_each(pipeline, &vr->pipeline_list, link) {
		if (vulkan_pipeline_requirements_cmp(reqs, &pipeline->key) == 0)
			return pipeline;
	}

	pipeline = vulkan_pipeline_create(vr, reqs);
	if (pipeline)
		return pipeline;

	return NULL;
}


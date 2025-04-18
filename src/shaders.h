#pragma once

#include "common.h"

struct Shader
{
	VkShaderModule module;
	VkShaderStageFlagBits stage;

	// TODO: Replace with array of 32 descriptor types
	uint32_t storageBufferMask;

	bool usePushConstants;
};

struct Program
{
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout layout;
	VkDescriptorUpdateTemplate updateTemplate;
	VkShaderStageFlags pushConstantStages;
};

bool loadShader(Shader& shader, VkDevice device, const char* path);
void destroyShader(Shader& shader, VkDevice device);

using Shaders = std::initializer_list<const Shader*>;

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device, Shaders shaders);
VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, Shaders shaders, VkShaderStageFlags pushConstantStages, size_t pushConstantSize);
VkDescriptorUpdateTemplate createUpdateTemplate(VkDevice device, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, Shaders shaders);
VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineCache pipelineCache, VkRenderPass renderPass, Shaders shaders, VkPipelineLayout layout);
VkPipeline createComputePipeline(VkDevice device, VkPipelineCache pipelineCache, const Shader& shader, VkPipelineLayout layout);

Program createProgram(VkDevice device, VkPipelineBindPoint bindPoint, Shaders shaders, size_t pushConstantSize);
void destroyProgram(VkDevice device, Program& program);

struct DescriptorInfo
{
	union
	{
		VkDescriptorImageInfo image;
		VkDescriptorBufferInfo buffer;
	};

	DescriptorInfo()
	{
	}

	DescriptorInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
	{
		image.sampler = sampler;
		image.imageView = imageView;
		image.imageLayout = imageLayout;
	}

	DescriptorInfo(VkBuffer buffer_, VkDeviceSize offset, VkDeviceSize range)
	{
		buffer.buffer = buffer_;
		buffer.offset = offset;
		buffer.range = range;
	}

	DescriptorInfo(VkBuffer buffer_)
	{
		buffer.buffer = buffer_;
		buffer.offset = 0;
		buffer.range = VK_WHOLE_SIZE;
	}
	
};
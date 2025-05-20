#pragma once

#include "common.h"

struct Shader
{
	VkShaderModule module;
	VkShaderStageFlagBits stage;

	VkDescriptorType resourceTypes[32];
	uint32_t resourceMask;

	uint32_t localSizeX;
	uint32_t localSizeY;
	uint32_t localSizeZ;

	bool usePushConstants;
};

struct Program
{
	VkPipelineBindPoint bindPoint;
	VkPipelineLayout layout;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorUpdateTemplate updateTemplate;
	VkShaderStageFlags pushConstantStages;
};

bool loadShader(Shader& shader, VkDevice device, const char* path);
void destroyShader(Shader& shader, VkDevice device);

using Shaders = std::initializer_list<const Shader*>;

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device, Shaders shaders, bool pushDescriptorsSupported);
VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, VkShaderStageFlags pushConstantStages, size_t pushConstantSize);
VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineCache pipelineCache, VkRenderPass renderPass, Shaders shaders, VkPipelineLayout layout);
VkPipeline createComputePipeline(VkDevice device, VkPipelineCache pipelineCache, const Shader& shader, VkPipelineLayout layout, bool useSpecializationConstants = false, VkBool32 LATE = VK_FALSE);

Program createProgram(VkDevice device, VkPipelineBindPoint bindPoint, Shaders shaders, size_t pushConstantSize, bool pushDescriptorsSupported);
void destroyProgram(VkDevice device, Program& program);

inline uint32_t getGroupCount(uint32_t threadCount, uint32_t localSize)
{
	return (threadCount + localSize - 1) / localSize;
}

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

	DescriptorInfo(VkImageView imageView, VkImageLayout imageLayout)
	{
		image.sampler = VK_NULL_HANDLE;
		image.imageView = imageView;
		image.imageLayout = imageLayout;
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
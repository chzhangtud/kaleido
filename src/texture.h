#pragma once

#include <volk.h>

struct Image;
struct Buffer;

bool loadImage(Image& image, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue,
	const VkPhysicalDeviceMemoryProperties& memoryProperties, const Buffer& scratch, const char* path);

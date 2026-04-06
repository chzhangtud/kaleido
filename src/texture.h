#pragma once

#include <volk.h>

struct Image;
struct Buffer;

bool loadImage(Image& image, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue,
    const VkPhysicalDeviceMemoryProperties& memoryProperties, const Buffer& scratch, const char* path);

// Raw PNG/JPEG (or other stb-supported) bytes, e.g. glTF image from GLB bufferView.
bool loadImageFromMemory(Image& image, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue,
    const VkPhysicalDeviceMemoryProperties& memoryProperties, const Buffer& scratch, const void* data, size_t size);

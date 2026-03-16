#pragma once

#include <memory>

#include "common.h"
#include "resources.h"

// Simple wrapper that centralizes GPU resource creation and destruction.
// In this phase it is a thin shell over Vulkan helpers, but all buffers/textures
// used by the renderer should go through this manager instead of calling
// vkCreateImage / vkCreateBuffer directly.
class RenderResourceManager
{
public:
	RenderResourceManager() = default;

	RenderResourceManager(VkDevice device, const VkPhysicalDeviceMemoryProperties& memoryProps)
	    : m_device(device)
	    , m_memoryProperties(memoryProps)
	{
	}

	void Initialize(VkDevice device, const VkPhysicalDeviceMemoryProperties& memoryProps)
	{
		m_device = device;
		m_memoryProperties = memoryProps;
	}

	const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_memoryProperties; }

	// Buffer API
	void CreateBuffer(Buffer& out, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags)
	{
		createBuffer(out, m_device, m_memoryProperties, size, usage, memoryFlags);
	}

	void DestroyBuffer(const Buffer& buffer)
	{
		if (buffer.buffer != VK_NULL_HANDLE)
		{
			destroyBuffer(buffer, m_device);
		}
	}

	// Texture / image API
	void CreateImage(Image& out, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkBufferUsageFlags usage)
	{
		createImage(out, m_device, m_memoryProperties, width, height, mipLevels, format, usage);
	}

	void DestroyImage(const Image& image)
	{
		if (image.image != VK_NULL_HANDLE)
		{
			destroyImage(image, m_device);
		}
	}

	// Sampler API
	VkSampler CreateSampler(VkFilter filter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode addressMode, VkSamplerReductionModeEXT reductionMode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT)
	{
		return createSampler(m_device, filter, mipmapMode, addressMode, reductionMode);
	}

	void DestroySampler(VkSampler sampler)
	{
		if (sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(m_device, sampler, nullptr);
		}
	}

private:
	VkDevice m_device{ VK_NULL_HANDLE };
	VkPhysicalDeviceMemoryProperties m_memoryProperties{};
};


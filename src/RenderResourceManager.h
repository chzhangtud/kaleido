#pragma once

#include <memory>
#include <vector>

#include "common.h"
#include "resources.h"
#include "RenderBackend.h"

// Virtual handles used by the RenderGraph. ID 0 is always invalid.
struct RGTextureHandle
{
	uint32_t id = 0;
	bool IsValid() const { return id != 0; }
};

struct RGBufferHandle
{
	uint32_t id = 0;
	bool IsValid() const { return id != 0; }
};

// Descriptors used to describe virtual resources.
struct RGTextureDesc
{
	uint32_t     width        = 0;
	uint32_t     height       = 0;
	uint32_t     mipLevels    = 1;
	TextureFormat format      = TextureFormat::Unknown;
	TextureUsage  usage       = TextureUsage::Unknown;
};

struct RGBufferDesc
{
	uint64_t    size        = 0;
	BufferUsage usage       = BufferUsage::Unknown;
	bool        hostVisible = false;
};

inline bool operator==(const RGTextureDesc& a, const RGTextureDesc& b)
{
	return a.width == b.width &&
	       a.height == b.height &&
	       a.mipLevels == b.mipLevels &&
	       a.format == b.format &&
	       a.usage == b.usage;
}

inline bool operator==(const RGBufferDesc& a, const RGBufferDesc& b)
{
	return a.size == b.size &&
	       a.usage == b.usage &&
	       a.hostVisible == b.hostVisible;
}

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

	// Frame lifecycle: transient resources created during the frame are automatically released on EndFrame.
	void BeginFrame()
	{
		m_frameTempTextures.clear();
		m_frameTempBuffers.clear();
		m_frameTempViews.clear();
	}

	void EndFrame()
	{
		for (RGTextureHandle h : m_frameTempTextures)
			ReleaseTexture(h);
		for (RGBufferHandle h : m_frameTempBuffers)
			ReleaseBuffer(h);
		for (VkImageView v : m_frameTempViews)
			ReleaseImageView(v);
		m_frameTempTextures.clear();
		m_frameTempBuffers.clear();
		m_frameTempViews.clear();
	}

	// ---------------------------------------------------------------------------------------------
	// Low-level immediate APIs (existing usage in renderer).
	// ---------------------------------------------------------------------------------------------

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

	// ImageView API (cached/pooled). Useful for mip views (e.g. depth pyramid).
	VkImageView AcquireImageView(VkImage image, VkFormat format, uint32_t mipLevel, uint32_t levelCount, bool transient = false)
	{
		for (auto& record : m_viewPool)
		{
			if (!record.inUse &&
			    record.image == image &&
			    record.format == format &&
			    record.mipLevel == mipLevel &&
			    record.levelCount == levelCount)
			{
				record.inUse = true;
				if (transient)
					m_frameTempViews.push_back(record.view);
				return record.view;
			}
		}

		VkImageView view = createImageView(m_device, image, format, mipLevel, levelCount);
		ViewRecord record{};
		record.image = image;
		record.format = format;
		record.mipLevel = mipLevel;
		record.levelCount = levelCount;
		record.view = view;
		record.inUse = true;
		m_viewPool.push_back(record);

		if (transient)
			m_frameTempViews.push_back(view);
		return view;
	}

	// ImageView API (uncached). Useful for swapchain image views (tied to swapchain lifetime).
	VkImageView CreateImageView(VkImage image, VkFormat format, uint32_t mipLevel, uint32_t levelCount)
	{
		return createImageView(m_device, image, format, mipLevel, levelCount);
	}

	void ReleaseImageView(VkImageView view)
	{
		if (view == VK_NULL_HANDLE)
			return;
		for (auto& record : m_viewPool)
		{
			if (record.view == view)
			{
				record.inUse = false;
				return;
			}
		}
	}

	void DestroyImageView(VkImageView view)
	{
		if (view == VK_NULL_HANDLE)
			return;
		// If this view is from the cache, remove it from the pool.
		for (size_t i = 0; i < m_viewPool.size(); ++i)
		{
			if (m_viewPool[i].view == view)
			{
				vkDestroyImageView(m_device, view, nullptr);
				m_viewPool.erase(m_viewPool.begin() + i);
				return;
			}
		}
		// Otherwise treat it as an uncached/owned view.
		vkDestroyImageView(m_device, view, nullptr);
	}

	// ---------------------------------------------------------------------------------------------
	// Handle-based APIs for RenderGraph.
	// ---------------------------------------------------------------------------------------------

	RGTextureHandle CreateTexture(const RGTextureDesc& desc, bool transient = false)
	{
		// Try to reuse an existing compatible texture.
		for (size_t i = 0; i < m_texturePool.size(); ++i)
		{
			auto& record = m_texturePool[i];
			if (!record.inUse && record.desc == desc)
			{
				record.inUse = true;
				if (record.handle.id == 0)
				{
					record.handle.id = static_cast<uint32_t>(i + 1);
				}
				if (transient)
					m_frameTempTextures.push_back(record.handle);
				return record.handle;
			}
		}

		// Create a new one.
		Image image{};
		VkFormat vkFormat = ToVkFormat(desc.format);
		VkBufferUsageFlags usageFlags = ToVkUsageFlags(desc.usage);
		createImage(image, m_device, m_memoryProperties, desc.width, desc.height, desc.mipLevels, vkFormat, usageFlags);

		TextureRecord record{};
		record.desc = desc;
		record.image = image;
		record.inUse = true;
		record.handle.id = static_cast<uint32_t>(m_texturePool.size() + 1);

		m_texturePool.push_back(record);

		LOGI("RG: Created new texture handle %u for %ux%u", record.handle.id, desc.width, desc.height);
		if (transient)
			m_frameTempTextures.push_back(record.handle);
		return record.handle;
	}

	Image* GetTexture(RGTextureHandle handle)
	{
		if (!handle.IsValid())
			return nullptr;
		uint32_t index = handle.id - 1;
		if (index >= m_texturePool.size())
			return nullptr;
		return &m_texturePool[index].image;
	}

	void ReleaseTexture(RGTextureHandle handle)
	{
		if (!handle.IsValid())
			return;
		uint32_t index = handle.id - 1;
		if (index >= m_texturePool.size())
			return;
		m_texturePool[index].inUse = false;
	}

	RGBufferHandle CreateBuffer(const RGBufferDesc& desc, bool transient = false)
	{
		// Try to reuse an existing compatible buffer.
		for (size_t i = 0; i < m_bufferPool.size(); ++i)
		{
			auto& record = m_bufferPool[i];
			if (!record.inUse && record.desc == desc)
			{
				record.inUse = true;
				if (record.handle.id == 0)
				{
					record.handle.id = static_cast<uint32_t>(i + 1);
				}
				if (transient)
					m_frameTempBuffers.push_back(record.handle);
				return record.handle;
			}
		}

		Buffer buffer{};
		VkBufferUsageFlags usageFlags = ToVkBufferUsageFlags(desc.usage);
		VkMemoryPropertyFlags memoryFlags = desc.hostVisible
		                                       ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		                                       : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		createBuffer(buffer, m_device, m_memoryProperties, desc.size, usageFlags, memoryFlags);

		BufferRecord record{};
		record.desc = desc;
		record.buffer = buffer;
		record.inUse = true;
		record.handle.id = static_cast<uint32_t>(m_bufferPool.size() + 1);

		m_bufferPool.push_back(record);

		LOGI("RG: Created new buffer handle %u (size=%llu)", record.handle.id, static_cast<unsigned long long>(desc.size));
		if (transient)
			m_frameTempBuffers.push_back(record.handle);
		return record.handle;
	}

	Buffer* GetBuffer(RGBufferHandle handle)
	{
		if (!handle.IsValid())
			return nullptr;
		uint32_t index = handle.id - 1;
		if (index >= m_bufferPool.size())
			return nullptr;
		return &m_bufferPool[index].buffer;
	}

	void ReleaseBuffer(RGBufferHandle handle)
	{
		if (!handle.IsValid())
			return;
		uint32_t index = handle.id - 1;
		if (index >= m_bufferPool.size())
			return;
		m_bufferPool[index].inUse = false;
	}

	// Destroy all pooled resources. Should be called during shutdown when the device is idle.
	void DestroyAll()
	{
		// Destroy views first (they may reference pooled images).
		for (auto& record : m_viewPool)
		{
			if (record.view != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_device, record.view, nullptr);
				record.view = VK_NULL_HANDLE;
				record.inUse = false;
			}
		}
		m_viewPool.clear();

		for (auto& record : m_texturePool)
		{
			if (record.image.image != VK_NULL_HANDLE)
			{
				destroyImage(record.image, m_device);
				record.image = {};
				record.handle.id = 0;
				record.inUse = false;
			}
		}

		for (auto& record : m_bufferPool)
		{
			if (record.buffer.buffer != VK_NULL_HANDLE)
			{
				destroyBuffer(record.buffer, m_device);
				record.buffer = {};
				record.handle.id = 0;
				record.inUse = false;
			}
		}
	}

private:
	struct ViewRecord
	{
		VkImage     image{ VK_NULL_HANDLE };
		VkFormat    format{ VK_FORMAT_UNDEFINED };
		uint32_t    mipLevel{ 0 };
		uint32_t    levelCount{ 1 };
		VkImageView view{ VK_NULL_HANDLE };
		bool        inUse{ false };
	};

	struct TextureRecord
	{
		RGTextureDesc  desc{};
		Image          image{};
		RGTextureHandle handle{};
		bool           inUse = false;
	};

	struct BufferRecord
	{
		RGBufferDesc   desc{};
		Buffer         buffer{};
		RGBufferHandle handle{};
		bool           inUse = false;
	};

	static VkFormat ToVkFormat(TextureFormat format)
	{
		switch (format)
		{
		case TextureFormat::R8_UNorm:      return VK_FORMAT_R8_UNORM;
		case TextureFormat::RG8_UNorm:     return VK_FORMAT_R8G8_UNORM;
		case TextureFormat::RGBA8_UNorm:   return VK_FORMAT_R8G8B8A8_UNORM;
		case TextureFormat::A2B10G10R10_UNorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		case TextureFormat::RGBA16_Float:  return VK_FORMAT_R16G16B16A16_SFLOAT;
		case TextureFormat::R32_Float:     return VK_FORMAT_R32_SFLOAT;
		case TextureFormat::RGBA32_Float:  return VK_FORMAT_R32G32B32A32_SFLOAT;
		case TextureFormat::D24S8:         return VK_FORMAT_D24_UNORM_S8_UINT;
		case TextureFormat::D32_Float:     return VK_FORMAT_D32_SFLOAT;
		default:                           return VK_FORMAT_UNDEFINED;
		}
	}

	static VkBufferUsageFlags ToVkBufferUsageFlags(BufferUsage usage)
	{
		VkBufferUsageFlags flags = 0;
		const uint32_t bits = static_cast<uint32_t>(usage);
		if (bits & static_cast<uint32_t>(BufferUsage::Vertex))
			flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		if (bits & static_cast<uint32_t>(BufferUsage::Index))
			flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if (bits & static_cast<uint32_t>(BufferUsage::Uniform))
			flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		if (bits & static_cast<uint32_t>(BufferUsage::Storage))
			flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (bits & static_cast<uint32_t>(BufferUsage::Indirect))
			flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		return flags;
	}

	static VkBufferUsageFlags ToVkUsageFlags(TextureUsage usage)
	{
		VkBufferUsageFlags flags = 0;
		const uint32_t bits = static_cast<uint32_t>(usage);
		if (bits & static_cast<uint32_t>(TextureUsage::Sampled))
			flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (bits & static_cast<uint32_t>(TextureUsage::ColorAttachment))
			flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (bits & static_cast<uint32_t>(TextureUsage::DepthStencil))
			flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (bits & static_cast<uint32_t>(TextureUsage::Storage))
			flags |= VK_IMAGE_USAGE_STORAGE_BIT;
		if (bits & static_cast<uint32_t>(TextureUsage::TransferSrc))
			flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (bits & static_cast<uint32_t>(TextureUsage::TransferDst))
			flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		return flags;
	}

private:
	VkDevice m_device{ VK_NULL_HANDLE };
	VkPhysicalDeviceMemoryProperties m_memoryProperties{};

	std::vector<ViewRecord>   m_viewPool;
	std::vector<TextureRecord> m_texturePool;
	std::vector<BufferRecord>  m_bufferPool;

	std::vector<RGTextureHandle> m_frameTempTextures;
	std::vector<RGBufferHandle>  m_frameTempBuffers;
	std::vector<VkImageView>      m_frameTempViews;
};

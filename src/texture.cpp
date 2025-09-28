#include "texture.h"

#include "common.h"
#include "resources.h"

#include <stdio.h>
#include <memory>

#include <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct DDS_PIXELFORMAT
{
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwFourCC;
	unsigned int dwRGBBitCount;
	unsigned int dwRBitMask;
	unsigned int dwGBitMask;
	unsigned int dwBBitMask;
	unsigned int dwABitMask;
};

struct DDS_HEADER
{
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwHeight;
	unsigned int dwWidth;
	unsigned int dwPitchOrLinearSize;
	unsigned int dwDepth;
	unsigned int dwMipMapCount;
	unsigned int dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	unsigned int dwCaps;
	unsigned int dwCaps2;
	unsigned int dwCaps3;
	unsigned int dwCaps4;
	unsigned int dwReserved2;
};

struct DDS_HEADER_DXT10
{
	unsigned int dxgiFormat;
	unsigned int resourceDimension;
	unsigned int miscFlag;
	unsigned int arraySize;
	unsigned int miscFlags2;
};

const unsigned int DDSCAPS2_CUBEMAP = 0x200;
const unsigned int DDSCAPS2_VOLUME = 0x200000;

const unsigned int DDS_DIMENSION_TEXTURE2D = 3;

enum DXGI_FORMAT
{
	DXGI_FORMAT_BC1_UNORM = 71,
	DXGI_FORMAT_BC1_UNORM_SRGB = 72,
	DXGI_FORMAT_BC2_UNORM = 74,
	DXGI_FORMAT_BC2_UNORM_SRGB = 75,
	DXGI_FORMAT_BC3_UNORM = 77,
	DXGI_FORMAT_BC3_UNORM_SRGB = 78,
	DXGI_FORMAT_BC4_UNORM = 80,
	DXGI_FORMAT_BC4_SNORM = 81,
	DXGI_FORMAT_BC5_UNORM = 83,
	DXGI_FORMAT_BC5_SNORM = 84,
	DXGI_FORMAT_BC6H_UF16 = 95,
	DXGI_FORMAT_BC6H_SF16 = 96,
	DXGI_FORMAT_BC7_UNORM = 98,
	DXGI_FORMAT_BC7_UNORM_SRGB = 99,
};

static unsigned int fourCC(const char (&str)[5])
{
	return (unsigned(str[0]) << 0) | (unsigned(str[1]) << 8) | (unsigned(str[2]) << 16) | (unsigned(str[3]) << 24);
}

static VkFormat getFormat(const DDS_HEADER& header, const DDS_HEADER_DXT10& header10)
{
	if (header.ddspf.dwFourCC == fourCC("DXT1"))
		return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("DXT3"))
		return VK_FORMAT_BC2_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("DXT5"))
		return VK_FORMAT_BC3_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("ATI1"))
		return VK_FORMAT_BC4_UNORM_BLOCK;
	if (header.ddspf.dwFourCC == fourCC("ATI2"))
		return VK_FORMAT_BC5_UNORM_BLOCK;

	if (header.ddspf.dwFourCC == fourCC("DX10"))
	{
		switch (header10.dxgiFormat)
		{
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
			return VK_FORMAT_BC2_UNORM_BLOCK;
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case DXGI_FORMAT_BC4_UNORM:
			return VK_FORMAT_BC4_UNORM_BLOCK;
		case DXGI_FORMAT_BC4_SNORM:
			return VK_FORMAT_BC4_SNORM_BLOCK;
		case DXGI_FORMAT_BC5_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case DXGI_FORMAT_BC5_SNORM:
			return VK_FORMAT_BC5_SNORM_BLOCK;
		case DXGI_FORMAT_BC6H_UF16:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
		case DXGI_FORMAT_BC6H_SF16:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

static size_t getImageSizeBC(unsigned int width, unsigned int height, unsigned int levels, unsigned int blockSize)
{
	size_t result = 0;

	for (unsigned int i = 0; i < levels; ++i)
	{
		result += ((width + 3) / 4) * ((height + 3) / 4) * blockSize;

		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}

	return result;
}

bool loadDDSImage(Image& image, VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties, const Buffer& scratch, const char* path)
{
#if defined(WIN32)
	FILE* file = fopen(path, "rb");
	if (!file)
		return false;
	std::unique_ptr<FILE, int (*)(FILE*)> filePtr(file, fclose);

	auto readData = [&](void* dst, size_t size) -> bool
	{
		return fread(dst, 1, size, file) == size;
	};
#elif defined(__ANDROID__)
	AAsset* asset = AAssetManager_open(g_assetManager, path, AASSET_MODE_BUFFER);
	if (!asset)
	{
		LOGE("Failed to open DDS asset: %s", path);
		return false;
	}

	// Get size and read asset into memory
	off_t assetLength = AAsset_getLength(asset);
	std::vector<uint8_t> fileData(assetLength);
	int64_t bytesRead = AAsset_read(asset, fileData.data(), assetLength);
	AAsset_close(asset);

	if (bytesRead != assetLength)
	{
		LOGE("Failed to read full DDS asset: %s", path);
		return false;
	}

	const uint8_t* dataPtr = fileData.data();
	size_t dataOffset = 0;

	auto readData = [&](void* dst, size_t size) -> bool
	{
		if (dataOffset + size > fileData.size())
			return false;
		memcpy(dst, dataPtr + dataOffset, size);
		dataOffset += size;
		return true;
	};
#endif

	unsigned int magic = 0;
	if (!readData(&magic, sizeof(magic)) || magic != fourCC("DDS "))
		return false;

	DDS_HEADER header = {};
	if (!readData(&header, sizeof(header)))
		return false;

	DDS_HEADER_DXT10 header10 = {};
	if (header.ddspf.dwFourCC == fourCC("DX10") && !readData(&header10, sizeof(header10)))
		return false;

	if (header.dwSize != sizeof(header) || header.ddspf.dwSize != sizeof(header.ddspf))
		return false;

	if (header.dwCaps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME))
		return false;

	if (header.ddspf.dwFourCC == fourCC("DX10") && header10.resourceDimension != DDS_DIMENSION_TEXTURE2D)
		return false;

	VkFormat format = getFormat(header, header10);
	if (format == VK_FORMAT_UNDEFINED)
		return false;

	unsigned int blockSize = (format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || format == VK_FORMAT_BC4_SNORM_BLOCK || format == VK_FORMAT_BC4_UNORM_BLOCK) ? 8 : 16;
	size_t imageSize = getImageSizeBC(header.dwWidth, header.dwHeight, header.dwMipMapCount, blockSize);

	if (scratch.size < imageSize)
		return false;

	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	createImage(image, device, memoryProperties, header.dwWidth, header.dwHeight, header.dwMipMapCount, format, usage);

#if defined(WIN32)
	size_t readSize = fread(scratch.data, 1, imageSize, file);
	if (readSize != imageSize)
		return false;

	if (fgetc(file) != -1)
		return false;

	filePtr.reset();
	file = nullptr;
#elif defined(__ANDROID__)
	if (!readData(scratch.data, imageSize))
		return false;
#endif

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	VkImageMemoryBarrier2 preBarrier = imageBarrier(image.image,
	    0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
	    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &preBarrier);

	size_t bufferOffset = 0;
	unsigned int mipWidth = header.dwWidth, mipHeight = header.dwHeight;

	for (unsigned int i = 0; i < header.dwMipMapCount; ++i)
	{
		VkBufferImageCopy region = { bufferOffset, 0, 0, {
			                                                 VK_IMAGE_ASPECT_COLOR_BIT,
			                                                 i,
			                                                 0,
			                                                 1,
			                                             },
			{ 0, 0, 0 }, { mipWidth, mipHeight, 1 } };
		vkCmdCopyBufferToImage(commandBuffer, scratch.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		bufferOffset += ((mipWidth + 3) / 4) * ((mipHeight + 3) / 4) * blockSize;

		mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
	}
	assert(bufferOffset == imageSize);

	VkImageMemoryBarrier2 postBarrier = imageBarrier(image.image,
	    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &postBarrier);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

	VK_CHECK(vkDeviceWaitIdle(device));

	return true;
}

uint32_t get_memory_type(uint32_t bits, const VkPhysicalDeviceMemoryProperties& memoryProperties, VkMemoryPropertyFlags properties, VkBool32* memory_type_found = nullptr)
{
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((bits & 1) == 1)
		{
			if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				if (memory_type_found)
				{
					*memory_type_found = true;
				}
				return i;
			}
		}
		bits >>= 1;
	}

	if (memory_type_found)
	{
		*memory_type_found = false;
		return ~0;
	}
	else
	{
		LOGE("Could not find a matching memory type");
		assert(false);
	}
}

bool loadKtxImage(Image& image, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties, const char* path)
{
	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
	ktxTexture* ktxTexture = nullptr;
	KTX_error_code result;

#if defined(WIN32)
	result = ktxTexture_CreateFromNamedFile(path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#elif defined(__ANDROID__)
	AAsset* asset = AAssetManager_open(g_assetManager, path, AASSET_MODE_BUFFER);
	if (!asset)
	{
		LOGE("Error: Cannot open asset %s", path);
		return false;
	}

	size_t size = AAsset_getLength(asset);
	std::vector<uint8_t> buffer(size);
	int64_t bytesRead = AAsset_read(asset, buffer.data(), size);
	AAsset_close(asset);

	if (bytesRead != size)
	{
		LOGE("Error: Failed to read asset %s", path);
		return false;
	}

	result = ktxTexture_CreateFromMemory(buffer.data(), buffer.size(),
	    KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif

	if (ktxTexture == nullptr)
	{
		LOGE("Error: KTX image %s failed to load: %s", path, ktxErrorString(result));
		return false;
	}

	uint32_t width = ktxTexture->baseWidth;
	uint32_t height = ktxTexture->baseHeight;
	uint32_t mipLevels = ktxTexture->numLevels;

	bool useStaging = true;
	bool forceLinearTiling = false;
	if (forceLinearTiling)
	{
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
		useStaging = !(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	}

	VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkMemoryRequirements memoryRequirements = {};

	ktx_uint8_t* ktxImageData = ktxTexture->pData;
	ktx_size_t ktxtextureSize = ktxTexture->dataSize;

	if (useStaging)
	{
		// Copy data to an optimal tiled image
		// This loads the texture data into a host local buffer that is copied to the optimal tiled image to the device

		// Create a host-visible staging buffer that contains the raw image data
		// This buffer will be the data source for copying texture data to the optimal tiled image on the device
		VkBuffer stagingBuffer = 0;
		VkDeviceMemory stagingMemory = 0;

		VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferCreateInfo.size = ktxtextureSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

		// Get memory requirements for the staging buffer (alignment, memory byte bits)
		vkGetBufferMemoryRequirements(device, stagingBuffer, &memoryRequirements);
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		// Get memory type index for a host visible buffer
		memoryAllocateInfo.memoryTypeIndex = get_memory_type(memoryRequirements.memoryTypeBits, memoryProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &stagingMemory));
		VK_CHECK(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

		// Copy texture data into hhost local staging buffer
		uint8_t* data = nullptr;
		VK_CHECK(vkMapMemory(device, stagingMemory, 0, memoryRequirements.size, 0, (void**)&data));
		memcpy(data, ktxImageData, ktxtextureSize);
		vkUnmapMemory(device, stagingMemory);

		// Setup buffer copy regions for each mip level
		std::vector<VkBufferImageCopy> bufferCopyRegions;
		for (uint32_t i = 0; i < mipLevels; ++i)
		{
			ktx_size_t offset;
			KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
			VkBufferImageCopy buffer_copy_region = {};
			buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			buffer_copy_region.imageSubresource.mipLevel = i;
			buffer_copy_region.imageSubresource.baseArrayLayer = 0;
			buffer_copy_region.imageSubresource.layerCount = 1;
			buffer_copy_region.imageExtent.width = ktxTexture->baseWidth >> i;
			buffer_copy_region.imageExtent.height = ktxTexture->baseHeight >> i;
			buffer_copy_region.imageExtent.depth = 1;
			buffer_copy_region.bufferOffset = offset;
			bufferCopyRegions.push_back(buffer_copy_region);
		}

		// Create optimal tiled target image on the device
		VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// Set initial layout of the image to undefined
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK(vkCreateImage(device, &imageCreateInfo, nullptr, &image.image));

		vkGetImageMemoryRequirements(device, image.image, &memoryRequirements);
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = get_memory_type(memoryRequirements.memoryTypeBits, memoryProperties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &image.memory));
		VK_CHECK(vkBindImageMemory(device, image.image, image.memory, 0));

		// Image memory barriers for the texture image

		VK_CHECK(vkResetCommandPool(device, commandPool, 0));

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
		VkImageSubresourceRange subresource_range = {};
		// Image only contains color data
		subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// Start at first mip level
		subresource_range.baseMipLevel = 0;
		// We will transition on all mip levels
		subresource_range.levelCount = mipLevels;
		// The 2D texture only has one layer
		subresource_range.layerCount = 1;

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		VkImageMemoryBarrier imageMemoryBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

		imageMemoryBarrier.image = image.image;
		imageMemoryBarrier.subresourceRange = subresource_range;
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage is host write/read execution (VK_PIPELINE_STAGE_HOST_BIT)
		// Destination pipeline stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
		vkCmdPipelineBarrier(
		    commandBuffer,
		    VK_PIPELINE_STAGE_HOST_BIT,
		    VK_PIPELINE_STAGE_TRANSFER_BIT,
		    0,
		    0, nullptr,
		    0, nullptr,
		    1, &imageMemoryBarrier);

		// Copy mip levels from staging buffer
		vkCmdCopyBufferToImage(
		    commandBuffer,
		    stagingBuffer,
		    image.image,
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		    static_cast<uint32_t>(bufferCopyRegions.size()),
		    bufferCopyRegions.data());

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
		// Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
		vkCmdPipelineBarrier(
		    commandBuffer,
		    VK_PIPELINE_STAGE_TRANSFER_BIT,
		    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		    0,
		    0, nullptr,
		    0, nullptr,
		    1, &imageMemoryBarrier);

		VK_CHECK(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		// TODO: waitIdle is ok here because loading image only happens at start, but if starting time
		// is too long, maybe it is better to try async loading and some other synchronization methods instead of
		// brute-force waiting idle.
		VK_CHECK(vkDeviceWaitIdle(device));

		// Clean up staging resources
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingMemory, nullptr);
	}
	else
	{
		// TODO: support this later
		return false;
	}

	// now, the ktx_texture can be destroyed
	ktxTexture_Destroy(ktxTexture);

	// Create image view
	// Textures are not directly accessed by the shaders and
	// are abstracted by image views containing additional
	// information and sub resource ranges
	VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = format;
	imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
	// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	// Linear tiling usually won't support mip maps
	// Only set mip map count if optimal tiling is used
	imageViewCreateInfo.subresourceRange.levelCount = (useStaging) ? mipLevels : 1;
	// The view will be based on the texture's image
	imageViewCreateInfo.image = image.image;
	VK_CHECK(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &image.imageView));

	return true;
}

static std::vector<uint8_t> Base64Decode(const std::string& base64)
{
	static constexpr unsigned char kDecodingTable[] = {
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
		52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 0, 64, 64,
		64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
		64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
	};
	std::vector<uint8_t> output;
	int val = 0, valb = -8;
	for (char c : base64)
	{
		if (c > 127)
			break;
		unsigned char d = kDecodingTable[c];
		if (d == 64)
			break;
		val = (val << 6) + d;
		valb += 6;
		if (valb >= 0)
		{
			output.push_back((val >> valb) & 0xFF);
			valb -= 8;
		}
	}
	return output;
}

static bool ExtractBase64Data(const char* uri, std::string& outBase64)
{
	const char* prefix_png = "data:image/png;base64,";
	const char* prefix_jpg = "data:image/jpeg;base64,";
	if (strncmp(uri, prefix_png, strlen(prefix_png)) == 0)
	{
		outBase64 = uri + strlen(prefix_png);
		return true;
	}
	if (strncmp(uri, prefix_jpg, strlen(prefix_jpg)) == 0)
	{
		outBase64 = uri + strlen(prefix_jpg);
		return true;
	}
	return false;
}

bool loadUncompressedImage(
    Image& image,
    VkDevice device,
    VkCommandPool commandPool,
    VkCommandBuffer commandBuffer,
    VkQueue queue,
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    const Buffer& scratch,
    int texWidth,
    int texHeight,
    int texChannels)
{
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	createImage(image, device, memoryProperties, texWidth, texHeight, 1, format, usage);

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	VkImageMemoryBarrier2 preBarrier = imageBarrier(image.image,
	    0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
	    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &preBarrier);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

	vkCmdCopyBufferToImage(commandBuffer, scratch.buffer, image.image,
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	VkImageMemoryBarrier2 postBarrier = imageBarrier(image.image,
	    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &postBarrier);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));

	return true;
}

bool loadImage(Image& image, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties, const Buffer& scratch, const char* path)
{
	const char* prefix_png = "data:image/png;base64,";
	const char* prefix_jpg = "data:image/jpeg;base64,";
	if (strncmp(path, prefix_png, strlen(prefix_png)) == 0 || strncmp(path, prefix_jpg, strlen(prefix_jpg)) == 0)
	{
		std::string base64;
		if (!ExtractBase64Data(path, base64))
		{
			LOGE("Unsupported or invalid data URI format");
			return false;
		}

		std::vector<uint8_t> decoded = Base64Decode(base64);
		if (decoded.empty())
		{
			LOGE("Base64 decode failed");
			return false;
		}

		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = stbi_load_from_memory(decoded.data(), (int)decoded.size(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

		if (!pixels)
		{
			LOGE("Failed to load image from memory: %s", stbi_failure_reason());
			return false;
		}

		size_t imageSize = texWidth * texHeight * 4;
		if (scratch.size < imageSize)
		{
			LOGE("Scratch buffer too small");
			stbi_image_free(pixels);
			return false;
		}

		memcpy(scratch.data, pixels, imageSize);
		stbi_image_free(pixels);

		return loadUncompressedImage(image, device, commandPool, commandBuffer, queue, memoryProperties, scratch, texWidth, texHeight, texChannels);
	}
	else if (strstr(path, ".png") || strstr(path, ".PNG") || strstr(path, ".jpg") || strstr(path, ".JPG") || strstr(path, ".jpeg") || strstr(path, ".JPEG"))
	{
		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = nullptr;
#if defined(WIN32)
		pixels = stbi_load(path, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha); // force RGBA8
#elif defined(__ANDROID__)
		// On Android, use AAssetManager to load image files from assets
		AAsset* asset = AAssetManager_open(g_assetManager, path, AASSET_MODE_BUFFER);
		if (!asset)
		{
			LOGE("Failed to open asset: %s", path);
			return false;
		}

		// Get asset length and read into buffer
		off_t assetLength = AAsset_getLength(asset);
		std::vector<unsigned char> assetBuffer(assetLength);
		int64_t bytesRead = AAsset_read(asset, assetBuffer.data(), assetLength);
		AAsset_close(asset);

		if (bytesRead != assetLength)
		{
			LOGE("Failed to read asset fully: %s", path);
			return false;
		}

		// Load image from memory buffer
		pixels = stbi_load_from_memory(assetBuffer.data(), static_cast<int>(assetLength),
		    &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
#endif
		if (!pixels)
			return false;

		size_t imageSize = texWidth * texHeight * 4; // 4 bytes per pixel

		if (scratch.size < imageSize)
		{
			stbi_image_free(pixels);
			return false;
		}

		memcpy(scratch.data, pixels, imageSize);
		stbi_image_free(pixels);

		return loadUncompressedImage(image, device, commandPool, commandBuffer, queue, memoryProperties, scratch, texWidth, texHeight, texChannels);
	}
	else if (strstr(path, ".dds") || strstr(path, ".DDS"))
	{
		return loadDDSImage(image, device, commandPool, commandBuffer, queue, memoryProperties, scratch, path);
	}
	else if (strstr(path, ".ktx") || strstr(path, ".KTX"))
	{
		return loadKtxImage(image, device, physicalDevice, commandPool, commandBuffer, queue, memoryProperties, path);
	}
	else
	{
		LOGE("Unsupported image format: %s", path);
		return false;
	}
}

#include <iostream>
#include <stdio.h>
#include <algorithm>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VOLK_IMPLEMENTATION
#include <objparser.h>
#include <meshoptimizer.h>

#include "common.h"
#include "shaders.h"

#define RTX 1

bool rtxEnabled = false;

VkInstance createInstance()
{
	// SHORTCUT: In real VUlkans applications you should check if the used version is available via vkEnumerateInstanceVersion.
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = VK_API_VERSION_1_4;

	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &appInfo;

#if _DEBUG
	const char* debugLayers[] =
	{
		"VK_LAYER_KHRONOS_validation"
	};
	createInfo.ppEnabledLayerNames = debugLayers;
	createInfo.enabledLayerCount = sizeof(debugLayers) / sizeof(debugLayers[0]);
#endif

	const char* extensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
		VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
	};

	createInfo.ppEnabledExtensionNames = extensions;
	createInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);

	VkInstance instance = 0;
	VK_CHECK(vkCreateInstance(&createInfo, 0, &instance));
	return instance;
}

VkBool32 debugReportCallback(VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t object,
	size_t location,
	int32_t messageCode,
	const char* pLayerPrefix,
	const char* pMessage,
	void* pUserData)
{
	const char* type =
		(flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) ? ERROR_HEADER :
		(flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) ? WARNING_HEADER :
		INFO_HEADER;

	char message[4096];
	snprintf(message, ARRAYSIZE(message), "%s: %s\n", type, pMessage);

	printf("%s", message);

#ifdef _WIN32
	OutputDebugStringA(message);
#endif

	int xxx = flags & VK_DEBUG_REPORT_ERROR_BIT_EXT;
	if (xxx)
	{
		assert(!"Validation error encountered!");
	}
	return VK_FALSE;
}

VkDebugReportCallbackEXT registerDebugCallback(VkInstance instance)
{
	VkDebugReportCallbackCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	createInfo.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT
					| VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT
					| VK_DEBUG_REPORT_ERROR_BIT_EXT;
	createInfo.pfnCallback = debugReportCallback;

	VkDebugReportCallbackEXT callback = 0;

	vkCreateDebugReportCallbackEXT(instance, &createInfo, 0, &callback);

	return callback;
}


uint32_t getGraphicsFamilyIndex(VkPhysicalDevice physicalDevice)
{
	uint32_t queueCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, 0);

	std::vector<VkQueueFamilyProperties> queues(queueCount);
	assert(queueCount < ARRAYSIZE(queues));
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queues.data());

	for (uint32_t i = 0; i < queueCount; ++i)
		if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			return i;

	// TODO: this can be used in pickPhysicalDevice to pick rasterization-capable device
	assert(!"No queue families support graphics, is this a compute-only device?");
	return VK_QUEUE_FAMILY_IGNORED;
}

bool supportsPresentation(VkPhysicalDevice physicalDevice, uint32_t familyIndex)
{	
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	return vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, familyIndex);
#else
	return true;
#endif
}

VkPhysicalDevice pickPhysicalDevice(VkPhysicalDevice* physicalDevices, uint32_t physicalDeviceCount)
{
	VkPhysicalDevice discrete = 0;
	VkPhysicalDevice fallback = 0;

	for (uint32_t i = 0; i < physicalDeviceCount; ++i)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physicalDevices[i], &props);

		printf(LOGI("GPU%d: %s\n"), i, props.deviceName);

		uint32_t familyIndex = getGraphicsFamilyIndex(physicalDevices[i]);

		if (familyIndex == VK_QUEUE_FAMILY_IGNORED)
			continue;
	
		if (!supportsPresentation(physicalDevices[i], familyIndex))
			continue;

		if (props.apiVersion < VK_API_VERSION_1_4)
			continue;

		if (!discrete && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			discrete = physicalDevices[i];
		}

		if (!fallback)
		{
			fallback = physicalDevices[i];
		}
	}
	
	VkPhysicalDevice result = discrete ? discrete : fallback;

	if (result)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(result, &props);
		printf(LOGI("Selected GPU %s.\n"), props.deviceName);
	}
	else
	{
		printf(LOGE("No GPUs found!\n"));
	}
	return result;
}

VkDevice createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t familyIndex, bool rtxEnabled)
{
	float queuePriorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = familyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = queuePriorities;

	std::vector<const char*> extensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
		VK_KHR_8BIT_STORAGE_EXTENSION_NAME
	};

	if (rtxEnabled)
	{
		extensions.emplace_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
	}

	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.features.vertexPipelineStoresAndAtomics = VK_TRUE; // TODO: we aren't using this yet.

	VkPhysicalDevice16BitStorageFeaturesKHR features16 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR };
	features16.storageBuffer16BitAccess = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.shaderInt8 = VK_TRUE;
	features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
	features12.storageBuffer8BitAccess = VK_TRUE;
	features12.shaderFloat16 = VK_TRUE;

	VkPhysicalDeviceMaintenance4Features featuresMaintenance4 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
	featuresMaintenance4.maintenance4 = VK_TRUE;

	// This will only be used if rtxEnabled = true (see below)
	VkPhysicalDeviceMeshShaderFeaturesEXT featuresMesh = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	featuresMesh.taskShader = VK_TRUE;
	featuresMesh.meshShader = VK_TRUE;

	VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;

	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledExtensionCount = uint32_t(extensions.size());

	createInfo.pNext = &features;
	features.pNext = &features16;
	features16.pNext = &features12;
	features12.pNext = &featuresMaintenance4;

	if (rtxEnabled)
		featuresMaintenance4.pNext = &featuresMesh;
	
	VkDevice device = 0;
	VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, 0, &device));

	return device;
}

VkSurfaceKHR createSurface(VkInstance instance, GLFWwindow* window)
{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VkWin32SurfaceCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	createInfo.hinstance = GetModuleHandle(0);
	createInfo.hwnd = glfwGetWin32Window(window);

	VkSurfaceKHR surface = 0;
	VK_CHECK(vkCreateWin32SurfaceKHR(instance, &createInfo, 0, &surface));
	return surface;
#else
#error Unsupported platform
#endif
}

VkFormat getSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
	uint32_t formatCount = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, 0));

	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()));
	
	if (formatCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
		return VK_FORMAT_R8G8B8A8_UNORM;

	for (int32_t i = 0; i < formatCount; ++i)
	{
		if (formats[i].format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 || formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
			return formats[i].format;
	}

	for (int32_t i = 0; i < formatCount; ++i)
	{
		if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
			return formats[i].format;
	}

	return formats[0].format;
}

VkSwapchainKHR createSwapchain(VkDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR surfaceCaps, uint32_t familyIndex, VkFormat format, uint32_t width, uint32_t height,
	VkSwapchainKHR oldSwapchain)
{
	VkCompositeAlphaFlagBitsKHR surfaceComposite =
		(surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR :
		(surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR :
		(surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) ? VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR :
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

	VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	createInfo.surface = surface;
	createInfo.minImageCount = std::max(2u, surfaceCaps.minImageCount);
	createInfo.imageFormat = format;
	createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	createInfo.imageExtent.width = width;
	createInfo.imageExtent.height = height;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.queueFamilyIndexCount = 1;
	createInfo.pQueueFamilyIndices = &familyIndex;
	createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	createInfo.compositeAlpha = surfaceComposite;
	createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // TODO: use immediate mode if available
	createInfo.oldSwapchain = oldSwapchain;

	VkSwapchainKHR swapchain = 0;
	VK_CHECK(vkCreateSwapchainKHR(device, &createInfo, 0, &swapchain));

	return swapchain;
}

VkSemaphore createSemaphore(VkDevice device)
{
	VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = 0;
	VK_CHECK(vkCreateSemaphore(device, &createInfo, 0, &semaphore));

	return semaphore;
}

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex)
{
	VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	createInfo.queueFamilyIndex = familyIndex;

	VkCommandPool commandPool = 0;
	VK_CHECK(vkCreateCommandPool(device, &createInfo, 0, &commandPool));

	return commandPool;
}

VkRenderPass createRenderPass(VkDevice device, VkFormat format)
{
	VkAttachmentDescription attachments[1] = {};
	attachments[0].format = format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachments = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachments;

	VkRenderPassCreateInfo createInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	createInfo.attachmentCount = sizeof(attachments) / sizeof(attachments[0]);
	createInfo.pAttachments = attachments;
	createInfo.subpassCount = 1;
	createInfo.pSubpasses = &subpass;

	VkRenderPass renderPass = 0;
	VK_CHECK(vkCreateRenderPass(device, &createInfo, 0, &renderPass));

	return renderPass;
}

VkFramebuffer createFramebuffer(VkDevice device, VkRenderPass renderPass, VkImageView imageView, uint32_t width, uint32_t height)
{
	VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	createInfo.renderPass = renderPass;
	createInfo.attachmentCount = 1;
	createInfo.pAttachments = &imageView;
	createInfo.width = width;
	createInfo.height = height;
	createInfo.layers = 1;

	VkFramebuffer framebuffer = 0;
	VK_CHECK(vkCreateFramebuffer(device, &createInfo, 0, &framebuffer));
	return framebuffer;
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format)
{
	VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	createInfo.image = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = format;
	createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	createInfo.subresourceRange.levelCount = 1;
	createInfo.subresourceRange.layerCount = 1;

	VkImageView view = 0;
	VK_CHECK(vkCreateImageView(device, &createInfo, 0, &view));

	return view;
}

VkImageMemoryBarrier imageBarrier(VkImage image, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout,  VkImageLayout newLayout)
{
	VkImageMemoryBarrier result = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

	result.srcAccessMask = srcAccessMask;
	result.dstAccessMask = dstAccessMask;
	result.oldLayout = oldLayout;
	result.newLayout = newLayout;

	result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.image = image;
	result.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	result.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	result.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;


	return result;
}

VkBufferMemoryBarrier bufferBarrier(VkBuffer buffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask)
{
	VkBufferMemoryBarrier result = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	result.srcAccessMask = srcAccessMask;
	result.dstAccessMask = dstAccessMask;
	result.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	result.buffer = buffer;
	result.offset = 0;
	result.size = VK_WHOLE_SIZE;

	return result;
}

struct Swapchain
{
	VkSwapchainKHR swapchain;
	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkFramebuffer> framebuffers;
	uint32_t width, height;
	uint32_t imageCount;
};

void createSwapchain(Swapchain& result, VkPhysicalDevice physicallDevice, VkDevice device, VkSurfaceKHR surface, uint32_t familyIndex,
	VkFormat format, VkRenderPass renderPass, VkSwapchainKHR oldSwapchain = 0)
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicallDevice, surface, &surfaceCaps);

	uint32_t width = surfaceCaps.currentExtent.width;
	uint32_t height = surfaceCaps.currentExtent.height;

	VkSwapchainKHR swapchain = createSwapchain(device, surface, surfaceCaps, familyIndex, format, width, height, oldSwapchain);
	assert(swapchain);
		
	uint32_t imageCount = 0;
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, 0));
	std::vector<VkImage> images(imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()));

	std::vector<VkImageView> imageViews(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		imageViews[i] = createImageView(device, images[i], format);
		assert(imageViews[i]);
	}

	std::vector<VkFramebuffer> framebuffers(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		framebuffers[i] = createFramebuffer(device, renderPass, imageViews[i], width, height);
		assert(framebuffers[i]);
	}

	result.swapchain = swapchain;

	result.images = images;
	result.imageViews = imageViews;
	result.framebuffers = framebuffers;

	result.width = width;
	result.height = height;

	result.imageCount = imageCount;
}

void destroySwapchain(VkDevice device, Swapchain& swapchain)
{
	for (uint32_t ii = 0; ii < swapchain.imageCount; ++ii)
	{
		vkDestroyFramebuffer(device, swapchain.framebuffers[ii], 0);
	}
	for (uint32_t ii = 0; ii < swapchain.imageCount; ++ii)
	{
		vkDestroyImageView(device, swapchain.imageViews[ii], 0);
	}
	vkDestroySwapchainKHR(device, swapchain.swapchain, 0);
}

void resizeSwapchainIfNecessary(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, uint32_t familyIndex,
	VkFormat format, VkRenderPass renderPass)
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

	uint32_t newWidth = surfaceCaps.currentExtent.width;
	uint32_t newHeight = surfaceCaps.currentExtent.height;

	if (result.width == newWidth && result.height == newHeight)
		return;

	VkSwapchainKHR oldSwapchain = result.swapchain;

	Swapchain old = result;

	createSwapchain(result, physicalDevice, device, surface, familyIndex, format, renderPass, oldSwapchain);

	VK_CHECK(vkDeviceWaitIdle(device));

	destroySwapchain(device, old);
}

VkQueryPool createQueryPool(VkDevice device, uint32_t queryCount)
{
	VkQueryPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = queryCount;

	VkQueryPool queryPool = 0;
	VK_CHECK(vkCreateQueryPool(device, &createInfo, 0, &queryPool));

	return queryPool;
}

struct alignas(16) Meshlet
{
	float cone[4];
	uint32_t vertices[64];
	uint8_t indices[126 * 3]; // up to 126 triangles
	uint8_t triangleCount;
	uint8_t vertexCount;
};

struct Vertex
{
	uint16_t vx, vy, vz, vw;
	uint8_t nx, ny, nz, nw;
	uint16_t tu, tv;
};

struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet> meshlets;
};

bool loadMesh(Mesh& result, const char* path)
{
	ObjFile file;
	if (!objParseFile(file, path))
		return false;

	size_t index_count = file.f_size / 3;

	std::vector<Vertex> vertices(index_count);

	for (size_t i = 0; i < index_count; ++i)
	{
		Vertex& v = vertices[i];

		int vi = file.f[i * 3 + 0];
		int vti = file.f[i * 3 + 1];
		int vni = file.f[i * 3 + 2];

		float nx = vni < 0 ? 0.f : file.vn[vni * 3 + 0];
		float ny = vni < 0 ? 0.f : file.vn[vni * 3 + 1];
		float nz = vni < 0 ? 1.f : file.vn[vni * 3 + 2];

		v.vx = meshopt_quantizeHalf(file.v[vi * 3 + 0]);
		v.vy = meshopt_quantizeHalf(file.v[vi * 3 + 1]);
		v.vz = meshopt_quantizeHalf(file.v[vi * 3 + 2]);
		v.nx = uint8_t(nx * 127.f + 127.f); // TODO: fix rounding
		v.ny = uint8_t(ny * 127.f + 127.f); // TODO: fix rounding
		v.nz = uint8_t(nz * 127.f + 127.f); // TODO: fix rounding
		v.tu = meshopt_quantizeHalf(vti < 0 ? 0.f : file.vt[vti * 3 + 0]);
		v.tv = meshopt_quantizeHalf(vti < 0 ? 0.f : file.vt[vti * 3 + 1]);
	}

	std::vector<uint32_t> remap(index_count);
	size_t vertex_count = meshopt_generateVertexRemap(remap.data(), 0, index_count, vertices.data(), index_count, sizeof(Vertex));

	result.vertices.resize(vertex_count);
	result.indices.resize(index_count);

	meshopt_remapVertexBuffer(result.vertices.data(), vertices.data(), index_count, sizeof(Vertex), remap.data());
	meshopt_remapIndexBuffer(result.indices.data(), 0, index_count, remap.data());

	meshopt_optimizeVertexCache(result.indices.data(), result.indices.data(), index_count, vertex_count);
	meshopt_optimizeVertexFetch(result.vertices.data(), result.indices.data(), index_count, result.vertices.data(), vertex_count, sizeof(Vertex));

	return true;
}

float halfToFloat(uint16_t v)
{
	// This function is AI generated.
	int s = (v >> 15) & 0x1;
	int e = (v >> 10) & 0x1f;
	int f = v & 0x3ff;

	assert(e != 31);

	if (e == 0)
	{
		if (f == 0)
			return s ? -0.f : 0.f;
		while ((f & 0x400) == 0)
		{
			f <<= 1;
			e -= 1;
		}
		e += 1;
		f &= ~0x400;
	}
	else if (e == 31)
	{
		if (f == 0)
			return s ? -INFINITY : INFINITY;
		return f ? NAN : s ? -INFINITY : INFINITY;
	}
	e = e + (127 - 15);
	f = f << 13;
	union { uint32_t u; float f; } result;
	result.u = (s << 31) | (e << 23) | f;
	return result.f;
}

void buildMeshletCones(Mesh& mesh)
{
	for (Meshlet& meshlet : mesh.meshlets)
	{
		float normals[126][3] = {};

		for (size_t i = 0; i < meshlet.triangleCount; ++i)
		{
			uint32_t a = meshlet.indices[i * 3 + 0];
			uint32_t b = meshlet.indices[i * 3 + 1];
			uint32_t c = meshlet.indices[i * 3 + 2];
			
			const Vertex& va = mesh.vertices[meshlet.vertices[a]];
			const Vertex& vb = mesh.vertices[meshlet.vertices[b]];
			const Vertex& vc = mesh.vertices[meshlet.vertices[c]];

			float p0[3] = { halfToFloat(va.vx), halfToFloat(va.vy), halfToFloat(va.vz) };
			float p1[3] = { halfToFloat(vb.vx), halfToFloat(vb.vy), halfToFloat(vb.vz) };
			float p2[3] = { halfToFloat(vc.vx), halfToFloat(vc.vy), halfToFloat(vc.vz) };

			float p10[3] = { p1[0] - p0[0],p1[1] - p0[1], p1[2] - p0[2] };
			float p20[3] = { p2[0] - p0[0],p2[1] - p0[1], p2[2] - p0[2] };

			float normalx = p10[1] * p20[2] - p10[2] * p20[1];
			float normaly = p10[2] * p20[0] - p10[0] * p20[2];
			float normalz = p10[0] * p20[1] - p10[1] * p20[0];

			float area = sqrtf(normalx * normalx + normaly * normaly + normalz * normalz);
			float invarea = area == 0.f ? 0.f : 1.f / area;

			normals[i][0] = normalx * invarea;
			normals[i][1] = normaly * invarea;
			normals[i][2] = normalz * invarea;
		}

		float avgnormal[3] = {};

		for (size_t i = 0; i < meshlet.triangleCount; ++i)
		{
			avgnormal[0] += normals[i][0];
			avgnormal[1] += normals[i][1];
			avgnormal[2] += normals[i][2];
		}

		float avglength = sqrtf(avgnormal[0] * avgnormal[0] + avgnormal[1] * avgnormal[1] + avgnormal[2] * avgnormal[2]);
		if (avglength == 0.f)
		{
			avgnormal[0] = 1.f;
			avgnormal[1] = 0.f;
			avgnormal[2] = 0.f;
		}
		else
		{
			avgnormal[0] /= avglength;
			avgnormal[1] /= avglength;
			avgnormal[2] /= avglength;
		}

		float mindp = 1.f;

		for (size_t i = 0; i < meshlet.triangleCount; ++i)
		{
			float dp = normals[i][0] * avgnormal[0] + normals[i][1] * avgnormal[1] + normals[i][2] * avgnormal[2];
			mindp = std::min(mindp, dp);
		}

		// for cone to be backfacing, the angle between view vector and cone axis should be > (mindp angle + 90 degrees)
		float conew = mindp < 0.f ? -1.0f : -sqrtf(1.f - mindp * mindp);

		meshlet.cone[0] = avgnormal[0];
		meshlet.cone[1] = avgnormal[1];
		meshlet.cone[2] = avgnormal[2];
		meshlet.cone[3] = conew;
	}
}

struct Buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* data;
	size_t size;
};

void buildMeshlets(Mesh& mesh)
{
	Meshlet meshlet = {};
	std::vector<uint8_t> meshletVertices(mesh.vertices.size(), 0xff);

	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		unsigned int a = mesh.indices[i + 0];
		unsigned int b = mesh.indices[i + 1];
		unsigned int c = mesh.indices[i + 2];

		uint8_t& av = meshletVertices[a];
		uint8_t& bv = meshletVertices[b];
		uint8_t& cv = meshletVertices[c];

		if (meshlet.vertexCount + (av == 0xff) + (bv == 0xff) + (cv == 0xff) > 64 || meshlet.triangleCount >= 126)
		{
			mesh.meshlets.push_back(meshlet);

			for (size_t j = 0; j < meshlet.vertexCount; ++j)
				meshletVertices[meshlet.vertices[j]] = 0xff;

			meshlet = {};
		}

		if (av == 0xff)
		{
			av = meshlet.vertexCount;
			meshlet.vertices[meshlet.vertexCount++] = a;
		}

		if (bv == 0xff)
		{
			bv = meshlet.vertexCount;
			meshlet.vertices[meshlet.vertexCount++] = b;
		}

		if (cv == 0xff)
		{
			cv = meshlet.vertexCount;
			meshlet.vertices[meshlet.vertexCount++] = c;
		}

		meshlet.indices[meshlet.triangleCount * 3 + 0] = av;
		meshlet.indices[meshlet.triangleCount * 3 + 1] = bv;
		meshlet.indices[meshlet.triangleCount * 3 + 2] = cv;
		meshlet.triangleCount++;
	}

	if (meshlet.triangleCount)
		mesh.meshlets.push_back(meshlet);

	// TODO: we don't really need this but this makes sure we can assume that we need all 32 meshlets in task shader.
	while (mesh.meshlets.size() % 32)
		mesh.meshlets.push_back({});
}

uint32_t selectMemoryTpe(const VkPhysicalDeviceMemoryProperties& memoryProperties, uint32_t memoryTypeBits, VkMemoryPropertyFlags flags)
{
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
	{
		if ((memoryTypeBits & (1 << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}

	assert(!"No compatible memory type found.");
	return ~0u;
}

void createBuffer(Buffer& result, VkDevice device, const VkPhysicalDeviceMemoryProperties& memoryProperties, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags)
{
	VkBufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	createInfo.size = size;
	createInfo.usage = usage;

	VkBuffer buffer = 0;
	VK_CHECK(vkCreateBuffer(device, &createInfo, 0, &buffer));

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

	uint32_t memoryTypeIndex = selectMemoryTpe(memoryProperties, memoryRequirements.memoryTypeBits, memoryFlags);
	assert(memoryTypeIndex != ~0u);

	VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = memoryTypeIndex;

	VkDeviceMemory memory = 0;
	VK_CHECK(vkAllocateMemory(device, &allocateInfo, 0, &memory));

	VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));

	void* data = 0;
	if (memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		VK_CHECK(vkMapMemory(device, memory, 0, size, 0, &data));

	result.buffer = buffer;
	result.memory = memory;
	result.data = data;
	result.size = size;
}

void uploadBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const Buffer& buffer, const Buffer& scratch, const void* data, size_t size)
{
	// TODO: this function is submitting a command buffer and waiting for device idle for each buffer upload, this is obviously suboptimal and we need to batch this later.
	assert(scratch.data);
	assert(scratch.size >= size);
	memcpy(scratch.data, data, size);

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	VkBufferCopy region = { 0, 0, VkDeviceSize(size) };
	vkCmdCopyBuffer(commandBuffer, scratch.buffer, buffer.buffer, 1, &region);

	VkBufferMemoryBarrier copyBarrier = bufferBarrier(buffer.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 1, &copyBarrier, 0, 0);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

	VK_CHECK(vkQueueWaitIdle(queue));
}

void destroyBuffer(const Buffer& buffer, VkDevice device)
{
	vkFreeMemory(device, buffer.memory, 0);
	vkDestroyBuffer(device, buffer.buffer, 0);
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_R)
		{
			rtxEnabled = !rtxEnabled;
		}
	}
}

int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		printf(LOGE("Usage: %s [mesh]\n"), argv[0]);
		return 1;
	}

	int rc = glfwInit();
	assert(rc);

	VK_CHECK(volkInitialize());

	VkInstance instance = createInstance();
	assert(instance);

	volkLoadInstance(instance);

	VkDebugReportCallbackEXT debugCallback = registerDebugCallback(instance);

	VkPhysicalDevice physicalDevices[16];
	uint32_t physicalDeviceCount = sizeof(physicalDevices) / sizeof(physicalDevices[0]);
	auto result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
	VK_CHECK(result);

	VkPhysicalDevice physicalDevice = pickPhysicalDevice(physicalDevices, physicalDeviceCount);
	assert(physicalDevice);

	uint32_t extensionCount;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, 0));

	std::vector<VkExtensionProperties> extensions(extensionCount);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, extensions.data()));

	bool rtxSupported = false;
	for (const auto& ext : extensions)
	{
		if (strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
		{
			rtxSupported = true;
			break;
		}
	}

	rtxEnabled = rtxSupported;

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	uint32_t graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(familyIndex != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = createDevice(instance, physicalDevice, graphicsFamily, rtxEnabled);
	assert(device);

	GLFWwindow* window = glfwCreateWindow(1024, 768, "kaleido", 0 ,0);
	assert(window);

	glfwSetKeyCallback(window, keyCallback);

	VkSurfaceKHR surface = createSurface(instance, window);
	assert(surface);

	// Check if VkSurfaceKHR is supported in physical device.
    VkBool32 presentSupported = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsFamily, surface, &presentSupported));
    assert(presentSupported);

	VkFormat swapchainFormat = getSwapchainFormat(physicalDevice, surface);

	VkSemaphore acquireSemaphore = createSemaphore(device);
	assert(acquireSemaphore);

	VkSemaphore releaseSemaphore = createSemaphore(device);
	assert(releaseSemaphore);

	VkQueue queue = 0;
	vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

	VkRenderPass renderPass = createRenderPass(device, swapchainFormat);
	
	Shader meshletTS = {};
	Shader meshletMS = {};
	if (rtxEnabled)
	{
		bool tc = loadShader(meshletTS, device, "shaders/meshlet.task.spv");
		assert(tc);

		bool rc = loadShader(meshletMS, device, "shaders/meshlet.mesh.spv");
		assert(rc);
	}
	
	Shader meshVS = {};
	{
		bool rc = loadShader(meshVS, device, "shaders/mesh.vert.spv");
		assert(cS);
	}

	Shader meshFS = {};
	{
		bool rc = loadShader(meshFS, device, "shaders/mesh.frag.spv");
		assert(rc);
	}

	// TODO: this is critical for performance!
	VkPipelineCache pipelineCache = 0;

	VkDescriptorSetLayout descriptorSetLayout = createDescriptorSetLayout(device, { &meshVS, &meshFS });
	VkPipelineLayout meshLayout = createPipelineLayout(device, descriptorSetLayout);
	assert(meshLayout);

	VkDescriptorUpdateTemplate meshUpdateTemplate = createUpdateTemplate(device, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayout, { &meshVS, &meshFS });
	assert(meshUpdateTemplate);

	VkDescriptorSetLayout descriptorSetLayoutRTX = 0;
	VkPipelineLayout meshLayoutRTX = 0;
	VkDescriptorUpdateTemplate meshUpdateTemplateRTX = 0;
	if (rtxEnabled)
	{
		descriptorSetLayoutRTX = createDescriptorSetLayout(device, { &meshletTS, &meshletMS, &meshFS });
		meshLayoutRTX = createPipelineLayout(device, descriptorSetLayoutRTX);
		assert(meshLayoutRTX);

		meshUpdateTemplateRTX = createUpdateTemplate(device, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayoutRTX, { &meshletTS, &meshletMS, &meshFS });
		assert(meshUpdateTemplateRTX);
	}

	VkPipeline meshPipeline = createGraphicsPipeline(device, pipelineCache, renderPass, { &meshVS, &meshFS }, meshLayout);
	assert(meshPipeline);

	VkPipeline meshPipelineRTX = 0;
	if (rtxEnabled)
	{
		meshPipelineRTX = createGraphicsPipeline(device, pipelineCache, renderPass, { &meshletTS, &meshletMS, &meshFS }, meshLayoutRTX);
		assert(meshPipelineRTX);
	}

	Swapchain swapchain;
	createSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, swapchainFormat, renderPass);

	VkQueryPool queryPool = createQueryPool(device, 128);
	assert(queryPool);
	
	VkCommandPool commandPool = createCommandPool(device, graphicsFamily);
	assert(commandPool);

	VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocateInfo.commandPool = commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer = 0;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	Mesh mesh;
	bool rcm = loadMesh(mesh, argv[1]);
	assert(rcm);

	if (rtxEnabled)
	{
		buildMeshlets(mesh);
		buildMeshletCones(mesh);
	}

	Buffer scratch = {};
	createBuffer(scratch, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	Buffer vb = {};
	createBuffer(vb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer ib = {};
	createBuffer(ib, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer mb = {};
	if (rtxEnabled)
	{
		createBuffer(mb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	uploadBuffer(device, commandPool, commandBuffer, queue, vb, scratch, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
	uploadBuffer(device, commandPool, commandBuffer, queue, ib, scratch, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));

	if (rtxEnabled)
	{
		uploadBuffer(device, commandPool, commandBuffer, queue, mb, scratch, mesh.meshlets.data(), mesh.meshlets.size() * sizeof(Meshlet));
	}

	double frameCPUAvg = 0.0;
	double frameGPUAvg = 0.0;

	while (!glfwWindowShouldClose(window))
	{
		double frameCPUBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();
		
		resizeSwapchainIfNecessary(swapchain, physicalDevice, device, surface, graphicsFamily, swapchainFormat, renderPass);
		
		uint32_t imageIndex = 0;
		VK_CHECK(vkAcquireNextImageKHR(device, swapchain.swapchain, ~0ull, acquireSemaphore, VK_NULL_HANDLE, &imageIndex));

		VK_CHECK(vkResetCommandPool(device, commandPool, 0));

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		vkCmdResetQueryPool(commandBuffer, queryPool, 0, 128);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 0);

		VkImageMemoryBarrier renderBeginBarrier = imageBarrier(swapchain.images[imageIndex], 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &renderBeginBarrier);

		VkClearColorValue color = { 48.f / 255.f, 10.f / 255.f, 36.f / 255.f, 1 };
		VkClearValue clearColor = { color };

		VkRenderPassBeginInfo passBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		passBeginInfo.renderPass = renderPass;
		passBeginInfo.framebuffer = swapchain.framebuffers[imageIndex];
		passBeginInfo.renderArea.extent.width = swapchain.width;
		passBeginInfo.renderArea.extent.height = swapchain.height;
		passBeginInfo.clearValueCount = 1;
		passBeginInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(commandBuffer, &passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = { 0, float(swapchain.height), float(swapchain.width), -float(swapchain.height), 0, 1 };
		VkRect2D scissor = { { 0, 0 }, { uint32_t(swapchain.width), uint32_t(swapchain.height) } };

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);


		if (rtxEnabled)
		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineRTX);

			DescriptorInfo descriptors[] = { vb.buffer, mb.buffer };
			vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshUpdateTemplateRTX, meshLayoutRTX, 0, descriptors);

			uint32_t drawCount = 100;
			for (uint32_t i = 0; i< drawCount; ++i)
			vkCmdDrawMeshTasksEXT(commandBuffer, uint32_t(mesh.meshlets.size()) / 32, 1, 1); // TODO: use more meaning full group size, and this extension is now standard, not only for NV
		}
		else
		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

			DescriptorInfo descriptors[] = { vb.buffer };
			vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshUpdateTemplate, meshLayout, 0, descriptors);


			vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffer, mesh.indices.size(), 1, 0, 0, 0);
		}

		vkCmdEndRenderPass(commandBuffer);

		VkImageMemoryBarrier renderEndBarrier = imageBarrier(swapchain.images[imageIndex], VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &renderEndBarrier);
		
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);
		
		VK_CHECK(vkEndCommandBuffer(commandBuffer));

		VkPipelineStageFlags submitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &acquireSemaphore;
		submitInfo.pWaitDstStageMask = &submitStageMask;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &releaseSemaphore;

		VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &releaseSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain.swapchain;
		presentInfo.pImageIndices = &imageIndex;

		VK_CHECK(vkQueuePresentKHR(queue, &presentInfo));

		VK_CHECK(vkDeviceWaitIdle(device));

		uint64_t queryResults[2];
		VK_CHECK(vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(queryResults), queryResults, sizeof(queryResults[0]), VK_QUERY_RESULT_64_BIT));

		double frameGPUBegin = double(queryResults[0]) * props.limits.timestampPeriod * 1e-6;
		double frameGPUEnd = double(queryResults[1]) * props.limits.timestampPeriod * 1e-6;
		
		double frameCPUEnd = glfwGetTime() * 1000.0;

		frameCPUAvg = frameCPUAvg * 0.9 + (frameCPUEnd - frameCPUBegin) * 0.1;
		frameGPUAvg = frameGPUAvg * 0.9 + (frameGPUEnd - frameGPUBegin) * 0.1;

		char title[256];
		sprintf(title, "cpu: %.2f ms; gpu %.2f ms; triangles %d; meshlets % d; RTX %s", frameCPUAvg, frameGPUAvg, int(mesh.indices.size() / 3), int(mesh.meshlets.size()), rtxEnabled ? "ON" : "OFF");
		glfwSetWindowTitle(window, title);
	}

	VK_CHECK(vkDeviceWaitIdle(device));	

	{
		destroyBuffer(mb, device);
	}

	destroyBuffer(ib, device);
	destroyBuffer(vb, device);
	destroyBuffer(scratch, device);

	vkDestroyCommandPool(device, commandPool, 0);

	destroySwapchain(device, swapchain);
    
	vkDestroyQueryPool(device, queryPool, 0);

	vkDestroyPipeline(device, meshPipeline, 0);
	vkDestroyPipelineLayout(device, meshLayout, 0);
	vkDestroyDescriptorUpdateTemplate(device, meshUpdateTemplate, 0);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, 0);

	{
		vkDestroyPipeline(device, meshPipelineRTX, 0);
		vkDestroyPipelineLayout(device, meshLayoutRTX, 0);
		vkDestroyDescriptorUpdateTemplate(device, meshUpdateTemplateRTX, 0);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayoutRTX, 0);
	}

	destroyShader(meshVS, device);
	destroyShader(meshFS, device);

	{
		destroyShader(meshletTS, device);
		destroyShader(meshletMS, device);
	}

	vkDestroyRenderPass(device, renderPass, 0);
    vkDestroySemaphore(device, acquireSemaphore, 0);
    vkDestroySemaphore(device, releaseSemaphore, 0);
    vkDestroySurfaceKHR(instance, surface, 0);
	glfwDestroyWindow(window);
    vkDestroyDevice(device, 0);

#ifdef _DEBUG
	vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);
#endif

	vkDestroyInstance(instance, 0);

    return 0;
}

// video 8: 1:34:52

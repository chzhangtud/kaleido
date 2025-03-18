#include <iostream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <algorithm>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <objparser.h>
#include <meshoptimizer.h>

#define RTX 1

#define VK_CHECK(call)                  \
    do                                  \
    {                                   \
        VkResult result_ = call;        \
        assert(result_ == VK_SUCCESS);  \
    } while(0)

#define LOGI(str) ("\033[34m[INFO]: \033[0m" str)
#define LOGW(str) ("\033[33m[WARNING]: \033[0m" str)
#define LOGE(str) ("\033[31m[ERROR]: \033[0m" str)

#define INFO_HEADER LOGI("")
#define WARNING_HEADER LOGW("")
#define ERROR_HEADER LOGE("")

#ifndef ARRAYSIZE
#define ARRAYSIZE(array) (sizeof(array) / sizeof(array[0]))
#endif // !ARRAYSIZE

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

VkDevice createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t familyIndex)
{
	float queuePriorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = familyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = queuePriorities;

	const char* extensions[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
		VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
#if RTX
		VK_EXT_MESH_SHADER_EXTENSION_NAME,
#endif
	};

	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.features.vertexPipelineStoresAndAtomics = VK_TRUE; // TODO: we aren't using this yet.

	VkPhysicalDevice16BitStorageFeaturesKHR features16 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR };
	features16.storageBuffer16BitAccess = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.shaderInt8 = VK_TRUE;
	features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
	features12.storageBuffer8BitAccess = VK_TRUE;

	VkPhysicalDeviceMaintenance4Features featuresMaintenance4 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
	featuresMaintenance4.maintenance4 = VK_TRUE;

#if RTX
	VkPhysicalDeviceMeshShaderFeaturesEXT featuresMesh = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	featuresMesh.meshShader = VK_TRUE;
#endif

	VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;

	createInfo.ppEnabledExtensionNames = extensions;
	createInfo.enabledExtensionCount = ARRAYSIZE(extensions);

	createInfo.pNext = &features;
	features.pNext = &features16;
	features16.pNext = &features12;
	features12.pNext = &featuresMaintenance4;
#if RTX
	featuresMaintenance4.pNext = &featuresMesh;
#endif
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
	createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
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

VkShaderModule loadShader(VkDevice device, const char* path)
{
	FILE* file = fopen(path, "rb");
	assert(file);

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	assert(length >= 0);
	fseek(file, 0, SEEK_SET);

	char* buffer = new char[length];
	assert(buffer);

	size_t rc = fread(buffer, 1, length, file);
	assert(rc == size_t(length));
	fclose(file);

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = length;
	createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer);

	VkShaderModule shaderModule = 0;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, 0, &shaderModule));

	delete[] buffer;

	return shaderModule;
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device)
{
#if RTX
	VkDescriptorSetLayoutBinding setBindings[2] = {};
	setBindings[0].binding = 0;
	setBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	setBindings[0].descriptorCount = 1;
	setBindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	setBindings[1].binding = 1;
	setBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	setBindings[1].descriptorCount = 1;
	setBindings[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
#else
	VkDescriptorSetLayoutBinding setBindings[1] = {};
	setBindings[0].binding = 0;
	setBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	setBindings[0].descriptorCount = 1;
	setBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
#endif

	VkDescriptorSetLayoutCreateInfo setCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	setCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	setCreateInfo.bindingCount = ARRAYSIZE(setBindings);
	setCreateInfo.pBindings = setBindings;

	VkDescriptorSetLayout setLayout = 0;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &setCreateInfo, 0, &setLayout));
	
	return setLayout;
}

VkPipelineLayout createPipelineLayout(VkDevice device, const VkDescriptorSetLayout descriptorSetLayout)
{
	VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	createInfo.setLayoutCount = 1;
	createInfo.pSetLayouts = &descriptorSetLayout;

	VkPipelineLayout layout = 0;
	VK_CHECK(vkCreatePipelineLayout(device, &createInfo, 0, &layout));

	return layout;
}

VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineCache pipelineCache, VkRenderPass renderPass, VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout)
{
	VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
#if RTX
	stages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
#else
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
#endif
	stages[0].module = vs;
	stages[0].pName = "main";
	
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs;
	stages[1].pName = "main";

	createInfo.stageCount = sizeof(stages) / sizeof(stages[0]);
	createInfo.pStages = stages;

	VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	createInfo.pVertexInputState = &vertexInput;

	// TODO: temporary, legacy FFP IA
	VkVertexInputBindingDescription stream = { 0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attrs[3] = {};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	createInfo.pInputAssemblyState = &inputAssembly;

	VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	createInfo.pViewportState = &viewportState;

	VkPipelineRasterizationStateCreateInfo rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationState.lineWidth = 1.f;
	//rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	createInfo.pRasterizationState = &rasterizationState;

	VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	createInfo.pMultisampleState = &multisampleState;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	createInfo.pDepthStencilState = &depthStencilState;

	VkPipelineColorBlendAttachmentState colorAttachmentState = {};
	colorAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                        | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT
                                        | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = &colorAttachmentState;
	createInfo.pColorBlendState = &colorBlendState;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
	dynamicState.pDynamicStates = dynamicStates;
	createInfo.pDynamicState = &dynamicState;

	createInfo.layout = layout;
	createInfo.renderPass = renderPass;

	VkPipeline pipeline = 0;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &createInfo, 0, &pipeline));

	return pipeline;
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

struct Vertex
{
	float vx, vy, vz;
	uint8_t nx, ny, nz, nw;
	float tu, tv;
};

struct Meshlet
{
	uint32_t vertices[64];
	uint8_t indices[126]; // up to 42 triangles
	uint8_t indexCount;
	uint8_t vertexCount;
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

		v.vx = file.v[vi * 3 + 0];
		v.vy = file.v[vi * 3 + 1];
		v.vz = file.v[vi * 3 + 2];
		v.nx = uint8_t(nx * 127.f + 127.f); // TODO: fix rounding
		v.ny = uint8_t(ny * 127.f + 127.f); // TODO: fix rounding
		v.nz = uint8_t(nz * 127.f + 127.f); // TODO: fix rounding
		v.tu = vti < 0 ? 0.f : file.vt[vti * 3 + 0];
		v.tv = vti < 0 ? 0.f : file.vt[vti * 3 + 1];
	}

	if (0)
	{
		result.vertices = vertices;
		result.indices.resize(index_count);

		for (size_t i = 0; i < index_count; ++i)
			result.indices[i] = uint32_t(i);
	}
	else
	{
		std::vector<uint32_t> remap(index_count);
		size_t vertex_count = meshopt_generateVertexRemap(remap.data(), 0, index_count, vertices.data(), index_count, sizeof(Vertex));

		result.vertices.resize(vertex_count);
		result.indices.resize(index_count);

		meshopt_remapVertexBuffer(result.vertices.data(), vertices.data(), index_count, sizeof(Vertex), remap.data());
		meshopt_remapIndexBuffer(result.indices.data(), 0, index_count, remap.data());
	}

	// TODO: optimize the mesh for more efficient GPU rendering
	return true;
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

		if (meshlet.vertexCount + (av == 0xff) + (bv == 0xff) + (cv == 0xff) > 64 || meshlet.indexCount + 1 > 126)
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

		meshlet.indices[meshlet.indexCount++] = av;
		meshlet.indices[meshlet.indexCount++] = bv;
		meshlet.indices[meshlet.indexCount++] = cv;
	}

	if (meshlet.indexCount)
		mesh.meshlets.push_back(meshlet);
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

void createBuffer(Buffer& result, VkDevice device, const VkPhysicalDeviceMemoryProperties& memoryProperties, size_t size, VkBufferUsageFlags usage)
{
	VkBufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	createInfo.size = size;
	createInfo.usage = usage;

	VkBuffer buffer = 0;
	VK_CHECK(vkCreateBuffer(device, &createInfo, 0, &buffer));

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

	uint32_t memoryTypeIndex = selectMemoryTpe(memoryProperties, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	assert(memoryTypeIndex != ~0u);

	VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = memoryTypeIndex;

	VkDeviceMemory memory = 0;
	VK_CHECK(vkAllocateMemory(device, &allocateInfo, 0, &memory));

	VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));

	void* data = 0;
	VK_CHECK(vkMapMemory(device, memory, 0, size, 0, &data));

	result.buffer = buffer;
	result.memory = memory;
	result.data = data;
	result.size = size;
}

void destroyBuffer(const Buffer& buffer, VkDevice device)
{
	vkFreeMemory(device, buffer.memory, 0);
	vkDestroyBuffer(device, buffer.buffer, 0);
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

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	uint32_t graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(familyIndex != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = createDevice(instance, physicalDevice, graphicsFamily);
	assert(device);

	GLFWwindow* window = glfwCreateWindow(1024, 768, "kaleido", 0 ,0);
	assert(window);

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
	assert(renderPass);

#if RTX
	VkShaderModule triangleVS = loadShader(device, "shaders/meshlet.mesh.spv");
	assert(triangleVS);
#else
	VkShaderModule triangleVS = loadShader(device, "shaders/triangle.vert.spv");
	assert(triangleVS);
#endif

	VkShaderModule triangleFS = loadShader(device, "shaders/triangle.frag.spv");
	assert(triangleFS);

	// TODO: this is critical for performance!
	VkPipelineCache pipelineCache = 0;

	VkDescriptorSetLayout descriptorSetLayout = createDescriptorSetLayout(device);

	VkPipelineLayout triangleLayout = createPipelineLayout(device, descriptorSetLayout);
	assert(triangleLayout);

	VkPipeline trianglePipeline = createGraphicsPipeline(device, pipelineCache, renderPass, triangleVS, triangleFS, triangleLayout);
	assert(trianglePipeline);

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

#if RTX
	buildMeshlets(mesh);
#endif

	Buffer vb = {};
	createBuffer(vb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	Buffer ib = {};
	createBuffer(ib, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

#if RTX
	Buffer mb = {};
	createBuffer(mb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
#endif

	assert(vb.size >= mesh.vertices.size() * sizeof(Vertex));
	memcpy(vb.data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));

	assert(ib.size >= mesh.indices.size() * sizeof(uint32_t));
	memcpy(ib.data, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));

#if RTX
	assert(mb.size >= mesh.meshlets.size() * sizeof(Meshlet));
	memcpy(mb.data, mesh.meshlets.data(), mesh.meshlets.size() * sizeof(Meshlet));
#endif

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

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);

		VkDescriptorBufferInfo vbInfo = {};
		vbInfo.buffer = vb.buffer;
		vbInfo.offset = 0;
		vbInfo.range = vb.size;

#if RTX
		VkDescriptorBufferInfo mbInfo = {};
		mbInfo.buffer = mb.buffer;
		mbInfo.offset = 0;
		mbInfo.range = mb.size;

		VkWriteDescriptorSet descriptors[2];
		descriptors[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptors[0].dstBinding = 0;
		descriptors[0].descriptorCount = 1;
		descriptors[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptors[0].pBufferInfo = &vbInfo;
		descriptors[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptors[1].dstBinding = 1;
		descriptors[1].descriptorCount = 1;
		descriptors[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptors[1].pBufferInfo = &mbInfo;

		vkCmdPushDescriptorSetKHR(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, triangleLayout, 0, ARRAYSIZE(descriptors), descriptors);

		//vkCmdDrawIndexed(commandBuffer, mesh.indices.size(), 1, 0, 0, 0);
		vkCmdDrawMeshTasksEXT(commandBuffer, uint32_t(mesh.meshlets.size()), 1, 1); // TODO: use more meaning full group size, and this extension is now standard, not only for NV
#else
		VkWriteDescriptorSet descriptors[1];
		descriptors[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptors[0].dstBinding = 0;
		descriptors[0].descriptorCount = 1;
		descriptors[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptors[0].pBufferInfo = &vbInfo;

		vkCmdPushDescriptorSetKHR(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, triangleLayout, 0, ARRAYSIZE(descriptors), descriptors);

		vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(commandBuffer, mesh.indices.size(), 1, 0, 0, 0);
#endif

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
		sprintf(title, "cpu: %.2f ms; gpu %.2f ms; triangles %d; meshlets % d", frameCPUAvg, frameGPUAvg, int(mesh.indices.size() / 3), int(mesh.meshlets.size()));
		glfwSetWindowTitle(window, title);
	}

	VK_CHECK(vkDeviceWaitIdle(device));

	destroyBuffer(vb, device);
	destroyBuffer(ib, device);

#if RTX
	destroyBuffer(mb, device);
#endif

	vkDestroyCommandPool(device, commandPool, 0);

	destroySwapchain(device, swapchain);
    
	vkDestroyQueryPool(device, queryPool, 0);
	vkDestroyPipeline(device, trianglePipeline, 0);
	vkDestroyPipelineLayout(device, triangleLayout, 0);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, 0);
	vkDestroyShaderModule(device, triangleVS, 0);
	vkDestroyShaderModule(device, triangleFS, 0);
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

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "common.h"
#include "device.h"

#ifdef _WIN32
#include <Windows.h>
#endif

// Validation is enabled by default in Debug
#ifndef NDEBUG
#define KHR_VALIDATION 1
#endif

// Synchronization validation is enabled by default in Debug but it's rather slow
#define SYNC_VALIDATION 1

VkInstance createInstance()
{
	assert(volkGetInstanceVersion() >= VK_API_VERSION_1_4);

	// SHORTCUT: In real VUlkans applications you should check if the used version is available via vkEnumerateInstanceVersion.
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = VK_API_VERSION_1_4;

	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &appInfo;

#if KHR_VALIDATION
	const char* debugLayers[] =
	{
		"VK_LAYER_KHRONOS_validation"
	};
	createInfo.ppEnabledLayerNames = debugLayers;
	createInfo.enabledLayerCount = sizeof(debugLayers) / sizeof(debugLayers[0]);

	VkValidationFeatureEnableEXT enabledValidationFeatures[] =
	{
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
#if SYNC_VALIDATION
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
#endif
	};

	VkValidationFeaturesEXT validationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	validationFeatures.enabledValidationFeatureCount = sizeof(enabledValidationFeatures) / sizeof(enabledValidationFeatures[0]);
	validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;

	createInfo.pNext = &validationFeatures;
#endif

	const char* extensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#if KHR_VALIDATION
		VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
	};

	createInfo.ppEnabledExtensionNames = extensions;
	createInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);

	VkInstance instance = 0;
	VK_CHECK(vkCreateInstance(&createInfo, 0, &instance));
	return instance;
}

static VkBool32 debugReportCallback(VkDebugReportFlagsEXT flags,
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
		(flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) ? WARNING_HEADER :
		INFO_HEADER;

	char message[4096];
	snprintf(message, COUNTOF(message), "%s: %s\n", type, pMessage);

	printf("%s", message);

#ifdef _WIN32
	OutputDebugStringA(message);
#endif

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
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
	assert(queueCount <= queues.size());
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queues.data());

	for (uint32_t i = 0; i < queueCount; ++i)
		if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			return i;

	// TODO: this can be used in pickPhysicalDevice to pick rasterization-capable device
	assert(!"No queue families support graphics, is this a compute-only device?");
	return VK_QUEUE_FAMILY_IGNORED;
}

static bool supportsPresentation(VkPhysicalDevice physicalDevice, uint32_t familyIndex)
{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	return !!vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, familyIndex);
#else
	return true;
#endif
}

VkPhysicalDevice pickPhysicalDevice(VkPhysicalDevice* physicalDevices, uint32_t physicalDeviceCount)
{
	return physicalDevices[1];

	VkPhysicalDevice preferred = 0;
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

		if (!preferred && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			preferred = physicalDevices[i];
		}

		if (!fallback)
		{
			fallback = physicalDevices[i];
		}
	}

	VkPhysicalDevice result = preferred ? preferred : fallback;

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

VkDevice createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t familyIndex, bool pushDescriptorsSupported, bool checkpointsSupported, bool meshShadingEnabled)
{
	float queuePriorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = familyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = queuePriorities;

	std::vector<const char*> extensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	if (pushDescriptorsSupported)
	{
		extensions.emplace_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	}

	if (checkpointsSupported)
	{
		extensions.emplace_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
	}

	if (meshShadingEnabled)
	{
		extensions.emplace_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
	}

	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.features.vertexPipelineStoresAndAtomics = VK_TRUE; // TODO: we aren't using this yet.
	features.features.multiDrawIndirect = VK_TRUE;
	features.features.pipelineStatisticsQuery = VK_TRUE;
	features.features.shaderInt16 = VK_TRUE;
	features.features.shaderInt64 = VK_TRUE;
	
	VkPhysicalDevice16BitStorageFeaturesKHR features16 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR };
	features16.storageBuffer16BitAccess = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.shaderInt8 = VK_TRUE;
	features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
	features12.storageBuffer8BitAccess = VK_TRUE;
	features12.shaderFloat16 = VK_TRUE;
	features12.drawIndirectCount = VK_TRUE;
	features12.samplerFilterMinmax = VK_TRUE;
	features12.scalarBlockLayout = VK_TRUE;
	
	VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderFeature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
	dynamicRenderFeature.dynamicRendering = VK_TRUE;

	VkPhysicalDeviceMaintenance4Features featuresMaintenance4 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
	featuresMaintenance4.maintenance4 = VK_TRUE;

	VkPhysicalDeviceShaderDrawParameterFeatures featuresShaderDrawParameter = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES };
	featuresShaderDrawParameter.shaderDrawParameters = VK_TRUE;

	// This will only be used if meshShadingEnabled = true (see below)
	VkPhysicalDeviceMeshShaderFeaturesEXT featuresMesh = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	featuresMesh.taskShader = VK_TRUE;
	featuresMesh.meshShader = VK_TRUE;

	VkPhysicalDeviceSynchronization2Features sync2Feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
	sync2Feature.synchronization2 = VK_TRUE;

	VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;

	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledExtensionCount = uint32_t(extensions.size());

	createInfo.pNext = &features;
	features.pNext = &features16;
	features16.pNext = &features12;
	features12.pNext = &dynamicRenderFeature;
	dynamicRenderFeature.pNext = &featuresMaintenance4;
	featuresMaintenance4.pNext = &featuresShaderDrawParameter;
	featuresShaderDrawParameter.pNext = &sync2Feature;

	if (meshShadingEnabled)
		sync2Feature.pNext = &featuresMesh;

	VkDevice device = 0;
	VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, 0, &device));

	return device;
}


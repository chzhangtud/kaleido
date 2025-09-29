#if defined(WIN32)
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
#include <string>
#include <stdlib.h>

#include "common.h"
#include "device.h"
#include "config.h"

#ifdef _WIN32
#include <Windows.h>
#endif

// Validation is enabled by default in Debug
#ifndef NDEBUG
#define KHR_VALIDATION 1
#else
#define KHR_VALIDATION CONFIG_RELVAL
#endif

// Synchronization validation is disabled by default in Debug since it's rather slow
#define SYNC_VALIDATION CONFIG_SYNCVAL

static bool isLayerSupported(const char* name)
{
	uint32_t propertyCount = 0;
	VK_CHECK(vkEnumerateInstanceLayerProperties(&propertyCount, 0));

	std::vector<VkLayerProperties> properties(propertyCount);
	VK_CHECK(vkEnumerateInstanceLayerProperties(&propertyCount, properties.data()));

	for (uint32_t i = 0; i < propertyCount; ++i)
		if (strcmp(name, properties[i].layerName) == 0)
			return true;

	return false;
}

VkInstance createInstance()
{
	if (volkGetInstanceVersion() < API_VERSION)
	{
		fprintf(stderr, "ERROR: Vulkan 1.%d instance not found\n", VK_VERSION_MINOR(API_VERSION));
		return 0;
	}

	// SHORTCUT: In real VUlkans applications you should check if the used version is available via vkEnumerateInstanceVersion.
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.apiVersion = API_VERSION;

	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &appInfo;

#if KHR_VALIDATION || SYNC_VALIDATION
	const char* debugLayers[] = {
		"VK_LAYER_KHRONOS_validation"
	};

	if (isLayerSupported("VK_LAYER_KHRONOS_validation"))
	{
		createInfo.ppEnabledLayerNames = debugLayers;
		createInfo.enabledLayerCount = sizeof(debugLayers) / sizeof(debugLayers[0]);
		LOGI("Enabled Vulkan validation layers (sync validation %s)", SYNC_VALIDATION ? "enabled" : "disabled");
	}

	else
	{
		LOGW("Vulkan debug layers are not available.");
	}

#if SYNC_VALIDATION
	VkValidationFeatureEnableEXT enabledValidationFeatures[] = {
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
	};

	VkValidationFeaturesEXT validationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	validationFeatures.enabledValidationFeatureCount = sizeof(enabledValidationFeatures) / sizeof(enabledValidationFeatures[0]);
	validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;

	createInfo.pNext = &validationFeatures;
#endif
#endif

	const char* extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#endif
#ifndef NDEBUG
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
	char message[4096];
	snprintf(message, COUNTOF(message), "%s\n", pMessage);

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
		LOGE("%s", message);
	else if ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) || (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT))
		LOGW("%s", message);
	else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
		LOGD("%s", message);
	else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
		LOGI("%s", message);

#ifdef _WIN32
	OutputDebugStringA(message);
#endif

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		assert(!"Validation error encountered!");
	}
	return VK_FALSE;
}

#ifndef NDEBUG
VkDebugReportCallbackEXT registerDebugCallback(VkInstance instance)
{
	if (!vkCreateDebugReportCallbackEXT)
		return nullptr;
	VkDebugReportCallbackCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	createInfo.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT;
	createInfo.pfnCallback = debugReportCallback;

	VkDebugReportCallbackEXT callback = 0;

	vkCreateDebugReportCallbackEXT(instance, &createInfo, 0, &callback);

	return callback;
}
#endif

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
	return true; // @TODO
#endif
}

VkPhysicalDevice pickPhysicalDevice(VkPhysicalDevice* physicalDevices, uint32_t physicalDeviceCount)
{
	VkPhysicalDevice preferred = 0;
	VkPhysicalDevice fallback = 0;

	const char* ngpu = getenv("NGPU");

	for (uint32_t i = 0; i < physicalDeviceCount; ++i)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physicalDevices[i], &props);

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
			continue;

		LOGI("GPU%d: %s (Vulkan 1.%d)", i, props.deviceName, VK_VERSION_MINOR(props.apiVersion));

		uint32_t familyIndex = getGraphicsFamilyIndex(physicalDevices[i]);

		if (familyIndex == VK_QUEUE_FAMILY_IGNORED)
		{
			LOGI("GPU%d skipped: no graphics queue", i);
			continue;
		}

		if (!supportsPresentation(physicalDevices[i], familyIndex))
		{
			LOGI("GPU%d skipped: doesn't support presentation", i);
			continue;
		}

		if (props.apiVersion < API_VERSION)
		{
			uint32_t major = VK_VERSION_MAJOR(props.apiVersion);
			uint32_t minor = VK_VERSION_MINOR(props.apiVersion);
			uint32_t patch = VK_VERSION_PATCH(props.apiVersion);

			uint32_t requiredMajor = VK_VERSION_MAJOR(API_VERSION);
			uint32_t requiredMinor = VK_VERSION_MINOR(API_VERSION);
			uint32_t requiredPatch = VK_VERSION_PATCH(API_VERSION);

			LOGI("GPU%d skipped: Vulkan API version too low: %u.%u.%u, required: %u.%u.%u",
			    i, major, minor, patch, requiredMajor, requiredMinor, requiredPatch);
			continue;
		}

		if (ngpu && atoi(ngpu) == i)
		{
			preferred = physicalDevices[i];
		}

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
		LOGI("Selected GPU %s.", props.deviceName);
	}
	else
	{
		LOGE("No compatible GPU found!");
	}

	return result;
}

VkDevice createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t familyIndex, bool pushDescriptorSupported, bool meshShadingEnabled, bool raytracingSupported, bool clusterrtSupported)
{
	float queuePriorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = familyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = queuePriorities;

	std::vector<const char*> extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	if (pushDescriptorSupported)
	{
		// extensions.emplace_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	}
	else
	{
		extensions.emplace_back(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME);
	}

	if (meshShadingEnabled)
	{
		extensions.emplace_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
	}

	if (raytracingSupported)
	{
		extensions.emplace_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		extensions.emplace_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
		extensions.emplace_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	}
    
#if defined(WIN32)
	if (clusterrtSupported)
		extensions.push_back(VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME);
#endif

	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.features.vertexPipelineStoresAndAtomics = VK_TRUE; // TODO: we aren't using this yet.
	features.features.multiDrawIndirect = VK_TRUE;
	features.features.pipelineStatisticsQuery = VK_TRUE;
	features.features.shaderInt16 = VK_TRUE;
	features.features.shaderInt64 = VK_TRUE;
	features.features.samplerAnisotropy = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	features11.shaderDrawParameters = VK_TRUE;
	features11.storageBuffer16BitAccess = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.shaderInt8 = VK_TRUE;
	features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
	features12.storageBuffer8BitAccess = VK_TRUE;
	features12.shaderFloat16 = VK_TRUE;
	features12.drawIndirectCount = VK_TRUE;
	features12.samplerFilterMinmax = VK_TRUE;
	features12.scalarBlockLayout = VK_TRUE;

	features12.descriptorIndexing = VK_TRUE;
	features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
	features12.descriptorBindingPartiallyBound = VK_TRUE;
	features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	features12.runtimeDescriptorArray = VK_TRUE;
	features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

	if (raytracingSupported)
	{
		features12.bufferDeviceAddress = VK_TRUE;
	}

	VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	features13.maintenance4 = VK_TRUE;
	features13.shaderDemoteToHelperInvocation = VK_TRUE;

#if defined(WIN32)
	VkPhysicalDeviceVulkan14Features features14 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
	features14.maintenance5 = true;
	features14.maintenance6 = true;
	features14.pushDescriptor = true;
#endif

	// This will only be used if meshShadingEnabled = true (see below)
	VkPhysicalDeviceMeshShaderFeaturesEXT featuresMesh = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	featuresMesh.taskShader = VK_TRUE;
	featuresMesh.meshShader = VK_TRUE;

	// This will only be used if raytraicingSupported = true (see below)
	VkPhysicalDeviceRayQueryFeaturesKHR featuresRayQuery = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	featuresRayQuery.rayQuery = VK_TRUE;

	// This will only be used if raytracingSupported=true (see below)
	VkPhysicalDeviceAccelerationStructureFeaturesKHR featuresAccelerationStructure = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	featuresAccelerationStructure.accelerationStructure = VK_TRUE;

	VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;

	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledExtensionCount = uint32_t(extensions.size());
	createInfo.pEnabledFeatures = &features.features;

	createInfo.pNext = &features11;
	features11.pNext = &features12;
	features12.pNext = &features13;
#if defined(WIN32)
	features13.pNext = &features14;
	void** ppNext = &features14.pNext;
#elif defined(ANDROID)
    void** ppNext = &features13.pNext;
#endif

	if (meshShadingEnabled)
	{
		*ppNext = &featuresMesh;
		ppNext = &featuresMesh.pNext;
	}
	if (raytracingSupported)
	{
		*ppNext = &featuresRayQuery;
		ppNext = &featuresRayQuery.pNext;

		*ppNext = &featuresAccelerationStructure;
		ppNext = &featuresAccelerationStructure.pNext;
	}

	vkGetPhysicalDeviceFeatures(physicalDevice, &features.features);
	VkDevice device = 0;
	VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, 0, &device));

	return device;
}

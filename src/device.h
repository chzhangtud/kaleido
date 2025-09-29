#pragma once

// We have a strict requirement for latest Vulkan version to be available
#if defined(WIN32)
#define API_VERSION VK_API_VERSION_1_4
#elif defined(ANDROID)
#define API_VERSION VK_API_VERSION_1_3
#endif

VkInstance createInstance();

VkDebugReportCallbackEXT registerDebugCallback(VkInstance instance);
uint32_t getGraphicsFamilyIndex(VkPhysicalDevice physicalDevice);
VkPhysicalDevice pickPhysicalDevice(VkPhysicalDevice* physicalDevices, uint32_t physicalDeviceCount);
VkDevice createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t familyIndex, bool pushDescriptorSupported, bool meshShadingEnabled, bool raytracingSupported, bool clusterrtSupported);

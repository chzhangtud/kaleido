#pragma once
#define CURRENT_VK_VERSION VK_API_VERSION_1_3

VkInstance createInstance();

VkDebugReportCallbackEXT registerDebugCallback(VkInstance instance);
uint32_t getGraphicsFamilyIndex(VkPhysicalDevice physicalDevice);
VkPhysicalDevice pickPhysicalDevice(VkPhysicalDevice* physicalDevices, uint32_t physicalDeviceCount);
VkDevice createDevice(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t familyIndex, bool meshShadingEnabled, bool raytracingSupported);

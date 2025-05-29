#pragma once

struct Swapchain
{
	VkSwapchainKHR swapchain;
	std::vector<VkImage> images;
	uint32_t width, height;
	uint32_t imageCount;
};

VkSurfaceKHR createSurface(VkInstance instance, GLFWwindow* window);
VkFormat getSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
void createSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, uint32_t familyIndex, VkFormat format, VkSwapchainKHR oldSwapchain = 0);
void destroySwapchain(VkDevice device, Swapchain& swapchain);

enum SwapchainStatus
{
	Swapchain_Ready,
	Swapchain_Resized,
	Swapchain_NotReady,
};

SwapchainStatus updateSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, uint32_t familyIndex, VkFormat format);

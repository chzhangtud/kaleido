#if defined(VK_USE_PLATFORM_WIN32_KHR)
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#include <android/native_window.h>
#include <android/native_window_jni.h>
#endif

#include "common.h"
#include "swapchain.h"
#include "config.h"

#define VSYNC CONFIG_VSYNC

VkSurfaceKHR createSurface(VkInstance instance,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    GLFWwindow* window
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    ANativeWindow* window
#endif
)
{
	// Note: GLFW has a helper glfwCreateWindowSurface but we're going to do this the hard way to reduce our reliance on GLFW Vulkan specifics
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VkWin32SurfaceCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	createInfo.hinstance = GetModuleHandle(0);
	createInfo.hwnd = glfwGetWin32Window(window);

	VkSurfaceKHR surface = 0;
	VK_CHECK(vkCreateWin32SurfaceKHR(instance, &createInfo, 0, &surface));
	return surface;
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	VkAndroidSurfaceCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
	createInfo.window = window;

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &createInfo, 0, &surface));
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

	for (uint32_t i = 0; i < formatCount; ++i)
	{
		if (formats[i].format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 || formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
			return formats[i].format;
	}

	for (uint32_t i = 0; i < formatCount; ++i)
	{
		if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
			return formats[i].format;
	}

	return formats[0].format;
}

static VkSwapchainKHR createSwapchain(VkDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR surfaceCaps, uint32_t familyIndex, VkFormat format, uint32_t width, uint32_t height,
    VkSwapchainKHR oldSwapchain, const std::vector<VkPresentModeKHR>& presentModes)
{
	VkPresentModeKHR presentMode = VSYNC ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
	bool presentModeSupported = false;
	for (const auto& mode : presentModes)
	{
		if (mode == presentMode)
		{
			presentModeSupported = true;
			break;
		}
	}
	assert(presentModeSupported);

	VkCompositeAlphaFlagBitsKHR surfaceComposite =
	    (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
	                                                                                                                : (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)  ? VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
	                                                                                                                                                                                                      : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

	VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	createInfo.surface = surface;
	createInfo.minImageCount = std::max(uint32_t(MAX_FRAMES), surfaceCaps.minImageCount);
	createInfo.imageFormat = format;
	createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	createInfo.imageExtent.width = width;
	createInfo.imageExtent.height = height;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.queueFamilyIndexCount = 1;
	createInfo.pQueueFamilyIndices = &familyIndex;
	createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	createInfo.compositeAlpha = surfaceComposite;
	createInfo.presentMode = presentMode;
	createInfo.oldSwapchain = oldSwapchain;

	VkSwapchainKHR swapchain = 0;
	VK_CHECK(vkCreateSwapchainKHR(device, &createInfo, 0, &swapchain));

	return swapchain;
}

void createSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, uint32_t familyIndex,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    GLFWwindow* window,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    ANativeWindow* window,
#endif
    VkFormat format, VkSwapchainKHR oldSwapchain)
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

	int width = 0, height = 0;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	glfwGetFramebufferSize(window, &width, &height);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	width = ANativeWindow_getWidth(window);
	height = ANativeWindow_getHeight(window);
#endif

	uint32_t presentModeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

	VkSwapchainKHR swapchain = createSwapchain(device, surface, surfaceCaps, familyIndex, format, width, height, oldSwapchain, presentModes);
	assert(swapchain);

	uint32_t imageCount = 0;
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, 0));
	std::vector<VkImage> images(imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()));

	result.swapchain = swapchain;
	result.images = images;
	result.width = width;
	result.height = height;
	result.imageCount = imageCount;
}

void destroySwapchain(VkDevice device, Swapchain& swapchain)
{
	vkDestroySwapchainKHR(device, swapchain.swapchain, 0);
}

SwapchainStatus updateSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, uint32_t familyIndex,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    GLFWwindow* window,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    ANativeWindow* window,
#endif
    VkFormat format)
{
	int newWidth = 0, newHeight = 0;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	glfwGetFramebufferSize(window, &newWidth, &newHeight);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	newWidth = ANativeWindow_getWidth(window);
	newHeight = ANativeWindow_getHeight(window);
#endif

	if (newWidth == 0 || newHeight == 0)
		return Swapchain_NotReady;

	if (result.width == newWidth && result.height == newHeight)
		return Swapchain_Ready;

	VkSwapchainKHR oldSwapchain = result.swapchain;

	Swapchain old = result;

	createSwapchain(result, physicalDevice, device, surface, familyIndex, window, format, oldSwapchain);

	VK_CHECK(vkDeviceWaitIdle(device));

	destroySwapchain(device, old);

	return Swapchain_Resized;
}

#pragma once

#define VK_NO_PROTOTYPES

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#if defined(_WIN32)
    #include <imgui_impl_glfw.h>
    struct GLFWwindow;
#elif defined(__ANDROID__)
    #include <android/native_window.h>
    #include <imgui_impl_android.h>
#endif

class GuiRenderer
{
public:
	static const std::shared_ptr<GuiRenderer>& GetInstance();

	void Initialize(
   #if defined(_WIN32)
        GLFWwindow* window,
    #elif defined(__ANDROID__)
        ANativeWindow* window,
    #endif
	    uint32_t apiVersion,
	    VkInstance instance,
	    VkPhysicalDevice physicalDevice,
	    VkDevice device,
	    uint32_t graphicsQueueFamily,
	    VkQueue graphicsQueue,
	    const VkPipelineRenderingCreateInfo& renderingInfo,
	    VkFormat swapchainFormat,
	    uint32_t imageCount);

	void BeginFrame();
	void EndFrame();
	void RenderDrawData(VkCommandBuffer cmdBuf, VkImageView targetView, VkExtent2D extent);

	void Shutdown(VkDevice device);

private:
	void CreateImGuiDescriptorPool(VkDevice device);

	static std::shared_ptr<GuiRenderer> gInstance;

#if defined(_WIN32)
    GLFWwindow* mWindow = nullptr;
#elif defined(__ANDROID__)
    ANativeWindow* mWindow = nullptr;
#endif
	VkInstance mInstance = VK_NULL_HANDLE;
	VkDevice mDevice = VK_NULL_HANDLE;
	VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
};

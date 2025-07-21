#pragma once

#define VK_NO_PROTOTYPES

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>

struct GLFWwindow;

class GuiRenderer
{
public:
    static const std::shared_ptr<GuiRenderer>& GetInstance();

    void Initialize(GLFWwindow* window,
        uint32_t apiVersion,
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t graphicsQueueFamily,
        VkQueue graphicsQueue,
        VkDescriptorPool descriptorPool,
        VkFormat swapchainFormat,
        uint32_t imageCount);

    void BeginFrame();
    void EndFrame();
    void RenderDrawData(VkCommandBuffer cmdBuf, VkImageView targetView, VkExtent2D extent);

    void Shutdown();

private:
    void CreateImGuiDescriptorPool(VkDevice device);
    
    static std::shared_ptr<GuiRenderer> gInstance;

    GLFWwindow* mWindow = nullptr;
    VkInstance mInstance = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkRenderPass mDummyRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
};

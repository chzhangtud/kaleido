#include "GuiRenderer.h"
#if defined(_WIN32)
#include <GLFW/glfw3.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <android/log.h>
#include <imgui_impl_android.h>
#endif
#include "common.h"

std::shared_ptr<GuiRenderer> GuiRenderer::gInstance = nullptr;

const std::shared_ptr<GuiRenderer>& GuiRenderer::GetInstance()
{
	if (!gInstance)
		gInstance = std::make_shared<GuiRenderer>();
	return gInstance;
}

void GuiRenderer::Initialize(
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
    uint32_t imageCount)
{
	ImGui_ImplVulkan_LoadFunctions(
	    apiVersion,
	    [](const char* function_name, void* user_data) -> PFN_vkVoidFunction
	    {
		    return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
	    },
	    instance);

	mWindow = window;
	mInstance = instance;
	mDevice = device;

	if (!mDescriptorPool)
		CreateImGuiDescriptorPool(device);

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	float scale = 1.0f;
	io.FontGlobalScale = scale;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

#if defined(_WIN32)
	ImGui_ImplGlfw_InitForVulkan(window, true);
#elif defined(__ANDROID__)
	ImGui_ImplAndroid_Init(window);
#endif

	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = instance;
	initInfo.PhysicalDevice = physicalDevice;
	initInfo.Device = device;
	initInfo.QueueFamily = graphicsQueueFamily;
	initInfo.Queue = graphicsQueue;
	initInfo.PipelineCache = VK_NULL_HANDLE;
	initInfo.DescriptorPool = mDescriptorPool;
	initInfo.Subpass = 0;
	initInfo.MinImageCount = imageCount;
	initInfo.ImageCount = imageCount;
	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	initInfo.RenderPass = 0;
	initInfo.Allocator = nullptr;
	initInfo.UseDynamicRendering = true;
	initInfo.PipelineRenderingCreateInfo = renderingInfo;

	bool ret = ImGui_ImplVulkan_Init(&initInfo);
}

void GuiRenderer::BeginFrame()
{
	ImGui_ImplVulkan_NewFrame();
#if defined(_WIN32)
	ImGui_ImplGlfw_NewFrame();
#elif defined(__ANDROID__)
	ImGui_ImplAndroid_NewFrame();
#endif
	ImGui::NewFrame();
}

void GuiRenderer::EndFrame()
{
	ImGui::Render();
}

void GuiRenderer::RenderDrawData(VkCommandBuffer cmdBuf, VkImageView targetView, VkExtent2D extent)
{
	ImDrawData* drawData = ImGui::GetDrawData();
	if (!drawData || drawData->TotalVtxCount == 0)
		return;

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = targetView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = { 0, 0 };
	renderingInfo.renderArea.extent = extent;
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmdBuf, &renderingInfo);
	ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuf);
	vkCmdEndRendering(cmdBuf);
}

void GuiRenderer::Shutdown(VkDevice device)
{
	ImGui_ImplVulkan_Shutdown();
#if defined(_WIN32)
	ImGui_ImplGlfw_Shutdown();
#elif defined(__ANDROID__)
	ImGui_ImplAndroid_Shutdown();
#endif
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(device, mDescriptorPool, nullptr);
	mDescriptorPool = nullptr;
}

void GuiRenderer::CreateImGuiDescriptorPool(VkDevice device)
{
	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 32;
	pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
	pool_info.pPoolSizes = pool_sizes;

	if (vkCreateDescriptorPool(device, &pool_info, nullptr, &mDescriptorPool) != VK_SUCCESS)
	{
		LOGE("Failed to create ImGui descriptor pool");
		assert(false);
	}
}

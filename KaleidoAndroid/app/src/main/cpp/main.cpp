#include <jni.h>
#include <string>
#include <vector>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Kaleido", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "Kaleido", __VA_ARGS__)

ANativeWindow* g_window = nullptr;
VkInstance g_instance = VK_NULL_HANDLE;
VkSurfaceKHR g_surface = VK_NULL_HANDLE;
VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
VkQueue g_graphicsQueue = VK_NULL_HANDLE;
uint32_t g_graphicsQueueFamily = 0;
VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;

// Helper: check Vulkan result
#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err != VK_SUCCESS) {                                        \
            LOGE("Detected Vulkan error: %d", err);                     \
            return;                                                     \
        }                                                               \
    } while (0)

extern "C" JNIEXPORT jstring JNICALL
Java_com_chzhang_kaleido_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from Vulkan!";
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_initVulkan(
        JNIEnv* env,
        jobject /* this */,
        jobject surface) {
    g_window = ANativeWindow_fromSurface(env, surface);

    // 1. Create Vulkan instance
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Kaleido";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {"VK_KHR_surface", "VK_KHR_android_surface"};

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

    if (vkCreateInstance(&createInfo, nullptr, &g_instance) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance");
        return;
    }

    // 2. Create Android surface
    VkAndroidSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.window = g_window;
    if (vkCreateAndroidSurfaceKHR(g_instance, &surfaceInfo, nullptr, &g_surface) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan surface");
        return;
    }

    // 3. Pick first physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(g_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        LOGE("No Vulkan devices found");
        return;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(g_instance, &deviceCount, devices.data());
    g_physicalDevice = devices[0];

    // 4. Find graphics queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(g_physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_physicalDevice, i, g_surface, &presentSupport);
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            g_graphicsQueueFamily = i;
            break;
        }
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = g_graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    if (vkCreateDevice(g_physicalDevice, &deviceCreateInfo, nullptr, &g_device) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan device");
        return;
    }
    vkGetDeviceQueue(g_device, g_graphicsQueueFamily, 0, &g_graphicsQueue);

    // 5. Create swapchain (simplified)
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &capabilities);

    VkSwapchainCreateInfoKHR swapchainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainInfo.surface = g_surface;
    swapchainInfo.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && swapchainInfo.minImageCount > capabilities.maxImageCount) {
        swapchainInfo.minImageCount = capabilities.maxImageCount;
    }
    swapchainInfo.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = capabilities.currentExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
    swapchainInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(g_device, &swapchainInfo, nullptr, &g_swapchain) != VK_SUCCESS) {
        LOGE("Failed to create swapchain");
        return;
    }

    LOGI("Vulkan initialized successfully!");
}

extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_cleanupVulkan(
        JNIEnv* env,
        jobject /* this */) {
    if (g_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(g_device);
        if (g_swapchain) vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
        vkDestroyDevice(g_device, nullptr);
        g_device = VK_NULL_HANDLE;
    }
    if (g_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
        g_surface = VK_NULL_HANDLE;
    }
    if (g_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
    }
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    LOGI("Vulkan cleaned up.");
}

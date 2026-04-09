#pragma once

#include <vector>
#include <assert.h>
#include <vector>
#include <memory>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#include <volk.h>
#include <imgui.h>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

extern AAssetManager* g_assetManager;
#endif

#define VK_CHECK(call) \
	do \
	{ \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS); \
	} while (0)

#define VK_CHECK_FORCE(call) \
	do \
	{ \
		VkResult result_ = call; \
		if (result_ != VK_SUCCESS) \
		{ \
			LOGE("%s:%d: %s failed with error %d\n", __FILE__, __LINE__, #call, result_); \
			abort(); \
		} \
	} while (0)

#define VK_CHECK_SWAPCHAIN(call) \
	do \
	{ \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS || result_ == VK_SUBOPTIMAL_KHR || result_ == VK_ERROR_OUT_OF_DATE_KHR); \
	} while (0)

#define VK_CHECK_QUERY(call) \
	do \
	{ \
		VkResult result_ = call; \
		assert(result_ == VK_SUCCESS || result_ == VK_NOT_READY); \
	} while (0)

template <typename T, size_t Size>
char (*countof_helper(T (&_Array)[Size]))[Size];

#define COUNTOF(array) (sizeof(*countof_helper(array)) + 0)

#if defined(WIN32)
#include <stdio.h>
#define LOGE(fmt, ...) fprintf(stderr, "\033[31m[ERROR]: \033[0m" fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stderr, "\033[33m[WARNING]: \033[0m" fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) fprintf(stdout, "\033[34m[INFO]: \033[0m" fmt "\n", ##__VA_ARGS__)
#define LOGD(fmt, ...) fprintf(stdout, "\033[36m[DEBUG]: \033[0m: " fmt "\n", ##__VA_ARGS__)
#elif defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "Kaleido"
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#endif

#define INFO_HEADER ("\033[34m[INFO]: \033[0m")
#define WARNING_HEADER ("\033[33m[WARNING]: \033[0m")
#define ERROR_HEADER ("\033[31m[ERROR]: \033[0m")

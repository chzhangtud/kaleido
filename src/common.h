#pragma once

#include <vector>
#include <assert.h>
#include <vector>

#include <volk.h>

#define VK_CHECK(call)                  \
    do                                  \
    {                                   \
        VkResult result_ = call;        \
        assert(result_ == VK_SUCCESS);  \
    } while(0)

template <typename T, size_t Size>
char (*countof_helper(T(&_Array)[Size]))[Size];

#define COUNTOF(array) (sizeof(*countof_helper(array)) + 0)

#define LOGI(str) ("\033[34m[INFO]: \033[0m" str)
#define LOGW(str) ("\033[33m[WARNING]: \033[0m" str)
#define LOGE(str) ("\033[31m[ERROR]: \033[0m" str)

#define INFO_HEADER LOGI("")
#define WARNING_HEADER LOGW("")
#define ERROR_HEADER LOGE("")

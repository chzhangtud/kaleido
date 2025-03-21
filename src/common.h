#pragma once

#include <vector>
#include <assert.h>

#include <volk.h>

#define VK_CHECK(call)                  \
    do                                  \
    {                                   \
        VkResult result_ = call;        \
        assert(result_ == VK_SUCCESS);  \
    } while(0)

#ifndef ARRAYSIZE
#define ARRAYSIZE(array) (sizeof(array) / sizeof(array[0]))
#endif // !ARRAYSIZE

#define LOGI(str) ("\033[34m[INFO]: \033[0m" str)
#define LOGW(str) ("\033[33m[WARNING]: \033[0m" str)
#define LOGE(str) ("\033[31m[ERROR]: \033[0m" str)

#define INFO_HEADER LOGI("")
#define WARNING_HEADER LOGW("")
#define ERROR_HEADER LOGE("")

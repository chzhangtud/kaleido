#pragma once

#include <vector>
#include <assert.h>
#include <vector>
#include <memory>
#include <string>

#include <volk.h>
#include <imgui.h>

#define VK_CHECK(call)                  \
    do                                  \
    {                                   \
        VkResult result_ = call;        \
        assert(result_ == VK_SUCCESS);  \
    } while(0)


#define VK_CHECK_SWAPCHAIN(call)                                       \
do {                                                                    \
        VkResult result_ = call;                                        \
        assert(result_ == VK_SUCCESS || result_ == VK_SUBOPTIMAL_KHR || result_ == VK_ERROR_OUT_OF_DATE_KHR); \
} while (0)

template <typename T, size_t Size>
char (*countof_helper(T(&_Array)[Size]))[Size];

#define COUNTOF(array) (sizeof(*countof_helper(array)) + 0)

#define LOGI(str) ("\033[34m[INFO]: \033[0m" str)
#define LOGW(str) ("\033[33m[WARNING]: \033[0m" str)
#define LOGE(str) ("\033[31m[ERROR]: \033[0m" str)

#define INFO_HEADER LOGI("")
#define WARNING_HEADER LOGW("")
#define ERROR_HEADER LOGE("")

template <typename T, typename Compare = std::less<T>>
void DisplayProfilingData(const char* str, T data, T threshGreen, T threshOrange, Compare comp = Compare())
{
	ImGui::Text(str);
	if (comp(data, threshGreen))
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
	}
	else if (comp(data, threshOrange))
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 165, 0, 255));
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
	}
	ImGui::SameLine();
	ImGui::Text("%.2f", data);
	ImGui::PopStyleColor();
}

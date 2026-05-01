#pragma once

#include "imgui.h"

#include <vector>

struct GraphCanvasRect
{
	ImVec2 min{ 0.0f, 0.0f };
	ImVec2 max{ 0.0f, 0.0f };
};

bool GraphCanvasContainsPoint(const GraphCanvasRect& rect, const ImVec2& p);
float GraphCanvasClampZoom(float currentZoom, float wheelDelta, float minZoom, float maxZoom);
bool GraphCanvasApplyWheelZoom(const GraphCanvasRect& rect, const std::vector<int>& nodeIds, float& inOutZoom);

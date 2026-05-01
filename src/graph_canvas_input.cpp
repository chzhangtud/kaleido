#include "graph_canvas_input.h"

#include "imnodes.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kMinZoom = 0.4f;
constexpr float kMaxZoom = 2.5f;
constexpr float kWheelZoomStep = 1.15f;
} // namespace

bool GraphCanvasContainsPoint(const GraphCanvasRect& rect, const ImVec2& p)
{
	return p.x >= rect.min.x && p.x <= rect.max.x && p.y >= rect.min.y && p.y <= rect.max.y;
}

float GraphCanvasClampZoom(float currentZoom, float wheelDelta, float minZoom, float maxZoom)
{
	if (wheelDelta == 0.0f)
		return currentZoom;
	const float target = currentZoom * std::pow(kWheelZoomStep, wheelDelta);
	return std::max(minZoom, std::min(target, maxZoom));
}

bool GraphCanvasApplyWheelZoom(const GraphCanvasRect& rect, const std::vector<int>& nodeIds, float& inOutZoom)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.MouseWheel == 0.0f)
		return false;
	if (!GraphCanvasContainsPoint(rect, io.MousePos))
		return false;

	// Zoom is ctrl+wheel only in graph canvas.
	if (!io.KeyCtrl)
	{
		io.MouseWheel = 0.0f;
		return true;
	}

	// Consume wheel in canvas so side panels/sliders do not react.
	const float wheel = io.MouseWheel;
	io.MouseWheel = 0.0f;
	if (nodeIds.empty())
		return true;

	const float newZoom = GraphCanvasClampZoom(inOutZoom, wheel, kMinZoom, kMaxZoom);
	if (newZoom == inOutZoom)
		return true;

	const float scale = newZoom / inOutZoom;
	const ImVec2 panning = ImNodes::EditorContextGetPanning();
	// Convert screen-space mouse position into the same grid space used by nodes.
	const ImVec2 anchor(io.MousePos.x - rect.min.x - panning.x, io.MousePos.y - rect.min.y - panning.y);
	for (int nodeId : nodeIds)
	{
		const ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodeId);
		const ImVec2 out(anchor.x + (pos.x - anchor.x) * scale, anchor.y + (pos.y - anchor.y) * scale);
		ImNodes::SetNodeGridSpacePos(nodeId, out);
	}
	inOutZoom = newZoom;
	return true;
}

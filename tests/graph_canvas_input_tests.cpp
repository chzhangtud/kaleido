#include "../src/graph_canvas_input.h"

#include <cassert>

static void TestContainsPoint()
{
	GraphCanvasRect rect{};
	rect.min = ImVec2(10.0f, 20.0f);
	rect.max = ImVec2(100.0f, 120.0f);
	assert(GraphCanvasContainsPoint(rect, ImVec2(10.0f, 20.0f)));
	assert(GraphCanvasContainsPoint(rect, ImVec2(55.0f, 88.0f)));
	assert(!GraphCanvasContainsPoint(rect, ImVec2(9.0f, 40.0f)));
	assert(!GraphCanvasContainsPoint(rect, ImVec2(40.0f, 121.0f)));
}

static void TestClampZoom()
{
	assert(GraphCanvasClampZoom(1.0f, 0.0f, 0.4f, 2.5f) == 1.0f);
	const float zIn = GraphCanvasClampZoom(1.0f, 1.0f, 0.4f, 2.5f);
	const float zOut = GraphCanvasClampZoom(1.0f, -1.0f, 0.4f, 2.5f);
	assert(zIn > 1.0f);
	assert(zOut < 1.0f);
	assert(GraphCanvasClampZoom(2.45f, 2.0f, 0.4f, 2.5f) <= 2.5f);
	assert(GraphCanvasClampZoom(0.42f, -2.0f, 0.4f, 2.5f) >= 0.4f);
}

int main()
{
	TestContainsPoint();
	TestClampZoom();
	return 0;
}

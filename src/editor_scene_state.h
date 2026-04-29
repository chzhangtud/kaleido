#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "shader_graph_editor_session.h"

#include "math.h"

struct EditorRenderSettings
{
	bool meshShadingEnabled = true;
	bool taskShadingEnabled = false;
	bool cullingEnabled = true;
	bool occlusionEnabled = true;
	bool clusterOcclusionEnabled = true;
	bool shadowEnabled = true;
	int shadowQuality = 1;
	bool shadowBlurEnabled = true;
	bool shadowCheckerboard = false;
	bool taaEnabled = true;
	float taaBlendAlpha = 0.1f;
	bool screenSpaceRefractionEnabled = true;
	bool lodEnabled = true;
	int debugLodStep = 0;
	bool animationEnabled = false;
	bool reloadShaders = false;
	int debugGuiMode = 1;
	bool debugSleep = false;
	int gbufferDebugViewMode = 0;
	bool clusterRTEnabled = false;
};

struct EditorCameraState
{
	vec3 position = vec3(0.f);
	vec3 eulerDegrees = vec3(0.f);
	float fovY = 0.0f;
	float znear = 0.1f;
	float moveSpeed = 5.0f;
	bool dollyZoomEnabled = false;
	float dollyZoomRefDistance = 5.0f;
};

// Scene-tree selection and editor visualization toggles (mirrors `Scene` editor fields).
struct EditorSceneUiState
{
	std::optional<uint32_t> selectedGltfNode;
	bool selectionOutlineEnabled = false;
	bool showSelectedSubtreeAabb = false;
	bool visualizeRenderGraph = false;
	bool renderGraphVisualizerWindowOpen = false;
	int renderGraphVisualizerMode = 0; // 0 = live, 1 = imported
	int renderGraphVisualizerGraphMode = 0; // 0 = simple(pass->pass), 1 = full(pass/resource)
	std::string renderGraphVisualizerImportedPath;
	bool shaderGraphWindowOpen = false;
	std::string shaderGraphCurrentPath;
	int shaderGraphFocusedNodeId = -1;
	ShaderGraphEditorState shaderGraphEditorState = ShaderGraphEditorState::Clean;
};

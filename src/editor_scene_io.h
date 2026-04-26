#pragma once

#include <string>
#include <vector>

#include "editor_scene_state.h"

struct EditorSceneSnapshot
{
	std::string modelPath;
	EditorCameraState camera;
	EditorRenderSettings renderSettings;
	// Editor 3D viewport render size in pixels; (0,0) if unset (legacy scene files).
	uint32_t viewportWidth = 0;
	uint32_t viewportHeight = 0;
	EditorSceneUiState editorUi;
	// Per gltf node local TRS matrices (column-major), same length/order as `Scene::transformNodes` after load.
	// Empty = omit / legacy file (keep transforms from the gltf).
	std::vector<mat4> transformNodeLocals;
};

bool SaveEditorSceneSnapshot(const std::string& sceneFilePath, const EditorSceneSnapshot& snapshot, std::string* outError);
bool LoadEditorSceneSnapshot(const std::string& sceneFilePath, EditorSceneSnapshot& outSnapshot, std::string* outError);

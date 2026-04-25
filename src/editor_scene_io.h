#pragma once

#include <string>

#include "editor_scene_state.h"

struct EditorSceneSnapshot
{
	std::string modelPath;
	EditorCameraState camera;
	EditorRenderSettings renderSettings;
	// Editor 3D viewport render size in pixels; (0,0) if unset (legacy scene files).
	uint32_t viewportWidth = 0;
	uint32_t viewportHeight = 0;
};

bool SaveEditorSceneSnapshot(const std::string& sceneFilePath, const EditorSceneSnapshot& snapshot, std::string* outError);
bool LoadEditorSceneSnapshot(const std::string& sceneFilePath, EditorSceneSnapshot& outSnapshot, std::string* outError);

#pragma once

#include <string>

#include "editor_scene_state.h"

struct EditorSceneSnapshot
{
	std::string modelPath;
	EditorCameraState camera;
	EditorRenderSettings renderSettings;
};

bool SaveEditorSceneSnapshot(const std::string& sceneFilePath, const EditorSceneSnapshot& snapshot, std::string* outError);
bool LoadEditorSceneSnapshot(const std::string& sceneFilePath, EditorSceneSnapshot& outSnapshot, std::string* outError);

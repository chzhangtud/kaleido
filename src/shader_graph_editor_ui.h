#pragma once

#include "scene.h"

#include <functional>
#include <string>

struct ShaderGraphEditorUiDeps
{
	std::function<std::string(const std::string&)> resolvePathForIO;
};

void DrawShaderGraphEditorBridge(Scene& scene, const ShaderGraphEditorUiDeps& deps);

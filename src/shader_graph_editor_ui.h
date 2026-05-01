#pragma once

#include "scene.h"
#include "shader_graph_editor_ops.h"
#include <functional>
#include <string>

struct ImNodesContext;

struct ShaderGraphEditorUiDeps
{
	std::function<std::string(const std::string&)> resolvePathForIO;
};

void SetShaderGraphImNodesContext(ImNodesContext* context);
void DrawShaderGraphEditorBridge(Scene& scene, const ShaderGraphEditorUiDeps& deps);

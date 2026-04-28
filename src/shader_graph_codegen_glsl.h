#pragma once

#include "shader_graph_types.h"

#include <string>

struct SGCodegenResult
{
	bool ok = false;
	std::string fragmentFunction;
	std::string error;
};

SGCodegenResult GenerateShaderGraphGlsl(const ShaderGraphAsset& graph);

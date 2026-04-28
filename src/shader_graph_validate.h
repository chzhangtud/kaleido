#pragma once

#include "shader_graph_types.h"

#include <string>
#include <vector>

struct SGValidateResult
{
	bool ok = false;
	std::string error;
	std::vector<int> topoOrder;
};

SGValidateResult ValidateShaderGraph(const ShaderGraphAsset& g);

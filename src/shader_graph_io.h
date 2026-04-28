#pragma once

#include "shader_graph_types.h"

#include <string>

bool SerializeShaderGraphToJson(const ShaderGraphAsset& g, std::string& outJson, std::string* outError);
bool DeserializeShaderGraphFromJson(const std::string& json, ShaderGraphAsset& outGraph, std::string* outError);

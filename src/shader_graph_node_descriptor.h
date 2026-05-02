#pragma once

#include "shader_graph_types.h"

#include <cstdint>
#include <string>
#include <vector>

struct SGNodePortDesc
{
	uint16_t id = 0;
	std::string name;
	SGPortType type = SGPortType::PortFloat;
	bool optional = false;
};

struct ShaderGraphNodeDescriptor
{
	std::string id;
	int version = 1;
	std::string evalKind;
	std::string glslTemplate;
	std::vector<SGNodePortDesc> inputs;
	std::vector<SGNodePortDesc> outputs;
	std::string compatFallbackId;
};

bool DeserializeShaderGraphNodeDescriptor(const std::string& json, ShaderGraphNodeDescriptor& out, std::string* outError);

#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class SGPortType : uint8_t
{
	PortBool = 0,
	PortInt,
	PortFloat,
	PortVec2,
	PortVec3,
	PortVec4,
};

enum class SGNodeOp : uint16_t
{
	InputUV = 0,
	InputTime,
	InputWorldPos,
	InputNormal,
	ConstFloat,
	ConstVec2,
	ConstVec3,
	ParamFloat,
	Add,
	Sub,
	Mul,
	Div,
	Sin,
	Cos,
	Frac,
	Lerp,
	Saturate,
	ComposeVec3,
	SplitVec3X,
	SplitVec3Y,
	SplitVec3Z,
	NoisePerlin3D,
	Remap,
	OutputSurface,
};

struct SGNode
{
	int id = -1;
	SGNodeOp op = SGNodeOp::InputUV;
	std::vector<float> values;
	std::string text;
};

struct SGEdge
{
	int fromNode = -1;
	int fromPort = 0;
	int toNode = -1;
	int toPort = 0;
};

struct ShaderGraphAsset
{
	std::string format = "kaleido_shader_graph";
	int version = 1;
	std::string domain = "spatial_fragment";
	std::string entry = "material_surface";
	std::vector<SGNode> nodes;
	std::vector<SGEdge> edges;
	bool hasEditorMeta = false;
	float editorViewX = 0.0f;
	float editorViewY = 0.0f;
	float editorZoom = 1.0f;
};

const char* SGPortTypeToString(SGPortType type);
bool SGPortTypeFromString(const std::string& text, SGPortType& outType);
bool SGPortTypeIsNumeric(SGPortType type);
bool SGPortTypeCanImplicitConvert(SGPortType from, SGPortType to);

const char* SGNodeOpToString(SGNodeOp op);
bool SGNodeOpFromString(const std::string& text, SGNodeOp& outOp);

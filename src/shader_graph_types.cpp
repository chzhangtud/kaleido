#include "shader_graph_types.h"

#include <cstddef>

namespace
{
template <typename T>
struct SGEnumName
{
	T value;
	const char* name;
};

const SGEnumName<SGPortType> kPortTypes[] = {
	{ SGPortType::PortBool, "bool" },
	{ SGPortType::PortInt, "int" },
	{ SGPortType::PortFloat, "float" },
	{ SGPortType::PortVec2, "vec2" },
	{ SGPortType::PortVec3, "vec3" },
	{ SGPortType::PortVec4, "vec4" },
};

const SGEnumName<SGNodeOp> kNodeOps[] = {
	{ SGNodeOp::InputUV, "InputUV" },
	{ SGNodeOp::InputTime, "InputTime" },
	{ SGNodeOp::InputWorldPos, "InputWorldPos" },
	{ SGNodeOp::InputNormal, "InputNormal" },
	{ SGNodeOp::ConstFloat, "ConstFloat" },
	{ SGNodeOp::ConstVec2, "ConstVec2" },
	{ SGNodeOp::ConstVec3, "ConstVec3" },
	{ SGNodeOp::ParamFloat, "ParamFloat" },
	{ SGNodeOp::Add, "Add" },
	{ SGNodeOp::Sub, "Sub" },
	{ SGNodeOp::Mul, "Mul" },
	{ SGNodeOp::Div, "Div" },
	{ SGNodeOp::Sin, "Sin" },
	{ SGNodeOp::Cos, "Cos" },
	{ SGNodeOp::Frac, "Frac" },
	{ SGNodeOp::Lerp, "Lerp" },
	{ SGNodeOp::Saturate, "Saturate" },
	{ SGNodeOp::ComposeVec3, "ComposeVec3" },
	{ SGNodeOp::SplitVec3X, "SplitVec3X" },
	{ SGNodeOp::SplitVec3Y, "SplitVec3Y" },
	{ SGNodeOp::SplitVec3Z, "SplitVec3Z" },
	{ SGNodeOp::NoisePerlin3D, "NoisePerlin3D" },
	{ SGNodeOp::Remap, "Remap" },
	{ SGNodeOp::OutputSurface, "OutputSurface" },
};

template <typename T, std::size_t N>
const char* EnumToString(const SGEnumName<T> (&items)[N], T value)
{
	for (const SGEnumName<T>& item : items)
	{
		if (item.value == value)
			return item.name;
	}
	return "";
}

template <typename T, std::size_t N>
bool EnumFromString(const SGEnumName<T> (&items)[N], const std::string& text, T& out)
{
	for (const SGEnumName<T>& item : items)
	{
		if (text == item.name)
		{
			out = item.value;
			return true;
		}
	}
	return false;
}
} // namespace

const char* SGPortTypeToString(SGPortType type)
{
	return EnumToString(kPortTypes, type);
}

bool SGPortTypeFromString(const std::string& text, SGPortType& outType)
{
	return EnumFromString(kPortTypes, text, outType);
}

bool SGPortTypeIsNumeric(SGPortType type)
{
	return type == SGPortType::PortInt || type == SGPortType::PortFloat || type == SGPortType::PortVec2 ||
	       type == SGPortType::PortVec3 || type == SGPortType::PortVec4;
}

bool SGPortTypeCanImplicitConvert(SGPortType from, SGPortType to)
{
	if (from == to)
		return true;
	if (from == SGPortType::PortBool || to == SGPortType::PortBool)
		return false;
	if (from == SGPortType::PortInt)
		return to == SGPortType::PortFloat || to == SGPortType::PortVec2 || to == SGPortType::PortVec3 ||
		       to == SGPortType::PortVec4;
	if (from == SGPortType::PortFloat)
		return to == SGPortType::PortInt || to == SGPortType::PortVec2 || to == SGPortType::PortVec3 ||
		       to == SGPortType::PortVec4;
	if (from == SGPortType::PortVec2)
		return to == SGPortType::PortFloat || to == SGPortType::PortInt || to == SGPortType::PortVec3 ||
		       to == SGPortType::PortVec4;
	if (from == SGPortType::PortVec3)
		return to == SGPortType::PortFloat || to == SGPortType::PortInt || to == SGPortType::PortVec2 ||
		       to == SGPortType::PortVec4;
	if (from == SGPortType::PortVec4)
		return to == SGPortType::PortFloat || to == SGPortType::PortInt || to == SGPortType::PortVec2 ||
		       to == SGPortType::PortVec3;
	return false;
}

const char* SGNodeOpToString(SGNodeOp op)
{
	return EnumToString(kNodeOps, op);
}

bool SGNodeOpFromString(const std::string& text, SGNodeOp& outOp)
{
	return EnumFromString(kNodeOps, text, outOp);
}

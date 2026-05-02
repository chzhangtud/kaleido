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

bool SGNodeOpToDescriptorId(SGNodeOp op, std::string& outId)
{
	switch (op)
	{
	case SGNodeOp::InputUV: outId = "builtin/input/uv"; return true;
	case SGNodeOp::InputTime: outId = "builtin/input/time"; return true;
	case SGNodeOp::InputWorldPos: outId = "builtin/input/world_pos"; return true;
	case SGNodeOp::InputNormal: outId = "builtin/input/normal"; return true;
	case SGNodeOp::ConstFloat: outId = "builtin/const/float"; return true;
	case SGNodeOp::ConstVec2: outId = "builtin/const/vec2"; return true;
	case SGNodeOp::ConstVec3: outId = "builtin/const/vec3"; return true;
	case SGNodeOp::ParamFloat: outId = "builtin/param/float"; return true;
	case SGNodeOp::Add: outId = "builtin/math/add"; return true;
	case SGNodeOp::Sub: outId = "builtin/math/sub"; return true;
	case SGNodeOp::Mul: outId = "builtin/math/mul"; return true;
	case SGNodeOp::Div: outId = "builtin/math/div"; return true;
	case SGNodeOp::Sin: outId = "builtin/math/sin"; return true;
	case SGNodeOp::Cos: outId = "builtin/math/cos"; return true;
	case SGNodeOp::Frac: outId = "builtin/math/frac"; return true;
	case SGNodeOp::Lerp: outId = "builtin/math/lerp"; return true;
	case SGNodeOp::Saturate: outId = "builtin/math/saturate"; return true;
	case SGNodeOp::ComposeVec3: outId = "builtin/vector/compose_vec3"; return true;
	case SGNodeOp::SplitVec3X: outId = "builtin/vector/split_vec3_x"; return true;
	case SGNodeOp::SplitVec3Y: outId = "builtin/vector/split_vec3_y"; return true;
	case SGNodeOp::SplitVec3Z: outId = "builtin/vector/split_vec3_z"; return true;
	case SGNodeOp::NoisePerlin3D: outId = "builtin/noise/perlin3d"; return true;
	case SGNodeOp::Remap: outId = "builtin/math/remap"; return true;
	case SGNodeOp::OutputSurface: outId = "builtin/output/surface"; return true;
	default: return false;
	}
}

bool SGNodeOpFromDescriptorId(const std::string& id, SGNodeOp& outOp)
{
	static const char kLegacy[] = "builtin/legacy/";
	const size_t legacyLen = sizeof(kLegacy) - 1u;
	if (id.size() > legacyLen && id.compare(0, legacyLen, kLegacy) == 0)
		return SGNodeOpFromString(id.substr(legacyLen), outOp);

	struct Entry
	{
		const char* idStr;
		SGNodeOp op;
	};
	static const Entry kCanonical[] = {
		{ "builtin/input/uv", SGNodeOp::InputUV },
		{ "builtin/input/time", SGNodeOp::InputTime },
		{ "builtin/input/world_pos", SGNodeOp::InputWorldPos },
		{ "builtin/input/normal", SGNodeOp::InputNormal },
		{ "builtin/const/float", SGNodeOp::ConstFloat },
		{ "builtin/const/vec2", SGNodeOp::ConstVec2 },
		{ "builtin/const/vec3", SGNodeOp::ConstVec3 },
		{ "builtin/param/float", SGNodeOp::ParamFloat },
		{ "builtin/math/add", SGNodeOp::Add },
		{ "builtin/math/sub", SGNodeOp::Sub },
		{ "builtin/math/mul", SGNodeOp::Mul },
		{ "builtin/math/div", SGNodeOp::Div },
		{ "builtin/math/sin", SGNodeOp::Sin },
		{ "builtin/math/cos", SGNodeOp::Cos },
		{ "builtin/math/frac", SGNodeOp::Frac },
		{ "builtin/math/lerp", SGNodeOp::Lerp },
		{ "builtin/math/saturate", SGNodeOp::Saturate },
		{ "builtin/vector/compose_vec3", SGNodeOp::ComposeVec3 },
		{ "builtin/vector/split_vec3_x", SGNodeOp::SplitVec3X },
		{ "builtin/vector/split_vec3_y", SGNodeOp::SplitVec3Y },
		{ "builtin/vector/split_vec3_z", SGNodeOp::SplitVec3Z },
		{ "builtin/noise/perlin3d", SGNodeOp::NoisePerlin3D },
		{ "builtin/math/remap", SGNodeOp::Remap },
		{ "builtin/output/surface", SGNodeOp::OutputSurface },
	};
	for (const Entry& e : kCanonical)
	{
		if (id == e.idStr)
		{
			outOp = e.op;
			return true;
		}
	}
	return false;
}

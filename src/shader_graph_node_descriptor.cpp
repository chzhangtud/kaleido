#include "shader_graph_node_descriptor.h"

#include "rapidjson/document.h"

namespace
{
bool Fail(std::string* outError, const char* msg)
{
	if (outError)
		*outError = msg;
	return false;
}

bool ParsePorts(const rapidjson::Value& arr, std::vector<SGNodePortDesc>& outPorts, std::string* outError)
{
	if (!arr.IsArray())
		return Fail(outError, "ports must be array");
	outPorts.clear();
	for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
	{
		const rapidjson::Value& item = arr[i];
		if (!item.IsObject())
			return Fail(outError, "port entry must be object");
		const auto idIt = item.FindMember("id");
		const auto nameIt = item.FindMember("name");
		const auto typeIt = item.FindMember("type");
		if (idIt == item.MemberEnd() || !idIt->value.IsUint())
			return Fail(outError, "port.id missing or invalid");
		if (nameIt == item.MemberEnd() || !nameIt->value.IsString())
			return Fail(outError, "port.name missing or invalid");
		if (typeIt == item.MemberEnd() || !typeIt->value.IsString())
			return Fail(outError, "port.type missing or invalid");

		SGPortType type = SGPortType::PortFloat;
		if (!SGPortTypeFromString(typeIt->value.GetString(), type))
			return Fail(outError, "unknown port.type");
		SGNodePortDesc p{};
		p.id = static_cast<uint16_t>(idIt->value.GetUint());
		p.name = nameIt->value.GetString();
		p.type = type;
		const auto optIt = item.FindMember("optional");
		if (optIt != item.MemberEnd() && optIt->value.IsBool())
			p.optional = optIt->value.GetBool();
		outPorts.push_back(std::move(p));
	}
	return true;
}
} // namespace

bool DeserializeShaderGraphNodeDescriptor(const std::string& json, ShaderGraphNodeDescriptor& out, std::string* outError)
{
	rapidjson::Document doc;
	if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject())
		return Fail(outError, "invalid descriptor json");

	const auto fmtIt = doc.FindMember("format");
	const auto idIt = doc.FindMember("id");
	const auto verIt = doc.FindMember("version");
	const auto inIt = doc.FindMember("inputs");
	const auto outIt = doc.FindMember("outputs");
	if (fmtIt == doc.MemberEnd() || !fmtIt->value.IsString() ||
	    std::string(fmtIt->value.GetString()) != "kaleido_shader_graph_node")
		return Fail(outError, "descriptor format mismatch");
	if (idIt == doc.MemberEnd() || !idIt->value.IsString())
		return Fail(outError, "descriptor id missing");
	if (verIt == doc.MemberEnd() || !verIt->value.IsInt())
		return Fail(outError, "descriptor version missing");
	if (inIt == doc.MemberEnd())
		return Fail(outError, "descriptor inputs missing");
	if (outIt == doc.MemberEnd())
		return Fail(outError, "descriptor outputs missing");

	ShaderGraphNodeDescriptor d{};
	d.id = idIt->value.GetString();
	d.version = verIt->value.GetInt();
	if (!ParsePorts(inIt->value, d.inputs, outError))
		return false;
	if (!ParsePorts(outIt->value, d.outputs, outError))
		return false;

	const auto evalIt = doc.FindMember("evalKind");
	if (evalIt != doc.MemberEnd() && evalIt->value.IsString())
		d.evalKind = evalIt->value.GetString();
	const auto tplIt = doc.FindMember("glslTemplate");
	if (tplIt != doc.MemberEnd() && tplIt->value.IsString())
		d.glslTemplate = tplIt->value.GetString();

	const auto compatIt = doc.FindMember("compat");
	if (compatIt != doc.MemberEnd() && compatIt->value.IsObject())
	{
		const auto fallbackIt = compatIt->value.FindMember("fallback");
		if (fallbackIt != compatIt->value.MemberEnd() && fallbackIt->value.IsString())
			d.compatFallbackId = fallbackIt->value.GetString();
	}

	out = std::move(d);
	return true;
}

#include "shader_graph_io.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace
{
bool AppendError(std::string* outError, const std::string& message)
{
	if (outError)
		*outError = message;
	return false;
}
} // namespace

bool SerializeShaderGraphToJson(const ShaderGraphAsset& g, std::string& outJson, std::string* outError)
{
	if (g.format != "kaleido_shader_graph")
		return AppendError(outError, "graph.format must be kaleido_shader_graph.");
	if (g.version != 1)
		return AppendError(outError, "graph.version must be 1.");
	if (g.domain != "spatial_fragment")
		return AppendError(outError, "graph.domain must be spatial_fragment.");

	rapidjson::StringBuffer sb;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
	writer.SetIndent(' ', 2);
	writer.StartObject();
	writer.Key("format");
	writer.String(g.format.c_str());
	writer.Key("version");
	writer.Int(g.version);
	writer.Key("domain");
	writer.String(g.domain.c_str());
	writer.Key("entry");
	writer.String(g.entry.c_str());

	writer.Key("nodes");
	writer.StartArray();
	for (const SGNode& n : g.nodes)
	{
		writer.StartObject();
		writer.Key("id");
		writer.Int(n.id);
		writer.Key("op");
		writer.String(SGNodeOpToString(n.op));
		if (!n.values.empty())
		{
			writer.Key("values");
			writer.StartArray();
			for (float v : n.values)
				writer.Double(double(v));
			writer.EndArray();
		}
		if (!n.text.empty())
		{
			writer.Key("text");
			writer.String(n.text.c_str());
		}
		writer.EndObject();
	}
	writer.EndArray();

	writer.Key("edges");
	writer.StartArray();
	for (const SGEdge& e : g.edges)
	{
		writer.StartObject();
		writer.Key("fromNode");
		writer.Int(e.fromNode);
		writer.Key("fromPort");
		writer.Int(e.fromPort);
		writer.Key("toNode");
		writer.Int(e.toNode);
		writer.Key("toPort");
		writer.Int(e.toPort);
		writer.EndObject();
	}
	writer.EndArray();
	writer.EndObject();

	outJson.assign(sb.GetString(), sb.GetSize());
	return true;
}

bool DeserializeShaderGraphFromJson(const std::string& json, ShaderGraphAsset& outGraph, std::string* outError)
{
	rapidjson::Document doc;
	const rapidjson::ParseResult parseResult = doc.Parse(json.c_str());
	if (!parseResult)
	{
		return AppendError(outError,
		    std::string("JSON parse failed: ") + rapidjson::GetParseError_En(parseResult.Code()) +
		        " at offset " + std::to_string(parseResult.Offset()));
	}
	if (!doc.IsObject())
		return AppendError(outError, "graph root must be object.");

	const auto fmtIt = doc.FindMember("format");
	const auto verIt = doc.FindMember("version");
	const auto domainIt = doc.FindMember("domain");
	const auto entryIt = doc.FindMember("entry");
	const auto nodesIt = doc.FindMember("nodes");
	const auto edgesIt = doc.FindMember("edges");

	if (fmtIt == doc.MemberEnd() || !fmtIt->value.IsString())
		return AppendError(outError, "graph.format missing.");
	if (std::string(fmtIt->value.GetString()) != "kaleido_shader_graph")
		return AppendError(outError, "graph.format mismatch.");
	if (verIt == doc.MemberEnd() || !verIt->value.IsInt() || verIt->value.GetInt() != 1)
		return AppendError(outError, "graph.version must be 1.");
	if (domainIt == doc.MemberEnd() || !domainIt->value.IsString() || std::string(domainIt->value.GetString()) != "spatial_fragment")
		return AppendError(outError, "graph.domain must be spatial_fragment.");
	if (entryIt == doc.MemberEnd() || !entryIt->value.IsString())
		return AppendError(outError, "graph.entry missing.");
	if (nodesIt == doc.MemberEnd() || !nodesIt->value.IsArray())
		return AppendError(outError, "graph.nodes must be array.");
	if (edgesIt == doc.MemberEnd() || !edgesIt->value.IsArray())
		return AppendError(outError, "graph.edges must be array.");

	ShaderGraphAsset graph{};
	graph.format = fmtIt->value.GetString();
	graph.version = verIt->value.GetInt();
	graph.domain = domainIt->value.GetString();
	graph.entry = entryIt->value.GetString();

	for (rapidjson::SizeType i = 0; i < nodesIt->value.Size(); ++i)
	{
		const rapidjson::Value& n = nodesIt->value[i];
		if (!n.IsObject())
			return AppendError(outError, "graph.nodes entry must be object.");
		const auto idIt = n.FindMember("id");
		const auto opIt = n.FindMember("op");
		if (idIt == n.MemberEnd() || !idIt->value.IsInt())
			return AppendError(outError, "graph.nodes.id missing or invalid.");
		if (opIt == n.MemberEnd() || !opIt->value.IsString())
			return AppendError(outError, "graph.nodes.op missing or invalid.");
		SGNodeOp op = SGNodeOp::InputUV;
		if (!SGNodeOpFromString(opIt->value.GetString(), op))
			return AppendError(outError, "graph.nodes.op unknown.");
		SGNode node{};
		node.id = idIt->value.GetInt();
		node.op = op;
		const auto valuesIt = n.FindMember("values");
		if (valuesIt != n.MemberEnd())
		{
			if (!valuesIt->value.IsArray())
				return AppendError(outError, "graph.nodes.values must be an array.");
			for (rapidjson::SizeType j = 0; j < valuesIt->value.Size(); ++j)
			{
				if (!valuesIt->value[j].IsNumber())
					return AppendError(outError, "graph.nodes.values entries must be numbers.");
				node.values.push_back(valuesIt->value[j].GetFloat());
			}
		}
		const auto textIt = n.FindMember("text");
		if (textIt != n.MemberEnd())
		{
			if (!textIt->value.IsString())
				return AppendError(outError, "graph.nodes.text must be string.");
			node.text = textIt->value.GetString();
		}
		graph.nodes.push_back(node);
	}

	for (rapidjson::SizeType i = 0; i < edgesIt->value.Size(); ++i)
	{
		const rapidjson::Value& e = edgesIt->value[i];
		if (!e.IsObject())
			return AppendError(outError, "graph.edges entry must be object.");
		const auto fromNode = e.FindMember("fromNode");
		const auto fromPort = e.FindMember("fromPort");
		const auto toNode = e.FindMember("toNode");
		const auto toPort = e.FindMember("toPort");
		if (fromNode == e.MemberEnd() || fromPort == e.MemberEnd() || toNode == e.MemberEnd() || toPort == e.MemberEnd())
			return AppendError(outError, "graph.edges entry missing required field.");
		if (!fromNode->value.IsInt() || !fromPort->value.IsInt() || !toNode->value.IsInt() || !toPort->value.IsInt())
			return AppendError(outError, "graph.edges fields must be integers.");
		graph.edges.push_back({
		    fromNode->value.GetInt(),
		    fromPort->value.GetInt(),
		    toNode->value.GetInt(),
		    toPort->value.GetInt(),
		});
	}

	outGraph = std::move(graph);
	return true;
}

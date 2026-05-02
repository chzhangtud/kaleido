#include "shader_graph_io.h"
#include "shader_graph_migration.h"

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
	if (g.domain != "spatial_fragment")
		return AppendError(outError, "graph.domain must be spatial_fragment.");

	ShaderGraphAsset toWrite{};
	if (!g.nodes.empty())
	{
		ShaderGraphAsset legacy{};
		legacy.format = g.format;
		legacy.version = 1;
		legacy.domain = g.domain;
		legacy.entry = g.entry;
		legacy.nodes = g.nodes;
		legacy.edges = g.edges;
		legacy.hasEditorMeta = g.hasEditorMeta;
		legacy.editorViewX = g.editorViewX;
		legacy.editorViewY = g.editorViewY;
		legacy.editorZoom = g.editorZoom;
		if (!MigrateLegacyShaderGraph(legacy, toWrite, outError))
			return false;
	}
	else if (!g.nodeInstances.empty())
	{
		toWrite = g;
		toWrite.version = 3;
	}
	else
		return AppendError(outError, "Graph has no nodes.");

	rapidjson::StringBuffer sb;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
	writer.SetIndent(' ', 2);
	writer.StartObject();
	writer.Key("format");
	writer.String(toWrite.format.c_str());
	writer.Key("version");
	writer.Int(toWrite.version);
	writer.Key("domain");
	writer.String(toWrite.domain.c_str());
	writer.Key("entry");
	writer.String(toWrite.entry.c_str());

	writer.Key("nodeInstances");
	writer.StartArray();
	for (const ShaderGraphNodeInstance& n : toWrite.nodeInstances)
	{
		writer.StartObject();
		writer.Key("id");
		writer.Int(n.id);
		writer.Key("descriptorId");
		writer.String(n.descriptorId.c_str());
		writer.Key("descriptorVersion");
		writer.Int(n.descriptorVersion);
		if (!n.numericOverrides.empty())
		{
			writer.Key("numericOverrides");
			writer.StartArray();
			for (float v : n.numericOverrides)
				writer.Double(double(v));
			writer.EndArray();
		}
		if (!n.textOverride.empty())
		{
			writer.Key("textOverride");
			writer.String(n.textOverride.c_str());
		}
		writer.EndObject();
	}
	writer.EndArray();

	writer.Key("edges");
	writer.StartArray();
	for (const SGEdge& e : toWrite.edges)
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
	if (toWrite.hasEditorMeta)
	{
		writer.Key("editorMeta");
		writer.StartObject();
		writer.Key("viewX");
		writer.Double(double(toWrite.editorViewX));
		writer.Key("viewY");
		writer.Double(double(toWrite.editorViewY));
		writer.Key("zoom");
		writer.Double(double(toWrite.editorZoom));
		writer.EndObject();
	}
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
	const auto instancesIt = doc.FindMember("nodeInstances");
	const auto edgesIt = doc.FindMember("edges");
	const auto editorMetaIt = doc.FindMember("editorMeta");

	if (fmtIt == doc.MemberEnd() || !fmtIt->value.IsString())
		return AppendError(outError, "graph.format missing.");
	if (std::string(fmtIt->value.GetString()) != "kaleido_shader_graph")
		return AppendError(outError, "graph.format mismatch.");
	if (verIt == doc.MemberEnd() || !verIt->value.IsInt())
		return AppendError(outError, "graph.version missing.");
	if (domainIt == doc.MemberEnd() || !domainIt->value.IsString() || std::string(domainIt->value.GetString()) != "spatial_fragment")
		return AppendError(outError, "graph.domain must be spatial_fragment.");
	if (entryIt == doc.MemberEnd() || !entryIt->value.IsString())
		return AppendError(outError, "graph.entry missing.");
	if (edgesIt == doc.MemberEnd() || !edgesIt->value.IsArray())
		return AppendError(outError, "graph.edges must be array.");

	ShaderGraphAsset graph{};
	graph.format = fmtIt->value.GetString();
	const int version = verIt->value.GetInt();
	graph.version = version;
	graph.domain = domainIt->value.GetString();
	graph.entry = entryIt->value.GetString();

	if (version == 1)
	{
		if (nodesIt == doc.MemberEnd() || !nodesIt->value.IsArray())
			return AppendError(outError, "graph.nodes must be array for version 1.");
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
		ShaderGraphAsset migrated{};
		if (!MigrateLegacyShaderGraph(graph, migrated, outError))
			return false;
		graph = std::move(migrated);
	}
	else if (version == 3)
	{
		if (instancesIt == doc.MemberEnd() || !instancesIt->value.IsArray())
			return AppendError(outError, "graph.nodeInstances must be array for version 3.");
		for (rapidjson::SizeType i = 0; i < instancesIt->value.Size(); ++i)
		{
			const rapidjson::Value& n = instancesIt->value[i];
			if (!n.IsObject())
				return AppendError(outError, "graph.nodeInstances entry must be object.");
			const auto idIt = n.FindMember("id");
			const auto didIt = n.FindMember("descriptorId");
			if (idIt == n.MemberEnd() || !idIt->value.IsInt())
				return AppendError(outError, "graph.nodeInstances.id missing or invalid.");
			if (didIt == n.MemberEnd() || !didIt->value.IsString())
				return AppendError(outError, "graph.nodeInstances.descriptorId missing or invalid.");
			ShaderGraphNodeInstance instance{};
			instance.id = idIt->value.GetInt();
			instance.descriptorId = didIt->value.GetString();
			const auto dverIt = n.FindMember("descriptorVersion");
			if (dverIt != n.MemberEnd())
			{
				if (!dverIt->value.IsInt())
					return AppendError(outError, "graph.nodeInstances.descriptorVersion must be int.");
				instance.descriptorVersion = dverIt->value.GetInt();
			}
			const auto valuesIt = n.FindMember("numericOverrides");
			if (valuesIt != n.MemberEnd())
			{
				if (!valuesIt->value.IsArray())
					return AppendError(outError, "graph.nodeInstances.numericOverrides must be array.");
				for (rapidjson::SizeType j = 0; j < valuesIt->value.Size(); ++j)
				{
					if (!valuesIt->value[j].IsNumber())
						return AppendError(outError, "graph.nodeInstances.numericOverrides entries must be numbers.");
					instance.numericOverrides.push_back(valuesIt->value[j].GetFloat());
				}
			}
			const auto textIt = n.FindMember("textOverride");
			if (textIt != n.MemberEnd())
			{
				if (!textIt->value.IsString())
					return AppendError(outError, "graph.nodeInstances.textOverride must be string.");
				instance.textOverride = textIt->value.GetString();
			}
			graph.nodeInstances.push_back(std::move(instance));
		}
		PopulateLegacyNodesFromInstances(graph);
	}
	else
	{
		return AppendError(outError, "graph.version unsupported.");
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
	if (editorMetaIt != doc.MemberEnd())
	{
		if (!editorMetaIt->value.IsObject())
			return AppendError(outError, "graph.editorMeta must be an object when present.");
		const auto viewXIt = editorMetaIt->value.FindMember("viewX");
		const auto viewYIt = editorMetaIt->value.FindMember("viewY");
		const auto zoomIt = editorMetaIt->value.FindMember("zoom");
		if (viewXIt != editorMetaIt->value.MemberEnd())
		{
			if (!viewXIt->value.IsNumber())
				return AppendError(outError, "graph.editorMeta.viewX must be number.");
			graph.editorViewX = viewXIt->value.GetFloat();
			graph.hasEditorMeta = true;
		}
		if (viewYIt != editorMetaIt->value.MemberEnd())
		{
			if (!viewYIt->value.IsNumber())
				return AppendError(outError, "graph.editorMeta.viewY must be number.");
			graph.editorViewY = viewYIt->value.GetFloat();
			graph.hasEditorMeta = true;
		}
		if (zoomIt != editorMetaIt->value.MemberEnd())
		{
			if (!zoomIt->value.IsNumber())
				return AppendError(outError, "graph.editorMeta.zoom must be number.");
			graph.editorZoom = zoomIt->value.GetFloat();
			graph.hasEditorMeta = true;
		}
	}

	outGraph = std::move(graph);
	return true;
}

#include "rendergraph_viz.h"

#include "RenderGraph.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <tuple>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace
{
std::string MakePassId(const RGPass& pass, size_t index)
{
	return pass.name + "#" + std::to_string(index);
}

std::string MakeTextureResourceId(uint32_t textureId)
{
	return "tex:" + std::to_string(textureId);
}

std::string MakeExternalResourceId(const std::string& name)
{
	return "ext:" + name;
}

std::string EscapeDotString(const std::string& text)
{
	std::string out;
	out.reserve(text.size() + 4);
	for (char ch : text)
	{
		if (ch == '"' || ch == '\\')
			out.push_back('\\');
		out.push_back(ch);
	}
	return out;
}

bool ParseStringMember(const rapidjson::Value& obj, const char* key, std::string& out, std::string& outError)
{
	const auto it = obj.FindMember(key);
	if (it == obj.MemberEnd() || !it->value.IsString())
	{
		outError = std::string("rendergraph json: missing or invalid string field: ") + key;
		return false;
	}
	out = it->value.GetString();
	return true;
}
} // namespace

RenderGraphVizSnapshot BuildRenderGraphVizSnapshot(const RenderGraph& rg, uint64_t frameIndex)
{
	RenderGraphVizSnapshot snapshot{};
	snapshot.frameIndex = frameIndex;

	const std::vector<RGPass>& passes = rg.getPasses();
	const std::vector<size_t> topo = rg.getTopologicalOrder();
	std::vector<uint32_t> topoOrderForPass(passes.size(), 0);
	for (size_t i = 0; i < topo.size(); ++i)
	{
		if (topo[i] < topoOrderForPass.size())
			topoOrderForPass[topo[i]] = uint32_t(i);
	}

	std::set<std::string> seenResources;
	std::set<std::tuple<std::string, std::string, std::string, std::string>> seenEdges;

	for (size_t passIdx = 0; passIdx < passes.size(); ++passIdx)
	{
		const RGPass& pass = passes[passIdx];
		RenderGraphVizPassNode passNode{};
		passNode.id = MakePassId(pass, passIdx);
		passNode.name = pass.name;
		passNode.index = uint32_t(passIdx);
		passNode.topoOrder = passIdx < topoOrderForPass.size() ? topoOrderForPass[passIdx] : uint32_t(passIdx);
		snapshot.passes.push_back(std::move(passNode));
	}

	auto addResourceNode = [&](const std::string& id, const std::string& kind, const std::string& name)
	{
		if (!seenResources.insert(id).second)
			return;
		RenderGraphVizResourceNode node{};
		node.id = id;
		node.kind = kind;
		node.name = name;
		snapshot.resources.push_back(std::move(node));
	};

	auto addEdge = [&](const std::string& from, const std::string& to, const std::string& type, const std::string& state)
	{
		auto key = std::make_tuple(from, to, type, state);
		if (!seenEdges.insert(key).second)
			return;
		RenderGraphVizEdge edge{};
		edge.from = from;
		edge.to = to;
		edge.type = type;
		edge.state = state;
		snapshot.edges.push_back(std::move(edge));
	};

	for (size_t passIdx = 0; passIdx < passes.size(); ++passIdx)
	{
		const RGPass& pass = passes[passIdx];
		const std::string passId = MakePassId(pass, passIdx);

		for (size_t i = 0; i < pass.readTextures.size(); ++i)
		{
			const RGTextureHandle handle = pass.readTextures[i];
			if (!handle.IsValid())
				continue;
			const std::string resId = MakeTextureResourceId(handle.id);
			addResourceNode(resId, "internal_texture", std::to_string(handle.id));
			const std::string state = i < pass.readTextureStates.size() ? std::to_string(int(pass.readTextureStates[i])) : "";
			addEdge(resId, passId, "resource_to_pass", state);
		}
		for (const RGTextureHandle handle : pass.readTexturesFromPreviousFrame)
		{
			if (!handle.IsValid())
				continue;
			const std::string resId = MakeTextureResourceId(handle.id);
			addResourceNode(resId, "internal_texture", std::to_string(handle.id));
			addEdge(resId, passId, "resource_to_pass", "prev");
		}
		for (const RGTextureWrite& write : pass.writeTextures)
		{
			if (!write.handle.IsValid())
				continue;
			const std::string resId = MakeTextureResourceId(write.handle.id);
			addResourceNode(resId, "internal_texture", std::to_string(write.handle.id));
			addEdge(passId, resId, "pass_to_resource", std::to_string(int(write.state)));
		}
		for (size_t i = 0; i < pass.readExternalTextures.size(); ++i)
		{
			const std::string& name = pass.readExternalTextures[i];
			if (name.empty())
				continue;
			const std::string resId = MakeExternalResourceId(name);
			addResourceNode(resId, "external_texture", name);
			const std::string state = i < pass.readExternalTextureStates.size() ? std::to_string(int(pass.readExternalTextureStates[i])) : "";
			addEdge(resId, passId, "resource_to_pass", state);
		}
		for (const RGExternalTextureWrite& write : pass.writeExternalTextures)
		{
			if (write.name.empty())
				continue;
			const std::string resId = MakeExternalResourceId(write.name);
			addResourceNode(resId, "external_texture", write.name);
			addEdge(passId, resId, "pass_to_resource", std::to_string(int(write.state)));
		}
	}

	std::map<uint32_t, RGResourceRecord> textureRecords;
	std::map<std::string, RGResourceRecord> externalRecords;
	rg.buildResourceDependencyMap(textureRecords, externalRecords);
	for (const auto& [texId, rec] : textureRecords)
	{
		for (size_t producer : rec.producerPassIndices)
		{
			for (size_t consumer : rec.consumerPassIndices)
			{
				if (producer == consumer || producer >= passes.size() || consumer >= passes.size())
					continue;
				addEdge(MakePassId(passes[producer], producer), MakePassId(passes[consumer], consumer), "pass_to_pass", MakeTextureResourceId(texId));
			}
		}
	}
	for (const auto& [name, rec] : externalRecords)
	{
		for (size_t producer : rec.producerPassIndices)
		{
			for (size_t consumer : rec.consumerPassIndices)
			{
				if (producer == consumer || producer >= passes.size() || consumer >= passes.size())
					continue;
				addEdge(MakePassId(passes[producer], producer), MakePassId(passes[consumer], consumer), "pass_to_pass", MakeExternalResourceId(name));
			}
		}
	}

	std::sort(snapshot.passes.begin(), snapshot.passes.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
	std::sort(snapshot.resources.begin(), snapshot.resources.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	std::sort(snapshot.edges.begin(), snapshot.edges.end(), [](const auto& a, const auto& b)
	    {
		    if (a.from != b.from)
			    return a.from < b.from;
		    if (a.to != b.to)
			    return a.to < b.to;
		    if (a.type != b.type)
			    return a.type < b.type;
		    return a.state < b.state;
	    });
	return snapshot;
}

bool SerializeRenderGraphVizToJson(const RenderGraphVizSnapshot& snapshot, std::string& outJson, std::string* outError)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("format");
	writer.String("kaleido_rendergraph_viz");
	writer.Key("version");
	writer.Uint(1);
	writer.Key("frameIndex");
	writer.Uint64(snapshot.frameIndex);

	writer.Key("passes");
	writer.StartArray();
	for (const RenderGraphVizPassNode& pass : snapshot.passes)
	{
		writer.StartObject();
		writer.Key("id");
		writer.String(pass.id.c_str());
		writer.Key("name");
		writer.String(pass.name.c_str());
		writer.Key("index");
		writer.Uint(pass.index);
		writer.Key("topoOrder");
		writer.Uint(pass.topoOrder);
		writer.EndObject();
	}
	writer.EndArray();

	writer.Key("resources");
	writer.StartArray();
	for (const RenderGraphVizResourceNode& resource : snapshot.resources)
	{
		writer.StartObject();
		writer.Key("id");
		writer.String(resource.id.c_str());
		writer.Key("kind");
		writer.String(resource.kind.c_str());
		writer.Key("name");
		writer.String(resource.name.c_str());
		writer.EndObject();
	}
	writer.EndArray();

	writer.Key("edges");
	writer.StartArray();
	for (const RenderGraphVizEdge& edge : snapshot.edges)
	{
		writer.StartObject();
		writer.Key("from");
		writer.String(edge.from.c_str());
		writer.Key("to");
		writer.String(edge.to.c_str());
		writer.Key("type");
		writer.String(edge.type.c_str());
		if (!edge.state.empty())
		{
			writer.Key("state");
			writer.String(edge.state.c_str());
		}
		writer.EndObject();
	}
	writer.EndArray();
	writer.EndObject();

	outJson.assign(buffer.GetString(), buffer.GetSize());
	(void)outError;
	return true;
}

bool SerializeRenderGraphVizToDot(const RenderGraphVizSnapshot& snapshot, std::string& outDot, std::string* outError)
{
	std::ostringstream oss;
	oss << "digraph RenderGraph {\n";
	oss << "  rankdir=LR;\n";
	for (const RenderGraphVizPassNode& pass : snapshot.passes)
	{
		oss << "  \"" << EscapeDotString(pass.id) << "\" [shape=box, style=filled, fillcolor=\"#CCE5FF\", label=\""
		    << EscapeDotString(pass.name) << "\"];\n";
	}
	for (const RenderGraphVizResourceNode& resource : snapshot.resources)
	{
		oss << "  \"" << EscapeDotString(resource.id) << "\" [shape=ellipse, style=filled, fillcolor=\"#FFF2CC\", label=\""
		    << EscapeDotString(resource.id) << "\"];\n";
	}
	for (const RenderGraphVizEdge& edge : snapshot.edges)
	{
		oss << "  \"" << EscapeDotString(edge.from) << "\" -> \"" << EscapeDotString(edge.to) << "\"";
		if (!edge.type.empty() || !edge.state.empty())
		{
			oss << " [label=\"";
			if (!edge.type.empty())
				oss << EscapeDotString(edge.type);
			if (!edge.state.empty())
			{
				if (!edge.type.empty())
					oss << ":";
				oss << EscapeDotString(edge.state);
			}
			oss << "\"]";
		}
		oss << ";\n";
	}
	oss << "}\n";
	outDot = oss.str();
	(void)outError;
	return true;
}

bool DeserializeRenderGraphVizFromJson(const std::string& json, RenderGraphVizSnapshot& outSnapshot, std::string& outError)
{
	rapidjson::Document doc;
	const rapidjson::ParseResult ok = doc.Parse(json.c_str());
	if (!ok)
	{
		outError = std::string("rendergraph json parse failed: ") + rapidjson::GetParseError_En(ok.Code());
		return false;
	}
	if (!doc.IsObject())
	{
		outError = "rendergraph json root must be object";
		return false;
	}
	const auto formatIt = doc.FindMember("format");
	if (formatIt == doc.MemberEnd() || !formatIt->value.IsString() || std::string(formatIt->value.GetString()) != "kaleido_rendergraph_viz")
	{
		outError = "rendergraph json format mismatch";
		return false;
	}

	RenderGraphVizSnapshot snapshot{};
	const auto frameIt = doc.FindMember("frameIndex");
	if (frameIt != doc.MemberEnd() && frameIt->value.IsUint64())
		snapshot.frameIndex = frameIt->value.GetUint64();

	const auto passesIt = doc.FindMember("passes");
	if (passesIt == doc.MemberEnd() || !passesIt->value.IsArray())
	{
		outError = "rendergraph json missing passes array";
		return false;
	}
	for (rapidjson::SizeType i = 0; i < passesIt->value.Size(); ++i)
	{
		const rapidjson::Value& item = passesIt->value[i];
		if (!item.IsObject())
		{
			outError = "rendergraph json passes item must be object";
			return false;
		}
		RenderGraphVizPassNode pass{};
		if (!ParseStringMember(item, "id", pass.id, outError) || !ParseStringMember(item, "name", pass.name, outError))
			return false;
		const auto indexIt = item.FindMember("index");
		const auto topoIt = item.FindMember("topoOrder");
		if (indexIt == item.MemberEnd() || !indexIt->value.IsUint() || topoIt == item.MemberEnd() || !topoIt->value.IsUint())
		{
			outError = "rendergraph json pass index/topoOrder must be uint";
			return false;
		}
		pass.index = indexIt->value.GetUint();
		pass.topoOrder = topoIt->value.GetUint();
		snapshot.passes.push_back(std::move(pass));
	}

	const auto resourcesIt = doc.FindMember("resources");
	if (resourcesIt == doc.MemberEnd() || !resourcesIt->value.IsArray())
	{
		outError = "rendergraph json missing resources array";
		return false;
	}
	for (rapidjson::SizeType i = 0; i < resourcesIt->value.Size(); ++i)
	{
		const rapidjson::Value& item = resourcesIt->value[i];
		if (!item.IsObject())
		{
			outError = "rendergraph json resources item must be object";
			return false;
		}
		RenderGraphVizResourceNode resource{};
		if (!ParseStringMember(item, "id", resource.id, outError) ||
		    !ParseStringMember(item, "kind", resource.kind, outError) ||
		    !ParseStringMember(item, "name", resource.name, outError))
			return false;
		snapshot.resources.push_back(std::move(resource));
	}

	const auto edgesIt = doc.FindMember("edges");
	if (edgesIt == doc.MemberEnd() || !edgesIt->value.IsArray())
	{
		outError = "rendergraph json missing edges array";
		return false;
	}
	for (rapidjson::SizeType i = 0; i < edgesIt->value.Size(); ++i)
	{
		const rapidjson::Value& item = edgesIt->value[i];
		if (!item.IsObject())
		{
			outError = "rendergraph json edges item must be object";
			return false;
		}
		RenderGraphVizEdge edge{};
		if (!ParseStringMember(item, "from", edge.from, outError) ||
		    !ParseStringMember(item, "to", edge.to, outError) ||
		    !ParseStringMember(item, "type", edge.type, outError))
			return false;
		const auto stateIt = item.FindMember("state");
		if (stateIt != item.MemberEnd() && stateIt->value.IsString())
			edge.state = stateIt->value.GetString();
		snapshot.edges.push_back(std::move(edge));
	}

	std::sort(snapshot.passes.begin(), snapshot.passes.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
	std::sort(snapshot.resources.begin(), snapshot.resources.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	std::sort(snapshot.edges.begin(), snapshot.edges.end(), [](const auto& a, const auto& b)
	    {
		    if (a.from != b.from)
			    return a.from < b.from;
		    if (a.to != b.to)
			    return a.to < b.to;
		    if (a.type != b.type)
			    return a.type < b.type;
		    return a.state < b.state;
	    });

	outSnapshot = std::move(snapshot);
	return true;
}

#include "shader_graph_editor_ui.h"

#include "shader_graph_io.h"
#include "imgui.h"
#include "imnodes.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
int MakeLocalImnodesId(std::string_view text)
{
	uint32_t h = 2166136261u;
	for (unsigned char c : text)
	{
		h ^= c;
		h *= 16777619u;
	}
	h &= 0x7fffffffu;
	return h ? int(h) : 1;
}

std::vector<const char*> GetShaderGraphInputPortLabels(SGNodeOp op)
{
	switch (op)
	{
	case SGNodeOp::Add:
	case SGNodeOp::Sub:
	case SGNodeOp::Mul:
	case SGNodeOp::Div:
		return { "A", "B" };
	case SGNodeOp::Sin:
	case SGNodeOp::Cos:
	case SGNodeOp::Frac:
	case SGNodeOp::Saturate:
		return { "In" };
	case SGNodeOp::Lerp:
		return { "A", "B", "T" };
	case SGNodeOp::ComposeVec3:
		return { "X", "Y", "Z" };
	case SGNodeOp::SplitVec3X:
	case SGNodeOp::SplitVec3Y:
	case SGNodeOp::SplitVec3Z:
		return { "Vec3" };
	case SGNodeOp::NoisePerlin3D:
		return { "P" };
	case SGNodeOp::Remap:
		return { "Value", "InMin", "InMax", "OutMin", "OutMax" };
	case SGNodeOp::OutputSurface:
		return { "BaseColor" };
	default:
		return {};
	}
}

std::vector<const char*> GetShaderGraphOutputPortLabels(SGNodeOp op)
{
	switch (op)
	{
	case SGNodeOp::InputUV:
		return { "U", "V", "UV" };
	case SGNodeOp::InputWorldPos:
	case SGNodeOp::InputNormal:
	case SGNodeOp::ConstVec2:
	case SGNodeOp::ConstVec3:
	case SGNodeOp::ComposeVec3:
		return { "Out" };
	case SGNodeOp::InputTime:
	case SGNodeOp::ConstFloat:
	case SGNodeOp::ParamFloat:
	case SGNodeOp::Add:
	case SGNodeOp::Sub:
	case SGNodeOp::Mul:
	case SGNodeOp::Div:
	case SGNodeOp::Sin:
	case SGNodeOp::Cos:
	case SGNodeOp::Frac:
	case SGNodeOp::Lerp:
	case SGNodeOp::Saturate:
	case SGNodeOp::SplitVec3X:
	case SGNodeOp::SplitVec3Y:
	case SGNodeOp::SplitVec3Z:
	case SGNodeOp::NoisePerlin3D:
	case SGNodeOp::Remap:
		return { "Out" };
	default:
		return {};
	}
}

struct SgAttrRef
{
	int nodeId = -1;
	int port = -1;
	bool isOutput = false;
};

void DrawShaderGraphPreview(const ShaderGraphAsset& graph,
    int focusedNodeId,
    std::unordered_map<int, SgAttrRef>& outAttrRefs,
    std::unordered_map<int, int>& outImNodeToNode,
    std::unordered_map<int, size_t>& outLinkToEdgeIndex,
    std::unordered_set<int>& initializedNodes)
{
	std::unordered_map<int, int> depthByNode;
	std::unordered_map<int, std::vector<int>> adjacency;
	std::unordered_map<int, int> indegree;
	for (const SGNode& node : graph.nodes)
	{
		indegree[node.id] = 0;
		depthByNode[node.id] = 0;
	}
	for (const SGEdge& edge : graph.edges)
	{
		adjacency[edge.fromNode].push_back(edge.toNode);
		indegree[edge.toNode] += 1;
	}
	std::queue<int> q;
	for (const SGNode& node : graph.nodes)
	{
		if (indegree[node.id] == 0)
			q.push(node.id);
	}
	while (!q.empty())
	{
		const int n = q.front();
		q.pop();
		auto it = adjacency.find(n);
		if (it == adjacency.end())
			continue;
		for (int to : it->second)
		{
			depthByNode[to] = std::max(depthByNode[to], depthByNode[n] + 1);
			if (--indegree[to] == 0)
				q.push(to);
		}
	}

	std::unordered_map<int, int> rowByNode;
	std::unordered_map<int, int> rowCursorByDepth;
	for (const SGNode& node : graph.nodes)
	{
		const int d = depthByNode[node.id];
		rowByNode[node.id] = rowCursorByDepth[d]++;
	}

	ImNodes::BeginNodeEditor();
	for (const SGNode& node : graph.nodes)
	{
		const int depth = depthByNode[node.id];
		const int row = rowByNode[node.id];
		const int nodeId = MakeLocalImnodesId("sg:node:" + std::to_string(node.id));
		outImNodeToNode[nodeId] = node.id;
		if (initializedNodes.insert(node.id).second)
			ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(80.0f + depth * 260.0f, 80.0f + row * 180.0f));

		ImNodes::BeginNode(nodeId);
		ImNodes::BeginNodeTitleBar();
		if (node.id == focusedNodeId)
			ImGui::Text(">> %s  #%d", SGNodeOpToString(node.op), node.id);
		else
			ImGui::Text("%s  #%d", SGNodeOpToString(node.op), node.id);
		ImNodes::EndNodeTitleBar();

		const std::vector<const char*> inputs = GetShaderGraphInputPortLabels(node.op);
		for (size_t i = 0; i < inputs.size(); ++i)
		{
			const int attrId = MakeLocalImnodesId("sg:in:" + std::to_string(node.id) + ":" + std::to_string(i));
			outAttrRefs[attrId] = SgAttrRef{ node.id, int(i), false };
			ImNodes::BeginInputAttribute(attrId);
			ImGui::TextUnformatted(inputs[i]);
			ImNodes::EndInputAttribute();
		}

		const std::vector<const char*> outputs = GetShaderGraphOutputPortLabels(node.op);
		for (size_t i = 0; i < outputs.size(); ++i)
		{
			const int attrId = MakeLocalImnodesId("sg:out:" + std::to_string(node.id) + ":" + std::to_string(i));
			outAttrRefs[attrId] = SgAttrRef{ node.id, int(i), true };
			ImNodes::BeginOutputAttribute(attrId);
			ImGui::Indent(64.0f);
			ImGui::TextUnformatted(outputs[i]);
			ImNodes::EndOutputAttribute();
		}

		ImNodes::EndNode();
	}

	for (size_t i = 0; i < graph.edges.size(); ++i)
	{
		const SGEdge& e = graph.edges[i];
		const int fromAttr = MakeLocalImnodesId("sg:out:" + std::to_string(e.fromNode) + ":" + std::to_string(e.fromPort));
		const int toAttr = MakeLocalImnodesId("sg:in:" + std::to_string(e.toNode) + ":" + std::to_string(e.toPort));
		const int linkId =
		    MakeLocalImnodesId("sg:link:" + std::to_string(i) + ":" + std::to_string(e.fromNode) + ":" + std::to_string(e.toNode));
		outLinkToEdgeIndex[linkId] = i;
		ImNodes::Link(linkId, fromAttr, toAttr);
	}
	ImNodes::EndNodeEditor();
}

const char* SeverityName(SGCompileMessageSeverity severity)
{
	switch (severity)
	{
	case SGCompileMessageSeverity::Info:
		return "Info";
	case SGCompileMessageSeverity::Warning:
		return "Warning";
	case SGCompileMessageSeverity::Error:
		return "Error";
	default:
		return "Unknown";
	}
}

bool ReadTextFileUtf8Local(const std::string& path, std::string& outText, std::string* outError)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		if (outError)
			*outError = "Unable to open file: " + path;
		return false;
	}
	std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
	    static_cast<unsigned char>(bytes[2]) == 0xBF)
	{
		outText.assign(bytes.begin() + 3, bytes.end());
	}
	else
	{
		outText = std::move(bytes);
	}
	return true;
}

bool WriteTextFileUtf8Local(const std::string& path, const std::string& text, std::string* outError)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		if (outError)
			*outError = "Unable to write file: " + path;
		return false;
	}
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!out.good())
	{
		if (outError)
			*outError = "Write failed: " + path;
		return false;
	}
	return true;
}

std::string ToLowerAscii(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

std::vector<SGNodeOp> FilterNodeOps(const std::string& query)
{
	const SGNodeOp allOps[] = {
		SGNodeOp::InputUV,      SGNodeOp::InputTime,  SGNodeOp::InputWorldPos, SGNodeOp::InputNormal, SGNodeOp::ConstFloat,
		SGNodeOp::ConstVec2,    SGNodeOp::ConstVec3,  SGNodeOp::ParamFloat,    SGNodeOp::Add,         SGNodeOp::Sub,
		SGNodeOp::Mul,          SGNodeOp::Div,        SGNodeOp::Sin,           SGNodeOp::Cos,         SGNodeOp::Frac,
		SGNodeOp::Lerp,         SGNodeOp::Saturate,   SGNodeOp::ComposeVec3,   SGNodeOp::SplitVec3X,  SGNodeOp::SplitVec3Y,
		SGNodeOp::SplitVec3Z,   SGNodeOp::NoisePerlin3D, SGNodeOp::Remap,      SGNodeOp::OutputSurface,
	};
	const std::string q = ToLowerAscii(query);
	std::vector<SGNodeOp> result;
	for (SGNodeOp op : allOps)
	{
		const std::string name = ToLowerAscii(SGNodeOpToString(op));
		if (q.empty() || name.find(q) != std::string::npos)
			result.push_back(op);
	}
	return result;
}

bool IsNumeric(SGPortType t)
{
	return t == SGPortType::PortFloat || t == SGPortType::PortVec2 || t == SGPortType::PortVec3;
}

bool IsCompatible(SGPortType from, SGPortType to)
{
	if (from == to)
		return true;
	if (from == SGPortType::PortFloat && (to == SGPortType::PortVec2 || to == SGPortType::PortVec3))
		return true;
	return false;
}

bool TryGetExpectedInputType(const SGNode& node, int inputPort, SGPortType& out)
{
	switch (node.op)
	{
	case SGNodeOp::Add:
	case SGNodeOp::Sub:
	case SGNodeOp::Mul:
	case SGNodeOp::Div:
	case SGNodeOp::Sin:
	case SGNodeOp::Cos:
	case SGNodeOp::Frac:
	case SGNodeOp::Lerp:
	case SGNodeOp::Saturate:
	case SGNodeOp::Remap:
		out = SGPortType::PortFloat;
		return true;
	case SGNodeOp::ComposeVec3:
		out = SGPortType::PortFloat;
		return inputPort >= 0 && inputPort <= 2;
	case SGNodeOp::SplitVec3X:
	case SGNodeOp::SplitVec3Y:
	case SGNodeOp::SplitVec3Z:
	case SGNodeOp::NoisePerlin3D:
		out = SGPortType::PortVec3;
		return inputPort == 0;
	case SGNodeOp::OutputSurface:
		out = SGPortType::PortVec3;
		return inputPort == 0;
	default:
		return false;
	}
}

std::unordered_map<int, std::unordered_map<int, SGPortType>> BuildOutputTypes(const ShaderGraphAsset& graph)
{
	std::unordered_map<int, std::unordered_map<int, SGPortType>> out;
	std::unordered_map<int, const SGNode*> nodeById;
	std::unordered_map<int, int> indegree;
	std::unordered_map<int, std::vector<int>> adjacency;
	std::unordered_map<int, std::vector<const SGEdge*>> inEdges;
	for (const SGNode& node : graph.nodes)
	{
		nodeById[node.id] = &node;
		indegree[node.id] = 0;
	}
	for (const SGEdge& edge : graph.edges)
	{
		indegree[edge.toNode] += 1;
		adjacency[edge.fromNode].push_back(edge.toNode);
		inEdges[edge.toNode].push_back(&edge);
	}

	std::queue<int> q;
	for (const auto& kv : indegree)
	{
		if (kv.second == 0)
			q.push(kv.first);
	}
	while (!q.empty())
	{
		const int nodeId = q.front();
		q.pop();
		const SGNode* node = nodeById[nodeId];
		if (!node)
			continue;
		switch (node->op)
		{
		case SGNodeOp::InputUV:
			out[nodeId][0] = SGPortType::PortFloat;
			out[nodeId][1] = SGPortType::PortFloat;
			out[nodeId][2] = SGPortType::PortVec2;
			break;
		case SGNodeOp::InputTime:
		case SGNodeOp::ConstFloat:
		case SGNodeOp::ParamFloat:
		case SGNodeOp::SplitVec3X:
		case SGNodeOp::SplitVec3Y:
		case SGNodeOp::SplitVec3Z:
		case SGNodeOp::NoisePerlin3D:
		case SGNodeOp::Remap:
			out[nodeId][0] = SGPortType::PortFloat;
			break;
		case SGNodeOp::InputWorldPos:
		case SGNodeOp::InputNormal:
		case SGNodeOp::ConstVec3:
		case SGNodeOp::ComposeVec3:
			out[nodeId][0] = SGPortType::PortVec3;
			break;
		case SGNodeOp::ConstVec2:
			out[nodeId][0] = SGPortType::PortVec2;
			break;
		case SGNodeOp::Add:
		case SGNodeOp::Sub:
		case SGNodeOp::Mul:
		case SGNodeOp::Div:
		case SGNodeOp::Sin:
		case SGNodeOp::Cos:
		case SGNodeOp::Frac:
		case SGNodeOp::Lerp:
		case SGNodeOp::Saturate:
		{
			SGPortType t = SGPortType::PortFloat;
			for (const SGEdge* e : inEdges[nodeId])
			{
				auto fromNodeIt = out.find(e->fromNode);
				if (fromNodeIt == out.end())
					continue;
				auto fromPortIt = fromNodeIt->second.find(e->fromPort);
				if (fromPortIt == fromNodeIt->second.end())
					continue;
				if (IsNumeric(fromPortIt->second))
					t = fromPortIt->second;
			}
			out[nodeId][0] = t;
			break;
		}
		default:
			break;
		}
		for (int next : adjacency[nodeId])
		{
			int& d = indegree[next];
			d -= 1;
			if (d == 0)
				q.push(next);
		}
	}
	return out;
}

int NextNodeId(const ShaderGraphAsset& graph)
{
	int id = 0;
	for (const SGNode& n : graph.nodes)
		id = std::max(id, n.id + 1);
	return id;
}
} // namespace

ImNodesContext* gShaderGraphImNodesContext = nullptr;

void SetShaderGraphImNodesContext(ImNodesContext* context)
{
	gShaderGraphImNodesContext = context;
}

void DrawShaderGraphEditorBridge(Scene& scene, const ShaderGraphEditorUiDeps& deps)
{
	if (gShaderGraphImNodesContext)
		ImNodes::SetCurrentContext(gShaderGraphImNodesContext);
	if (!scene.uiShaderGraphWindowOpen)
		return;
	bool windowOpen = scene.uiShaderGraphWindowOpen;
	if (ImGui::Begin("Shader Graph Editor", &windowOpen))
	{
		static char searchBuf[128] = "";
		static std::vector<SGNodeOp> recentOps;
		static bool loaded = false;
		static std::string cachedPath;
		static ShaderGraphAsset editableGraph{};
		static SGNode copiedNode{};
		static bool hasCopiedNode = false;
		static bool graphDirty = false;
		static std::unordered_set<int> initializedNodes;

		std::string resolvedGraphPath = scene.uiShaderGraphCurrentPath;
		if (deps.resolvePathForIO)
			resolvedGraphPath = deps.resolvePathForIO(scene.uiShaderGraphCurrentPath);
		if (!loaded || cachedPath != resolvedGraphPath)
		{
			loaded = false;
			cachedPath = resolvedGraphPath;
			graphDirty = false;
			initializedNodes.clear();
			std::string json;
			std::string loadError;
			if (!resolvedGraphPath.empty() && ReadTextFileUtf8Local(resolvedGraphPath, json, &loadError) &&
			    DeserializeShaderGraphFromJson(json, editableGraph, &loadError))
			{
				loaded = true;
			}
			else
			{
				scene.uiShaderGraphLastError = "Unable to load graph preview: " + loadError;
			}
		}

		auto persistGraph = [&]() {
			if (!loaded || !graphDirty || resolvedGraphPath.empty())
				return;
			std::string json;
			std::string ioError;
			if (!SerializeShaderGraphToJson(editableGraph, json, &ioError) || !WriteTextFileUtf8Local(resolvedGraphPath, json, &ioError))
			{
				scene.uiShaderGraphLastError = ioError;
				return;
			}
			graphDirty = false;
			scene.uiShaderGraphLastError = "Shader graph saved.";
		};

		auto buildOutputTypes = [&]() { return BuildOutputTypes(editableGraph); };

		auto tryGetNodeOutputType = [&](int nodeId, int port, SGPortType& outType) -> bool {
			const auto allOut = buildOutputTypes();
			auto nodeIt = allOut.find(nodeId);
			if (nodeIt == allOut.end())
				return false;
			auto portIt = nodeIt->second.find(port);
			if (portIt == nodeIt->second.end())
				return false;
			outType = portIt->second;
			return true;
		};

		ImGui::InputTextWithHint("Search Node", "type node keyword...", searchBuf, sizeof(searchBuf));
		const std::vector<SGNodeOp> filtered = FilterNodeOps(searchBuf);
		ImGui::BeginChild("sg_node_lib", ImVec2(0.0f, 120.0f), true);
		for (SGNodeOp op : filtered)
		{
			const char* opName = SGNodeOpToString(op);
			if (ImGui::Selectable(opName, false))
			{
				recentOps.erase(std::remove(recentOps.begin(), recentOps.end(), op), recentOps.end());
				recentOps.insert(recentOps.begin(), op);
				if (recentOps.size() > 8)
					recentOps.resize(8);
				if (loaded)
				{
					SGNode n{};
					n.id = NextNodeId(editableGraph);
					n.op = op;
					if (op == SGNodeOp::ConstVec3)
						n.values = { 1.0f, 1.0f, 1.0f };
					else if (op == SGNodeOp::ConstFloat)
						n.values = { 0.0f };
					editableGraph.nodes.push_back(n);
					initializedNodes.erase(n.id);
					scene.uiShaderGraphFocusedNodeId = n.id;
					graphDirty = true;
				}
				scene.uiShaderGraphCompileReport.Add(
				    SGCompileMessageSeverity::Info, scene.uiShaderGraphFocusedNodeId, SGCompileMessagePhase::Validate,
				    std::string("Added node: ") + opName);
			}
		}
		ImGui::EndChild();
		if (!recentOps.empty())
		{
			ImGui::TextUnformatted("Recent Nodes:");
			for (SGNodeOp op : recentOps)
			{
				ImGui::SameLine();
				ImGui::TextUnformatted(SGNodeOpToString(op));
			}
		}
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			if (loaded && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && scene.uiShaderGraphFocusedNodeId >= 0)
			{
				for (const SGNode& n : editableGraph.nodes)
				{
					if (n.id == scene.uiShaderGraphFocusedNodeId)
					{
						copiedNode = n;
						hasCopiedNode = true;
						ImGui::SetClipboardText(SGNodeOpToString(n.op));
						break;
					}
				}
			}
			if (loaded && hasCopiedNode && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
			{
				SGNode pasted = copiedNode;
				pasted.id = NextNodeId(editableGraph);
				editableGraph.nodes.push_back(pasted);
				scene.uiShaderGraphFocusedNodeId = pasted.id;
				graphDirty = true;
				scene.uiShaderGraphCompileReport.Add(
				    SGCompileMessageSeverity::Info, pasted.id, SGCompileMessagePhase::Validate, "Pasted node.");
			}
			bool deletedSelectedLink = false;
			if (loaded && ImGui::IsKeyPressed(ImGuiKey_Delete))
			{
				std::unordered_map<int, size_t> localLinkToEdgeIndex;
				for (size_t i = 0; i < editableGraph.edges.size(); ++i)
				{
					const SGEdge& e = editableGraph.edges[i];
					const int linkId = MakeLocalImnodesId(
					    "sg:link:" + std::to_string(i) + ":" + std::to_string(e.fromNode) + ":" + std::to_string(e.toNode));
					localLinkToEdgeIndex[linkId] = i;
				}
				std::vector<int> selectedLinks(static_cast<size_t>(ImNodes::NumSelectedLinks()));
				if (!selectedLinks.empty())
				{
					ImNodes::GetSelectedLinks(selectedLinks.data());
					deletedSelectedLink = DeleteSelectedShaderGraphLinks(editableGraph, selectedLinks, localLinkToEdgeIndex);
				}
				if (deletedSelectedLink)
				{
					graphDirty = true;
					scene.uiShaderGraphCompileReport.Add(
					    SGCompileMessageSeverity::Warning, -1, SGCompileMessagePhase::Validate, "Deleted selected link(s).");
				}
			}
			if (!deletedSelectedLink && loaded && ImGui::IsKeyPressed(ImGuiKey_Delete) && scene.uiShaderGraphFocusedNodeId >= 0)
			{
				const int removeId = scene.uiShaderGraphFocusedNodeId;
				editableGraph.nodes.erase(std::remove_if(editableGraph.nodes.begin(), editableGraph.nodes.end(),
				                           [removeId](const SGNode& n) { return n.id == removeId; }),
				    editableGraph.nodes.end());
				editableGraph.edges.erase(std::remove_if(editableGraph.edges.begin(), editableGraph.edges.end(),
				                           [removeId](const SGEdge& e) { return e.fromNode == removeId || e.toNode == removeId; }),
				    editableGraph.edges.end());
				scene.uiShaderGraphFocusedNodeId = -1;
				graphDirty = true;
				scene.uiShaderGraphCompileReport.Add(
				    SGCompileMessageSeverity::Warning, removeId, SGCompileMessagePhase::Validate, "Deleted node.");
			}
		}
		if (ImGui::Button("Save Graph"))
			persistGraph();

		ImGui::Separator();
		ImGui::Text("Graph Asset: %s", scene.uiShaderGraphCurrentPath.c_str());
		if (loaded)
		{
			std::unordered_map<int, SgAttrRef> attrRefs;
			std::unordered_map<int, int> imNodeToNode;
			std::unordered_map<int, size_t> linkToEdgeIndex;
			ImGui::Text("Resolved Path: %s", resolvedGraphPath.c_str());
			ImGui::Text("Nodes: %d  Edges: %d", int(editableGraph.nodes.size()), int(editableGraph.edges.size()));
			DrawShaderGraphPreview(
			    editableGraph, scene.uiShaderGraphFocusedNodeId, attrRefs, imNodeToNode, linkToEdgeIndex, initializedNodes);

			{
				std::vector<int> selectedImNodes(static_cast<size_t>(ImNodes::NumSelectedNodes()));
				if (!selectedImNodes.empty())
				{
					ImNodes::GetSelectedNodes(selectedImNodes.data());
					const int selectedImNode = selectedImNodes.back();
					auto selectedIt = imNodeToNode.find(selectedImNode);
					if (selectedIt != imNodeToNode.end())
						scene.uiShaderGraphFocusedNodeId = selectedIt->second;
				}
			}
			int createdA = -1;
			int createdB = -1;
			if (ImNodes::IsLinkCreated(&createdA, &createdB))
			{
				auto aIt = attrRefs.find(createdA);
				auto bIt = attrRefs.find(createdB);
				if (aIt != attrRefs.end() && bIt != attrRefs.end())
				{
					SgAttrRef outRef{};
					SgAttrRef inRef{};
					if (aIt->second.isOutput && !bIt->second.isOutput)
					{
						outRef = aIt->second;
						inRef = bIt->second;
					}
					else if (!aIt->second.isOutput && bIt->second.isOutput)
					{
						outRef = bIt->second;
						inRef = aIt->second;
					}
					else
					{
						scene.uiShaderGraphCompileReport.Add(
						    SGCompileMessageSeverity::Error, -1, SGCompileMessagePhase::Validate, "Invalid link direction.");
						goto after_link_create;
					}
					if (outRef.nodeId == inRef.nodeId)
					{
						scene.uiShaderGraphCompileReport.Add(
						    SGCompileMessageSeverity::Error, inRef.nodeId, SGCompileMessagePhase::Validate, "Self link is not allowed.");
						goto after_link_create;
					}
					for (const SGEdge& e : editableGraph.edges)
					{
						if (e.toNode == inRef.nodeId && e.toPort == inRef.port)
						{
							scene.uiShaderGraphCompileReport.Add(
							    SGCompileMessageSeverity::Error, inRef.nodeId, SGCompileMessagePhase::Validate,
							    "Input port already connected.");
							goto after_link_create;
						}
					}
					{
						SGPortType fromType = SGPortType::PortFloat;
						SGPortType toType = SGPortType::PortFloat;
						bool hasFrom = tryGetNodeOutputType(outRef.nodeId, outRef.port, fromType);
						bool hasTo = false;
						for (const SGNode& n : editableGraph.nodes)
						{
							if (n.id == inRef.nodeId)
							{
								hasTo = TryGetExpectedInputType(n, inRef.port, toType);
								break;
							}
						}
						if (hasFrom && hasTo && !IsCompatible(fromType, toType))
						{
							scene.uiShaderGraphCompileReport.Add(
							    SGCompileMessageSeverity::Error, inRef.nodeId, SGCompileMessagePhase::Validate,
							    "Type mismatch link rejected.");
							goto after_link_create;
						}
					}
					editableGraph.edges.push_back(SGEdge{ outRef.nodeId, outRef.port, inRef.nodeId, inRef.port });
					graphDirty = true;
					scene.uiShaderGraphCompileReport.Add(
					    SGCompileMessageSeverity::Info, inRef.nodeId, SGCompileMessagePhase::Validate, "Link created.");
				}
			}
		after_link_create:
			{
				int destroyedLink = -1;
				if (ImNodes::IsLinkDestroyed(&destroyedLink))
				{
					auto linkIt = linkToEdgeIndex.find(destroyedLink);
					if (linkIt != linkToEdgeIndex.end() && linkIt->second < editableGraph.edges.size())
					{
						editableGraph.edges.erase(editableGraph.edges.begin() + static_cast<std::ptrdiff_t>(linkIt->second));
						graphDirty = true;
						scene.uiShaderGraphCompileReport.Add(
						    SGCompileMessageSeverity::Warning, -1, SGCompileMessagePhase::Validate, "Link deleted.");
					}
				}
			}

			const auto outputTypes = BuildOutputTypes(editableGraph);
			std::vector<std::string> incompatibleLinks;
			std::unordered_map<int, const SGNode*> nodeById;
			for (const SGNode& node : editableGraph.nodes)
				nodeById[node.id] = &node;
			for (const SGEdge& edge : editableGraph.edges)
			{
				auto fromNodeIt = outputTypes.find(edge.fromNode);
				if (fromNodeIt == outputTypes.end())
					continue;
				auto fromPortIt = fromNodeIt->second.find(edge.fromPort);
				if (fromPortIt == fromNodeIt->second.end())
					continue;
				auto toNodeIt = nodeById.find(edge.toNode);
				if (toNodeIt == nodeById.end())
					continue;
				SGPortType expected = SGPortType::PortFloat;
				if (!TryGetExpectedInputType(*toNodeIt->second, edge.toPort, expected))
					continue;
				if (!IsCompatible(fromPortIt->second, expected))
				{
					incompatibleLinks.push_back(
					    "node#" + std::to_string(edge.fromNode) + ":" + std::to_string(edge.fromPort) + " -> node#" +
					    std::to_string(edge.toNode) + ":" + std::to_string(edge.toPort));
				}
			}
			if (!incompatibleLinks.empty())
			{
				ImGui::Separator();
				ImGui::TextUnformatted("Connection Type Warnings:");
				for (const std::string& warn : incompatibleLinks)
					ImGui::TextWrapped("%s", warn.c_str());
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Node Properties");
			SGNode* focusedNode = nullptr;
			for (SGNode& n : editableGraph.nodes)
			{
				if (n.id == scene.uiShaderGraphFocusedNodeId)
				{
					focusedNode = &n;
					break;
				}
			}
			if (!focusedNode)
			{
				ImGui::TextDisabled("Select a node to edit.");
			}
			else
			{
				ImGui::Text("Node #%d (%s)", focusedNode->id, SGNodeOpToString(focusedNode->op));
				if (focusedNode->op == SGNodeOp::ConstVec3)
				{
					if (focusedNode->values.size() < 3)
						focusedNode->values.resize(3, 1.0f);
					float color[3] = { focusedNode->values[0], focusedNode->values[1], focusedNode->values[2] };
					if (ImGui::ColorEdit3("Const Color", color))
					{
						focusedNode->values[0] = color[0];
						focusedNode->values[1] = color[1];
						focusedNode->values[2] = color[2];
						graphDirty = true;
					}
				}
				else if (focusedNode->op == SGNodeOp::ConstFloat)
				{
					if (focusedNode->values.empty())
						focusedNode->values.push_back(0.0f);
					float v = focusedNode->values[0];
					if (ImGui::DragFloat("Value", &v, 0.01f))
					{
						focusedNode->values[0] = v;
						graphDirty = true;
					}
				}
			}
		}
		else
		{
			ImGui::TextWrapped("Unable to load graph preview.");
		}

		if (graphDirty)
			persistGraph();

		ImGui::Separator();
		ImGui::TextUnformatted("Compile Log");
		std::string logText;
		for (const SGCompileMessage& message : scene.uiShaderGraphCompileReport.Messages())
		{
			std::string label = std::string("[") + SeverityName(message.severity) + "] node#" + std::to_string(message.nodeId) +
			                    " " + message.text;
			logText += label;
			logText += "\n";
			if (ImGui::Selectable(label.c_str(), scene.uiShaderGraphFocusedNodeId == message.nodeId))
				scene.uiShaderGraphFocusedNodeId = message.nodeId;
		}
		if (!scene.uiShaderGraphLastError.empty())
		{
			if (!logText.empty())
				logText += "\n";
			logText += "[LastError] ";
			logText += scene.uiShaderGraphLastError;
			ImGui::TextWrapped("%s", scene.uiShaderGraphLastError.c_str());
		}
		if (logText.empty())
			logText = "(empty)";
		if (ImGui::Button("Copy Log"))
			ImGui::SetClipboardText(logText.c_str());
		ImGui::BeginChild("sg_compile_log_text", ImVec2(0.0f, 140.0f), true);
		ImGui::InputTextMultiline("##sg_compile_log_text_readonly", const_cast<char*>(logText.c_str()),
		    static_cast<size_t>(logText.size() + 1), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndChild();
	}
	ImGui::End();
	scene.uiShaderGraphWindowOpen = windowOpen;
}

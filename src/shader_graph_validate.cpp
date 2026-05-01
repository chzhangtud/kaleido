#include "shader_graph_validate.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace
{
using NodeIndexMap = std::unordered_map<int, size_t>;
using NodeInputMap = std::unordered_map<int, std::unordered_map<int, const SGEdge*>>;
using NodeOutputTypeMap = std::unordered_map<int, std::unordered_map<int, SGPortType>>;

std::string NodePortLabel(int nodeId, int port)
{
	return "node " + std::to_string(nodeId) + " port " + std::to_string(port);
}

bool ResolveBroadcastType(SGPortType a, SGPortType b, SGPortType& out)
{
	auto rank = [](SGPortType t) -> int
	{
		switch (t)
		{
		case SGPortType::PortInt: return 1;
		case SGPortType::PortFloat: return 2;
		case SGPortType::PortVec2: return 3;
		case SGPortType::PortVec3: return 4;
		case SGPortType::PortVec4: return 5;
		default: return 0;
		}
	};
	if (a == b)
	{
		out = a;
		return true;
	}
	if (!SGPortTypeIsNumeric(a) || !SGPortTypeIsNumeric(b))
		return false;
	if (!SGPortTypeCanImplicitConvert(a, b) && !SGPortTypeCanImplicitConvert(b, a))
		return false;
	out = (rank(a) >= rank(b)) ? a : b;
	if (a == SGPortType::PortInt && b == SGPortType::PortFloat)
		out = SGPortType::PortFloat;
	if (a == SGPortType::PortFloat && b == SGPortType::PortInt)
		out = SGPortType::PortFloat;
	return true;
}

bool GetNodeInputType(int nodeId,
    int inPort,
    const NodeInputMap& inputEdges,
    const NodeIndexMap& nodeById,
    const ShaderGraphAsset& graph,
    const NodeOutputTypeMap& outputTypes,
    SGPortType& outType)
{
	auto inNodeIt = inputEdges.find(nodeId);
	if (inNodeIt == inputEdges.end())
		return false;
	auto inPortIt = inNodeIt->second.find(inPort);
	if (inPortIt == inNodeIt->second.end())
		return false;
	const SGEdge* edge = inPortIt->second;
	auto fromNodeIt = outputTypes.find(edge->fromNode);
	if (fromNodeIt == outputTypes.end())
		return false;
	auto fromPortIt = fromNodeIt->second.find(edge->fromPort);
	if (fromPortIt == fromNodeIt->second.end())
		return false;
	outType = fromPortIt->second;
	(void)nodeById;
	(void)graph;
	return true;
}
} // namespace

SGValidateResult ValidateShaderGraph(const ShaderGraphAsset& g)
{
	SGValidateResult result{};
	if (g.format != "kaleido_shader_graph")
	{
		result.error = "Unsupported graph format: " + g.format;
		return result;
	}
	if (g.version != 1)
	{
		result.error = "Unsupported graph version: " + std::to_string(g.version);
		return result;
	}
	if (g.nodes.empty())
	{
		result.error = "Graph has no nodes.";
		return result;
	}

	NodeIndexMap nodeById;
	nodeById.reserve(g.nodes.size());
	for (size_t i = 0; i < g.nodes.size(); ++i)
	{
		const SGNode& node = g.nodes[i];
		if (node.id < 0)
		{
			result.error = "Node id must be non-negative.";
			return result;
		}
		if (!nodeById.emplace(node.id, i).second)
		{
			result.error = "Duplicate node id: " + std::to_string(node.id);
			return result;
		}
	}

	NodeInputMap inputEdges;
	std::unordered_map<int, std::vector<int>> adjacency;
	std::unordered_map<int, std::vector<int>> reverseAdjacency;

	for (const SGEdge& edge : g.edges)
	{
		if (edge.fromNode == edge.toNode)
		{
			result.error = "Self loop is not allowed on node " + std::to_string(edge.fromNode);
			return result;
		}
		if (nodeById.find(edge.fromNode) == nodeById.end() || nodeById.find(edge.toNode) == nodeById.end())
		{
			result.error = "Edge references missing node.";
			return result;
		}
		auto& dstPorts = inputEdges[edge.toNode];
		if (dstPorts.find(edge.toPort) != dstPorts.end())
		{
			result.error = "Input port already connected: " + NodePortLabel(edge.toNode, edge.toPort);
			return result;
		}
		dstPorts[edge.toPort] = &edge;
		adjacency[edge.fromNode].push_back(edge.toNode);
		reverseAdjacency[edge.toNode].push_back(edge.fromNode);
	}

	int outputNodeId = -1;
	for (const SGNode& node : g.nodes)
	{
		if (node.op == SGNodeOp::OutputSurface)
		{
			outputNodeId = node.id;
			break;
		}
	}
	if (outputNodeId < 0)
	{
		result.error = "Graph missing OutputSurface node.";
		result.topoOrder.clear();
		return result;
	}

	std::unordered_set<int> activeNodeSet;
	std::queue<int> activeSearchQueue;
	activeNodeSet.insert(outputNodeId);
	activeSearchQueue.push(outputNodeId);
	while (!activeSearchQueue.empty())
	{
		const int nodeId = activeSearchQueue.front();
		activeSearchQueue.pop();
		auto reverseIt = reverseAdjacency.find(nodeId);
		if (reverseIt == reverseAdjacency.end())
			continue;
		for (int prev : reverseIt->second)
		{
			if (activeNodeSet.insert(prev).second)
				activeSearchQueue.push(prev);
		}
	}

	std::unordered_map<int, int> indegree;
	for (int nodeId : activeNodeSet)
		indegree[nodeId] = 0;
	for (const SGEdge& edge : g.edges)
	{
		if (activeNodeSet.count(edge.fromNode) == 0 || activeNodeSet.count(edge.toNode) == 0)
			continue;
		indegree[edge.toNode] += 1;
	}

	std::queue<int> q;
	for (int nodeId : activeNodeSet)
	{
		if (indegree[nodeId] == 0)
			q.push(nodeId);
	}
	while (!q.empty())
	{
		const int nodeId = q.front();
		q.pop();
		result.topoOrder.push_back(nodeId);
		auto adjIt = adjacency.find(nodeId);
		if (adjIt == adjacency.end())
			continue;
		for (int next : adjIt->second)
		{
			if (activeNodeSet.count(next) == 0)
				continue;
			int deg = --indegree[next];
			if (deg == 0)
				q.push(next);
		}
	}
	if (result.topoOrder.size() != activeNodeSet.size())
	{
		result.error = "Active output-reachable subgraph contains cycle.";
		result.topoOrder.clear();
		return result;
	}

	NodeOutputTypeMap outputTypes;
	for (int nodeId : result.topoOrder)
	{
		const SGNode& node = g.nodes[nodeById[nodeId]];

		auto requireInputType = [&](int port, SGPortType& t) -> bool
		{
			if (!GetNodeInputType(nodeId, port, inputEdges, nodeById, g, outputTypes, t))
			{
				result.error = "Missing or invalid input on " + NodePortLabel(nodeId, port);
				return false;
			}
			return true;
		};

		switch (node.op)
		{
		case SGNodeOp::InputUV:
			outputTypes[nodeId][0] = SGPortType::PortFloat;
			outputTypes[nodeId][1] = SGPortType::PortFloat;
			outputTypes[nodeId][2] = SGPortType::PortVec2;
			break;
		case SGNodeOp::InputTime:
		case SGNodeOp::ConstFloat:
		case SGNodeOp::ParamFloat:
			outputTypes[nodeId][0] = SGPortType::PortFloat;
			break;
		case SGNodeOp::InputWorldPos:
		case SGNodeOp::InputNormal:
		case SGNodeOp::ConstVec3:
			outputTypes[nodeId][0] = SGPortType::PortVec3;
			break;
		case SGNodeOp::ConstVec2:
			outputTypes[nodeId][0] = SGPortType::PortVec2;
			break;
		case SGNodeOp::Add:
		case SGNodeOp::Sub:
		case SGNodeOp::Mul:
		case SGNodeOp::Div:
		{
			SGPortType a = SGPortType::PortFloat;
			SGPortType b = SGPortType::PortFloat;
			if (!requireInputType(0, a) || !requireInputType(1, b))
			{
				result.topoOrder.clear();
				return result;
			}
			if (!SGPortTypeIsNumeric(a) || !SGPortTypeIsNumeric(b))
			{
				result.error = "Arithmetic node requires numeric input types.";
				result.topoOrder.clear();
				return result;
			}
			SGPortType out = SGPortType::PortFloat;
			if (!ResolveBroadcastType(a, b, out))
			{
				result.error = "Arithmetic node input types are incompatible.";
				result.topoOrder.clear();
				return result;
			}
			outputTypes[nodeId][0] = out;
			break;
		}
		case SGNodeOp::Sin:
		case SGNodeOp::Cos:
		case SGNodeOp::Frac:
		case SGNodeOp::Saturate:
		{
			SGPortType t = SGPortType::PortFloat;
			if (!requireInputType(0, t))
			{
				result.topoOrder.clear();
				return result;
			}
			if (!SGPortTypeIsNumeric(t))
			{
				result.error = "Unary math node requires numeric input.";
				result.topoOrder.clear();
				return result;
			}
			outputTypes[nodeId][0] = t;
			break;
		}
		case SGNodeOp::Lerp:
		{
			SGPortType a = SGPortType::PortFloat;
			SGPortType b = SGPortType::PortFloat;
			SGPortType t = SGPortType::PortFloat;
			if (!requireInputType(0, a) || !requireInputType(1, b) || !requireInputType(2, t))
			{
				result.topoOrder.clear();
				return result;
			}
			if (!SGPortTypeCanImplicitConvert(t, SGPortType::PortFloat))
			{
				result.error = "Lerp factor must be float.";
				result.topoOrder.clear();
				return result;
			}
			SGPortType out = SGPortType::PortFloat;
			if (!ResolveBroadcastType(a, b, out))
			{
				result.error = "Lerp input types are incompatible.";
				result.topoOrder.clear();
				return result;
			}
			outputTypes[nodeId][0] = out;
			break;
		}
		case SGNodeOp::ComposeVec3:
		{
			SGPortType x = SGPortType::PortFloat;
			SGPortType y = SGPortType::PortFloat;
			SGPortType z = SGPortType::PortFloat;
			if (!requireInputType(0, x) || !requireInputType(1, y) || !requireInputType(2, z))
			{
				result.topoOrder.clear();
				return result;
			}
			if (!SGPortTypeCanImplicitConvert(x, SGPortType::PortFloat) ||
			    !SGPortTypeCanImplicitConvert(y, SGPortType::PortFloat) ||
			    !SGPortTypeCanImplicitConvert(z, SGPortType::PortFloat))
			{
				result.error = "ComposeVec3 requires float inputs on ports 0..2.";
				result.topoOrder.clear();
				return result;
			}
			outputTypes[nodeId][0] = SGPortType::PortVec3;
			break;
		}
		case SGNodeOp::SplitVec3X:
		case SGNodeOp::SplitVec3Y:
		case SGNodeOp::SplitVec3Z:
		case SGNodeOp::NoisePerlin3D:
		{
			SGPortType in = SGPortType::PortFloat;
			if (!requireInputType(0, in))
			{
				result.topoOrder.clear();
				return result;
			}
			if (in != SGPortType::PortVec3)
			{
				result.error = (node.op == SGNodeOp::NoisePerlin3D)
				                   ? "NoisePerlin3D requires vec3 input."
				                   : "SplitVec3* requires vec3 input.";
				result.topoOrder.clear();
				return result;
			}
			outputTypes[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::Remap:
		{
			SGPortType v = SGPortType::PortFloat;
			if (!requireInputType(0, v))
			{
				result.topoOrder.clear();
				return result;
			}
			if (!SGPortTypeCanImplicitConvert(v, SGPortType::PortFloat))
			{
				result.error = "Remap value input must be float.";
				result.topoOrder.clear();
				return result;
			}
			for (int p = 1; p <= 4; ++p)
			{
				SGPortType t = SGPortType::PortFloat;
				if (GetNodeInputType(nodeId, p, inputEdges, nodeById, g, outputTypes, t) &&
				    !SGPortTypeCanImplicitConvert(t, SGPortType::PortFloat))
				{
					result.error = "Remap optional ports must be float.";
					result.topoOrder.clear();
					return result;
				}
			}
			outputTypes[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::OutputSurface:
		{
			SGPortType baseColor = SGPortType::PortVec3;
			if (GetNodeInputType(nodeId, 0, inputEdges, nodeById, g, outputTypes, baseColor) &&
			    baseColor != SGPortType::PortVec3 && !SGPortTypeCanImplicitConvert(baseColor, SGPortType::PortVec3))
			{
				result.error = "OutputSurface.baseColor requires vec3 input.";
				result.topoOrder.clear();
				return result;
			}
			break;
		}
		default:
			result.error = "Unsupported node op in validator.";
			result.topoOrder.clear();
			return result;
		}
	}

	result.ok = true;
	return result;
}

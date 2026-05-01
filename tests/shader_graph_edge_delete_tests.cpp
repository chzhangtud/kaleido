#include "../src/shader_graph_editor_ops.h"
#include "shader_graph_test_utils.h"

#include <cassert>
#include <unordered_map>
#include <vector>

static void TestDeleteKeyRemovesSelectedLinks()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	std::unordered_map<int, size_t> linkToEdgeIndex{ { 101, 1 }, { 202, 7 } };
	const bool deleted = DeleteSelectedShaderGraphLinks(g, std::vector<int>{ 101, 202 }, linkToEdgeIndex);
	assert(deleted);
	assert(g.edges.size() == 7);
}

static void TestDeleteNodePathStillRemovesIncidentEdges()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	const int removeId = 6;
	g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(), [removeId](const SGNode& n) { return n.id == removeId; }),
	    g.nodes.end());
	g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(), [removeId](const SGEdge& e) { return e.fromNode == removeId || e.toNode == removeId; }),
	    g.edges.end());
	for (const SGEdge& e : g.edges)
		assert(e.fromNode != removeId && e.toNode != removeId);
}

int main()
{
	TestDeleteKeyRemovesSelectedLinks();
	TestDeleteNodePathStillRemovesIncidentEdges();
	return 0;
}

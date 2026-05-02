#include "../src/shader_graph_io.h"
#include "shader_graph_test_utils.h"

#include <cassert>
#include <string>

static void TestRoundTrip()
{
	ShaderGraphAsset in = BuildTimeNoiseExampleGraph();
	in.hasEditorMeta = true;
	in.editorViewX = 120.0f;
	in.editorViewY = -48.0f;
	in.editorZoom = 1.25f;
	std::string json;
	std::string err;
	assert(SerializeShaderGraphToJson(in, json, &err));

	ShaderGraphAsset out{};
	assert(DeserializeShaderGraphFromJson(json, out, &err));
	assert(out.version == 3);
	assert(out.nodeInstances.size() == in.nodes.size());
	assert(out.edges.size() == in.edges.size());
	assert(out.nodeInstances[1].numericOverrides.size() == 1);
	assert(out.nodeInstances[1].numericOverrides[0] == 4.0f);
	assert(out.nodeInstances[4].numericOverrides.size() == 4);
	assert(out.hasEditorMeta);
	assert(out.editorViewX == 120.0f);
	assert(out.editorViewY == -48.0f);
	assert(out.editorZoom == 1.25f);
}

static void TestDeserializeLegacyAndReSerializeAsV3()
{
	ShaderGraphAsset in = BuildTimeNoiseExampleGraph();
	std::string legacyJson;
	std::string err;
	assert(SerializeShaderGraphToJson(in, legacyJson, &err));

	ShaderGraphAsset loaded{};
	assert(DeserializeShaderGraphFromJson(legacyJson, loaded, &err));
	assert(!loaded.nodeInstances.empty());

	std::string v3Json;
	assert(SerializeShaderGraphToJson(loaded, v3Json, &err));
	assert(v3Json.find("\"version\": 3") != std::string::npos);
}

static void TestDeserializeV3HydratesLegacyNodesForEditor()
{
	const std::string json = R"({
  "format": "kaleido_shader_graph",
  "version": 3,
  "domain": "spatial_fragment",
  "entry": "material_surface",
  "nodeInstances": [
    {"id": 1, "descriptorId": "builtin/legacy/InputUV", "descriptorVersion": 1},
    {"id": 2, "descriptorId": "builtin/legacy/OutputSurface", "descriptorVersion": 1}
  ],
  "edges": [
    {"fromNode": 1, "fromPort": 2, "toNode": 2, "toPort": 0}
  ]
})";
	ShaderGraphAsset g{};
	std::string err;
	assert(DeserializeShaderGraphFromJson(json, g, &err));
	assert(g.nodes.size() == g.nodeInstances.size());
	assert(g.nodes[0].op == SGNodeOp::InputUV);
	assert(g.nodes[1].op == SGNodeOp::OutputSurface);
	assert(g.edges.size() == 1u);
}

int main()
{
	TestRoundTrip();
	TestDeserializeLegacyAndReSerializeAsV3();
	TestDeserializeV3HydratesLegacyNodesForEditor();
	return 0;
}

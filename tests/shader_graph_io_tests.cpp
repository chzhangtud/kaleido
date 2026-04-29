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
	assert(out.nodes.size() == in.nodes.size());
	assert(out.edges.size() == in.edges.size());
	assert(out.nodes[1].values.size() == 1);
	assert(out.nodes[1].values[0] == 4.0f);
	assert(out.nodes[4].values.size() == 4);
	assert(out.hasEditorMeta);
	assert(out.editorViewX == 120.0f);
	assert(out.editorViewY == -48.0f);
	assert(out.editorZoom == 1.25f);
}

int main()
{
	TestRoundTrip();
	return 0;
}

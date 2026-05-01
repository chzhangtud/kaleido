#include "../src/shader_graph_codegen_glsl.h"
#include "../src/shader_graph_validate.h"
#include "shader_graph_test_utils.h"

#include <cassert>

static void TestDisconnectedInvalidBranchDoesNotFailCompile()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	g.nodes.push_back(MakeNode(100, SGNodeOp::Add));
	SGValidateResult vr = ValidateShaderGraph(g);
	assert(vr.ok);
	SGCodegenResult cg = GenerateShaderGraphGlsl(g);
	assert(cg.ok);
}

static void TestInvalidNodeOnOutputReachableChainStillFailsCompile()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	g.nodes.push_back(MakeNode(100, SGNodeOp::Add));
	g.edges.push_back({ 100, 0, 7, 0 });
	SGValidateResult vr = ValidateShaderGraph(g);
	assert(!vr.ok);
}

int main()
{
	TestDisconnectedInvalidBranchDoesNotFailCompile();
	TestInvalidNodeOnOutputReachableChainStillFailsCompile();
	return 0;
}

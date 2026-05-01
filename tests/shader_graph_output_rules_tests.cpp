#include "../src/shader_graph_codegen_glsl.h"
#include "../src/shader_graph_validate.h"
#include "shader_graph_test_utils.h"

#include <cassert>
#include <string>

static void TestOutputBaseColorCanBeDisconnected()
{
	ShaderGraphAsset g{};
	g.nodes = { MakeNode(1, SGNodeOp::OutputSurface) };
	SGValidateResult vr = ValidateShaderGraph(g);
	assert(vr.ok);
	SGCodegenResult cg = GenerateShaderGraphGlsl(g);
	assert(cg.ok);
	assert(cg.fragmentFunction.find("return vec3(1.0);") != std::string::npos);
}

static void TestOtherRequiredInputsStayStrict()
{
	ShaderGraphAsset g{};
	g.nodes = {
		MakeNode(1, SGNodeOp::ComposeVec3),
		MakeNode(2, SGNodeOp::OutputSurface),
	};
	g.edges = {
		{ 1, 0, 2, 0 },
	};
	SGValidateResult vr = ValidateShaderGraph(g);
	assert(!vr.ok);
	assert(vr.error.find("Missing or invalid input") != std::string::npos);
}

int main()
{
	TestOutputBaseColorCanBeDisconnected();
	TestOtherRequiredInputsStayStrict();
	return 0;
}

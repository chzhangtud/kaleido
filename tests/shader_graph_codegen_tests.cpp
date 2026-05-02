#include "../src/shader_graph_codegen_glsl.h"
#include "shader_graph_test_utils.h"

#include <cassert>
#include <string>

static void TestEmitTimeNoiseGraph()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	SGCodegenResult r = GenerateShaderGraphGlsl(g);
	assert(r.ok);
	assert(r.fragmentFunction.find("params.p2") != std::string::npos);
	assert(r.fragmentFunction.find("sg_noise_perlin3d") != std::string::npos);
	assert(r.fragmentFunction.find("fract(params.p2 / 4.000000)") != std::string::npos);
}

static void TestGeneratedHookSignature()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	SGCodegenResult r = GenerateShaderGraphGlsl(g);
	assert(r.ok);
	assert(r.fragmentFunction.find("vec3 sg_eval_base_color") != std::string::npos);
}

static void TestVec3ToFloatImplicitCastInCodegen()
{
	ShaderGraphAsset g{};
	g.nodes = {
		MakeNodeWithValues(1, SGNodeOp::ConstVec3, { 0.2f, 0.4f, 0.8f }),
		MakeNodeWithValues(2, SGNodeOp::Remap, { -1.0f, 1.0f, 0.0f, 1.0f }),
		MakeNode(3, SGNodeOp::ComposeVec3),
		MakeNode(4, SGNodeOp::OutputSurface),
	};
	g.edges = {
		{ 1, 0, 2, 0 },
		{ 2, 0, 3, 0 },
		{ 2, 0, 3, 1 },
		{ 2, 0, 3, 2 },
		{ 3, 0, 4, 0 },
	};
	SGCodegenResult r = GenerateShaderGraphGlsl(g);
	assert(r.ok);
	assert(r.fragmentFunction.find(".x") != std::string::npos);
}

static void TestNoRuntimeDependenceOnLegacyOpInV3()
{
	ShaderGraphAsset g{};
	g.version = 3;
	g.nodeInstances.push_back({ 1, "builtin/input/uv", 1, {}, "" });
	g.nodeInstances.push_back({ 2, "builtin/output/surface", 1, {}, "" });
	g.edges.push_back({ 1, 2, 2, 0 });
	SGCodegenResult r = GenerateShaderGraphGlsl(g);
	assert(r.error.empty());
	assert(r.ok);
}

static void TestCodegenV3ConstFloatToSurface()
{
	ShaderGraphAsset g{};
	g.version = 3;
	g.nodeInstances.push_back({ 1, "builtin/const/float", 1, { 0.25f }, "" });
	g.nodeInstances.push_back({ 2, "builtin/output/surface", 1, {}, "" });
	g.edges.push_back({ 1, 0, 2, 0 });
	SGCodegenResult r = GenerateShaderGraphGlsl(g);
	assert(r.ok);
	assert(r.fragmentFunction.find("0.250000") != std::string::npos);
}

int main()
{
	TestEmitTimeNoiseGraph();
	TestGeneratedHookSignature();
	TestVec3ToFloatImplicitCastInCodegen();
	TestNoRuntimeDependenceOnLegacyOpInV3();
	TestCodegenV3ConstFloatToSurface();
	return 0;
}

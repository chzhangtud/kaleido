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

int main()
{
	TestEmitTimeNoiseGraph();
	TestGeneratedHookSignature();
	return 0;
}

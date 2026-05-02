#include "../src/shader_graph_types.h"
#include "../src/shader_graph_validate.h"
#include "../src/shader_graph_node_registry.h"
#include "shader_graph_test_utils.h"

#include <cassert>
#include <string>

static void TestAssetHeader()
{
	ShaderGraphAsset asset{};
	asset.format = "kaleido_shader_graph";
	asset.version = 1;
	assert(asset.version == 1);
}

static void TestRejectCycle()
{
	ShaderGraphAsset g = BuildTinyCycleGraphForTest();
	SGValidateResult r = ValidateShaderGraph(g);
	assert(!r.ok);
}

static void TestAcceptTimeNoiseGraph()
{
	ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
	SGValidateResult r = ValidateShaderGraph(g);
	assert(r.ok);
}

static void TestValidateV3NodeInstances()
{
	ShaderGraphAsset g{};
	g.version = 3;
	g.nodeInstances.push_back({ 1, "builtin/const/float", 1, { 0.5f }, "" });
	g.nodeInstances.push_back({ 2, "builtin/math/add", 1, {}, "" });
	g.nodeInstances.push_back({ 3, "builtin/output/surface", 1, {}, "" });
	g.edges.push_back({ 1, 0, 2, 0 });
	g.edges.push_back({ 2, 0, 3, 0 });
	SGValidateResult r = ValidateShaderGraph(g);
	assert(!r.ok);
}

static void TestValidateV3ConstFloatToSurface()
{
	ShaderGraphAsset g{};
	g.version = 3;
	g.nodeInstances.push_back({ 1, "builtin/const/float", 1, { 0.25f }, "" });
	g.nodeInstances.push_back({ 2, "builtin/output/surface", 1, {}, "" });
	g.edges.push_back({ 1, 0, 2, 0 });
	SGValidateResult r = ValidateShaderGraph(g);
	assert(r.ok);
}

static void TestValidateUsesDescriptorPorts()
{
	ShaderGraphNodeRegistry reg{};
	std::string err;
	assert(ShaderGraphTryLoadBuiltinNodeRegistry(reg, &err));
	const ShaderGraphNodeDescriptor* add = reg.Find("builtin/math/add");
	assert(add != nullptr);
	assert(add->inputs.size() == 2u);
	assert(!add->inputs[0].optional && !add->inputs[1].optional);

	ShaderGraphAsset g{};
	g.version = 3;
	g.nodeInstances.push_back({ 1, "builtin/const/float", 1, { 0.25f }, "" });
	g.nodeInstances.push_back({ 2, "builtin/math/add", 1, {}, "" });
	g.nodeInstances.push_back({ 3, "builtin/output/surface", 1, {}, "" });
	g.edges.push_back({ 1, 0, 2, 0 });
	g.edges.push_back({ 2, 0, 3, 0 });
	SGValidateResult r = ValidateShaderGraph(g);
	assert(!r.ok);
	assert(r.error.find("Required input") != std::string::npos);
	assert(r.error.find("b") != std::string::npos);
}

int main()
{
	TestAssetHeader();
	TestRejectCycle();
	TestAcceptTimeNoiseGraph();
	TestValidateV3NodeInstances();
	TestValidateUsesDescriptorPorts();
	TestValidateV3ConstFloatToSurface();
	return 0;
}

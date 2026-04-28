#include "../src/shader_graph_types.h"
#include "../src/shader_graph_validate.h"
#include "shader_graph_test_utils.h"

#include <cassert>

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

int main()
{
	TestAssetHeader();
	TestRejectCycle();
	TestAcceptTimeNoiseGraph();
	return 0;
}

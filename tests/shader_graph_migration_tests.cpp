#include "../src/shader_graph_migration.h"
#include "shader_graph_test_utils.h"

#include <cassert>
#include <string>

static void TestMigratesTimeNoiseGraph()
{
	ShaderGraphAsset legacy = BuildTimeNoiseExampleGraph();
	ShaderGraphAsset migrated{};
	std::string err;
	assert(MigrateLegacyShaderGraph(legacy, migrated, &err));
	assert(migrated.version == 3);
	assert(!migrated.nodeInstances.empty());
	assert(migrated.nodeInstances.size() == legacy.nodes.size());
	assert(migrated.nodeInstances[0].descriptorId == "builtin/input/uv");
	assert(migrated.nodeInstances[1].descriptorId == "builtin/input/time");
	assert(migrated.nodeInstances[6].descriptorId == "builtin/output/surface");
}

int main()
{
	TestMigratesTimeNoiseGraph();
	return 0;
}

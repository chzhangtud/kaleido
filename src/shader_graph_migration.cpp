#include "shader_graph_migration.h"
#include "shader_graph_node_registry.h"

namespace
{
bool Fail(std::string* outError, const std::string& message)
{
	if (outError)
		*outError = message;
	return false;
}
} // namespace

bool MigrateLegacyShaderGraph(const ShaderGraphAsset& legacy, ShaderGraphAsset& outMigrated, std::string* outError)
{
	if (legacy.format != "kaleido_shader_graph")
		return Fail(outError, "legacy graph.format mismatch");
	if (legacy.version != 1)
		return Fail(outError, "legacy graph.version must be 1");

	ShaderGraphAsset migrated = legacy;
	migrated.version = 3;
	migrated.nodeInstances.clear();
	migrated.nodeInstances.reserve(legacy.nodes.size());

	for (const SGNode& node : legacy.nodes)
	{
		ShaderGraphNodeInstance instance{};
		instance.id = node.id;
		instance.descriptorVersion = 1;
		instance.numericOverrides = node.values;
		instance.textOverride = node.text;
		if (!SGNodeOpToDescriptorId(node.op, instance.descriptorId))
			return Fail(outError, "failed to map legacy op to descriptor id");
		migrated.nodeInstances.push_back(std::move(instance));
	}

	outMigrated = std::move(migrated);
	return true;
}

void PopulateLegacyNodesFromInstances(ShaderGraphAsset& graph)
{
	if (!graph.nodes.empty())
		return;
	if (graph.nodeInstances.empty())
		return;

	ShaderGraphNodeRegistry builtinReg;
	const bool regOk = ShaderGraphTryLoadBuiltinNodeRegistry(builtinReg, nullptr);

	graph.nodes.clear();
	graph.nodes.reserve(graph.nodeInstances.size());
	for (const ShaderGraphNodeInstance& inst : graph.nodeInstances)
	{
		SGNode n{};
		n.id = inst.id;
		n.values = inst.numericOverrides;
		n.text = inst.textOverride;
		const ShaderGraphNodeDescriptor* desc = regOk ? builtinReg.Find(inst.descriptorId) : nullptr;
		if (!ShaderGraphResolveNodeOp(desc, inst.descriptorId, n.op))
			n.op = SGNodeOp::InputUV; // Unknown id: keep graph loadable for tooling/tests.
		graph.nodes.push_back(std::move(n));
	}
}

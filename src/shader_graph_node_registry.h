#pragma once

#include "shader_graph_node_descriptor.h"
#include "shader_graph_types.h"

#include <string>
#include <unordered_map>
#include <vector>

class ShaderGraphNodeRegistry
{
public:
	bool LoadFromDirectory(const std::string& directory, std::string* outError);
	const ShaderGraphNodeDescriptor* Find(const std::string& id) const;
	void ListDescriptorIds(std::vector<std::string>& outSortedUnique) const;

private:
	std::unordered_map<std::string, ShaderGraphNodeDescriptor> byId_;
};

bool ShaderGraphTryLoadBuiltinNodeRegistry(ShaderGraphNodeRegistry& outRegistry, std::string* outError);
bool ShaderGraphResolveNodeOp(const ShaderGraphNodeDescriptor* desc, const std::string& descriptorId, SGNodeOp& outOp);

#pragma once

#include "shader_graph_types.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

inline bool DeleteSelectedShaderGraphLinks(
    ShaderGraphAsset& graph, const std::vector<int>& selectedLinkIds, const std::unordered_map<int, size_t>& linkToEdgeIndex)
{
	std::vector<size_t> removeIndices;
	removeIndices.reserve(selectedLinkIds.size());
	for (int linkId : selectedLinkIds)
	{
		auto it = linkToEdgeIndex.find(linkId);
		if (it != linkToEdgeIndex.end() && it->second < graph.edges.size())
			removeIndices.push_back(it->second);
	}
	if (removeIndices.empty())
		return false;
	std::sort(removeIndices.begin(), removeIndices.end());
	removeIndices.erase(std::unique(removeIndices.begin(), removeIndices.end()), removeIndices.end());
	for (auto it = removeIndices.rbegin(); it != removeIndices.rend(); ++it)
		graph.edges.erase(graph.edges.begin() + static_cast<std::ptrdiff_t>(*it));
	return true;
}

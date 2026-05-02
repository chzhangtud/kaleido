#pragma once

#include "shader_graph_types.h"

#include <string>

bool MigrateLegacyShaderGraph(const ShaderGraphAsset& legacy, ShaderGraphAsset& outMigrated, std::string* outError);

// Fills legacy SGNode entries from descriptor-backed instances (editor / tooling path).
void PopulateLegacyNodesFromInstances(ShaderGraphAsset& graph);

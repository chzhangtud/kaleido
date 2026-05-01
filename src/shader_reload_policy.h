#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

inline std::vector<std::string> BuildShaderHotReloadCandidates(
    const std::string& shaderName, const std::string& scenePath, const std::string& runtimeShaderDir)
{
	namespace fs = std::filesystem;
	std::vector<std::string> candidates;
	const std::string shaderFile = shaderName + ".spv";
	if (!runtimeShaderDir.empty() && shaderName == "mesh.frag")
		candidates.push_back((fs::path(runtimeShaderDir) / shaderFile).lexically_normal().string());
	if (const char* shaderDirEnv = std::getenv("KALEIDO_SHADER_DIR"))
	{
		if (shaderDirEnv[0] != '\0')
			candidates.push_back((fs::path(shaderDirEnv) / shaderFile).lexically_normal().string());
	}
	candidates.push_back((fs::path(scenePath).parent_path() / "shaders" / shaderFile).lexically_normal().string());
	candidates.push_back((fs::current_path() / "shaders" / shaderFile).lexically_normal().string());
	return candidates;
}

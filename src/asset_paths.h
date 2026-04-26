#pragma once

#include <string>

// Resolved once per process: empty if neither KALEIDO_ASSETS_ROOT nor <repo>/../assets exists.
const std::string& GetAssetsRoot();

// If raw is absolute, return normalized absolute path. Otherwise join with GetAssetsRoot().
std::string ResolveModelPath(const std::string& rawModelPath);

// If modelPath is under the assets root, return a forward-slash relative path; else absolute (normalized).
std::string MakeModelPathForSerialization(const std::string& modelPath);

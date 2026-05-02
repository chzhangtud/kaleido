#include "shader_graph_node_registry.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace
{
bool Fail(std::string* outError, const std::string& msg)
{
	if (outError)
		*outError = msg;
	return false;
}
} // namespace

bool ShaderGraphNodeRegistry::LoadFromDirectory(const std::string& directory, std::string* outError)
{
	std::vector<std::string> jsonFiles;
#ifdef _WIN32
	const std::string pattern = directory + "\\*.json";
	WIN32_FIND_DATAA findData{};
	HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return Fail(outError, "descriptor directory does not exist or has no json: " + directory);
	do
	{
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			jsonFiles.push_back(directory + "\\" + findData.cFileName);
	} while (FindNextFileA(hFind, &findData));
	FindClose(hFind);
#else
	DIR* dir = opendir(directory.c_str());
	if (!dir)
		return Fail(outError, "descriptor directory does not exist: " + directory);
	for (dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir))
	{
		const std::string name = entry->d_name ? entry->d_name : "";
		if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
			jsonFiles.push_back(directory + "/" + name);
	}
	closedir(dir);
#endif

	byId_.clear();
	for (const std::string& path : jsonFiles)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return Fail(outError, "failed to open descriptor: " + path);
		const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		ShaderGraphNodeDescriptor desc{};
		if (!DeserializeShaderGraphNodeDescriptor(content, desc, outError))
			return false;
		byId_[desc.id] = std::move(desc);
	}

	return true;
}

const ShaderGraphNodeDescriptor* ShaderGraphNodeRegistry::Find(const std::string& id) const
{
	const auto it = byId_.find(id);
	if (it == byId_.end())
		return nullptr;
	return &it->second;
}

void ShaderGraphNodeRegistry::ListDescriptorIds(std::vector<std::string>& outSortedUnique) const
{
	outSortedUnique.clear();
	outSortedUnique.reserve(byId_.size());
	for (const auto& kv : byId_)
		outSortedUnique.push_back(kv.first);
	std::sort(outSortedUnique.begin(), outSortedUnique.end());
}

bool ShaderGraphResolveNodeOp(const ShaderGraphNodeDescriptor* desc, const std::string& descriptorId, SGNodeOp& outOp)
{
	if (desc && !desc->evalKind.empty())
		return SGNodeOpFromString(desc->evalKind, outOp);
	return SGNodeOpFromDescriptorId(descriptorId, outOp);
}

bool ShaderGraphTryLoadBuiltinNodeRegistry(ShaderGraphNodeRegistry& outRegistry, std::string* outError)
{
	for (const char* candidate : { "assets/shader_graph_nodes", "../assets/shader_graph_nodes", "../../assets/shader_graph_nodes",
	         "../../../assets/shader_graph_nodes", "../../../../assets/shader_graph_nodes" })
	{
		if (outRegistry.LoadFromDirectory(candidate, outError))
			return true;
	}
	return false;
}

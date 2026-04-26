#include "asset_paths.h"

#include "common.h"

#include <cstdlib>
#include <filesystem>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

namespace
{
std::string ResolveAssetsRootOnce()
{
	namespace fs = std::filesystem;

	const char* env = std::getenv("KALEIDO_ASSETS_ROOT");
	if (env && env[0] != '\0')
	{
		std::error_code ec;
		const fs::path p(env);
		if (fs::is_directory(p, ec))
		{
			LOGI("Kaleido assets root: KALEIDO_ASSETS_ROOT=%s", env);
			return p.lexically_normal().string();
		}
		LOGW("KALEIDO_ASSETS_ROOT is set but not a directory: %s", env);
	}

#if defined(KALEIDO_REPO_ROOT)
	{
		std::error_code ec;
		const fs::path repo(KALEIDO_REPO_ROOT);
		const fs::path fallback = (repo / ".." / "assets").lexically_normal();
		if (fs::is_directory(fallback, ec))
		{
			LOGI("Kaleido assets root: default <repo>/../assets -> %s", fallback.string().c_str());
			return fallback.string();
		}
	}
#endif

	LOGW("Kaleido assets root: not resolved (set KALEIDO_ASSETS_ROOT or place assets next to the repo).");
	return {};
}

bool PathHasParentEscape(const std::filesystem::path& p)
{
	for (const auto& part : p)
	{
		if (part == "..")
			return true;
	}
	return false;
}
} // namespace

const std::string& GetAssetsRoot()
{
	static std::string root;
	static bool once = false;
	if (!once)
	{
		root = ResolveAssetsRootOnce();
		once = true;
	}
	return root;
}

std::string ResolveModelPath(const std::string& rawModelPath)
{
	namespace fs = std::filesystem;

	const fs::path raw(rawModelPath);
	if (raw.is_absolute())
	{
		std::error_code ec;
		const fs::path norm = fs::weakly_canonical(raw, ec);
		if (!ec)
			return norm.string();
		return raw.lexically_normal().string();
	}

	const std::string& rootStr = GetAssetsRoot();
	if (rootStr.empty())
		return rawModelPath;

	const fs::path joined = (fs::path(rootStr) / raw).lexically_normal();
	return joined.string();
}

std::string MakeModelPathForSerialization(const std::string& modelPath)
{
	namespace fs = std::filesystem;

	const fs::path p(modelPath);
	if (!p.is_absolute())
		return p.generic_string();

	std::error_code ec;
	fs::path abs = fs::weakly_canonical(p, ec);
	if (ec)
		abs = p.lexically_normal();

	const std::string& rootStr = GetAssetsRoot();
	if (rootStr.empty())
		return abs.string();

	fs::path normRoot = fs::weakly_canonical(fs::path(rootStr), ec);
	if (ec)
		normRoot = fs::path(rootStr).lexically_normal();

	const fs::path rel = abs.lexically_relative(normRoot);
	if (rel.empty() || PathHasParentEscape(rel))
		return abs.string();

	return rel.generic_string();
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

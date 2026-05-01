#include "../src/shader_reload_policy.h"

#include <cassert>
#include <string>
#include <vector>

static bool Contains(const std::vector<std::string>& v, const std::string& needle)
{
	for (const std::string& s : v)
	{
		if (s.find(needle) != std::string::npos)
			return true;
	}
	return false;
}

static void TestRuntimeCompileApplyPrefersRuntimeFragment()
{
	const std::vector<std::string> c = BuildShaderHotReloadCandidates(
	    "mesh.frag", "testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json", "build/Debug/shaders/runtime_generated");
	assert(!c.empty());
	assert(c.front().find("runtime_generated") != std::string::npos);
}

static void TestNonRuntimeShaderDoesNotProbeRuntimeDir()
{
	const std::vector<std::string> c = BuildShaderHotReloadCandidates(
	    "clustercull.comp", "testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json", "build/Debug/shaders/runtime_generated");
	assert(!Contains(c, "runtime_generated"));
}

int main()
{
	TestRuntimeCompileApplyPrefersRuntimeFragment();
	TestNonRuntimeShaderDoesNotProbeRuntimeDir();
	return 0;
}

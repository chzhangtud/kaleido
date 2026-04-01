#pragma once

#include <string>
#include <vector>

#include "renderer.h"
#if defined(__ANDROID__)
struct ANativeWindow;
#endif

enum class RuntimeLaunchMode
{
	Standalone,
	EditorViewport
};

struct KaleidoLaunchConfig
{
	std::string path;
	std::string modelPath;
	std::vector<std::string> meshPaths;
	bool loadSingleModel = true;
	bool enableRuntimeUi = true;
	RuntimeLaunchMode launchMode = RuntimeLaunchMode::Standalone;
#if defined(__ANDROID__)
	ANativeWindow* nativeWindow = nullptr;
#endif
};

class KaleidoRuntime
{
public:
	int Initialize(const KaleidoLaunchConfig& config);
	bool RenderFrame();
	void Shutdown();
};

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

struct RuntimeHostOptions
{
	bool enableRuntimeUi = true;
	RuntimeLaunchMode launchMode = RuntimeLaunchMode::Standalone;
};

struct KaleidoLaunchConfig
{
	std::string path;
	std::string modelPath;
	std::vector<std::string> meshPaths;
	bool loadSingleModel = true;
	RuntimeHostOptions hostOptions{};
#if defined(__ANDROID__)
	ANativeWindow* nativeWindow = nullptr;
#endif
};

class RuntimeHostBridge
{
public:
	virtual ~RuntimeHostBridge() = default;
	virtual KaleidoLaunchConfig BuildLaunchConfig() const = 0;
};

class KaleidoRuntime
{
public:
	int Initialize(const KaleidoLaunchConfig& config);
	bool RenderFrame();
	void Shutdown();

private:
	KaleidoLaunchConfig activeConfig{};
	bool initialized = false;
};

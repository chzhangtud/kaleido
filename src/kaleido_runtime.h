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
	// Optional: load editor scene snapshot on startup (RapidJSON .json from Save Scene).
	std::string editorSceneStatePath;
	// Optional: for automation - dump first editor viewport to this path (EXR, or PNG if path ends in .png) and exit.
	std::string autoDumpExrPath;
	// Number of render frames to wait after Initialize before scheduling the auto dump.
	uint32_t autoDumpExrFrameDelay = 64;
	// Optional: for automation - dump current RenderGraph visualization snapshot to DOT/JSON.
	std::string autoDumpRenderGraphDotPath;
	std::string autoDumpRenderGraphJsonPath;
	// Number of render frames to wait before RenderGraph dump.
	uint32_t autoDumpRenderGraphFrameDelay = 64;
	// Win32 editor: initial 3D viewport render resolution (0 = derive from window / ImGui layout).
	uint32_t editorInitialViewportWidth = 0;
	uint32_t editorInitialViewportHeight = 0;
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

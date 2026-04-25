#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION

#include "common.h"
#include "renderer.h"
#include "kaleido_runtime.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#if defined(WIN32)
#include <windows.h>
namespace
{
class EditorRuntimeHostBridge final : public RuntimeHostBridge
{
public:
	EditorRuntimeHostBridge(int inArgc, const char** inArgv)
	    : argc(inArgc)
	    , argv(inArgv)
	{
	}

	KaleidoLaunchConfig BuildLaunchConfig() const override
	{
		KaleidoLaunchConfig config{};
		config.path = argv[0];

		for (int i = 1; i < argc; ++i)
		{
			if (strcmp(argv[i], "--auto-dump-exr") == 0 && i + 1 < argc)
			{
				config.autoDumpExrPath = argv[++i];
			}
			else if (strcmp(argv[i], "--load-scene-state") == 0 && i + 1 < argc)
			{
				config.editorSceneStatePath = argv[++i];
			}
			else if (strcmp(argv[i], "--auto-dump-frames") == 0 && i + 1 < argc)
			{
				config.autoDumpExrFrameDelay = uint32_t(atoi(argv[++i]));
			}
			else if (argv[i][0] == '-')
			{
				LOGW("Unknown argument: %s", argv[i]);
			}
		}

		// First non-option argument: .json = editor scene file, else gltf/glb as a single model.
		for (int i = 1; i < argc; ++i)
		{
			if (argv[i][0] == '-')
			{
				if (strcmp(argv[i], "--auto-dump-exr") == 0 && i + 1 < argc)
					++i;
				else if (strcmp(argv[i], "--load-scene-state") == 0 && i + 1 < argc)
					++i;
				else if (strcmp(argv[i], "--auto-dump-frames") == 0 && i + 1 < argc)
					++i;
				continue;
			}
			const char* ext = strrchr(argv[i], '.');
			if (ext && _stricmp(ext, ".json") == 0)
			{
				if (config.editorSceneStatePath.empty())
					config.editorSceneStatePath = argv[i];
			}
			else if (ext && (_stricmp(ext, ".gltf") == 0 || _stricmp(ext, ".glb") == 0))
			{
				if (config.modelPath.empty())
				{
					config.modelPath = argv[i];
					config.loadSingleModel = true;
				}
			}
			break;
		}

		if (config.modelPath.empty() && config.editorSceneStatePath.empty())
		{
			const char* editorModel = getenv("KALEIDO_EDITOR_MODEL");
			if (editorModel && editorModel[0])
			{
				config.modelPath = editorModel;
				config.loadSingleModel = true;
				LOGI("No CLI scene specified for kaleido_editor; using KALEIDO_EDITOR_MODEL=%s", config.modelPath.c_str());
			}
			else
			{
				config.loadSingleModel = false;
				LOGI("No startup scene specified for kaleido_editor; launching with an empty scene.");
			}
		}
		else
		{
			if (!config.modelPath.empty())
				config.loadSingleModel = true;
		}

		config.hostOptions.enableRuntimeUi = true;
		config.hostOptions.launchMode = RuntimeLaunchMode::EditorViewport;
		return config;
	}

private:
	int argc;
	const char** argv;
};
} // namespace

int main(int argc, const char** argv)
{
	// Unattended automation: avoid blocking WER/abort UI when the process faults.
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--auto-dump-exr") == 0)
		{
			SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
			_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
			break;
		}
	}

	auto runtime = std::make_unique<KaleidoRuntime>();
	EditorRuntimeHostBridge bridge(argc, argv);

	int code = runtime->Initialize(bridge.BuildLaunchConfig());
	if (code != 0)
		return code;

	auto vContext = VulkanContext::GetInstance();
	while (!glfwWindowShouldClose(vContext->window))
	{
		if (!runtime->RenderFrame())
			break;
	}

	runtime->Shutdown();
	return 0;
}
#endif

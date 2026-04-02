#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION

#include "common.h"
#include "renderer.h"
#include "kaleido_runtime.h"

#include <cstdlib>
#include <memory>

#if defined(WIN32)
namespace
{
class EditorRuntimeHostBridge final : public RuntimeHostBridge
{
public:
	EditorRuntimeHostBridge(int inArgc, const char** inArgv)
	    : argc(inArgc), argv(inArgv)
	{
	}

	KaleidoLaunchConfig BuildLaunchConfig() const override
	{
		KaleidoLaunchConfig config{};
		config.path = argv[0];
		config.loadSingleModel = true;

		if (argc >= 2)
		{
			config.modelPath = argv[1];
		}
		else
		{
			const char* editorModel = getenv("KALEIDO_EDITOR_MODEL");
			config.modelPath = (editorModel && editorModel[0]) ? editorModel : "afterRain/scene.gltf";
			LOGW("No glTF specified for kaleido_editor; using %s", config.modelPath.c_str());
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

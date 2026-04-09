#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION

#include "common.h"
#include "renderer.h"
#include "kaleido_runtime.h"

#include <memory>
#include <unordered_map>

#if defined(__ANDROID__)
#include <jni.h>
#include <android/native_window_jni.h> // ANativeWindow_fromSurface
#include <vulkan/vulkan.h>
#include "imgui.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

AAssetManager* g_assetManager = nullptr;
static ANativeWindow* g_window = nullptr;

extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeSetAssetManager(JNIEnv* env, jobject /*this*/, jobject assetManager)
{
	g_assetManager = AAssetManager_fromJava(env, assetManager);
}
#endif

static std::unique_ptr<KaleidoRuntime> g_runtime;

static RuntimeHostOptions ResolveRuntimeOptionsFromEnv()
{
	RuntimeHostOptions options{};
	options.enableRuntimeUi = !(getenv("KALEIDO_NO_RUNTIME_UI") && atoi(getenv("KALEIDO_NO_RUNTIME_UI")));

	const char* mode = getenv("KALEIDO_SURFACE_MODE");
	if (mode && strcmp(mode, "editor_viewport") == 0)
	{
		options.launchMode = RuntimeLaunchMode::EditorViewport;
		options.enableRuntimeUi = false;
	}

	return options;
}

static int InitializeRuntimeWithBridge(const RuntimeHostBridge& bridge)
{
	KaleidoLaunchConfig config = bridge.BuildLaunchConfig();
	g_runtime = std::make_unique<KaleidoRuntime>();
	return g_runtime->Initialize(config);
}

#if defined(WIN32)
int init(int argc, const char** argv)
#elif defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jobject surface)
#endif
{
#if defined(WIN32)
	if (argc < 2)
	{
		LOGE("Usage: %s [mesh list]", argv[0]);
		return 1;
	}
	class DesktopRuntimeHostBridge final : public RuntimeHostBridge
	{
	public:
		DesktopRuntimeHostBridge(int inArgc, const char** inArgv)
		    : argc(inArgc)
		    , argv(inArgv)
		{
		}

		KaleidoLaunchConfig BuildLaunchConfig() const override
		{
			KaleidoLaunchConfig config{};
			config.path = argv[0];
			config.modelPath = argv[1];
			config.loadSingleModel = (argc == 2);
			if (!config.loadSingleModel)
			{
				for (int i = 1; i < argc; ++i)
					config.meshPaths.emplace_back(argv[i]);
			}
			config.hostOptions = ResolveRuntimeOptionsFromEnv();
			return config;
		}

	private:
		int argc;
		const char** argv;
	};

	DesktopRuntimeHostBridge bridge(argc, argv);
	return InitializeRuntimeWithBridge(bridge);
#elif defined(__ANDROID__)
	g_window = ANativeWindow_fromSurface(env, surface);
	class AndroidRuntimeHostBridge final : public RuntimeHostBridge
	{
	public:
		explicit AndroidRuntimeHostBridge(ANativeWindow* inWindow)
		    : window(inWindow)
		{
		}

		KaleidoLaunchConfig BuildLaunchConfig() const override
		{
			KaleidoLaunchConfig config{};
			config.path = "";
			config.modelPath = "afterRain/scene.gltf";
			config.loadSingleModel = true;
			config.hostOptions = ResolveRuntimeOptionsFromEnv();
			config.nativeWindow = window;
			return config;
		}

	private:
		ANativeWindow* window = nullptr;
	};

	AndroidRuntimeHostBridge bridge(g_window);
	int code = InitializeRuntimeWithBridge(bridge);
	if (code != 0)
	{
		LOGE("Failed to initialize runtime, code=%d", code);
		return;
	}
#endif
}

#if defined(WIN32)
bool render()
#elif defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeRender(JNIEnv* env, jobject thiz)
#endif
{
	if (!g_runtime)
	{
#if defined(WIN32)
		return false;
#else
		return;
#endif
	}

	bool ret = g_runtime->RenderFrame();
#if defined(WIN32)
	return ret;
#endif
}

#if defined(WIN32)
void destroy()
#elif defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz)
#endif
{
	if (g_runtime)
	{
		g_runtime->Shutdown();
		g_runtime.reset();
	}
#if defined(__ANDROID__)
	if (g_window)
	{
		ANativeWindow_release(g_window);
		g_window = nullptr;
	}
#endif
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeOnTouchEvent(JNIEnv* env, jobject obj,
    jint action, jfloat x, jfloat y, jint pointerId)
{
	ImGuiIO& io = ImGui::GetIO();
	switch (action)
	{
	case 0: // ACTION_DOWN
		io.AddMousePosEvent(x, y);
		io.AddMouseButtonEvent(0, true); // left mouse key down
		break;
	case 1: // ACTION_UP
		io.AddMouseButtonEvent(0, false);
		break;
	case 2: // ACTION_MOVE
		io.AddMousePosEvent(x, y);
		break;
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeOnVirtualSticks(JNIEnv* env, jobject obj,
    jfloat moveX, jfloat moveY, jfloat lookX, jfloat lookY)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse)
	{
		SetVirtualSticks(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}
	SetVirtualSticks(moveX, moveY, lookX, lookY);
}
static std::unordered_map<int, ImGuiKey> g_KeyMap = {
	{ 29, ImGuiKey_A }, { 30, ImGuiKey_B }, { 31, ImGuiKey_C }, { 32, ImGuiKey_D },
	{ 33, ImGuiKey_E }, { 34, ImGuiKey_F }, { 35, ImGuiKey_G }, { 36, ImGuiKey_H },
	{ 37, ImGuiKey_I }, { 38, ImGuiKey_J }, { 39, ImGuiKey_K }, { 40, ImGuiKey_L },
	{ 41, ImGuiKey_M }, { 42, ImGuiKey_N }, { 43, ImGuiKey_O }, { 44, ImGuiKey_P },
	{ 45, ImGuiKey_Q }, { 46, ImGuiKey_R }, { 47, ImGuiKey_S }, { 48, ImGuiKey_T },
	{ 49, ImGuiKey_U }, { 50, ImGuiKey_V }, { 51, ImGuiKey_W }, { 52, ImGuiKey_X },
	{ 53, ImGuiKey_Y }, { 54, ImGuiKey_Z },

	{ 66, ImGuiKey_Enter }, { 62, ImGuiKey_Space }, { 67, ImGuiKey_Backspace },
	{ 61, ImGuiKey_Tab }, { 111, ImGuiKey_Escape },

	{ 59, ImGuiKey_LeftShift }, { 60, ImGuiKey_RightShift },
	{ 113, ImGuiKey_LeftCtrl }, { 114, ImGuiKey_RightCtrl },
	{ 57, ImGuiKey_LeftAlt }, { 58, ImGuiKey_RightAlt },

	{ 19, ImGuiKey_UpArrow }, { 20, ImGuiKey_DownArrow },
	{ 21, ImGuiKey_LeftArrow }, { 22, ImGuiKey_RightArrow }
};

// JNI callback: Java -> Native
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeOnKeyEvent(JNIEnv* env, jobject obj, jint keyCode, jboolean down)
{
	ImGuiIO& io = ImGui::GetIO();
	auto it = g_KeyMap.find(keyCode);
	if (it != g_KeyMap.end())
	{
		io.AddKeyEvent(it->second, down);
	}
}
#endif

#if defined(WIN32)
int main(int argc, const char** argv)
{
	int code = init(argc, argv);
	if (code != 0)
		return code;

	auto vContext = VulkanContext::GetInstance();
	while (!glfwWindowShouldClose(vContext->window))
	{
		if (!render())
			break;
	}

	destroy();
	return 0;
}
#endif

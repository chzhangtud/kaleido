#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION

#include "common.h"
#include "renderer.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <android/native_window_jni.h> // ANativeWindow_fromSurface
#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
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
	std::string path = argv[0];
	std::string modelPath = argv[1];
#elif defined(__ANDROID__)
	g_window = ANativeWindow_fromSurface(env, surface);
	std::string path; // TODO
//    std::string modelPath = "DamagedHelmet.gltf";
    std::string modelPath = "afterRain/scene.gltf";
#endif

	scene = std::make_shared<Scene>(path.c_str());
	auto vContext = VulkanContext::GetInstance();
	vContext->SetScene(scene);
#if defined(WIN32)
	vContext->InitVulkan();
#elif defined(__ANDROID__)
    static bool vulkanInitialized = false;
    if(!vulkanInitialized)
    {
        vContext->InitVulkan(g_window);
        vulkanInitialized = true;
    }
#endif

	// material index 0 is always dummy
	scene->materials.resize(1);
	scene->materials[0].diffuseFactor = vec4(1);

	//scene->camera.position = { 2.f, 0.f, 0.4f };
	//scene->camera.orientation = glm::radians(glm::vec3(0.f, 80.f, 0.f));
	scene->camera.position = { 14.5f, 3.f, 10.f };
	scene->camera.orientation = glm::radians(glm::vec3(-5.f, -220.f, 0.f));
	scene->camera.fovY = glm::radians(70.f);
	scene->camera.znear = 0.1f;
	scene->sunDirection = normalize(vec3(1.0f, 1.0f, 1.0f));

	bool sceneMode = false;
	bool fastMode = getenv("FAST") && atoi(getenv("FAST"));

#if defined(WIN32)
	bool loadSingleModel = (argc == 2);
#elif defined(__ANDROID__)
	bool loadSingleModel = true; // TODO
#endif
	if (loadSingleModel)
	{
		const char* ext = strrchr(modelPath.c_str(), '.');
		if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
		{
			glm::vec3 euler(0.f);
			if (!loadScene(scene->geometry, scene->materials, scene->draws, scene->texturePaths, scene->animations, scene->camera, scene->sunDirection, modelPath.c_str(), vContext->meshShadingSupported, euler, fastMode))
			{
				LOGE("Error: scene %s failed to load", modelPath.c_str());
			#if defined(WIN32)
				return 1;
			#elif defined(__ANDROID__)
				return;
			#endif
			}

			pitch = euler.x;
			yaw = euler.y;
			roll = euler.z;

			sceneMode = true;
		}
	}

	size_t imageMemory = 0;
#if defined(WIN32)
	double imageTimer = glfwGetTime();
#endif

	for (size_t i = 0; i < scene->texturePaths.size(); ++i)
	{
		Image image;
		if (!loadImage(image, vContext->device, vContext->physicalDevice, vContext->commandPools[0], vContext->commandBuffers[0], vContext->queue, vContext->memoryProperties, vContext->scratch, scene->texturePaths[i].c_str()))
		{
			LOGE("Error: image %s failed to load", scene->texturePaths[i].c_str());
		#if defined(WIN32)
			return 1;
		#elif defined(__ANDROID__)
			return;
		#endif
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetImageMemoryRequirements(vContext->device, image.image, &memoryRequirements);
		imageMemory += memoryRequirements.size;

		scene->images.push_back(image);
	}

#if defined(WIN32)
	LOGI("Loaded %d textures (%.2f MB) in %.2f sec", int(scene->images.size()), double(imageMemory) / 1e6, glfwGetTime() - imageTimer);
#endif

	uint32_t descriptorCount = uint32_t(scene->texturePaths.size() + 1);
	scene->textureSet = createDescriptorArray(vContext->device, vContext->textureSetLayout, descriptorCount);

	for (size_t i = 0; i < scene->texturePaths.size(); ++i)
	{
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageView = scene->images[i].imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = scene->textureSet.second;
		write.dstBinding = 0;
		write.dstArrayElement = uint32_t(i + 1);
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(vContext->device, 1, &write, 0, nullptr);
	}

#if defined(WIN32)
	if (!sceneMode)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (!loadMesh(scene->geometry, argv[i], vContext->meshShadingSupported, fastMode))
			{
				LOGE("Error: mesh %s failed to load", argv[i]);
				return 1;
			}
		}
	}
#endif

	if (scene->geometry.meshes.empty())
	{
		LOGE("Error: no meshes loaded!");
	#if defined(WIN32)
		return 1;
	#elif defined(__ANDROID__)
		return;
	#endif
	}

	LOGI("Geometry: VB %.2f MB, IB %.2f MB, meshlets %.2f MB\n",
	    double(scene->geometry.vertices.size() * sizeof(Vertex)) / 1e6,
	    double(scene->geometry.indices.size() * sizeof(uint32_t)) / 1e6,
	    double(scene->geometry.meshlets.size() * sizeof(Meshlet) + scene->geometry.meshletdata.size() * sizeof(uint32_t)) / 1e6);

	if (scene->draws.empty())
	{
		rngstate.state = 0x42;

		uint32_t drawCount = 100'000;
		scene->draws.resize(drawCount);

		float sceneRadius = 150;

		for (uint32_t i = 0; i < drawCount; ++i)
		{
			MeshDraw& draw = scene->draws[i];

			size_t meshIndex = rand32() % scene->geometry.meshes.size();
			const Mesh& mesh = scene->geometry.meshes[meshIndex];

			draw.position[0] = float(rand01()) * sceneRadius * 2 - sceneRadius;
			draw.position[1] = float(rand01()) * sceneRadius * 2 - sceneRadius;
			draw.position[2] = float(rand01()) * sceneRadius * 2 - sceneRadius;
			draw.scale = float(rand01()) + 1;
			draw.scale *= 2;

			vec3 axis = normalize(vec3(float(rand01()) * 2 - 1, float(rand01()) * 2 - 1, float(rand01()) * 2 - 1));
			float angle = glm::radians(float(rand01()) * 90.f);

			draw.orientation = quat(cosf(angle * 0.5f), axis * sinf(angle * 0.5f));

			draw.meshIndex = uint32_t(meshIndex);
		}
	}

	scene->drawDistance = 2000.f;

	for (size_t i = 0; i < scene->draws.size(); ++i)
	{
		MeshDraw& draw = scene->draws[i];
		const Mesh& mesh = scene->geometry.meshes[draw.meshIndex];

		draw.meshletVisibilityOffset = scene->meshletVisibilityCount;

		uint32_t meshletCount = 0;
		for (uint32_t i = 0; i < mesh.lodCount; ++i)
			meshletCount = std::max(meshletCount, mesh.lods[i].meshletCount);

		scene->meshletVisibilityCount += meshletCount;
		scene->meshPostPasses |= 1 << draw.postPass;
	}

	vContext->InitResources();
#if defined(WIN32)
	return 0;
#endif
}

#if defined(WIN32)
bool render()
#elif defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeRender(JNIEnv* env, jobject thiz)
#endif
{
	auto vContext = VulkanContext::GetInstance();
    bool ret = vContext->DrawFrame();
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
	VulkanContext::DestroyInstance();
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
    switch (action) {
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
static std::unordered_map<int, ImGuiKey> g_KeyMap = {
        {29, ImGuiKey_A},{30, ImGuiKey_B}, {31, ImGuiKey_C}, {32, ImGuiKey_D},
        {33, ImGuiKey_E}, {34, ImGuiKey_F}, {35, ImGuiKey_G}, {36, ImGuiKey_H},
        {37, ImGuiKey_I}, {38, ImGuiKey_J}, {39, ImGuiKey_K}, {40, ImGuiKey_L},
        {41, ImGuiKey_M}, {42, ImGuiKey_N}, {43, ImGuiKey_O}, {44, ImGuiKey_P},
        {45, ImGuiKey_Q}, {46, ImGuiKey_R}, {47, ImGuiKey_S}, {48, ImGuiKey_T},
        {49, ImGuiKey_U}, {50, ImGuiKey_V}, {51, ImGuiKey_W}, {52, ImGuiKey_X},
        {53, ImGuiKey_Y}, {54, ImGuiKey_Z},

        {66, ImGuiKey_Enter}, {62, ImGuiKey_Space}, {67, ImGuiKey_Backspace},
        {61, ImGuiKey_Tab},   {111, ImGuiKey_Escape},

        {59, ImGuiKey_LeftShift}, {60, ImGuiKey_RightShift},
        {113, ImGuiKey_LeftCtrl}, {114, ImGuiKey_RightCtrl},
        {57, ImGuiKey_LeftAlt},   {58, ImGuiKey_RightAlt},

        {19, ImGuiKey_UpArrow}, {20, ImGuiKey_DownArrow},
        {21, ImGuiKey_LeftArrow}, {22, ImGuiKey_RightArrow}
};

// JNI callback: Java -> Native
extern "C" JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeOnKeyEvent(JNIEnv* env, jobject obj, jint keyCode, jboolean down)
{
    ImGuiIO& io = ImGui::GetIO();
    auto it = g_KeyMap.find(keyCode);
    if (it != g_KeyMap.end()) {
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
		{
			break;
		}
	}

	destroy();
}
#endif
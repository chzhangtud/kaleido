#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION

#include "common.h"
#include "renderer.h"

#if defined(_WIN32)
int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		LOGE("Usage: %s [mesh list]", argv[0]);
		return 1;
	}

	scene = std::make_shared<Scene>(argv[0]);

	auto vContext = VulkanContext::GetInstance();
	vContext->SetScene(scene);
	vContext->InitVulkan();

	// material index 0 is always dummy
	scene->materials.resize(1);
	scene->materials[0].diffuseFactor = vec4(1);

	scene->camera.position = { 2.f, 0.f, 0.4f };
	scene->camera.orientation = glm::radians(glm::vec3(0.f, 80.f, 0.f));
	scene->camera.fovY = glm::radians(70.f);
	scene->sunDirection = normalize(vec3(1.0f, 1.0f, 1.0f));

	bool sceneMode = false;
	bool fastMode = getenv("FAST") && atoi(getenv("FAST"));

	if (argc == 2)
	{
		const char* ext = strrchr(argv[1], '.');
		if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
		{
			glm::vec3 euler(0.f);
			if (!loadScene(scene->geometry, scene->materials, scene->draws, scene->texturePaths, scene->camera, scene->sunDirection, argv[1], vContext->meshShadingSupported, euler, fastMode))
			{
				LOGE("Error: scene %s failed to load", argv[1]);
				return 1;
			}

			pitch = euler.x;
			yaw = euler.y;
			roll = euler.z;

			sceneMode = true;
		}
	}

	size_t imageMemory = 0;
	double imageTimer = glfwGetTime();

	for (size_t i = 0; i < scene->texturePaths.size(); ++i)
	{
		Image image;
		if (!loadImage(image, vContext->device, vContext->physicalDevice, vContext->commandPool, vContext->commandBuffer, vContext->queue, vContext->memoryProperties, vContext->scratch, scene->texturePaths[i].c_str()))
		{
			LOGE("Error: image %s failed to load", scene->texturePaths[i].c_str());
			return 1;
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetImageMemoryRequirements(vContext->device, image.image, &memoryRequirements);
		imageMemory += memoryRequirements.size;

		scene->images.push_back(image);
	}

	LOGI("Loaded %d textures (%.2f MB) in %.2f sec", int(scene->images.size()), double(imageMemory) / 1e6, glfwGetTime() - imageTimer);

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

	if (scene->geometry.meshes.empty())
	{
		LOGE("Error: no meshes loaded!");
		return 1;
	}

	LOGI("Geometry: VB %.2f MB, IB %.2f MB, meshlets %.2f MB",
	    double(scene->geometry.vertices.size() * sizeof(Vertex)) / 1e6,
	    double(scene->geometry.indices.size() * sizeof(uint32_t)) / 1e6,
	    double(scene->geometry.meshlets.size() * sizeof(Meshlet) + scene->geometry.meshletVertexData.size() * sizeof(unsigned int)) / 1e6 + scene->geometry.meshletIndexData.size() * sizeof(unsigned char) / 1e6);

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
	while (!glfwWindowShouldClose(vContext->window))
	{
		if (!vContext->DrawFrame())
		{
			break;
		}
	}

	VulkanContext::DestroyInstance();
}

#elif defined(__ANDROID__)
#include <jni.h>
#include <android/native_window_jni.h> // ANativeWindow_fromSurface
#include <vulkan/vulkan.h>
#include <memory>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "renderer.h"

AAssetManager* g_assetManager = nullptr;
static ANativeWindow* g_window = nullptr;

extern "C" {
JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeSetAssetManager(JNIEnv* env, jobject /*this*/, jobject assetManager) {
    g_assetManager = AAssetManager_fromJava(env, assetManager);
}

JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jobject surface) {
    g_window = ANativeWindow_fromSurface(env, surface);

    std::string path; // TODO
    std::string modelPath = "DamagedHelmet.gltf";

    scene = std::make_shared<Scene>(path.c_str());
    auto vContext = VulkanContext::GetInstance();
    vContext->SetScene(scene);
    vContext->InitVulkan(g_window);

    // material index 0 is always dummy
    scene->materials.resize(1);
    scene->materials[0].diffuseFactor = vec4(1);

    scene->camera.position = { 2.f, 0.f, 0.4f };
	scene->camera.orientation = glm::radians(glm::vec3(0.f, 80.f, 0.f));
    scene->camera.fovY = glm::radians(70.f);
    scene->sunDirection = normalize(vec3(1.0f, 1.0f, 1.0f));

    bool sceneMode = false;
    bool fastMode = getenv("FAST") && atoi(getenv("FAST"));

    bool loadSingleModel = true; // TODO
    if (loadSingleModel)
    {
        const char* ext = strrchr(modelPath.c_str(), '.');
        if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
        {
            glm::vec3 euler(0.f);
            if (!loadScene(scene->geometry, scene->materials, scene->draws, scene->texturePaths, scene->camera, scene->sunDirection, modelPath.c_str(), vContext->meshShadingSupported, euler, fastMode))
            {
                LOGE("Error: scene %s failed to load", modelPath.c_str());
                return;
            }

            pitch = euler.x;
            yaw = euler.y;
            roll = euler.z;

            sceneMode = true;
        }
    }

    size_t imageMemory = 0;

    for (size_t i = 0; i < scene->texturePaths.size(); ++i)
    {
        Image image;
        if (!loadImage(image, vContext->device, vContext->physicalDevice, vContext->commandPool, vContext->commandBuffer, vContext->queue, vContext->memoryProperties, vContext->scratch, scene->texturePaths[i].c_str()))
        {
            LOGE("Error: image %s failed to load", scene->texturePaths[i].c_str());
            return;
        }

        VkMemoryRequirements memoryRequirements = {};
        vkGetImageMemoryRequirements(vContext->device, image.image, &memoryRequirements);
        imageMemory += memoryRequirements.size;

        scene->images.push_back(image);
    }

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

    if (scene->geometry.meshes.empty())
    {
        LOGE("Error: no meshes loaded!");
        return;
    }

    LOGI("Geometry: VB %.2f MB, IB %.2f MB, meshlets %.2f MB",
         double(scene->geometry.vertices.size() * sizeof(Vertex)) / 1e6,
         double(scene->geometry.indices.size() * sizeof(uint32_t)) / 1e6,
         double(scene->geometry.meshlets.size() * sizeof(Meshlet) + scene->geometry.meshletVertexData.size() * sizeof(unsigned int)) / 1e6 + scene->geometry.meshletIndexData.size() * sizeof(unsigned char) / 1e6);

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
}

JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeRender(JNIEnv* env, jobject thiz) {
    auto vContext = VulkanContext::GetInstance();
    vContext->DrawFrame();
}

JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    VulkanContext::DestroyInstance();
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}

}
#endif
#include "kaleido_runtime.h"

#include "common.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

int KaleidoRuntime::Initialize(const KaleidoLaunchConfig& config)
{
	if (config.modelPath.empty())
	{
		LOGE("modelPath is empty");
		return 1;
	}

	scene = std::make_shared<Scene>(config.path.c_str());
	auto vContext = VulkanContext::GetInstance();
	vContext->SetScene(scene);
	vContext->SetRuntimeUiEnabled(config.hostOptions.enableRuntimeUi);

#if defined(WIN32)
	vContext->InitVulkan();
#elif defined(__ANDROID__)
	vContext->InitVulkan(config.nativeWindow);
#endif

	// material index 0 is always dummy
	scene->materials.resize(1);
	scene->materials[0].baseColorFactor = vec4(1);
	scene->materials[0].pbrFactor = vec4(1, 1, 0, 1);
	scene->materials[0].workflow = 1;

	scene->camera.position = { 14.5f, 3.f, 10.f };
	scene->camera.orientation = glm::radians(glm::vec3(-5.f, -220.f, 0.f));
	scene->camera.fovY = glm::radians(70.f);
	scene->camera.znear = 0.1f;
	scene->sunDirection = normalize(vec3(1.0f, 1.0f, 1.0f));

	bool sceneMode = false;
	bool fastMode = getenv("FAST") && atoi(getenv("FAST"));
	clusterRTEnabled = getenv("CLRT") && atoi(getenv("CLRT"));

	if (config.loadSingleModel)
	{
		const char* ext = strrchr(config.modelPath.c_str(), '.');
		if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
		{
			glm::vec3 euler(0.f);
			if (!loadScene(scene->geometry, scene->materials, scene->draws, scene->texturePaths, scene->animations, scene->camera, scene->sunDirection, config.modelPath.c_str(), vContext->meshShadingSupported, euler, fastMode, clusterRTEnabled))
			{
				LOGE("Error: scene %s failed to load", config.modelPath.c_str());
				return 1;
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
			return 1;
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
		for (const std::string& meshPath : config.meshPaths)
		{
			if (!loadMesh(scene->geometry, meshPath.c_str(), vContext->meshShadingSupported, fastMode, clusterRTEnabled))
			{
				LOGE("Error: mesh %s failed to load", meshPath.c_str());
				return 1;
			}
		}
	}
#endif

	if (scene->geometry.meshes.empty())
	{
		LOGE("Error: no meshes loaded!");
		return 1;
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
		for (uint32_t lod = 0; lod < mesh.lodCount; ++lod)
			meshletCount = std::max(meshletCount, mesh.lods[lod].meshletCount);

		scene->meshletVisibilityCount += meshletCount;
		scene->meshPostPasses |= 1 << draw.postPass;
	}

	vContext->InitResources();
	return 0;
}

bool KaleidoRuntime::RenderFrame()
{
	auto vContext = VulkanContext::GetInstance();
	return vContext->DrawFrame();
}

void KaleidoRuntime::Shutdown()
{
	VulkanContext::DestroyInstance();
}

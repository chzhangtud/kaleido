#include "kaleido_runtime.h"

#include "common.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
void BootstrapEditorEmptyScene(const std::shared_ptr<Scene>& scene)
{
	// Keep one degenerate mesh/draw so GPU buffer uploads stay valid while rendering an "empty" scene.
	scene->geometry.vertices.resize(1);
	scene->geometry.indices = { 0, 0, 0 };

	Meshlet meshlet{};
	meshlet.dataOffset = 0;
	meshlet.baseVertex = 0;
	meshlet.vertexCount = 1;
	meshlet.triangleCount = 1;
	meshlet.shortRefs = 1;
	scene->geometry.meshlets.push_back(meshlet);
	scene->geometry.meshletdata = { 0, 0 };
	scene->geometry.meshletvtx0 = { 0 };

	Mesh mesh{};
	mesh.center = vec3(0.f);
	mesh.radius = 0.f;
	mesh.vertexOffset = 0;
	mesh.vertexCount = 1;
	mesh.lodCount = 1;
	mesh.lods[0].indexOffset = 0;
	mesh.lods[0].indexCount = 3;
	mesh.lods[0].meshletOffset = 0;
	mesh.lods[0].meshletCount = 1;
	scene->geometry.meshes.push_back(mesh);

	MeshDraw draw{};
	draw.position = vec3(0.f);
	draw.scale = 0.f;
	draw.orientation = quat(1.f, 0.f, 0.f, 0.f);
	draw.meshIndex = 0;
	draw.materialIndex = 0;
	scene->draws.push_back(draw);
}

bool BuildSceneContentFromConfig(const KaleidoLaunchConfig& config, const std::shared_ptr<Scene>& targetScene, VulkanContext* vContext)
{
	// material index 0 is always dummy
	targetScene->materialDb.Clear();
	targetScene->materialDb.Add(std::make_unique<PBRMaterial>(PBRMaterial::CreateDefault()));

	targetScene->camera.position = { 14.5f, 3.f, 10.f };
	targetScene->camera.orientation = glm::radians(glm::vec3(-5.f, -220.f, 0.f));
	targetScene->camera.fovY = glm::radians(70.f);
	targetScene->camera.znear = 0.1f;
	targetScene->sunDirection = normalize(vec3(1.0f, 1.0f, 1.0f));

	const bool allowEditorEmptyScene = config.hostOptions.launchMode == RuntimeLaunchMode::EditorViewport &&
		config.modelPath.empty() &&
		config.meshPaths.empty();
	bool sceneMode = false;
	bool fastMode = getenv("FAST") && atoi(getenv("FAST"));
	clusterRTEnabled = getenv("CLRT") && atoi(getenv("CLRT"));

	if (config.loadSingleModel)
	{
		const char* ext = strrchr(config.modelPath.c_str(), '.');
		if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
		{
			glm::vec3 euler(0.f);
			if (!loadScene(targetScene->geometry, targetScene->materialDb, targetScene->draws, targetScene->texturePaths, targetScene->animations, targetScene->camera, targetScene->sunDirection, config.modelPath.c_str(), vContext->meshShadingSupported, euler, fastMode, clusterRTEnabled))
			{
				LOGE("Error: scene %s failed to load", config.modelPath.c_str());
				return false;
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

	for (size_t i = 0; i < targetScene->texturePaths.size(); ++i)
	{
		Image image;
		if (!loadImage(image, vContext->device, vContext->physicalDevice, vContext->commandPools[0], vContext->commandBuffers[0], vContext->queue, vContext->memoryProperties, vContext->scratch, targetScene->texturePaths[i].c_str()))
		{
			LOGE("Error: image %s failed to load", targetScene->texturePaths[i].c_str());
			return false;
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetImageMemoryRequirements(vContext->device, image.image, &memoryRequirements);
		imageMemory += memoryRequirements.size;
		targetScene->images.push_back(image);
	}

#if defined(WIN32)
	LOGI("Loaded %d textures (%.2f MB) in %.2f sec", int(targetScene->images.size()), double(imageMemory) / 1e6, glfwGetTime() - imageTimer);
#endif

	uint32_t descriptorCount = uint32_t(targetScene->texturePaths.size() + 1);
	targetScene->textureSet = createDescriptorArray(vContext->device, vContext->textureSetLayout, descriptorCount);

	for (size_t i = 0; i < targetScene->texturePaths.size(); ++i)
	{
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageView = targetScene->images[i].imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = targetScene->textureSet.second;
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
			if (!loadMesh(targetScene->geometry, meshPath.c_str(), vContext->meshShadingSupported, fastMode, clusterRTEnabled))
			{
				LOGE("Error: mesh %s failed to load", meshPath.c_str());
				return false;
			}
		}
	}
#endif

	if (allowEditorEmptyScene && targetScene->geometry.meshes.empty())
	{
		BootstrapEditorEmptyScene(targetScene);
		LOGI("Initialized editor empty scene placeholder.");
	}

	if (targetScene->geometry.meshes.empty())
	{
		LOGE("Error: no meshes loaded!");
		return false;
	}

	LOGI("Geometry: VB %.2f MB, IB %.2f MB, meshlets %.2f MB\n",
	    double(targetScene->geometry.vertices.size() * sizeof(Vertex)) / 1e6,
	    double(targetScene->geometry.indices.size() * sizeof(uint32_t)) / 1e6,
	    double(targetScene->geometry.meshlets.size() * sizeof(Meshlet) + targetScene->geometry.meshletdata.size() * sizeof(uint32_t)) / 1e6);

	if (targetScene->draws.empty())
	{
		rngstate.state = 0x42;

		uint32_t drawCount = 100'000;
		targetScene->draws.resize(drawCount);

		float sceneRadius = 150;

		for (uint32_t i = 0; i < drawCount; ++i)
		{
			MeshDraw& draw = targetScene->draws[i];

			size_t meshIndex = rand32() % targetScene->geometry.meshes.size();
			const Mesh& mesh = targetScene->geometry.meshes[meshIndex];

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

	targetScene->drawDistance = 2000.f;

	SortSceneDrawsByMaterialKey(*targetScene);

	targetScene->meshletVisibilityCount = 0;
	targetScene->meshPostPasses = 0;
	for (size_t i = 0; i < targetScene->draws.size(); ++i)
	{
		MeshDraw& draw = targetScene->draws[i];
		const Mesh& mesh = targetScene->geometry.meshes[draw.meshIndex];

		draw.meshletVisibilityOffset = targetScene->meshletVisibilityCount;

		uint32_t meshletCount = 0;
		for (uint32_t lod = 0; lod < mesh.lodCount; ++lod)
			meshletCount = std::max(meshletCount, mesh.lods[lod].meshletCount);

		targetScene->meshletVisibilityCount += meshletCount;
		targetScene->meshPostPasses |= 1 << draw.postPass;
	}

	RebuildMaterialDrawBatches(*targetScene);

	return true;
}
} // namespace

int KaleidoRuntime::Initialize(const KaleidoLaunchConfig& config)
{
	const bool allowEditorEmptyScene = config.hostOptions.launchMode == RuntimeLaunchMode::EditorViewport &&
		config.modelPath.empty() &&
		config.meshPaths.empty();
	if (config.modelPath.empty() && !allowEditorEmptyScene)
	{
		LOGE("modelPath is empty");
		return 1;
	}

	scene = std::make_shared<Scene>(config.path.c_str());
	auto vContext = VulkanContext::GetInstance();
	vContext->SetScene(scene);
	vContext->SetRuntimeUiEnabled(config.hostOptions.enableRuntimeUi);
	vContext->SetEditorViewportMode(config.hostOptions.launchMode == RuntimeLaunchMode::EditorViewport);

#if defined(WIN32)
	vContext->InitVulkan();
#elif defined(__ANDROID__)
	vContext->InitVulkan(config.nativeWindow);
#endif

	if (!BuildSceneContentFromConfig(config, scene, vContext.get()))
		return 1;

	vContext->InitResources();
	activeConfig = config;
	initialized = true;
	return 0;
}

bool KaleidoRuntime::RenderFrame()
{
	auto vContext = VulkanContext::GetInstance();
	if (!vContext->DrawFrame())
		return false;

	std::string requestedScenePath;
	if (vContext->ConsumeEditorSceneLoadRequest(requestedScenePath))
	{
		KaleidoLaunchConfig nextConfig = activeConfig;
		nextConfig.modelPath = requestedScenePath;
		nextConfig.meshPaths.clear();
		nextConfig.loadSingleModel = true;
		LOGI("Editor requested scene load: %s", requestedScenePath.c_str());

		VK_CHECK(vkDeviceWaitIdle(vContext->device));

		auto nextScene = std::make_shared<Scene>(nextConfig.path.c_str());
		if (!BuildSceneContentFromConfig(nextConfig, nextScene, vContext.get()))
		{
			LOGE("Failed to reload scene: %s", requestedScenePath.c_str());
			return true;
		}
		vContext->ResetSceneResourcesForReload();
		scene = nextScene;
		vContext->SetScene(scene);
		vContext->InitResources();
		activeConfig = nextConfig;
		LOGI("Scene reloaded in-place: %s", requestedScenePath.c_str());
	}

	return true;
}

void KaleidoRuntime::Shutdown()
{
	VulkanContext::DestroyInstance();
	initialized = false;
}

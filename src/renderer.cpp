#include "renderer.h"
#include "tools.h"

template <typename PushConstants, size_t PushDescriptors>
void dispatch(VkCommandBuffer commandBuffer, const Program& program, uint32_t threadCountX, uint32_t threadCountY, const PushConstants& pushConstants, const DescriptorInfo (&pushDescriptors)[PushDescriptors])
{
	assert(program.pushConstantSize == sizeof(pushConstants));
	assert(program.pushDescriptorCount == PushDescriptors);

	if (program.pushConstantStages)
		vkCmdPushConstants(commandBuffer, program.layout, program.pushConstantStages, 0, sizeof(pushConstants), &pushConstants);

#if defined(WIN32)
	if (program.pushDescriptorCount)
		vkCmdPushDescriptorSetWithTemplate(commandBuffer, program.updateTemplate, program.layout, 0, pushDescriptors);
#endif
	vkCmdDispatch(commandBuffer, getGroupCount(threadCountX, program.localSizeX), getGroupCount(threadCountY, program.localSizeY), 1);
}

VkSemaphore createSemaphore(VkDevice device)
{
	VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = 0;
	VK_CHECK(vkCreateSemaphore(device, &createInfo, 0, &semaphore));

	return semaphore;
}

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex)
{
	VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	createInfo.queueFamilyIndex = familyIndex;

	VkCommandPool commandPool = 0;
	VK_CHECK(vkCreateCommandPool(device, &createInfo, 0, &commandPool));

	return commandPool;
}

VkFence createFence(VkDevice device)
{
	VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

	VkFence fence = 0;
	VK_CHECK(vkCreateFence(device, &createInfo, 0, &fence));

	return fence;
}

VkQueryPool createQueryPool(VkDevice device, uint32_t queryCount, VkQueryType queryType)
{
	VkQueryPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	createInfo.queryType = queryType;
	createInfo.queryCount = queryCount;

	if (queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS)
	{
		createInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
	}

	VkQueryPool queryPool = 0;
	VK_CHECK(vkCreateQueryPool(device, &createInfo, 0, &queryPool));

	return queryPool;
}

// deprecated
float halfToFloat(uint16_t v)
{
	// This function is AI generated.
	int s = (v >> 15) & 0x1;
	int e = (v >> 10) & 0x1f;
	int f = v & 0x3ff;

	assert(e != 31);

	if (e == 0)
	{
		if (f == 0)
			return s ? -0.f : 0.f;
		while ((f & 0x400) == 0)
		{
			f <<= 1;
			e -= 1;
		}
		e += 1;
		f &= ~0x400;
	}
	else if (e == 31)
	{
		if (f == 0)
			return s ? -INFINITY : INFINITY;
		return f ? NAN : s ? -INFINITY
		                   : INFINITY;
	}
	e = e + (127 - 15);
	f = f << 13;
	union
	{
		uint32_t u;
		float f;
	} result;
	result.u = (s << 31) | (e << 23) | f;
	return result.f;
}

#if defined(WIN32)
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_M)
		{
			meshShadingEnabled = !meshShadingEnabled;
			return;
		}
		if (key == GLFW_KEY_C)
		{
			cullingEnabled = !cullingEnabled;
			return;
		}
		if (key == GLFW_KEY_O)
		{
			occlusionEnabled = !occlusionEnabled;
			return;
		}
		if (key == GLFW_KEY_K)
		{
			clusterOcclusionEnabled = !clusterOcclusionEnabled;
			return;
		}
		if (key == GLFW_KEY_L)
		{
			lodEnabled = !lodEnabled;
			return;
		}
		if (key == GLFW_KEY_T)
		{
			taskShadingEnabled = !taskShadingEnabled;
		}
		if (key == GLFW_KEY_S && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
		{
			shadowEnabled = !shadowEnabled;
			return;
		}
		if (key == GLFW_KEY_B)
		{
			shadowblurEnabled = !shadowblurEnabled;
		}
		if (key == GLFW_KEY_X)
		{
			shadowCheckerboard = !shadowCheckerboard;
		}
		if (key == GLFW_KEY_Q)
		{
			shadowQuality = 1 - shadowQuality;
		}
		if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
		{
			debugLodStep = key - GLFW_KEY_0;
			return;
		}
		if (key == GLFW_KEY_R)
		{
			reloadShaders = !reloadShaders;
			reloadShadersTimer = 0;
		}
		if (key == GLFW_KEY_G)
		{
			debugGuiMode++;
		}
		if (key == GLFW_KEY_SPACE)
		{
			animationEnabled = !animationEnabled;
		}
		if (key == GLFW_KEY_APOSTROPHE)
		{
			debugSleep = !debugSleep;
		}
	}
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (ImGui::GetIO().WantCaptureMouse)
		return;
	if (!mousePressed)
		return;

	static const float sensitivity = 0.1f;

	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
		return;
	}

	float xoffset = lastX - xpos;
	float yoffset = lastY - ypos;
	lastX = xpos;
	lastY = ypos;

	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	cameraDirty = true;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			mousePressed = true;
			firstMouse = true;
		}
		else if (action == GLFW_RELEASE)
		{
			mousePressed = false;
		}
	}
}
#endif

void updateCamera()
{
	glm::mat3 matPitch = {
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, cos(glm::radians(pitch)), -sin(glm::radians(pitch))),
		glm::vec3(0.0f, sin(glm::radians(pitch)), cos(glm::radians(pitch)))
	};
	matPitch = glm::transpose(matPitch);

	glm::mat3 matYaw = {
		glm::vec3(cos(glm::radians(yaw)), 0.0f, sin(glm::radians(yaw))),
		glm::vec3(0.0f, 1.0f, 0.0f),
		glm::vec3(-sin(glm::radians(yaw)), 0.0f, cos(glm::radians(yaw)))
	};
	matYaw = glm::transpose(matYaw);

	glm::mat3 matRoll = {
		glm::vec3(cos(glm::radians(roll)), -sin(glm::radians(roll)), 0.0f),
		glm::vec3(sin(glm::radians(roll)), cos(glm::radians(roll)), 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f)
	};
	matRoll = glm::transpose(matRoll);

	glm::vec3 front = matRoll * matYaw * matPitch * glm::vec3(0.0f, 0.0f, -1.0f);

	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
	auto sce = VulkanContext::GetInstance()->GetScene();
	sce->camera.orientation = glm::quatLookAt(front, up);
}

mat4 perspectiveProjection(float fovY, float aspectWbyH, float zNear)
{
	float f = 1.0f / tanf(fovY / 2.0f);
	return mat4(
	    f / aspectWbyH, 0.0f, 0.0f, 0.0f,
	    0.0f, f, 0.0f, 0.0f,
	    0.0f, 0.0f, 0.0f, -1.0f,
	    0.0f, 0.0f, zNear, 0.0f);
}

mat4 perspectiveProjectionDollyZoom(float fovY, float aspectWbyH, float zNear, float so, float soRef)
{
	float f = 1.0f / tanf(fovY / 2.0f);

	double halfWidth = zNear * tanf(fovY / 2.0);
	double focalLengthRef = 1.0 / (1.0 / soRef + 1.0 / zNear);
	double transVerseMag = zNear / soRef;
	double focalLength = transVerseMag * so / (transVerseMag + 1.0);

	zNear = 1.f / (1.f / focalLength - 1.f / so);

	f = f * focalLength / focalLengthRef;

	return mat4(
	    f / aspectWbyH, 0.0f, 0.0f, 0.0f,
	    0.0f, f, 0.0f, 0.0f,
	    0.0f, 0.0f, 0.0f, -1.0f,
	    0.0f, 0.0f, zNear, 0.0f);
}

vec4 normalizePlane(vec4 p)
{
	return p / length(vec3(p));
}

uint32_t previousPow2(uint32_t v)
{
	uint32_t r = 1;
	while (r * 2 < v)
		r *= 2;

	return r;
}

uint32_t pcg32_random_r(pcg32_random_t* rng)
{
	uint64_t oldstate = rng->state;
	// Advance internal state
	rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
	// Calculate output function (XSH RR), uses old state for max ILP
	uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
	uint32_t rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
}

double rand01()
{
	return pcg32_random_r(&rngstate) / double(1ull << 32);
}

uint32_t rand32()
{
	return pcg32_random_r(&rngstate);
}

const std::shared_ptr<VulkanContext>& VulkanContext::GetInstance()
{
	if (!gInstance)
	{
		gInstance = std::make_shared<VulkanContext>();
	}
	return gInstance;
}

#if defined(WIN32)
void VulkanContext::InitVulkan()
#elif defined(__ANDROID__)
void VulkanContext::InitVulkan(ANativeWindow* _window)
#endif
{
#if defined(WIN32)
	int rc = glfwInit();
	assert(rc);
#endif

	VK_CHECK(volkInitialize());

	instance = createInstance();
	if (!instance)
		return;

	volkLoadInstanceOnly(instance);

	debugCallback = registerDebugCallback(instance);

	VkPhysicalDevice physicalDevices[16];
	uint32_t physicalDeviceCount = sizeof(physicalDevices) / sizeof(physicalDevices[0]);
	auto result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
	VK_CHECK(result);

	physicalDevice = pickPhysicalDevice(physicalDevices, physicalDeviceCount);
	if (!physicalDevice)
	{
		if (debugCallback) 
			vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);
		vkDestroyInstance(instance, 0);
		return;
	}

	uint32_t extensionCount;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, 0));

	std::vector<VkExtensionProperties> extensions(extensionCount);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, extensions.data()));

	for (const auto& ext : extensions)
	{
		meshShadingSupported = meshShadingSupported || strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0;
		raytracingSupported = raytracingSupported || strcmp(ext.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
	#if defined(VK_NV_cluster_acceleration_structure)
		clusterrtSupported = clusterrtSupported || strcmp(ext.extensionName, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
	#endif
	}
#if defined(WIN32)
	pushDescriptorSupported = true;
#endif

	meshShadingEnabled = meshShadingSupported;

	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(graphicsFamily != VK_QUEUE_FAMILY_IGNORED);

	device = createDevice(instance, physicalDevice, graphicsFamily, pushDescriptorSupported, meshShadingEnabled, raytracingSupported, clusterrtSupported);
	assert(device);

	volkLoadDevice(device);

#if defined(WIN32)
	window = glfwCreateWindow(1024, 768, "kaleido", 0, 0);
	assert(window);

	glfwSetKeyCallback(window, keyCallback);
#elif defined(__ANDROID__)
	window = _window;
#endif

	surface = createSurface(instance, window);
	assert(surface);

	// Check if VkSurfaceKHR is supported in physical device.
	VkBool32 presentSupported = VK_FALSE;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsFamily, surface, &presentSupported));
	assert(presentSupported);

	swapchainFormat = getSwapchainFormat(physicalDevice, surface);
	depthFormat = VK_FORMAT_D32_SFLOAT;

	vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

	if (!pushDescriptorSupported)
	{
		// TODO: find a proper way for resource allocation, maybe using bindless resources for android
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 160 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 160 },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 160 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 160 },
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 10 }
		};

		VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 50;
		poolInfo.poolSizeCount = COUNTOF(poolSizes);
		poolInfo.pPoolSizes = poolSizes;

		VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, 0, &descriptorPool));
	}

	textureSampler = createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	assert(textureSampler);

	readSampler = createSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	assert(readSampler);

	depthSampler = createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_REDUCTION_MODE_MIN);
	assert(depthSampler);

	gbufferFormats[0] = VK_FORMAT_R8G8B8A8_UNORM;
	gbufferFormats[1] = VK_FORMAT_A2B10G10R10_UNORM_PACK32;

	gbufferInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	gbufferInfo.colorAttachmentCount = gbufferCount;
	gbufferInfo.pColorAttachmentFormats = gbufferFormats;
	gbufferInfo.depthAttachmentFormat = depthFormat;

	createSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

	textureSetLayout = createDescriptorArrayLayout(device);

#if defined(WIN32)
	bool rcs = loadShaders(shaderSet, scene->path.c_str(), "shaders/");
#elif defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
	bool rcs = loadShaders(shaderSet, device, scene->path.c_str(), "shaders/");
#endif
	assert(rcs);

	debugtextProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["debugtext.comp"] }, sizeof(TextData), pushDescriptorSupported, descriptorPool);

	drawcullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["drawcull.comp"] }, sizeof(CullData), pushDescriptorSupported, descriptorPool);

	tasksubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["tasksubmit.comp"] }, 0, pushDescriptorSupported, descriptorPool);

	clustersubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["clustersubmit.comp"] }, 0, pushDescriptorSupported, descriptorPool);

	clustercullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["clustercull.comp"] }, sizeof(CullData), pushDescriptorSupported, descriptorPool);

	depthreduceProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["depthreduce.comp"] }, sizeof(vec4), pushDescriptorSupported, descriptorPool);

	meshProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["mesh.vert"], &shaderSet["mesh.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

	meshtaskProgram = {};
	clusterProgram = {};
	if (meshShadingEnabled)
	{
		meshtaskProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["meshlet.task"], &shaderSet["meshlet.mesh"], &shaderSet["mesh.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

		clusterProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["meshlet.mesh"], &shaderSet["mesh.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);
	}

	finalProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["final.comp"] }, sizeof(ShadeData), pushDescriptorSupported, descriptorPool);
	shadowProgram = {};
	shadowblurProgram = {};
	if (raytracingSupported)
	{
		shadowProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["shadow.comp"] }, sizeof(ShadowData), pushDescriptorSupported, descriptorPool, textureSetLayout);
		shadowfillProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["shadowfill.comp"] }, sizeof(vec4), pushDescriptorSupported, descriptorPool);
		shadowblurProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["shadowblur.comp"] }, sizeof(vec4), pushDescriptorSupported, descriptorPool);
	}

	pipelinesReloadedCallback = [&]()
	{
		auto replace = [&](VkPipeline& pipeline, VkPipeline newPipeline)
		{
			if (pipeline)
				vkDestroyPipeline(device, pipeline, 0);
			assert(newPipeline);
			pipeline = newPipeline;
			pipelines.emplace_back(newPipeline);
		};

		pipelines.clear();

		replace(debugtextPipeline, createComputePipeline(device, pipelineCache, debugtextProgram));
		replace(drawcullPipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE } }));
		replace(drawculllatePipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_FALSE } }));
		replace(taskcullPipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_TRUE } }));
		replace(taskculllatePipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_TRUE }, {/* TASK= */ VK_TRUE } }));

		replace(tasksubmitPipeline, createComputePipeline(device, pipelineCache, tasksubmitProgram));
		replace(clustersubmitPipeline, createComputePipeline(device, pipelineCache, clustersubmitProgram));
		replace(clustercullPipeline, createComputePipeline(device, pipelineCache, clustercullProgram, { { /* LATE= */ VK_FALSE } }));
		replace(clusterculllatePipeline, createComputePipeline(device, pipelineCache, clustercullProgram, { { /* LATE= */ VK_TRUE } }));
		replace(depthreducePipeline, createComputePipeline(device, pipelineCache, depthreduceProgram));
		replace(meshPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram));
		replace(meshpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE }, { /* POST= */ VK_TRUE } }));

		if (meshShadingSupported)
		{
			replace(meshtaskPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_TRUE } }));
			replace(meshtasklatePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_TRUE }, { /* POST= */ VK_FALSE } }));
			replace(meshtaskpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_TRUE }, { /* POST= */ VK_TRUE } }));
			replace(clusterPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram));
			replace(clusterpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE }, { /* POST= */ VK_TRUE } }));
		}

		replace(finalPipeline, createComputePipeline(device, pipelineCache, finalProgram));
		if (raytracingSupported)
		{
			replace(shadowlqPipeline, createComputePipeline(device, pipelineCache, shadowProgram, { { /* QUALITY= */ int32_t(0) } }));
			replace(shadowhqPipeline, createComputePipeline(device, pipelineCache, shadowProgram, { { /* QUALITY= */ int32_t(1) } }));
			replace(shadowfillPipeline, createComputePipeline(device, pipelineCache, shadowfillProgram));
			replace(shadowblurPipeline, createComputePipeline(device, pipelineCache, shadowblurProgram));
		}
	};

	pipelinesReloadedCallback();

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		queryPoolsTimestamp[ii] = createQueryPool(device, 128, VK_QUERY_TYPE_TIMESTAMP);
		assert(queryPoolsTimestamp[ii]);
	}
#if defined(WIN32)
	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		queryPoolsPipeline[ii] = createQueryPool(device, 4, VK_QUERY_TYPE_PIPELINE_STATISTICS);
		assert(queryPoolsPipeline[ii]);
	}
#endif

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		commandPools[ii] = createCommandPool(device, graphicsFamily);
		assert(commandPools[ii]);
		VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocateInfo.commandPool = commandPools[ii];
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffers[ii]));
	}

	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	createBuffer(scratch, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void VulkanContext::SetScene(const std::shared_ptr<Scene>& _scene)
{
	scene = _scene;
}

const std::shared_ptr<Scene>& VulkanContext::GetScene() const noexcept
{
	return scene;
}

void VulkanContext::InitResources()
{
	meshletVisibilityBytes = (scene->meshletVisibilityCount + 31) / 32 * sizeof(uint32_t);

	uint32_t raytracingBufferFlags =
	    raytracingSupported
	        ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	        : 0;

	createBuffer(mb, device, memoryProperties, scene->geometry.meshes.size() * sizeof(Mesh), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	createBuffer(mtb, device, memoryProperties, scene->materials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	createBuffer(vb, device, memoryProperties, scene->geometry.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	createBuffer(ib, device, memoryProperties, scene->geometry.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (meshShadingEnabled)
	{
		createBuffer(mlb, device, memoryProperties, scene->geometry.meshlets.size() * sizeof(Meshlet), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(mdb, device, memoryProperties, scene->geometry.meshletdata.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	VkCommandPool initCommandPool = commandPools[0];
	VkCommandBuffer initCommandBuffer = commandBuffers[0];

	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mb, scratch, scene->geometry.meshes.data(), scene->geometry.meshes.size() * sizeof(Mesh));
	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mtb, scratch, scene->materials.data(), scene->materials.size() * sizeof(Material));
	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, vb, scratch, scene->geometry.vertices.data(), scene->geometry.vertices.size() * sizeof(Vertex));
	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, ib, scratch, scene->geometry.indices.data(), scene->geometry.indices.size() * sizeof(uint32_t));

	if (meshShadingEnabled)
	{
		uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mlb, scratch, scene->geometry.meshlets.data(), scene->geometry.meshlets.size() * sizeof(Meshlet));
		uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mdb, scratch, scene->geometry.meshletdata.data(), scene->geometry.meshletdata.size() * sizeof(uint32_t));
	}

	createBuffer(db, device, memoryProperties, scene->draws.size() * sizeof(MeshDraw), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	createBuffer(dvb, device, memoryProperties, scene->draws.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	createBuffer(dcb, device, memoryProperties, TASK_WGLIMIT * sizeof(MeshTaskCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	createBuffer(dccb, device, memoryProperties, 16, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// TODO: there's a way to implement cluster visibility persistence *without* using bitwise storage at all, which may be beneficial on the balance, so we should try that.
	// *if* we do that, we can drop meshletVisibilityOffset et al from everywhere
	if (meshShadingSupported)
	{
		createBuffer(mvb, device, memoryProperties, meshletVisibilityBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	if (meshShadingSupported)
	{
		createBuffer(cib, device, memoryProperties, CLUSTER_LIMIT * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(ccb, device, memoryProperties, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, db, scratch, scene->draws.data(), scene->draws.size() * sizeof(MeshDraw));

	if (raytracingSupported)
	{
		if (clusterrtSupported && clusterRTEnabled)
		{
			Buffer vxb = {};
			createBuffer(vxb, device, memoryProperties, scene->geometry.meshletvtx0.size() * sizeof(uint16_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			memcpy(vxb.data, scene->geometry.meshletvtx0.data(), scene->geometry.meshletvtx0.size() * sizeof(uint16_t));

			buildCBLAS(device, scene->geometry.meshes, scene->geometry.meshlets, vxb, mdb, blas, blasBuffer, initCommandPool, initCommandBuffer, queue, memoryProperties);

			destroyBuffer(vxb, device);
		}
		else
		{
			std::vector<VkDeviceSize> compactedSizes;
			buildBLAS(device, scene->geometry.meshes, vb, ib, blas, compactedSizes, blasBuffer, initCommandPool, initCommandBuffer, queue, memoryProperties);
			compactBLAS(device, blas, compactedSizes, blasBuffer, initCommandPool, initCommandBuffer, queue, memoryProperties);
		}

		blasAddresses.resize(blas.size());

		for (size_t i = 0; i < blas.size(); ++i)
		{
			VkAccelerationStructureDeviceAddressInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
			info.accelerationStructure = blas[i];

			blasAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR(device, &info);
		}
		createBuffer(tlasInstanceBuffer, device, memoryProperties, sizeof(VkAccelerationStructureInstanceKHR) * scene->draws.size(), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		for (size_t i = 0; i < scene->draws.size(); ++i)
		{
			const MeshDraw& draw = scene->draws[i];
			assert(draw.meshIndex < blas.size());

			VkAccelerationStructureInstanceKHR instance = {};
			fillInstanceRT(instance, draw, uint32_t(i), blasAddresses[draw.meshIndex]);

			memcpy(static_cast<VkAccelerationStructureInstanceKHR*>(tlasInstanceBuffer.data) + i, &instance, sizeof(VkAccelerationStructureInstanceKHR));
		}
		tlas = createTLAS(device, tlasBuffer, tlasScratchBuffer, tlasInstanceBuffer, scene->draws.size(), memoryProperties);
		LOGI("Ray Tracing is supported!");
	}
	else
		LOGW("Ray Tracing is not supported, this may cause artifacts!");

	// Make sure we don't accidentally reuse the init command pool because that would require extra synchronization
	initCommandPool = VK_NULL_HANDLE;
	initCommandBuffer = VK_NULL_HANDLE;

	swapchainImageViews.resize(swapchain.imageCount);

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		acquireSemaphores[ii] = createSemaphore(device);
		releaseSemaphores[ii] = createSemaphore(device);
		frameFences[ii] = createFence(device);
		assert(acquireSemaphores[ii] && releaseSemaphores[ii] && frameFences[ii]);
	}

#if defined(WIN32)
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
#endif

	// Initialize GUI renderer
	const auto& guiRenderer = GuiRenderer::GetInstance();
	VkSurfaceCapabilitiesKHR surfaceCaps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);
	uint32_t imageCount = std::max(uint32_t(MAX_FRAMES), surfaceCaps.minImageCount); // using triple buffering

	if (!pushDescriptorSupported)
	{
		// cull descriptor sets
		{
			drawcullSets.resize(3); // early, late, post
			std::vector<VkDescriptorSetLayout> layouts(drawcullSets.size(), drawcullProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = uint32_t(drawcullSets.size());
			allocInfo.pSetLayouts = layouts.data();

			auto ret = vkAllocateDescriptorSets(device, &allocInfo, drawcullSets.data());
			VK_CHECK(ret);
		}
		{
			tasksubmitSets.resize(3); // early, late, post
			std::vector<VkDescriptorSetLayout> layouts(tasksubmitSets.size(), tasksubmitProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = uint32_t(tasksubmitSets.size());
			allocInfo.pSetLayouts = layouts.data();

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, tasksubmitSets.data()));
		}

		// render descriptor sets
		{
			clustercullSets.resize(3);
			std::vector<VkDescriptorSetLayout> layouts(clustercullSets.size(), clustercullProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = uint32_t(clustercullSets.size());
			allocInfo.pSetLayouts = layouts.data();

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, clustercullSets.data()));
		}
		{
			clustersubmitSets.resize(3);
			std::vector<VkDescriptorSetLayout> layouts(clustersubmitSets.size(), clustersubmitProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = uint32_t(clustersubmitSets.size());
			allocInfo.pSetLayouts = layouts.data();

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, clustersubmitSets.data()));
		}
		if (meshShadingEnabled)
		{
			{
				meshtaskSets.resize(3);
				std::vector<VkDescriptorSetLayout> layouts(meshtaskSets.size(), meshtaskProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = uint32_t(meshtaskSets.size());
				allocInfo.pSetLayouts = layouts.data();

				auto ret = vkAllocateDescriptorSets(device, &allocInfo, meshtaskSets.data());
				VK_CHECK(ret);
			}
			{
				clusterSets.resize(3);
				std::vector<VkDescriptorSetLayout> layouts(clusterSets.size(), clusterProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = uint32_t(clusterSets.size());
				allocInfo.pSetLayouts = layouts.data();

				auto ret = vkAllocateDescriptorSets(device, &allocInfo, clusterSets.data());
				VK_CHECK(ret);
			}
		}

		{
			meshSets.resize(3);
			std::vector<VkDescriptorSetLayout> layouts(meshSets.size(), meshProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = uint32_t(meshSets.size());
			allocInfo.pSetLayouts = layouts.data();

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, meshSets.data()));
		}

		// shadow blur sets
		{
			shadowblurSets.resize(3);
			std::vector<VkDescriptorSetLayout> layouts(shadowblurSets.size(), shadowblurProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = uint32_t(shadowblurSets.size());
			allocInfo.pSetLayouts = layouts.data();

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, shadowblurSets.data()));
		}
	}

	VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &swapchainFormat;
	renderingInfo.depthAttachmentFormat = depthFormat;

	guiRenderer->Initialize(window, API_VERSION, instance, physicalDevice, device, graphicsFamily, queue, renderingInfo, swapchainFormat, imageCount);

	lastFrame = GetTimeInSeconds();
}

bool VulkanContext::DrawFrame()
{
	static uint64_t frameIndex = 0;
	double frameCPUBegin = GetTimeInSeconds();

	const auto& guiRenderer = GuiRenderer::GetInstance();
#if defined(WIN32)
	glfwPollEvents();
#endif
	guiRenderer->BeginFrame();
	ImVec2 windowSize = ImVec2(800, 600);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

	// update camera position
	float currentFrame = GetTimeInSeconds();
	float deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;

	glm::vec3 front = scene->camera.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 right = scene->camera.orientation * glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 up = glm::cross(right, front);

#if defined(WIN32)
	float velocity = cameraSpeed * deltaTime;

	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) != GLFW_PRESS)
	{
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
			scene->camera.position -= front * velocity;
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
			scene->camera.position += front * velocity;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
			scene->camera.position -= right * velocity;
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
			scene->camera.position += right * velocity;
	}

	glfwPollEvents();
#endif

#if defined(WIN32)
	if (reloadShaders && glfwGetTime() >= reloadShadersTimer)
	{
		bool changed = false;

		for (Shader& shader : shaderSet.shaders)
		{
		#if defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
			if (shader.module)
				vkDestroyShaderModule(device, shader.module, 0);
		#endif

			std::vector<char> oldSpirv = std::move(shader.spirv);

			std::string spirvPath = "/shaders/" + shader.name + ".spv";
		#if defined(WIN32)
			bool rcs = loadShader(shader, scene->path.c_str(), spirvPath.c_str());
		#elif defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
			bool rcs = loadShader(shader, device, scene->path.c_str(), spirvPath.c_str());
		#endif
			assert(rcs);

			changed |= oldSpirv != shader.spirv;
		}

		if (changed)
		{
			VK_CHECK(vkDeviceWaitIdle(device));
			pipelinesReloadedCallback();
			reloadShadersColor = 0x00ff00;
		}
		else
		{
			reloadShadersColor = 0xffffff;
		}

		reloadShadersTimer = glfwGetTime() + 1;
	}
#endif

	if (cameraDirty)
	{
		updateCamera();
		cameraDirty = false;
	}

	SwapchainStatus swapchainStatus = updateSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

	if (swapchainStatus == Swapchain_NotReady)
		return true;

	if (swapchainStatus == Swapchain_Resized || !depthTarget.image)
	{
		for (Image& image : gbufferTargets)
			if (image.image)
				destroyImage(image, device);
		if (depthTarget.image)
			destroyImage(depthTarget, device);

		if (depthPyramid.image)
		{
			for (uint32_t i = 0; i < depthPyramidLevels; ++i)
				vkDestroyImageView(device, depthPyramidMips[i], 0);
			destroyImage(depthPyramid, device);
		}

		if (shadowTarget.image)
			destroyImage(shadowTarget, device);
		if (shadowblurTarget.image)
			destroyImage(shadowblurTarget, device);

		for (uint32_t i = 0; i < gbufferCount; ++i)
			createImage(gbufferTargets[i], device, memoryProperties, swapchain.width, swapchain.height, 1, gbufferFormats[i], VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		createImage(depthTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		createImage(shadowTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		createImage(shadowblurTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		// Note: previousPow2 makes sure all reductions are at most by 2x2 which makes sure they are consertive
		depthPyramidWidth = previousPow2(swapchain.width);
		depthPyramidHeight = previousPow2(swapchain.height);
		depthPyramidLevels = getImageMipLevels(depthPyramidWidth, depthPyramidHeight);

		createImage(depthPyramid, device, memoryProperties, depthPyramidWidth, depthPyramidHeight, depthPyramidLevels, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
		{
			depthPyramidMips[i] = createImageView(device, depthPyramid.image, VK_FORMAT_R32_SFLOAT, i, 1);
			assert(depthPyramidMips[i]);
		}

		for (uint32_t i = 0; i < swapchain.imageCount; ++i)
		{
			if (swapchainImageViews[i])
				vkDestroyImageView(device, swapchainImageViews[i], 0);

			swapchainImageViews[i] = createImageView(device, swapchain.images[i], swapchainFormat, 0, 1);
		}

		if (!pushDescriptorSupported)
		{
			if (!depthreduceSets.empty())
				vkFreeDescriptorSets(device, descriptorPool, uint32_t(depthreduceSets.size()), depthreduceSets.data());

			depthreduceSets.resize(depthPyramidLevels);
			std::vector<VkDescriptorSetLayout> layouts(depthPyramidLevels, depthreduceProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = depthPyramidLevels;
			allocInfo.pSetLayouts = layouts.data();

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, depthreduceSets.data()));
		}
	}

	// TODO: this code races the GPU reading the transforms from both TLAS and draw buffers, which can cause rendering issues
	if (animationEnabled)
	{
		static double animationTime = 0.0; // TODO: handle overflow when the program last for long time
		animationTime += deltaTime;

		for (Animation& animation : scene->animations)
		{
			double index = (animationTime - animation.startTime) / animation.period;

			if (index < 0)
				continue;

			index = fmod(index, double(animation.keyframes.size()));

			int index0 = int(index) % animation.keyframes.size();
			int index1 = (index0 + 1) % animation.keyframes.size();

			double t = index - floor(index);

			const Keyframe& keyframe0 = animation.keyframes[index0];
			const Keyframe& keyframe1 = animation.keyframes[index1];

			MeshDraw& draw = scene->draws[animation.drawIndex];
			draw.position = glm::mix(keyframe0.translation, keyframe1.translation, float(t));
			draw.scale = glm::mix(keyframe0.scale, keyframe1.scale, float(t));
			draw.orientation = glm::slerp(keyframe0.rotation, keyframe1.rotation, float(t));

			MeshDraw& gpuDraw = static_cast<MeshDraw*>(db.data)[animation.drawIndex];
			memcpy(&gpuDraw, &draw, sizeof(draw));

			if (raytracingSupported)
			{
				VkAccelerationStructureInstanceKHR instance = {};
				fillInstanceRT(instance, draw, uint32_t(animation.drawIndex), blasAddresses[draw.meshIndex]);

				memcpy(static_cast<VkAccelerationStructureInstanceKHR*>(tlasInstanceBuffer.data) + animation.drawIndex, &instance, sizeof(VkAccelerationStructureInstanceKHR));
			}
		}
	}

	VkCommandPool commandPool = commandPools[frameIndex % MAX_FRAMES];
	VkCommandBuffer commandBuffer = commandBuffers[frameIndex % MAX_FRAMES];
	VkSemaphore acquireSemaphore = acquireSemaphores[frameIndex % MAX_FRAMES];
	VkSemaphore releaseSemaphore = releaseSemaphores[frameIndex % MAX_FRAMES];
	VkFence frameFence = frameFences[frameIndex % MAX_FRAMES];
	VkQueryPool queryPoolTimestamp = queryPoolsTimestamp[frameIndex % MAX_FRAMES];
#if defined(WIN32)
	VkQueryPool queryPoolPipeline = queryPoolsPipeline[frameIndex % MAX_FRAMES];
#endif

	uint32_t imageIndex = 0;
	VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain.swapchain, ~0ull, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
		return true; // attempting to render to an out-of-date swapchain would break semaphore synchronization
	VK_CHECK_SWAPCHAIN(acquireResult);

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	vkCmdResetQueryPool(commandBuffer, queryPoolTimestamp, 0, 128);
	vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 0);

	if (!dvbCleared)
	{
		// TODO: this is stupidly redundant
		vkCmdFillBuffer(commandBuffer, dvb.buffer, 0, sizeof(uint32_t) * scene->draws.size(), 0);
		VkBufferMemoryBarrier2 fillBarrier = bufferBarrier(dvb.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			#if defined(WIN32)
			| VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT
			#endif
			, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		pipelineBarrier(commandBuffer, 0, 1, &fillBarrier, 0, nullptr);

		dvbCleared = true;
	}

	if (!mvbCleared && meshShadingSupported)
	{
		// TODO: this is stupidly redundant
		vkCmdFillBuffer(commandBuffer, mvb.buffer, 0, meshletVisibilityBytes, 0);

		VkBufferMemoryBarrier2 fillBarrier = bufferBarrier(mvb.buffer,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		    VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		pipelineBarrier(commandBuffer, 0, 1, &fillBarrier, 0, nullptr);

		mvbCleared = true;
	}

	if (raytracingSupported)
	{
		uint32_t timestamp = 21;

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);

		if (tlasNeedsRebuild)
		{
			buildTLAS(device, commandBuffer, tlas, tlasBuffer, tlasScratchBuffer, tlasInstanceBuffer, scene->draws.size(), VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR);
			tlasNeedsRebuild = false;
		}
		else if (animationEnabled)
			buildTLAS(device, commandBuffer, tlas, tlasBuffer, tlasScratchBuffer, tlasInstanceBuffer, scene->draws.size(), VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
	}

	mat4 view = glm::mat4_cast(scene->camera.orientation);
	// view[3] = vec4(-camera.position, 1.0f);
	// view = inverse(view);
	//  view = glm::scale(glm::identity<glm::mat4>(), vec3(1, 1, 1)) * view;
	view = glm::lookAt(scene->camera.position, scene->camera.position + front, up);

	mat4 projection;
	if (enableDollyZoom)
	{
		float so = soRef - glm::abs(glm::dot(glm::normalize(front), scene->camera.position - cameraOriginForDolly));
		projection = perspectiveProjectionDollyZoom(scene->camera.fovY, float(swapchain.width) / float(swapchain.height), scene->camera.znear, so, soRef);
	}
	else
	{
		projection = perspectiveProjection(scene->camera.fovY, float(swapchain.width) / float(swapchain.height), scene->camera.znear);
	}
	mat4 projectionT = transpose(projection);

	vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
	vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

	CullData cullData = {};
	cullData.view = view;
	cullData.P00 = projection[0][0];
	cullData.P11 = projection[1][1];
	cullData.znear = scene->camera.znear;
	cullData.zfar = scene->drawDistance;
	cullData.frustum[0] = frustumX.x;
	cullData.frustum[1] = frustumX.z;
	cullData.frustum[2] = frustumY.y;
	cullData.frustum[3] = frustumY.z;
	cullData.drawCount = uint32_t(scene->draws.size());
	cullData.cullingEnabled = int(cullingEnabled);
	cullData.lodEnabled = int(lodEnabled);
	cullData.occlusionEnabled = int(occlusionEnabled);
	cullData.lodTarget = (2 / cullData.P11) * (1.f / float(swapchain.height)) * (1 << debugLodStep); // 1px
	cullData.pyramidWidth = float(depthPyramidWidth);
	cullData.clusterOcclusionEnabled = occlusionEnabled && clusterOcclusionEnabled && meshShadingSupported && meshShadingEnabled;

	Globals globals = {};
	globals.projection = projection;
	globals.cullData = cullData;

	globals.screenWidth = float(swapchain.width);
	globals.screenHeight = float(swapchain.height);

	bool taskSubmit = meshShadingSupported && meshShadingEnabled; // TODO; refactor this to be false when taskShadingEnabled is false
	bool clusterSubmit = meshShadingSupported && meshShadingEnabled && !taskShadingEnabled;

	auto fullbarrier = [&]()
	{
		VkMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependencyInfo.memoryBarrierCount = 1;
		dependencyInfo.pMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
	};

	auto cull = [&](VkPipeline pipeline, uint32_t timestamp, const char* phase, bool late, unsigned int postPass = 0)
	{
		size_t descriptorSetIndex = late ? (postPass > 0 ? 2 : 1) : 0;
		uint32_t rasterizationStage =
		    taskSubmit
		        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			#if defined(WIN32)
				| VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT
			#endif
		        : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

		VkBufferMemoryBarrier2 prefillBarrier = bufferBarrier(dccb.buffer,
		    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		pipelineBarrier(commandBuffer, 0, 1, &prefillBarrier, 0, nullptr);

		vkCmdFillBuffer(commandBuffer, dccb.buffer, 0, 4, 0);

		// pyramid barrier is tricky: our frame sequence is cull -> render -> pyramid -> cull -> render
		// the first cull (late=0) doesn't read pyramid data BUT the read in the shader is guarded by a push constant value (which could be specialization constant but isn't due to AMD bug)
		// the second cull (late=1) does read pyramid data that was written in the pyramid stage
		// as such, second cull needs to transition GENERAL->GENERAL with a COMPUTE->COMPUTE barrier, but the first cull needs to have a dummy transition because pyramid starts in UNDEFINED state on first frame
		VkImageMemoryBarrier2 pyramidBarrier = imageBarrier(depthPyramid.image,
		    late ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0, late ? VK_ACCESS_SHADER_WRITE_BIT : 0, late ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

		VkBufferMemoryBarrier2 fillBarriers[] = {
			bufferBarrier(dcb.buffer,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | rasterizationStage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT),
			bufferBarrier(dccb.buffer,
			    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
		};

		pipelineBarrier(commandBuffer, 0, COUNTOF(fillBarriers), fillBarriers, 1, &pyramidBarrier);

		{
			CullData passData = cullData;
			passData.clusterBackfaceEnabled = postPass == 0;
			passData.postPass = postPass;
			vkCmdBindPipeline(commandBuffer, drawcullProgram.bindPoint, pipeline);

			DescriptorInfo pyramidDesc{ depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL };
			DescriptorInfo descriptors[] = { db.buffer, mb.buffer, dcb.buffer, dccb.buffer, dvb.buffer, pyramidDesc };

			if (pushDescriptorSupported)
			{
				dispatch(commandBuffer, drawcullProgram, uint32_t(scene->draws.size()), 1, passData, descriptors);
			}
			else
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, drawcullSets[descriptorSetIndex], drawcullProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, drawcullProgram.bindPoint, drawcullProgram.layout, 0, 1, &drawcullSets[descriptorSetIndex], 0, nullptr);
				vkCmdPushConstants(commandBuffer, drawcullProgram.layout, drawcullProgram.pushConstantStages, 0, sizeof(cullData), &passData);
				vkCmdDispatch(commandBuffer, getGroupCount(uint32_t(scene->draws.size()), drawcullProgram.localSizeX), 1, 1);
			}
		}

		if (taskSubmit)
		{
			VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(dccb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

			vkCmdBindPipeline(commandBuffer, tasksubmitProgram.bindPoint, tasksubmitPipeline);

			DescriptorInfo descriptors[] = { dccb.buffer, dcb.buffer };
#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, tasksubmitProgram.updateTemplate, tasksubmitProgram.layout, 0, descriptors);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, tasksubmitSets[descriptorSetIndex], tasksubmitProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, tasksubmitProgram.bindPoint, tasksubmitProgram.layout, 0, 1, &tasksubmitSets[descriptorSetIndex], 0, nullptr);
			}
			vkCmdDispatch(commandBuffer, 1, 1, 1);
		}

		VkBufferMemoryBarrier2 cullBarriers[] = {
			bufferBarrier(dcb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | rasterizationStage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT),
			bufferBarrier(dccb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
		};

		pipelineBarrier(commandBuffer, 0, COUNTOF(cullBarriers), cullBarriers, 0, nullptr);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
	};

	auto render = [&](bool late, const std::vector<VkClearColorValue>& clearColors, const VkClearDepthStencilValue& depthClear, uint32_t query, uint32_t timestamp, const char* phase, unsigned int postPass = 0)
	{
		size_t descriptorSetIndex = late ? (postPass > 0 ? 2 : 1) : 0;
		assert(clearColors.size() == gbufferCount);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

#if defined(WIN32)
		vkCmdBeginQuery(commandBuffer, queryPoolPipeline, query, 0);
#endif

		if (clusterSubmit)
		{
			VkBufferMemoryBarrier2 prefillBarrier = bufferBarrier(ccb.buffer,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			pipelineBarrier(commandBuffer, 0, 1, &prefillBarrier, 0, nullptr);

			vkCmdFillBuffer(commandBuffer, ccb.buffer, 0, 4, 0);

			VkBufferMemoryBarrier2 fillBarriers[] = {
				bufferBarrier(cib.buffer,
				    VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT),
				bufferBarrier(ccb.buffer,
				    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
			};
			pipelineBarrier(commandBuffer, 0, COUNTOF(fillBarriers), fillBarriers, 0, nullptr);

			vkCmdBindPipeline(commandBuffer, clustercullProgram.bindPoint, late ? clusterculllatePipeline : clustercullPipeline);

			DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mlb.buffer, mvb.buffer, pyramidDesc, cib.buffer, ccb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, clustercullProgram.updateTemplate, clustercullProgram.layout, 0, descriptors);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, clustercullSets[descriptorSetIndex], clustercullProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, clustercullProgram.bindPoint, clustercullProgram.layout, 0, 1, &clustercullSets[descriptorSetIndex], 0, nullptr);
			}

			CullData passData = cullData;
			passData.postPass = postPass;

			vkCmdPushConstants(commandBuffer, clustercullProgram.layout, clustercullProgram.pushConstantStages, 0, sizeof(cullData), &passData);
			vkCmdDispatchIndirect(commandBuffer, dccb.buffer, 4);

			VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(ccb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

			vkCmdBindPipeline(commandBuffer, clustersubmitProgram.bindPoint, clustersubmitPipeline);

			DescriptorInfo descriptors2[] = { ccb.buffer, cib.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, clustersubmitProgram.updateTemplate, clustersubmitProgram.layout, 0, descriptors2);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, clustersubmitSets[descriptorSetIndex], clustersubmitProgram.updateTemplate, descriptors2);
				vkCmdBindDescriptorSets(commandBuffer, clustersubmitProgram.bindPoint, clustersubmitProgram.layout, 0, 1, &clustersubmitSets[descriptorSetIndex], 0, nullptr);
			}

			vkCmdDispatch(commandBuffer, 1, 1, 1);

			VkBufferMemoryBarrier2 cullBarriers[] = {
				bufferBarrier(cib.buffer,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				    VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT),
				bufferBarrier(ccb.buffer,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
			};

			pipelineBarrier(commandBuffer, 0, COUNTOF(cullBarriers), cullBarriers, 0, nullptr);
		}

		VkRenderingAttachmentInfo gbufferAttachments[gbufferCount] = {};
		for (uint32_t i = 0; i < gbufferCount; ++i)
		{
			gbufferAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			gbufferAttachments[i].imageView = gbufferTargets[i].imageView;
			gbufferAttachments[i].imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			gbufferAttachments[i].loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
			gbufferAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			gbufferAttachments[i].clearValue.color = clearColors[i];
		}

		VkRenderingAttachmentInfo depthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttachment.imageView = depthTarget.imageView;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachment.clearValue.depthStencil = depthClear;

		VkRenderingInfo passInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		passInfo.renderArea.extent.width = swapchain.width;
		passInfo.renderArea.extent.height = swapchain.height;
		passInfo.layerCount = 1;
		passInfo.colorAttachmentCount = gbufferCount;
		passInfo.pColorAttachments = gbufferAttachments;
		passInfo.pDepthAttachment = &depthAttachment;

		vkCmdBeginRendering(commandBuffer, &passInfo);

		VkViewport viewport = { 0, float(swapchain.height), float(swapchain.width), -float(swapchain.height), 0, 1 };
		VkRect2D scissor = { { 0, 0 }, { uint32_t(swapchain.width), uint32_t(swapchain.height) } };

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		vkCmdSetCullMode(commandBuffer, postPass == 0 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
		vkCmdSetDepthBias(commandBuffer, postPass == 0 ? 0 : 16, 0, postPass == 0 ? 0 : 1);

		Globals passGlobals = globals;
		passGlobals.cullData.postPass = postPass;

		if (clusterSubmit)
		{
			vkCmdBindPipeline(commandBuffer, clusterProgram.bindPoint, postPass >= 1 ? clusterpostPipeline : clusterPipeline);

			DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mdb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer, textureSampler, mtb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, clusterProgram.updateTemplate, clusterProgram.layout, 0, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, clusterProgram.bindPoint, clusterProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, clusterSets[descriptorSetIndex], clusterProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, clusterProgram.bindPoint, clusterProgram.layout, 0, 1, &clusterSets[descriptorSetIndex], 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, clusterProgram.bindPoint, clusterProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			}

			vkCmdPushConstants(commandBuffer, clusterProgram.layout, clusterProgram.pushConstantStages, 0, sizeof(globals), &passGlobals);
			vkCmdDrawMeshTasksIndirectEXT(commandBuffer, ccb.buffer, 4, 1, 0);
		}
		else if (taskSubmit)
		{
			vkCmdBindPipeline(commandBuffer, meshtaskProgram.bindPoint, postPass >= 1 ? meshtaskpostPipeline : late ? meshtasklatePipeline
			                                                                                                        : meshtaskPipeline);

			DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mdb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer, textureSampler, mtb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, meshtaskProgram.updateTemplate, meshtaskProgram.layout, 0, descriptors);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, meshtaskSets[descriptorSetIndex], meshtaskProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, meshtaskProgram.bindPoint, meshtaskProgram.layout, 0, 1, &meshtaskSets[descriptorSetIndex], 0, nullptr);
			}

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshtaskProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);

			vkCmdPushConstants(commandBuffer, meshtaskProgram.layout, meshtaskProgram.pushConstantStages, 0, sizeof(globals), &passGlobals);

			vkCmdDrawMeshTasksIndirectEXT(commandBuffer, dccb.buffer, 4, 1, 0);
		}
		else
		{
			vkCmdBindPipeline(commandBuffer, meshProgram.bindPoint, postPass >= 1 ? meshpostPipeline : meshPipeline);

			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, vb.buffer, DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), textureSampler, mtb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, meshProgram.updateTemplate, meshProgram.layout, 0, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, meshProgram.bindPoint, meshProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			}
			else
#endif
			{
				vkCmdBindDescriptorSets(commandBuffer, meshProgram.bindPoint, meshProgram.layout, 0, 1, &meshSets[descriptorSetIndex], 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, meshProgram.bindPoint, meshProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
				vkUpdateDescriptorSetWithTemplateKHR(device, meshSets[descriptorSetIndex], meshProgram.updateTemplate, descriptors);
			}

			vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdPushConstants(commandBuffer, meshProgram.layout, meshProgram.pushConstantStages, 0, sizeof(globals), &passGlobals);
			vkCmdDrawIndexedIndirectCount(commandBuffer, dcb.buffer, offsetof(MeshDrawCommand, indirect), dccb.buffer, 0, uint32_t(scene->draws.size()), sizeof(MeshDrawCommand));
		}

		vkCmdEndRendering(commandBuffer);
#if defined(WIN32)
		vkCmdEndQuery(commandBuffer, queryPoolPipeline, query);
#endif

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
	};

	auto pyramid = [&](uint32_t timestamp)
	{
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

		VkImageMemoryBarrier2 depthBarriers[] = {
			imageBarrier(depthTarget.image,
			    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
			    VK_IMAGE_ASPECT_DEPTH_BIT),
			imageBarrier(depthPyramid.image,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL)
		};

		pipelineBarrier(commandBuffer, 0, 0, nullptr, COUNTOF(depthBarriers), depthBarriers);
		vkCmdBindPipeline(commandBuffer, depthreduceProgram.bindPoint, depthreducePipeline);

		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
		{
			DescriptorInfo sourceDepth = (i == 0)
			                                 ? DescriptorInfo(depthSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL)
			                                 : DescriptorInfo(depthSampler, depthPyramidMips[i - 1], VK_IMAGE_LAYOUT_GENERAL);

			DescriptorInfo descriptors[] = { { depthPyramidMips[i], VK_IMAGE_LAYOUT_GENERAL }, sourceDepth };

			uint32_t levelWidth = std::max(1u, depthPyramidWidth >> i);
			uint32_t levelHeight = std::max(1u, depthPyramidHeight >> i);

			vec4 reduceData = vec4(levelWidth, levelHeight, 0.f, 0.f);
			if (pushDescriptorSupported)
			{
				dispatch(commandBuffer, depthreduceProgram, levelWidth, levelHeight, reduceData, descriptors);
			}
			else
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, depthreduceSets[i], depthreduceProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, depthreduceProgram.bindPoint, depthreduceProgram.layout, 0, 1, &depthreduceSets[i], 0, nullptr);
				vkCmdPushConstants(commandBuffer, depthreduceProgram.layout, depthreduceProgram.pushConstantStages, 0, sizeof(reduceData), &reduceData);
				vkCmdDispatch(commandBuffer, getGroupCount(levelWidth, depthreduceProgram.localSizeX), getGroupCount(levelHeight, depthreduceProgram.localSizeY), 1);
			}
			VkImageMemoryBarrier2 reduceBarrier = imageBarrier(depthPyramid.image,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			    VK_IMAGE_ASPECT_COLOR_BIT, i, 1);
			pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &reduceBarrier);
		}

		VkImageMemoryBarrier2 depthWriteBarrier = imageBarrier(depthTarget.image,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
		    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		    VK_IMAGE_ASPECT_DEPTH_BIT);
		pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &depthWriteBarrier);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
	};

	VkImageMemoryBarrier2 renderBeginBarriers[gbufferCount + 1] = {
		imageBarrier(depthTarget.image,
		    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		    VK_IMAGE_ASPECT_DEPTH_BIT),
	};

	for (uint32_t i = 0; i < gbufferCount; ++i)
		renderBeginBarriers[i + 1] = imageBarrier(gbufferTargets[i].image,
		    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(renderBeginBarriers), renderBeginBarriers);

#if defined(WIN32)
	vkCmdResetQueryPool(commandBuffer, queryPoolPipeline, 0, 4);
#endif

	std::vector<VkClearColorValue> clearColors(gbufferCount);
	clearColors[0] = { 135.f / 255.f, 206.f / 255.f, 250.f / 255.f, 15.f / 255.f };
	clearColors[1] = { 0.f, 0.f, 0.f, 0.f };
	VkClearDepthStencilValue depthClear = { 0.f, 0 };

	// early cull: frustum cull & fill objects that *were* visible last frame
	cull(taskSubmit ? taskcullPipeline : drawcullPipeline, 2, "early cull", /* late= */ false);
	// early render: render objects that were visible last frame
	render(/* late= */ false, clearColors, depthClear, 0, 4, "early render");
	// depth pyramid generation
	pyramid(6);
	// late cull: frustum + occlusion cull & fill objects that were *not* visible last frame
	cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 8, "late cull", /* late= */ true);

	// late render: render objects that are visible this frame but weren't drawn in the early pass
	render(/* late= */ true, clearColors, depthClear, 1, 10, "late render");

	// we can skip post passes if no draw call needs them
	if (scene->meshPostPasses >> 1)
	{
		// post cull: frustum + occlusion cull & fill extra objects
		cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 12, "post cull", /* late= */ true, /* postPass= */ 1);

		// post render: render extra objects
		render(/* late= */ true, clearColors, depthClear, 2, 14, "post render", /* postPass= */ 1);
	}

	VkImageMemoryBarrier2 shadingBarriers[2 + gbufferCount] = {
		imageBarrier(swapchain.images[imageIndex],
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
		imageBarrier(depthTarget.image,
		    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		    VK_IMAGE_ASPECT_DEPTH_BIT)
	};

	for (uint32_t i = 0; i < gbufferCount; ++i)
		shadingBarriers[i + 2] = imageBarrier(gbufferTargets[i].image,
		    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(shadingBarriers), shadingBarriers);

	if (raytracingSupported && shadowEnabled)
	{
		uint32_t timestamp = 16;

		// checkerboard rendering: we dispatch half as many columns and xform them to fill the screen
		int shadowWidthCB = shadowCheckerboard ? (swapchain.width + 1) / 2 : swapchain.width;
		int shadowCheckerboardF = shadowCheckerboard ? 1 : 0;

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
		VkImageMemoryBarrier2 preshadowBarrier =
		    imageBarrier(shadowTarget.image,
		        0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &preshadowBarrier);

		{
			vkCmdBindPipeline(commandBuffer, shadowProgram.bindPoint, shadowQuality == 0 ? shadowlqPipeline : shadowhqPipeline);
			DescriptorInfo descriptors[] = { { shadowTarget.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, tlas, db.buffer, mb.buffer, mtb.buffer, vb.buffer, ib.buffer, textureSampler };

			ShadowData shadowData = {};
			shadowData.sunDirection = scene->sunDirection;
			shadowData.sunJitter = shadowblurEnabled ? 1e-2f : 0;
			shadowData.inverseViewProjection = inverse(projection * view);
			shadowData.imageSize = vec2(float(swapchain.width), float(swapchain.height));
			shadowData.checkerboard = shadowCheckerboardF;

			if (pushDescriptorSupported)
			{
				vkCmdBindDescriptorSets(commandBuffer, shadowProgram.bindPoint, shadowProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
				dispatch(commandBuffer, shadowProgram, shadowWidthCB, swapchain.height, shadowData, descriptors);
			}
			else
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, shadowProgram.descriptorSet, shadowProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, shadowProgram.bindPoint, shadowProgram.layout, 0, 1, &shadowProgram.descriptorSet, 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, shadowProgram.bindPoint, shadowProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
				vkCmdPushConstants(commandBuffer, shadowProgram.layout, shadowProgram.pushConstantStages, 0, sizeof(shadowData), &shadowData);
				vkCmdDispatch(commandBuffer, getGroupCount(shadowWidthCB, shadowProgram.localSizeX), getGroupCount(swapchain.height, shadowProgram.localSizeY), 1);
			}
		}

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);

		if (shadowCheckerboard)
		{
			VkImageMemoryBarrier2 fillBarrier = imageBarrier(shadowTarget.image,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

			pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &fillBarrier);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadowfillPipeline);

			DescriptorInfo descriptors[] = { { shadowTarget.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
			vec4 fillData = vec4(float(swapchain.width), float(swapchain.height), 0, 0);

			dispatch(commandBuffer, shadowfillProgram, shadowWidthCB, swapchain.height, fillData, descriptors);
		}

		for (int pass = 0; pass < (shadowblurEnabled ? 2 : 0); ++pass)
		{
			const Image& blurFrom = pass == 0 ? shadowTarget : shadowblurTarget;
			const Image& blurTo = pass == 0 ? shadowblurTarget : shadowTarget;

			VkImageMemoryBarrier2 blurBarriers[] = {
				imageBarrier(blurFrom.image,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL),
				imageBarrier(blurTo.image,
				    pass == 0 ? 0 : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, pass == 0 ? 0 : VK_ACCESS_SHADER_READ_BIT, pass == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
			};

			pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(blurBarriers), blurBarriers);
			vkCmdBindPipeline(commandBuffer, shadowblurProgram.bindPoint, shadowblurPipeline);
			DescriptorInfo descriptors[] = { { blurTo.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, blurFrom.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
			vec4 blurData = vec4(float(swapchain.width), float(swapchain.height), pass == 0 ? 1 : 0, scene->camera.znear);

			if (pushDescriptorSupported)
			{
				dispatch(commandBuffer, shadowblurProgram, swapchain.width, swapchain.height, blurData, descriptors);
			}
			else
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, shadowblurSets[pass], shadowblurProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, shadowblurProgram.bindPoint, shadowblurProgram.layout, 0, 1, &shadowblurSets[pass], 0, nullptr);
				vkCmdPushConstants(commandBuffer, shadowblurProgram.layout, shadowblurProgram.pushConstantStages, 0, sizeof(blurData), &blurData);
				vkCmdDispatch(commandBuffer, getGroupCount(swapchain.width, shadowblurProgram.localSizeX), getGroupCount(swapchain.height, shadowblurProgram.localSizeY), 1);
			}
		}

		VkImageMemoryBarrier2 postblurBarrier =
		    imageBarrier(shadowTarget.image,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &postblurBarrier);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 2);
	}
	else
	{
		VkImageMemoryBarrier2 dummyBarrier =
			imageBarrier(shadowTarget.image,
				0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &dummyBarrier);

		uint32_t timestamp = 16; // todo: we need an actual profiler
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 2);
	}

	{
		uint32_t timestamp = 19;

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
		{
			vkCmdBindPipeline(commandBuffer, finalProgram.bindPoint, finalPipeline);

			DescriptorInfo descriptors[] = { { swapchainImageViews[imageIndex], VK_IMAGE_LAYOUT_GENERAL }, { readSampler, gbufferTargets[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { readSampler, gbufferTargets[1].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { readSampler, shadowTarget.imageView, VK_IMAGE_LAYOUT_GENERAL } };

			ShadeData shadeData = {};
			shadeData.cameraPosition = scene->camera.position;
			shadeData.sunDirection = scene->sunDirection;
			shadeData.shadowEnabled = shadowEnabled;
			shadeData.inverseViewProjection = inverse(projection * view);
			shadeData.imageSize = vec2(float(swapchain.width), float(swapchain.height));

			if (pushDescriptorSupported)
			{
				dispatch(commandBuffer, finalProgram, swapchain.width, swapchain.height, shadeData, descriptors);
			}
			else
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, finalProgram.descriptorSet, finalProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, finalProgram.bindPoint, finalProgram.layout, 0, 1, &finalProgram.descriptorSet, 0, nullptr);
				vkCmdPushConstants(commandBuffer, finalProgram.layout, finalProgram.pushConstantStages, 0, sizeof(shadeData), &shadeData);
				vkCmdDispatch(commandBuffer, getGroupCount(swapchain.width, finalProgram.localSizeX), getGroupCount(swapchain.height, finalProgram.localSizeY), 1);
			}
		}
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
	}

	static double cullGPUTime = 0.0;
	static double pyramidGPUTime = 0.0;
	static double culllateGPUTime = 0.0;
	static double cullpostGPUTime = 0.0;
	static double renderGPUTime = 0.0;
	static double renderlateGPUTime = 0.0;
	static double renderpostGPUTime = 0.0;
	static double shadowsGPUTime = 0.0;
	static double shadowblurGPUTime = 0.0;
	static double shadeGPUTime = 0.0;
	static double tlasGPUTime = 0.0;

	static double frameCPUAvg = 0.0;
	static double frameGPUAvg = 0.0;

	if (debugGuiMode % 3)
	{
		auto debugtext = [&](int line, uint32_t color, const char* format, ...)
		{
			TextData textData = {};
			textData.offsetX = 1;
			textData.offsetY = line + 2;
			textData.scale = 2;
			textData.color = color;

			va_list args;
			va_start(args, format);
			vsnprintf(textData.data, sizeof(textData.data), format, args);
			va_end(args);

			vkCmdPushConstants(commandBuffer, debugtextProgram.layout, debugtextProgram.pushConstantStages, 0, sizeof(textData), &textData);
			vkCmdDispatch(commandBuffer, strlen(textData.data), 1, 1);
		};

		VkImageMemoryBarrier2 textBarrier =
		    imageBarrier(swapchain.images[imageIndex],
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &textBarrier);

		vkCmdBindPipeline(commandBuffer, debugtextProgram.bindPoint, debugtextPipeline);

		DescriptorInfo descriptors[] = { { swapchainImageViews[imageIndex], VK_IMAGE_LAYOUT_GENERAL } };

#if defined(WIN32)
		if (pushDescriptorSupported)
		{
			vkCmdPushDescriptorSetWithTemplate(commandBuffer, debugtextProgram.updateTemplate, debugtextProgram.layout, 0, descriptors);
		}
		else
#endif
		{
			vkUpdateDescriptorSetWithTemplateKHR(device, debugtextProgram.descriptorSet, debugtextProgram.updateTemplate, descriptors);
			vkCmdBindDescriptorSets(commandBuffer, debugtextProgram.bindPoint, debugtextProgram.layout, 0, 1, &debugtextProgram.descriptorSet, 0, nullptr);
		}

		// debug text goes here!
#if defined(WIN32)
		uint64_t triangleCount = pipelineResults[0] + pipelineResults[1] + pipelineResults[2];
#elif defined(__ANDROID__)
		uint64_t triangleCount = 0;
#endif

		cullGPUTime = double(timestampResults[3] - timestampResults[2]) * props.limits.timestampPeriod * 1e-6;
		renderGPUTime = double(timestampResults[5] - timestampResults[4]) * props.limits.timestampPeriod * 1e-6;
		pyramidGPUTime = double(timestampResults[7] - timestampResults[6]) * props.limits.timestampPeriod * 1e-6;
		culllateGPUTime = double(timestampResults[9] - timestampResults[8]) * props.limits.timestampPeriod * 1e-6;
		renderlateGPUTime = double(timestampResults[11] - timestampResults[10]) * props.limits.timestampPeriod * 1e-6;
		cullpostGPUTime = double(timestampResults[13] - timestampResults[12]) * props.limits.timestampPeriod * 1e-6;
		renderpostGPUTime = double(timestampResults[15] - timestampResults[14]) * props.limits.timestampPeriod * 1e-6;
		shadowsGPUTime = double(timestampResults[17] - timestampResults[16]) * props.limits.timestampPeriod * 1e-6;
		shadowblurGPUTime = double(timestampResults[18] - timestampResults[17]) * props.limits.timestampPeriod * 1e-6;
		shadeGPUTime = double(timestampResults[20] - timestampResults[19]) * props.limits.timestampPeriod * 1e-6;
		tlasGPUTime = double(timestampResults[22] - timestampResults[21]) * props.limits.timestampPeriod * 1e-6;

		double trianglesPerSec = double(triangleCount) / double(frameGPUAvg * 1e-3);
		double drawsPerSec = double(scene->draws.size()) / double(frameGPUAvg * 1e-3);

		debugtext(0, ~0u, "%scpu: %.2f ms  (%+.2f); gpu: %.2f ms", reloadShaders ? "   " : "", frameCPUAvg, deltaTime * frameCPUAvg, frameGPUAvg);
		if (reloadShaders)
				debugtext(0, reloadShadersColor, "R*");

		if (debugGuiMode % 3 == 2)
		{
			debugtext(2, ~0u, "cull: %.2f ms, pyramid: %.2f ms, render: %.2f ms, shade: %.2f ms",
			    cullGPUTime + culllateGPUTime + cullpostGPUTime,
			    pyramidGPUTime,
			    renderGPUTime + renderlateGPUTime + renderpostGPUTime,
				shadeGPUTime);
			debugtext(3, ~0u, "render breakdown: early %.2f ms, late %.2f ms, post %.2f ms",
				    renderGPUTime, renderlateGPUTime, renderpostGPUTime);

			debugtext(4, ~0u, "tlas: %.2f ms, shadows: %.2f ms, shadow blur: %.2f ms",
				tlasGPUTime, shadowsGPUTime, shadowblurGPUTime);
			debugtext(5, ~0u, "triangles %.2fM; %.1fB tri / sec, %.1fM draws / sec",
			    double(triangleCount) * 1e-6, trianglesPerSec * 1e-9, drawsPerSec * 1e-6);
			debugtext(7, ~0u, "frustum culling %s, occlusion culling %s, level-of-detail %s",
			    cullingEnabled ? "ON" : "OFF", occlusionEnabled ? "ON" : "OFF", lodEnabled ? "ON" : "OFF");
			debugtext(8, ~0u, "mesh shading %s, task shading %s, cluster occlusion culling %s",
			    taskSubmit ? "ON" : "OFF", taskSubmit && taskShadingEnabled ? "ON" : "OFF",
			    clusterOcclusionEnabled ? "ON" : "OFF");

			debugtext(10, ~0u, "RT shadow %s, blur %s, shadow quality %d, shadow checkerboard %s",
			    raytracingSupported && shadowEnabled ? "ON" : "OFF",
			    raytracingSupported && shadowEnabled && shadowblurEnabled ? "ON" : "OFF",
			    shadowQuality, shadowCheckerboard ? "ON" : "OFF");
		}
	}

	vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 1);

	static bool bDisplaySettings = true;
	static bool bDisplayProfiling = true;
	static bool bDisplayScene = true;
	if (ImGui::BeginMainMenuBar())
	{
		ImGui::Dummy(ImVec2(50.f, 0.f));
		ImGui::Checkbox("Settings", &bDisplaySettings);
		ImGui::Checkbox("Profiling", &bDisplayProfiling);
		ImGui::Checkbox("Scene", &bDisplayScene);
		ImGui::EndMainMenuBar();
	}

	if (bDisplaySettings)
	{
		ImGui::Begin("Global Settings");
		ImGui::Checkbox("Enable Mesh Shading", &meshShadingEnabled);
		ImGui::Checkbox("Enable Task Shading", &taskShadingEnabled);
		ImGui::Checkbox("Enable Culing", &cullingEnabled);
		ImGui::Checkbox("Enable Occlusion Culling", &occlusionEnabled);
		ImGui::Checkbox("Enable Cluster Occlusion Culling", &clusterOcclusionEnabled);
		ImGui::Checkbox("Enable Shadow", &shadowEnabled);
		ImGui::SetNextItemWidth(200.f);
		ImGui::SliderInt("Shadow Quality (0=low, 1=high)", &shadowQuality, 0, 1);
		ImGui::Checkbox("Enable Shadow Blurring", &shadowblurEnabled);
		ImGui::Checkbox("Enable Shadow Checkerboard", &shadowCheckerboard);
		ImGui::Checkbox("Enable LoD", &lodEnabled);
		if (lodEnabled)
		{
			ImGui::SetNextItemWidth(100.f);
			ImGui::DragInt("Level Index(LoD)", &debugLodStep, 1, 0, 9);
		}
		ImGui::Checkbox("Enable Reload Shaders", &reloadShaders);
		ImGui::SetNextItemWidth(200.f);
		ImGui::SliderInt("Debug Info Mode (0=off, 1=on, 2=verbose)", &debugGuiMode, 0, 2);
		ImGui::Checkbox("Enable Animation", &animationEnabled);
		ImGui::Text("Cluster Ray Tracing Enabled: %s", clusterRTEnabled ? "ON" : "OFF");
		ImGui::Checkbox("Enable Debug Sleep", &debugSleep);

		ImGui::End();
	}

	if (bDisplayProfiling)
	{
		ImGui::Begin("Performance Monitor");
		// Frame Rate
		{
			static float framerate = 0.0f;
			framerate = 0.9f * framerate + 0.1f * 1.0f / deltaTime;
			static TimeSeriesPlot frPlot(100);
			frPlot.addValue(framerate);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
			ImGui::PlotLines("##Avg Frame Rate",
			    frPlot.data(),
			    frPlot.size(),
			    frPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Avg Frame Rate: ", framerate, 60.f, 30.f, std::greater<float>());
		}
		// CPU time
		{
			static TimeSeriesPlot cpuPlot(100);
			cpuPlot.addValue(frameCPUAvg);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
			ImGui::PlotLines("##Avg CPU Time",
			    cpuPlot.data(),
			    cpuPlot.size(),
			    cpuPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Avg CPU Time(ms): ", frameCPUAvg, 16.7, 33.4);
		}
		// GPU time
		{
			static TimeSeriesPlot gpuPlot(100);
			gpuPlot.addValue(frameGPUAvg);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
			ImGui::PlotLines("##Avg GPU Time",
			    gpuPlot.data(),
			    gpuPlot.size(),
			    gpuPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Avg GPU Time(ms): ", frameGPUAvg, 16.7, 33.4);
		}
		// GPU Culling time
		{
			static TimeSeriesPlot gpuCullPlot(100);
			gpuCullPlot.addValue(cullGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
			ImGui::PlotLines("##Culling GPU Time",
			    gpuCullPlot.data(),
			    gpuCullPlot.size(),
			    gpuCullPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Culling GPU Time(ms): ", cullGPUTime, 1.0, 2.0);
		}
		// GPU Culling late time
		{
			static TimeSeriesPlot gpuCullLatePlot(100);
			gpuCullLatePlot.addValue(culllateGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.5f, 1.0f, 0.0f, 1.0f));
			ImGui::PlotLines("##Culling Late GPU Time",
			    gpuCullLatePlot.data(),
			    gpuCullLatePlot.size(),
			    gpuCullLatePlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Culling Late GPU Time(ms): ", culllateGPUTime, 1.0, 2.0);
		}
		// Rendering GPU time
		{
			static TimeSeriesPlot gpuRenderingPlot(100);
			gpuRenderingPlot.addValue(renderGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.5f, 1.0f));
			ImGui::PlotLines("##Rendering GPU Time",
			    gpuRenderingPlot.data(),
			    gpuRenderingPlot.size(),
			    gpuRenderingPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Rendering GPU Time(ms): ", renderGPUTime, 4.0, 8.0);
		}
		// Rendering GPU late time
		{
			static TimeSeriesPlot gpuRenderingLatePlot(100);
			gpuRenderingLatePlot.addValue(renderlateGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));
			ImGui::PlotLines("##Rendering Late GPU Time",
			    gpuRenderingLatePlot.data(),
			    gpuRenderingLatePlot.size(),
			    gpuRenderingLatePlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Rendering Late GPU Time(ms): ", renderlateGPUTime, 4.0, 8.0);
		}
		// Depth Pyramid GPU time
		{
			static TimeSeriesPlot depthPyramidPlot(100);
			depthPyramidPlot.addValue(pyramidGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.5f, 0.0f, 0.3f, 1.0f));
			ImGui::PlotLines("##Depth Pyramid GPU Time",
			    depthPyramidPlot.data(),
			    depthPyramidPlot.size(),
			    depthPyramidPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Depth Pyramid GPU Time(ms): ", pyramidGPUTime, 1.0, 2.0);
		}
		// @TODO: add more profiling curves
		ImGui::End();
	}

	if (bDisplayScene)
	{
		ImGui::Begin("Scene");
		if (ImGui::CollapsingHeader("Camera"))
		{
			ImGui::DragFloat3("Camera Position", (float*)(&scene->camera.position), 0.01f);
			float rotations[3] = { pitch, yaw, roll };
			if (ImGui::DragFloat3("Camera Rotation (Pitch, Yaw, Roll)", rotations, 0.01f))
			{
				pitch = rotations[0];
				yaw = rotations[1];
				roll = rotations[2];
				cameraDirty = true;
			}
			ImGui::SetNextItemWidth(200.f);
			ImGui::DragFloat("Camera Moving Speed", &cameraSpeed, 0.01f, 0.0f, 10.f);
			if (ImGui::Checkbox("Enable Dolly Zoom", &enableDollyZoom))
			{
				// Update the camera origin for dolly zoom
				cameraOriginForDolly = scene->camera.position;
			}
			if (enableDollyZoom)
			{
				ImGui::SetNextItemWidth(200.f);
				ImGui::DragFloat("Dolly Zoom Ref Distance", &soRef, 0.01f, 1.0f, 100.f);
			}
		}
		ImGui::End();
	}

	guiRenderer->EndFrame();
	VkImageMemoryBarrier2 uiBarrier = imageBarrier(swapchain.images[imageIndex],
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &uiBarrier);
	guiRenderer->RenderDrawData(commandBuffer, swapchainImageViews[imageIndex], { swapchain.width, swapchain.height });

	VkImageMemoryBarrier2 presentBarrier = imageBarrier(swapchain.images[imageIndex],
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
	    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &presentBarrier);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSemaphoreSubmitInfo waitSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitSemaphoreInfo.semaphore = acquireSemaphore;
	waitSemaphoreInfo.value = 0;
	waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	waitSemaphoreInfo.deviceIndex = 0;

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	commandBufferSubmitInfo.commandBuffer = commandBuffer;
	commandBufferSubmitInfo.deviceMask = 0;

	VkSemaphoreSubmitInfo releaseSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	releaseSemaphoreInfo.semaphore = releaseSemaphores[imageIndex];
	releaseSemaphoreInfo.value = 0;
	releaseSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
	releaseSemaphoreInfo.deviceIndex = 0;

	VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &releaseSemaphoreInfo;

	VK_CHECK_FORCE(vkQueueSubmit2(queue, 1, &submitInfo, frameFence));

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &releaseSemaphores[imageIndex];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain.swapchain;
	presentInfo.pImageIndices = &imageIndex;

	VK_CHECK_SWAPCHAIN(vkQueuePresentKHR(queue, &presentInfo));

	if (frameIndex >= MAX_FRAMES - 1)
	{
		VkFence waitFence = frameFences[(frameIndex - (MAX_FRAMES - 1)) % MAX_FRAMES];
		VK_CHECK(vkWaitForFences(device, 1, &waitFence, VK_TRUE, ~0ull));
		VK_CHECK(vkResetFences(device, 1, &waitFence));

		VK_CHECK_QUERY(vkGetQueryPoolResults(device, queryPoolTimestamp, 0, COUNTOF(timestampResults), sizeof(timestampResults), timestampResults, sizeof(timestampResults[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
#if defined(WIN32)
		VK_CHECK_QUERY(vkGetQueryPoolResults(device, queryPoolPipeline, 0, COUNTOF(pipelineResults), sizeof(pipelineResults), pipelineResults, sizeof(pipelineResults[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
#endif
	}

	double frameGPUBegin = double(timestampResults[0]) * props.limits.timestampPeriod * 1e-6;
	double frameGPUEnd = double(timestampResults[1]) * props.limits.timestampPeriod * 1e-6;
	double frameCPUEnd = GetTimeInSeconds();

	frameCPUAvg = frameCPUAvg * 0.9 + (frameCPUEnd - frameCPUBegin) * 0.1;
	frameGPUAvg = frameGPUAvg * 0.9 + (frameGPUEnd - frameGPUBegin) * 0.1;

	if (debugSleep)
	{
#if defined(WIN32)
		Sleep(200);
#endif
	}

	frameIndex++;

	return true;
}

void VulkanContext::DestroyInstance()
{
	if (gInstance)
	{
		gInstance->Release();
		gInstance = nullptr;
	}
}

void VulkanContext::Release()
{
	VK_CHECK(vkDeviceWaitIdle(device));

	if (depthPyramid.image)
	{
		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
			vkDestroyImageView(device, depthPyramidMips[i], 0);

		destroyImage(depthPyramid, device);
	}
	if (shadowTarget.image)
		destroyImage(shadowTarget, device);
	if (shadowblurTarget.image)
		destroyImage(shadowblurTarget, device);
	for (uint32_t i = 0; i < swapchain.imageCount; ++i)
		if (swapchainImageViews[i])
			vkDestroyImageView(device, swapchainImageViews[i], 0);

	for (Image& image : scene->images)
	{
		destroyImage(image, device);
	}

	for (Image& image : gbufferTargets)
		destroyImage(image, device);

	destroyImage(depthTarget, device);

	destroyBuffer(dccb, device);
	destroyBuffer(dcb, device);
	destroyBuffer(dvb, device);
	destroyBuffer(db, device);

	destroyBuffer(mb, device);
	destroyBuffer(mtb, device);
	{
		destroyBuffer(mlb, device);
		destroyBuffer(mdb, device);
		destroyBuffer(mvb, device);
		destroyBuffer(cib, device);
		destroyBuffer(ccb, device);
	}

	if (raytracingSupported)
	{
		vkDestroyAccelerationStructureKHR(device, tlas, 0);
		for (VkAccelerationStructureKHR as : blas)
			vkDestroyAccelerationStructureKHR(device, as, 0);

		destroyBuffer(tlasBuffer, device);
		destroyBuffer(blasBuffer, device);
		destroyBuffer(tlasScratchBuffer, device);
		destroyBuffer(tlasInstanceBuffer, device);
	}

	destroyBuffer(ib, device);
	destroyBuffer(vb, device);
	destroyBuffer(scratch, device);

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		vkFreeCommandBuffers(device, commandPools[ii], 1, &commandBuffers[ii]);
		vkDestroyCommandPool(device, commandPools[ii], 0);
	}

	destroySwapchain(device, swapchain);

	for (auto queryPoolTimestamp : queryPoolsTimestamp)
		vkDestroyQueryPool(device, queryPoolTimestamp, 0);

#if defined(WIN32)
	for (auto queryPoolPipeline : queryPoolsPipeline)
		vkDestroyQueryPool(device, queryPoolPipeline, 0);
#endif

	for (VkPipeline pipeline : pipelines)
		vkDestroyPipeline(device, pipeline, 0);

	destroyProgram(device, meshProgram, descriptorPool);
	destroyProgram(device, meshtaskProgram, descriptorPool);
	destroyProgram(device, debugtextProgram, descriptorPool);
	destroyProgram(device, drawcullProgram, descriptorPool);
	destroyProgram(device, tasksubmitProgram, descriptorPool);
	destroyProgram(device, clustersubmitProgram, descriptorPool);
	destroyProgram(device, clustercullProgram, descriptorPool);
	destroyProgram(device, clusterProgram, descriptorPool);
	destroyProgram(device, depthreduceProgram, descriptorPool);

	if (raytracingSupported)
	{
		destroyProgram(device, finalProgram, descriptorPool);
		destroyProgram(device, shadowProgram, descriptorPool);
		destroyProgram(device, shadowfillProgram, descriptorPool);
		destroyProgram(device, shadowblurProgram, descriptorPool);
	}

	vkDestroyDescriptorSetLayout(device, textureSetLayout, 0);

	vkDestroyDescriptorPool(device, descriptorPool, 0);
	vkDestroyDescriptorPool(device, scene->textureSet.first, 0);

#if defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
	for (Shader& shader : shaderSet.shaders)
		if (shader.module)
			vkDestroyShaderModule(device, shader.module, 0);
#endif

	vkDestroySampler(device, textureSampler, 0);
	vkDestroySampler(device, readSampler, 0);
	vkDestroySampler(device, depthSampler, 0);

	for (auto frameFence : frameFences)
		vkDestroyFence(device, frameFence, 0);
	for (auto acquireSemaphore : acquireSemaphores)
		vkDestroySemaphore(device, acquireSemaphore, 0);
	for (auto releaseSemaphore : releaseSemaphores)
		vkDestroySemaphore(device, releaseSemaphore, 0);

	// move gui renderer
	auto guiRenderer = GuiRenderer::GetInstance();
	guiRenderer->Shutdown(device);

	vkDestroySurfaceKHR(instance, surface, 0);
#if defined(WIN32)
	glfwDestroyWindow(window);
#endif
	vkDestroyDevice(device, 0);

	if (debugCallback)
		vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);

	vkDestroyInstance(instance, 0);

	volkFinalize();
}

Renderer::Renderer()
{
	VulkanContext::GetInstance();
#if defined(WIN32)
	lastFrame = glfwGetTime();
#endif
}

Renderer::~Renderer()
{
}

bool Renderer::DrawFrame()
{
	auto gContext = VulkanContext::GetInstance();
	return gContext->DrawFrame();
}
#include <iostream>
#include <stdio.h>
#include <algorithm>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VOLK_IMPLEMENTATION
#include <objparser.h>
#include <meshoptimizer.h>

#include "common.h"
#include "device.h"
#include "swapchain.h"
#include "resources.h"
#include "shaders.h"
#include "math.h"

bool meshShadingEnabled = true;
bool cullingEnabled = true;
bool lodEnabled = true;
bool occlusionEnabled = true;

bool debugPyramid = false;
int debugPyramidLevel = 0;

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

VkRenderPass createRenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, bool late)
{
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = colorFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments[1].format = depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = late ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachment = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthAttachment = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachment;
	subpass.pDepthStencilAttachment = &depthAttachment;

	VkRenderPassCreateInfo createInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	createInfo.attachmentCount = sizeof(attachments) / sizeof(attachments[0]);
	createInfo.pAttachments = attachments;
	createInfo.subpassCount = 1;
	createInfo.pSubpasses = &subpass;

	VkRenderPass renderPass = 0;
	VK_CHECK(vkCreateRenderPass(device, &createInfo, 0, &renderPass));

	return renderPass;
}

VkFramebuffer createFramebuffer(VkDevice device, VkRenderPass renderPass, VkImageView colorView, VkImageView depthView, uint32_t width, uint32_t height)
{
	VkImageView attachments[] = { colorView, depthView };

	VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	createInfo.renderPass = renderPass;
	createInfo.attachmentCount = ARRAYSIZE(attachments);
	createInfo.pAttachments = attachments;
	createInfo.width = width;
	createInfo.height = height;
	createInfo.layers = 1;

	VkFramebuffer framebuffer = 0;
	VK_CHECK(vkCreateFramebuffer(device, &createInfo, 0, &framebuffer));
	return framebuffer;
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

struct alignas(16) Meshlet
{
	vec3 center;
	float radius;
	int8_t coneAxis[3];
	int8_t coneCutoff;

	uint32_t vertexOffset;
	uint32_t triangleOffset;
	uint8_t vertexCount;
	uint8_t triangleCount;
};

struct alignas(16) Globals
{
	mat4 projection;
	int lodEnabled;
};

struct alignas(16) MeshDraw
{
	vec3 position;
	float scale;
	quat orientation;

	uint32_t meshIndex;
	uint32_t vertexOffset;
};

struct MeshDrawCommand
{
	uint32_t drawId;
	VkDrawIndirectCommand indirect; // 4 uint32_t
	VkDrawMeshTasksIndirectCommandEXT indirectMS; // 3 uint32_t
};

struct Vertex
{
	float vx, vy, vz;
	uint8_t nx, ny, nz, nw;
	uint16_t tu, tv;
};

struct MeshLod
{
	uint32_t indexOffset;
	uint32_t indexCount;
	uint32_t meshletOffset;
	uint32_t meshletCount;
};

struct alignas(16) Mesh
{
	vec3 center;
	float radius;

	uint32_t vertexOffset;
	uint32_t vertexCount;
	uint32_t lodCount;
	uint32_t placeHolder;

	MeshLod lods[8];
};

struct Geometry
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet> meshlets;
	std::vector<unsigned int> meshletVertexData;
	std::vector<unsigned char> meshletIndexData;

	std::vector<Mesh> meshes;
};

struct alignas(16) DrawCullData
{
	float P00, P11, znear, zfar; // symmetric projection parameters
	float frustum[4]; // data for left/right/top/bottom frustum planes
	float lodBase, lodStep; // lod distance i = base * pow(step, i)
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint32_t drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
};

struct alignas(16) DepthReduceData
{
	vec2 imageSize;
};

size_t appendMeshlets(Geometry& result, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
	size_t max_vertices = 64;
	size_t max_triangles = 124;

	std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles));
	std::vector<unsigned int> meshletVertexData(meshlets.size() * max_vertices);
	std::vector<unsigned char> meshletIndexData(meshlets.size() * max_triangles * 3);

	meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertexData.data(), meshletIndexData.data(), indices.data(), indices.size(),
		&vertices[0].vx, vertices.size(), sizeof(Vertex), max_vertices, max_triangles, 0.5f));

	uint32_t meshletVertexOffset = uint32_t(result.meshletVertexData.size());
	uint32_t meshletIndexOffset = uint32_t(result.meshletIndexData.size());

	result.meshletVertexData.insert(result.meshletVertexData.end(), meshletVertexData.begin(), meshletVertexData.end());
	result.meshletIndexData.insert(result.meshletIndexData.end(), meshletIndexData.begin(), meshletIndexData.end());

	for (auto& meshlet : meshlets)
	{
		Meshlet m = {};

		m.vertexOffset = meshlet.vertex_offset + meshletVertexOffset;
		m.triangleOffset = meshlet.triangle_offset + meshletIndexOffset;
		m.vertexCount = uint8_t(meshlet.vertex_count);
		m.triangleCount = uint8_t(meshlet.triangle_count);

		meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshletVertexData[meshlet.vertex_offset], &meshletIndexData[meshlet.triangle_offset],
			meshlet.triangle_count, &vertices[0].vx, vertices.size(), sizeof(Vertex));

		m.center = vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
		m.radius = bounds.radius;
		m.coneAxis[0] = bounds.cone_axis_s8[0];
		m.coneAxis[1] = bounds.cone_axis_s8[1];
		m.coneAxis[2] = bounds.cone_axis_s8[2];
		m.coneCutoff = bounds.cone_cutoff_s8;
		result.meshlets.emplace_back(m);
	}

	while (result.meshlets.size() % 32)
		result.meshlets.push_back(Meshlet());

	return meshlets.size();
}

bool loadMesh(Geometry& result, const char* path, bool buildMeshlets)
{
	ObjFile file;
	if (!objParseFile(file, path))
		return false;

	size_t index_count = file.f_size / 3;

	std::vector<Vertex> triangle_vertices(index_count);

	for (size_t i = 0; i < index_count; ++i)
	{
		Vertex& v = triangle_vertices[i];

		int vi = file.f[i * 3 + 0];
		int vti = file.f[i * 3 + 1];
		int vni = file.f[i * 3 + 2];

		float nx = vni < 0 ? 0.f : file.vn[vni * 3 + 0];
		float ny = vni < 0 ? 0.f : file.vn[vni * 3 + 1];
		float nz = vni < 0 ? 1.f : file.vn[vni * 3 + 2];

		v.vx = file.v[vi * 3 + 0];
		v.vy = file.v[vi * 3 + 1];
		v.vz = file.v[vi * 3 + 2];
		v.nx = uint8_t(nx * 127.f + 127.5f);
		v.ny = uint8_t(ny * 127.f + 127.5f);
		v.nz = uint8_t(nz * 127.f + 127.5f);
		v.tu = meshopt_quantizeHalf(vti < 0 ? 0.f : file.vt[vti * 3 + 0]);
		v.tv = meshopt_quantizeHalf(vti < 0 ? 0.f : file.vt[vti * 3 + 1]);
	}

	std::vector<uint32_t> remap(index_count);
	size_t vertex_count = meshopt_generateVertexRemap(remap.data(), 0, index_count, triangle_vertices.data(), index_count, sizeof(Vertex));

	std::vector<Vertex> vertices(vertex_count);
	std::vector<uint32_t> indices(index_count);

	meshopt_remapVertexBuffer(vertices.data(), triangle_vertices.data(), index_count, sizeof(Vertex), remap.data());
	meshopt_remapIndexBuffer(indices.data(), 0, index_count, remap.data());

	meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count);
	meshopt_optimizeVertexFetch(vertices.data(), indices.data(), index_count, vertices.data(), vertex_count, sizeof(Vertex));

	Mesh mesh = {};

	mesh.vertexOffset = uint32_t(result.vertices.size());
	mesh.vertexCount = uint32_t(vertices.size());

	result.vertices.insert(result.vertices.end(), vertices.begin(), vertices.end());

	vec3 center = vec3(0.f);

	for (const auto& v : vertices)
		center += vec3(v.vx, v.vy, v.vz);
	center /= float(vertices.size());

	float radius = 0.f;
	for (const auto& v : vertices)
		radius = glm::max(radius, distance(center, vec3(v.vx, v.vy, v.vz)));

	mesh.center = center;
	mesh.radius = radius;

	std::vector<uint32_t> lodIndices = indices;

	while (mesh.lodCount < ARRAYSIZE(mesh.lods))
	{
		MeshLod& lod = mesh.lods[mesh.lodCount++];

		lod.indexOffset = uint32_t(result.indices.size());
		lod.indexCount = uint32_t(lodIndices.size());

		result.indices.insert(result.indices.end(), lodIndices.begin(), lodIndices.end());

		lod.meshletOffset = uint32_t(result.meshlets.size());
		lod.meshletCount = buildMeshlets ? uint32_t(appendMeshlets(result, vertices, lodIndices)) : 0;

		if (mesh.lodCount < ARRAYSIZE(mesh.lods))
		{
			size_t nextIndicesTarget = size_t(lodIndices.size() * 0.1f);
			size_t nextIndices = meshopt_simplify(lodIndices.data(), lodIndices.data(), lodIndices.size(), &vertices[0].vx, vertices.size(), sizeof(Vertex), nextIndicesTarget, 1e-4f);
			assert(nextIndices <= lodIndices.size());

			if (nextIndices == lodIndices.size())
				break;

			lodIndices.resize(nextIndices);

			meshopt_optimizeVertexCache(lodIndices.data(), lodIndices.data(), lodIndices.size(), vertex_count);
		}
	}

	result.meshes.emplace_back(mesh);

	return true;
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
		return f ? NAN : s ? -INFINITY : INFINITY;
	}
	e = e + (127 - 15);
	f = f << 13;
	union { uint32_t u; float f; } result;
	result.u = (s << 31) | (e << 23) | f;
	return result.f;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_R)
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
		if (key == GLFW_KEY_L)
		{
			lodEnabled = !lodEnabled;
			return;
		}
		if (key == GLFW_KEY_P)
		{
			debugPyramid = !debugPyramid;
			return;
		}
		if (debugPyramid && (key >= GLFW_KEY_0 && key <= GLFW_KEY_9))
		{
			debugPyramidLevel = key - GLFW_KEY_0;
			return;
		}
	}
}

mat4 perspectiveProjection(float fovY, float aspectWbyH, float zNear)
{
	float f = 1.0f / tanf(fovY/ 2.0f);
	return mat4(
		f / aspectWbyH, 0.0f,	0.0f,	0.0f,
		0.0f,			f,		0.0f,	0.0f,
		0.0f,			0.0f,	0.0f,	1.0f,
		0.0f,			0.0f,	zNear,	0.0f);
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

int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		printf(LOGE("Usage: %s [mesh list]\n"), argv[0]);
		return 1;
	}

	int rc = glfwInit();
	assert(rc);

	VK_CHECK(volkInitialize());

	VkInstance instance = createInstance();
	assert(instance);

	volkLoadInstance(instance);

	VkDebugReportCallbackEXT debugCallback = registerDebugCallback(instance);

	VkPhysicalDevice physicalDevices[16];
	uint32_t physicalDeviceCount = sizeof(physicalDevices) / sizeof(physicalDevices[0]);
	auto result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
	VK_CHECK(result);

	VkPhysicalDevice physicalDevice = pickPhysicalDevice(physicalDevices, physicalDeviceCount);
	assert(physicalDevice);

	uint32_t extensionCount;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, 0));

	std::vector<VkExtensionProperties> extensions(extensionCount);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, extensions.data()));

	bool meshShadingSupported = false;
	for (const auto& ext : extensions)
	{
		if (strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
		{
			meshShadingSupported = true;
			break;
		}
	}

	meshShadingEnabled = meshShadingSupported;

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	uint32_t graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(graphicsFamily != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = createDevice(instance, physicalDevice, graphicsFamily, meshShadingEnabled);
	assert(device);

	GLFWwindow* window = glfwCreateWindow(1024, 768, "kaleido", 0, 0);
	assert(window);

	glfwSetKeyCallback(window, keyCallback);

	VkSurfaceKHR surface = createSurface(instance, window);
	assert(surface);

	// Check if VkSurfaceKHR is supported in physical device.
	VkBool32 presentSupported = VK_FALSE;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsFamily, surface, &presentSupported));
	assert(presentSupported);

	VkFormat swapchainFormat = getSwapchainFormat(physicalDevice, surface);
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

	VkSemaphore acquireSemaphore = createSemaphore(device);
	assert(acquireSemaphore);

	VkSemaphore releaseSemaphore = createSemaphore(device);
	assert(releaseSemaphore);

	VkQueue queue = 0;
	vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

	VkRenderPass renderPass = createRenderPass(device, swapchainFormat, depthFormat, /* late = */false);
	assert(renderPass);

	VkRenderPass renderPassLate = createRenderPass(device, swapchainFormat, depthFormat, /* late = */true);
	assert(renderPassLate);

	VkSampler depthSampler = createSampler(device, VK_SAMPLER_REDUCTION_MODE_MIN_EXT);
	assert(depthSampler);

	Shader meshletTS = {};
	Shader meshletMS = {};
	if (meshShadingEnabled)
	{
		bool tc = loadShader(meshletTS, device, "shaders/meshlet.task.spv");
		assert(tc);

		bool rc = loadShader(meshletMS, device, "shaders/meshlet.mesh.spv");
		assert(rc);
	}

	Shader drawcullCS = {};
	{
		bool rc = loadShader(drawcullCS, device, "shaders/drawcull.comp.spv");
		assert(rc);
	}

	Shader depthreduceCS = {};
	{
		bool rc = loadShader(depthreduceCS, device, "shaders/depthreduce.comp.spv");
		assert(rc);
	}

	Shader meshVS = {};
	{
		bool rc = loadShader(meshVS, device, "shaders/mesh.vert.spv");
		assert(rc);
	}

	Shader meshFS = {};
	{
		bool rc = loadShader(meshFS, device, "shaders/mesh.frag.spv");
		assert(rc);
	}

	Swapchain swapchain;
	createSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, swapchainFormat);

	// TODO: this is critical for performance!
	VkPipelineCache pipelineCache = 0;

	Program drawcullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &drawcullCS }, sizeof(DrawCullData));
	VkPipeline drawcullPipeline = createComputePipeline(device, pipelineCache, drawcullCS, drawcullProgram.layout, true, /* LATE= */ VK_FALSE );
	VkPipeline drawculllatePipeline = createComputePipeline(device, pipelineCache, drawcullCS, drawcullProgram.layout, true, /* LATE= */ VK_TRUE );

	Program depthreduceProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &depthreduceCS }, sizeof(DepthReduceData));
	VkPipeline depthreducePipeline = createComputePipeline(device, pipelineCache, depthreduceCS, depthreduceProgram.layout);

	Shaders shaders = { &meshVS, &meshFS };
	Program meshProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, shaders, sizeof(Globals));

	Program meshProgramMS;
	if (meshShadingEnabled)
	{
		Shaders shadersMS = { &meshletTS, &meshletMS, &meshFS };
		meshProgramMS = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, shadersMS, sizeof(Globals));
	}

	VkPipeline meshPipeline = createGraphicsPipeline(device, pipelineCache, renderPass, shaders, meshProgram.layout);
	assert(meshPipeline);

	VkPipeline meshPipelineMS = 0;
	if (meshShadingEnabled)
	{
		meshPipelineMS = createGraphicsPipeline(device, pipelineCache, renderPass, { &meshletTS, &meshletMS, &meshFS }, meshProgramMS.layout);
		assert(meshPipelineMS);
	}

	VkQueryPool queryPoolTimestamp = createQueryPool(device, 128, VK_QUERY_TYPE_TIMESTAMP);
	assert(queryPoolTimestamp);

	VkQueryPool queryPoolPipeline = createQueryPool(device, 4, VK_QUERY_TYPE_PIPELINE_STATISTICS);
	assert(queryPoolPipeline);

	VkCommandPool commandPool = createCommandPool(device, graphicsFamily);
	assert(commandPool);

	VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocateInfo.commandPool = commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer = 0;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	Geometry geometry;

	for (size_t i = 1; i < argc; ++i)
	{
		if (!loadMesh(geometry, argv[i], meshShadingEnabled))
		{
			printf("Error: mesh %s failed to load\n", argv[i]);
		}
	}

	Buffer scratch = {};
	createBuffer(scratch, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	Buffer mb = {};
	createBuffer(mb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer vb = {};
	createBuffer(vb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer ib = {};
	createBuffer(ib, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer mlb = {};
	Buffer mvdb = {};
	Buffer midb = {};
	if (meshShadingEnabled)
	{
		createBuffer(mlb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(mvdb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(midb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	uploadBuffer(device, commandPool, commandBuffer, queue, mb, scratch, geometry.meshes.data(), geometry.meshes.size() * sizeof(Mesh));
	uploadBuffer(device, commandPool, commandBuffer, queue, vb, scratch, geometry.vertices.data(), geometry.vertices.size() * sizeof(Vertex));
	uploadBuffer(device, commandPool, commandBuffer, queue, ib, scratch, geometry.indices.data(), geometry.indices.size() * sizeof(uint32_t));

	if (meshShadingEnabled)
	{
		uploadBuffer(device, commandPool, commandBuffer, queue, mlb, scratch, geometry.meshlets.data(), geometry.meshlets.size() * sizeof(Meshlet));
		uploadBuffer(device, commandPool, commandBuffer, queue, mvdb, scratch, geometry.meshletVertexData.data(), geometry.meshletVertexData.size() * sizeof(unsigned int));
		uploadBuffer(device, commandPool, commandBuffer, queue, midb, scratch, geometry.meshletIndexData.data(), geometry.meshletIndexData.size() * sizeof(unsigned char));
	}

	uint32_t drawCount = 100'000;
	float sceneRatio = 5.f;
	float drawDistance = 10.f;

	std::vector<MeshDraw> draws(drawCount);

	srand(42);

	for (size_t i = 0; i < drawCount; ++i)
	{
		size_t meshIndex = rand() % geometry.meshes.size();
		const Mesh& mesh = geometry.meshes[meshIndex];

		draws[i].position[0] = (float(rand()) / RAND_MAX) * 2.f * sceneRatio - sceneRatio;
		draws[i].position[1] = (float(rand()) / RAND_MAX) * 2.f * sceneRatio - sceneRatio;
		draws[i].position[2] = (float(rand()) / RAND_MAX) * 2.f * sceneRatio - sceneRatio;
		draws[i].scale = (float(rand()) / RAND_MAX) * 0.2f;

		vec3 axis(float(rand()) / RAND_MAX * 2 - 1, float(rand()) / RAND_MAX * 2 - 1, float(rand()) / RAND_MAX * 2 - 1);
		float angle = glm::radians(float(rand()) / RAND_MAX * 90.f);
		draws[i].orientation = rotate(quat(1, 0, 0, 0), angle, axis);
		
		draws[i].meshIndex = uint32_t(meshIndex);
		draws[i].vertexOffset = mesh.vertexOffset;
	}

	Buffer db = {};
	createBuffer(db, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer dvb = {};
	createBuffer(dvb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	bool dvbCleared = false;

	uploadBuffer(device, commandPool, commandBuffer, queue, db, scratch, draws.data(), draws.size() * sizeof(MeshDraw));

	Buffer dcb = {};
	createBuffer(dcb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer dccb = {};
	createBuffer(dccb, device, memoryProperties, 4, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Image colorTarget = {};
	Image depthTarget = {};
	VkFramebuffer targetFB = 0;

	Image depthPyramid = {};
	VkImageView depthPyramidMips[16] = {};
	uint32_t depthPyramidWidth = 0;
	uint32_t depthPyramidHeight = 0;
	uint32_t depthPyramidLevels = 0;

	double frameCPUAvg = 0.0;
	double frameGPUAvg = 0.0;

	while (!glfwWindowShouldClose(window))
	{
		double frameCPUBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();
		
		SwapchainStatus swapchainStatus = updateSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, swapchainFormat);

		if (swapchainStatus == Swapchain_NotReady)
			continue;

		if (swapchainStatus == Swapchain_Resized || !targetFB)
		{
			if (colorTarget.image)
				destroyImage(colorTarget, device);
			if (depthTarget.image)
				destroyImage(depthTarget, device);
			if (targetFB)
				vkDestroyFramebuffer(device, targetFB, 0);

			if (depthPyramid.image)
			{
				for (uint32_t i = 0; i < depthPyramidLevels; ++i)
					vkDestroyImageView(device, depthPyramidMips[i], 0);
				destroyImage(depthPyramid, device);
			}

			createImage(colorTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, swapchainFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
			createImage(depthTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			targetFB = createFramebuffer(device, renderPass, colorTarget.imageView, depthTarget.imageView, swapchain.width, swapchain.height);
		
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
		}
		
		uint32_t imageIndex = 0;
		VK_CHECK(vkAcquireNextImageKHR(device, swapchain.swapchain, ~0ull, acquireSemaphore, VK_NULL_HANDLE, &imageIndex));

		VK_CHECK(vkResetCommandPool(device, commandPool, 0));

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		vkCmdResetQueryPool(commandBuffer, queryPoolTimestamp, 0, 128);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 0);

		if (!dvbCleared)
		{
			vkCmdFillBuffer(commandBuffer, dvb.buffer, 0, 4 * drawCount, 0);

			VkBufferMemoryBarrier fillBarrier = bufferBarrier(dvb.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 1, &fillBarrier, 0, 0);

			dvbCleared = true;
		}

		float znear = 1.f;
		mat4 projection = perspectiveProjection(glm::radians(70.f), float(swapchain.width) / float(swapchain.height), znear);
		mat4 projectionT = transpose(projection);

		vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
		vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

		DrawCullData cullData = {};
		cullData.P00 = projection[0][0];
		cullData.P11 = projection[1][1];
		cullData.znear = znear;
		cullData.zfar = drawDistance;
		cullData.frustum[0] = frustumX.x;
		cullData.frustum[1] = frustumX.z;
		cullData.frustum[2] = frustumY.y;
		cullData.frustum[3] = frustumY.z;
		cullData.drawCount = drawCount;
		cullData.cullingEnabled = int(cullingEnabled);
		cullData.lodEnabled = int(lodEnabled);
		cullData.occlusionEnabled = int(occlusionEnabled);
		cullData.lodBase = 10.f;
		cullData.lodStep = 1.5f;
		cullData.pyramidWidth = float(depthPyramidWidth);
		cullData.pyramidHeight = float(depthPyramidHeight);

		Globals globals = {};
		globals.projection = projection;
		globals.lodEnabled = int(lodEnabled);

		auto barrier = [&]()
		{
			VkMemoryBarrier wfi = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			wfi.srcAccessMask = 0x1ffff;
			wfi.dstAccessMask = 0x1ffff;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &wfi, 0, 0, 0, 0);
		};

		auto flush = [&]()
		{
				VK_CHECK(vkEndCommandBuffer(commandBuffer));

				VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffer;

				VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

				VK_CHECK(vkDeviceWaitIdle(device));

				VK_CHECK(vkResetCommandPool(device, commandPool, 0));

				VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

				VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		};

		auto cull = [&](VkPipeline pipeline, uint32_t timestamp)
		{
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

			VkBufferMemoryBarrier prefillBarrier = bufferBarrier(dccb.buffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 1, &prefillBarrier, 0, 0);
			
			vkCmdFillBuffer(commandBuffer, dccb.buffer, 0, 4, 0);

			VkBufferMemoryBarrier fillBarrier = bufferBarrier(dccb.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 1, &fillBarrier, 0, 0);


			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

			DescriptorInfo pyramidDesc{ depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL };
			DescriptorInfo descriptors[] = { db.buffer, mb.buffer, dcb.buffer, dccb.buffer, dvb.buffer, pyramidDesc };
			vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, drawcullProgram.updateTemplate, drawcullProgram.layout, 0, descriptors);

			vkCmdPushConstants(commandBuffer, drawcullProgram.layout, drawcullProgram.pushConstantStages, 0, sizeof(cullData), &cullData);
			vkCmdDispatch(commandBuffer, getGroupCount(uint32_t(draws.size()), drawcullCS.localSizeX), 1, 1);

			VkBufferMemoryBarrier cullBarriers[] =
			{
				bufferBarrier(dcb.buffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
				bufferBarrier(dccb.buffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
			};
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, 0, ARRAYSIZE(cullBarriers), cullBarriers, 0, 0);
		
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
		};	

		auto render = [&](VkRenderPass renderPass, uint32_t clearValueCount, const VkClearValue* clearValues, uint32_t query)
		{
			vkCmdBeginQuery(commandBuffer, queryPoolPipeline, query, 0);

			VkRenderPassBeginInfo passBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
			passBeginInfo.renderPass = renderPass;
			passBeginInfo.framebuffer = targetFB;
			passBeginInfo.renderArea.extent.width = swapchain.width;
			passBeginInfo.renderArea.extent.height = swapchain.height;
			passBeginInfo.clearValueCount = clearValueCount;
			passBeginInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(commandBuffer, &passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = { 0, float(swapchain.height), float(swapchain.width), -float(swapchain.height), 0, 1 };
			VkRect2D scissor = { {0, 0}, {uint32_t(swapchain.width), uint32_t(swapchain.height)} };

			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			if (meshShadingSupported && meshShadingEnabled)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineMS);

				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mvdb.buffer, midb.buffer, vb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshProgramMS.updateTemplate, meshProgramMS.layout, 0, descriptors);

				vkCmdPushConstants(commandBuffer, meshProgramMS.layout, meshProgramMS.pushConstantStages, 0, sizeof(globals), &globals);
				vkCmdDrawMeshTasksIndirectCountEXT(commandBuffer, dcb.buffer, offsetof(MeshDrawCommand, indirectMS), dccb.buffer, 0, uint32_t(draws.size()), sizeof(MeshDrawCommand));
			}
			else
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, vb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshProgram.updateTemplate, meshProgram.layout, 0, descriptors);

				vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdPushConstants(commandBuffer, meshProgram.layout, meshProgram.pushConstantStages, 0, sizeof(globals), &globals);
				vkCmdDrawIndexedIndirectCountKHR(commandBuffer, dcb.buffer, offsetof(MeshDrawCommand, indirect), dccb.buffer, 0, uint32_t(draws.size()), sizeof(MeshDrawCommand));
			}

			vkCmdEndRenderPass(commandBuffer);

			vkCmdEndQuery(commandBuffer, queryPoolPipeline, query);
		};

		auto pyramid = [&]()
		{
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 4);

			VkImageMemoryBarrier depthReadBarriers[] =
			{
				imageBarrier(depthTarget.image,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
				imageBarrier(depthPyramid.image, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL),
			};
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, ARRAYSIZE(depthReadBarriers), depthReadBarriers);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, depthreducePipeline);

			for (uint32_t i = 0; i < depthPyramidLevels; ++i)
			{
				DescriptorInfo sourceDepth = (i == 0)
					? DescriptorInfo(depthSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL)
					: DescriptorInfo(depthSampler, depthPyramidMips[i - 1], VK_IMAGE_LAYOUT_GENERAL);

				DescriptorInfo descriptors[] = { { depthPyramidMips[i], VK_IMAGE_LAYOUT_GENERAL }, sourceDepth };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, depthreduceProgram.updateTemplate, depthreduceProgram.layout, 0, descriptors);

				uint32_t levelWidth = std::max(1u, depthPyramidWidth >> i);
				uint32_t levelHeight = std::max(1u, depthPyramidHeight >> i);

				DepthReduceData reduceData = { vec2(levelWidth, levelHeight) };

				vkCmdPushConstants(commandBuffer, depthreduceProgram.layout, depthreduceProgram.pushConstantStages, 0, sizeof(reduceData), &reduceData);
				vkCmdDispatch(commandBuffer, getGroupCount(levelWidth, depthreduceCS.localSizeX), getGroupCount(levelHeight, depthreduceCS.localSizeY), 1);
				VkImageMemoryBarrier reduceBarrier = imageBarrier(depthPyramid.image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
				vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);
			}

			VkImageMemoryBarrier depthWriteBarrier = imageBarrier(depthTarget.image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthWriteBarrier);

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 5);
		};

		VkImageMemoryBarrier renderBeginBarriers[] =
		{
			imageBarrier(colorTarget.image, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
			imageBarrier(depthTarget.image, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),

		};

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, ARRAYSIZE(renderBeginBarriers), renderBeginBarriers);

		vkCmdResetQueryPool(commandBuffer, queryPoolPipeline, 0, 4);

		VkClearValue clearValues[2] = {};
		clearValues[0].color = { 48.f / 255.f, 10.f / 255.f, 36.f / 255.f, 1 };
		clearValues[1].depthStencil = { 0.f, 0 };

		VkImageMemoryBarrier depthPyramidBeforeCullBarrier = imageBarrier(depthPyramid.image, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthPyramidBeforeCullBarrier);

		// early cull: frustum cull & fill objects that *were* visible last frame
		cull(drawcullPipeline, 2);
		// early render: render objects that were visible last frame
		render(renderPass, ARRAYSIZE(clearValues), clearValues, 0);
		// depth pyramid generation
		pyramid();
		// late cull: frustum + occlusion cull & fill objects that were *not* visible last frame
		cull(drawculllatePipeline, 6);
		

		// late render: render objects that are visible this frame but weren't drawn in the early pass
		render(renderPassLate, 0, nullptr, 1);

		VkImageMemoryBarrier copyBarriers[] =
		{
			imageBarrier(colorTarget.image, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			imageBarrier(swapchain.images[imageIndex], 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),

		};

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, ARRAYSIZE(copyBarriers), copyBarriers);
	
		if (debugPyramid)
		{
			uint32_t levelWidth = std::max(1u, depthPyramidWidth >> debugPyramidLevel);
			uint32_t levelHeight = std::max(1u, depthPyramidHeight >> debugPyramidLevel);

			VkImageBlit blitRegion = {};
			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.mipLevel = debugPyramidLevel;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.srcOffsets[0] = { 0, 0, 0 };
			blitRegion.srcOffsets[1] = { int32_t(levelWidth), int32_t(levelHeight), 1 };
			blitRegion.dstOffsets[0] = { 0, 0, 0 };
			blitRegion.dstOffsets[1] = { int32_t(swapchain.width), int32_t(swapchain.height), 1 };

			vkCmdBlitImage(commandBuffer, depthPyramid.image, VK_IMAGE_LAYOUT_GENERAL, swapchain.images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_NEAREST);
		}
		else
		{
			VkImageCopy copyRegion = {};
			copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.srcSubresource.layerCount = 1;
			copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.dstSubresource.layerCount = 1;
			copyRegion.extent = { swapchain.width, swapchain.height, 1 };

			vkCmdCopyImage(commandBuffer, colorTarget.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain.images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		}

		VkImageMemoryBarrier presentBarrier = imageBarrier(swapchain.images[imageIndex], VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &presentBarrier);
		
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 1);
		
		VK_CHECK(vkEndCommandBuffer(commandBuffer));

		VkPipelineStageFlags submitStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &acquireSemaphore;
		submitInfo.pWaitDstStageMask = &submitStageMask;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &releaseSemaphore;

		VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &releaseSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain.swapchain;
		presentInfo.pImageIndices = &imageIndex;

		VK_CHECK(vkQueuePresentKHR(queue, &presentInfo));

		VK_CHECK(vkDeviceWaitIdle(device));

		uint64_t timestampResults[8] = {};
		VK_CHECK(vkGetQueryPoolResults(device, queryPoolTimestamp, 0, ARRAYSIZE(timestampResults), sizeof(timestampResults), timestampResults, sizeof(timestampResults[0]), VK_QUERY_RESULT_64_BIT));

		uint64_t pipelineResults[2] = {};
		VK_CHECK(vkGetQueryPoolResults(device, queryPoolPipeline, 0, ARRAYSIZE(pipelineResults), sizeof(pipelineResults), pipelineResults, sizeof(pipelineResults[0]), 0));

		uint32_t triangleCount = pipelineResults[0] + pipelineResults[1];

		double frameGPUBegin = double(timestampResults[0]) * props.limits.timestampPeriod * 1e-6;
		double frameGPUEnd = double(timestampResults[1]) * props.limits.timestampPeriod * 1e-6;
		double cullGPUTime = double(timestampResults[3] - timestampResults[2]) * props.limits.timestampPeriod * 1e-6;
		double pyramidGPUTime = double(timestampResults[5] - timestampResults[4]) * props.limits.timestampPeriod * 1e-6;
		double culllateGPUTime = double(timestampResults[7] - timestampResults[6]) * props.limits.timestampPeriod * 1e-6;
		
		double frameCPUEnd = glfwGetTime() * 1000.0;

		frameCPUAvg = frameCPUAvg * 0.9 + (frameCPUEnd - frameCPUBegin) * 0.1;
		frameGPUAvg = frameGPUAvg * 0.9 + (frameGPUEnd - frameGPUBegin) * 0.1;
		
		double trianglesPerSec = double(drawCount * triangleCount) / double(frameGPUAvg * 1e-3);
		double modelsPerSec = double(drawCount) / double(frameGPUAvg * 1e-3);

		char title[256];
		sprintf(title, "mesh shading %s; frustum culling: %s; occlusion culling: %s; lod: %s; cpu: %.2f ms; gpu %.2f ms (cull: %.2f ms, pyramid: %.2f ms, cull late: %.2f); triangles %.1fM; %.1fB tri/sec,%.1fM models/sec",
			meshShadingEnabled ? "ON" : "OFF",
			cullingEnabled ? "ON" : "OFF",
			occlusionEnabled ? "ON" : "OFF",
			lodEnabled ? "ON" : "OFF",
			frameCPUAvg, frameGPUAvg, cullGPUTime, pyramidGPUTime, culllateGPUTime, double(triangleCount) * 1e-6, trianglesPerSec * 1e-9, modelsPerSec * 1e-6);
		glfwSetWindowTitle(window, title);
	}

	VK_CHECK(vkDeviceWaitIdle(device));	

	if (depthPyramid.image)
	{
		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
			vkDestroyImageView(device, depthPyramidMips[i], 0);

		destroyImage(depthPyramid, device);
	}

	vkDestroyFramebuffer(device, targetFB, 0);
	destroyImage(colorTarget, device);
	destroyImage(depthTarget, device);
	
	destroyBuffer(dccb, device);
	destroyBuffer(dcb, device);
	destroyBuffer(dvb, device);
	destroyBuffer(db, device);

	destroyBuffer(mb, device);
	{
		destroyBuffer(mlb, device);
		destroyBuffer(mvdb, device);
		destroyBuffer(midb, device);
	}

	destroyBuffer(ib, device);
	destroyBuffer(vb, device);
	destroyBuffer(scratch, device);

	vkDestroyCommandPool(device, commandPool, 0);

	destroySwapchain(device, swapchain);
    
	vkDestroyQueryPool(device, queryPoolTimestamp, 0);
	vkDestroyQueryPool(device, queryPoolPipeline, 0);

	vkDestroyPipeline(device, meshPipeline, 0);
	destroyProgram(device, meshProgram);
	{
		vkDestroyPipeline(device, meshPipelineMS, 0);
		destroyProgram(device, meshProgramMS);
	}
	{
		vkDestroyPipeline(device, drawcullPipeline, 0);
		vkDestroyPipeline(device, drawculllatePipeline, 0);
		destroyProgram(device, drawcullProgram);
	}
	{
		vkDestroyPipeline(device, depthreducePipeline, 0);
		destroyProgram(device, depthreduceProgram);
	}

	destroyShader(meshVS, device);
	destroyShader(meshFS, device);
	destroyShader(drawcullCS, device);
	destroyShader(depthreduceCS, device);

	{
		destroyShader(meshletTS, device);
		destroyShader(meshletMS, device);
	}

	vkDestroySampler(device, depthSampler, 0);

	vkDestroyRenderPass(device, renderPass, 0);
	vkDestroyRenderPass(device, renderPassLate, 0);
    vkDestroySemaphore(device, acquireSemaphore, 0);
    vkDestroySemaphore(device, releaseSemaphore, 0);
    vkDestroySurfaceKHR(instance, surface, 0);
	glfwDestroyWindow(window);
    vkDestroyDevice(device, 0);

#ifdef _DEBUG
	vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);
#endif

	vkDestroyInstance(instance, 0);

    return 0;
}


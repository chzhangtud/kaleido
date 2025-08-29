#if defined(_WIN32)
#include <iostream>
#include <stdio.h>
#include <algorithm>
#include <stdarg.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION

#include "common.h"
#include "device.h"
#include "swapchain.h"
#include "resources.h"
#include "texture.h"
#include "shaders.h"
#include "math.h"
#include "config.h"
#include "GuiRenderer.h"
#include "ProfilingTools.h"
#include "scene.h"

bool meshShadingEnabled = true;
bool cullingEnabled = true;
bool lodEnabled = true;
bool occlusionEnabled = true;
bool clusterOcclusionEnabled = true;
bool taskShadingEnabled = false;
bool shadingEnabled = true;
bool shadowblurEnabled = true;

int debugGuiMode = 1;
int debugLodStep = 0;
bool reloadShaders = false;
double reloadShadersTimer = 0;

#define SHADER_PATH "shaders/"

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

struct MeshDrawCommand
{
	uint32_t drawId;
	VkDrawIndexedIndirectCommand indirect; // 5 uint32_t
};

struct MeshTaskCommand
{
	uint32_t drawId;
	uint32_t taskOffset;
	uint32_t taskCount;
	uint32_t lateDrawVisibility;
	uint32_t meshletVisibilityOffset;
};

struct alignas(16) CullData
{
	mat4 view;

	float P00, P11, znear, zfar;       // symmetric projection parameters
	float frustum[4];                  // data for left/right/top/bottom frustum planes
	float lodTarget;                   // lod target error at z=1
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint32_t drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
	int clusterOcclusionEnabled;
	int clusterBackfaceEnabled;

	uint32_t postPass;
};

struct alignas(16) Globals
{
	mat4 projection;
	CullData cullData;
	float screenWidth, screenHeight;
};

struct alignas(16) ShadowData
{
	vec3 sunDirection;
	float sunJitter;

	mat4 inverseViewProjection;
	vec2 imageSize;
};

struct alignas(16) ShadeData
{
	vec3 cameraPosition;
	float pad0;
	vec3 sunDirection;
	float pad1;

	mat4 inverseViewProjection;

	vec2 imageSize;
};

bool mousePressed = false;
Camera camera;
bool firstMouse = true;
float cameraSpeed = 5.0f;
float lastX = 400, lastY = 300;
float pitch = 0.0f;
float yaw = 0.0f;
float roll = 0.0f;

bool enableDollyZoom = false;
float soRef = 5.0f;
glm::vec3 cameraOriginForDolly = glm::vec3(0.0f);

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

struct alignas(16) TextData
{
	int offsetX, offsetY;
	int scale;
	unsigned int color;

	char data[112];
};

void buildBLAS(VkDevice device, const std::vector<Mesh>& meshes, const Buffer& vb, const Buffer& ib, std::vector<VkAccelerationStructureKHR>& blas, std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties)
{
	std::vector<uint32_t> primitiveCounts(meshes.size());
	std::vector<VkAccelerationStructureGeometryKHR> geometries(meshes.size());
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(meshes.size());

	const size_t kAlignment = 256;                   // required by spec for acceleration structures, could be smaller for scratch but it's a small waste
	const size_t kDefaultScratch = 32 * 1024 * 1024; // 32 MB scratch by default

	size_t totalAccelerationSize = 0;
	size_t totalPrimitiveCount = 0;
	size_t maxScratchSize = 0;

	std::vector<size_t> accelerationOffsets(meshes.size());
	std::vector<size_t> accelerationSizes(meshes.size());
	std::vector<size_t> scratchSizes(meshes.size());

	VkDeviceAddress vbAddress = getBufferAddress(vb, device);
	VkDeviceAddress ibAddress = getBufferAddress(ib, device);

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		const Mesh& mesh = meshes[i];
		VkAccelerationStructureGeometryKHR& geo = geometries[i];
		VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = buildInfos[i];

		unsigned int lodIndex = 0;
		primitiveCounts[i] = mesh.lods[lodIndex].indexCount / 3;

		geo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

		static_assert(offsetof(Vertex, vz) == offsetof(Vertex, vx) + sizeof(uint16_t) * 2, "Vertex layout mismatch");

		geo.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		geo.geometry.triangles.vertexFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		geo.geometry.triangles.vertexData.deviceAddress = vbAddress + mesh.vertexOffset * sizeof(Vertex);
		geo.geometry.triangles.vertexStride = sizeof(Vertex);
		geo.geometry.triangles.maxVertex = mesh.vertexCount - 1;
		geo.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geo.geometry.triangles.indexData.deviceAddress = ibAddress + mesh.lods[lodIndex].indexOffset * sizeof(uint32_t);

		buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geo;

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCounts[i], &sizeInfo);

		accelerationOffsets[i] = totalAccelerationSize;
		accelerationSizes[i] = sizeInfo.accelerationStructureSize;
		scratchSizes[i] = sizeInfo.buildScratchSize;

		totalAccelerationSize = (totalAccelerationSize + sizeInfo.accelerationStructureSize + kAlignment - 1) & ~(kAlignment - 1);
		totalPrimitiveCount += primitiveCounts[i];
		maxScratchSize = std::max(maxScratchSize, size_t(sizeInfo.buildScratchSize));
	}

	createBuffer(blasBuffer, device, memoryProperties, totalAccelerationSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer scratchBuffer;
	createBuffer(scratchBuffer, device, memoryProperties, std::max(kDefaultScratch, maxScratchSize), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	LOGI("BLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB (max %.2f MB), %.3fM triangles", double(totalAccelerationSize) / 1e6, double(scratchBuffer.size) / 1e6, double(maxScratchSize) / 1e6, double(totalPrimitiveCount) / 1e6);
	VkDeviceAddress scratchAddress = getBufferAddress(scratchBuffer, device);

	blas.resize(meshes.size());

	std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(meshes.size());
	std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangePtrs(meshes.size());

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		VkAccelerationStructureCreateInfoKHR accelerationInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		accelerationInfo.buffer = blasBuffer.buffer;
		accelerationInfo.offset = accelerationOffsets[i];
		accelerationInfo.size = accelerationSizes[i];
		accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelerationInfo, nullptr, &blas[i]));
	}

	VkQueryPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	createInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
	createInfo.queryCount = blas.size();

	VkQueryPool queryPool = 0;
	VK_CHECK(vkCreateQueryPool(device, &createInfo, 0, &queryPool));

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	VkBufferMemoryBarrier2 scratchBarrier = bufferBarrier(scratchBuffer.buffer,
	    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
	    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);

	for (size_t start = 0; start < meshes.size();)
	{
		size_t scratchOffset = 0;

		// aggregate the range that fits into allocated scratch
		size_t i = start;
		while (i < meshes.size() && scratchOffset + scratchSizes[i] <= scratchBuffer.size)
		{
			buildInfos[i].scratchData.deviceAddress = scratchAddress + scratchOffset;
			buildInfos[i].dstAccelerationStructure = blas[i];
			buildRanges[i].primitiveCount = primitiveCounts[i];
			buildRangePtrs[i] = &buildRanges[i];

			scratchOffset = (scratchOffset + scratchSizes[i] + kAlignment - 1) & ~(kAlignment - 1);
			++i;
		}
		assert(i > start); // guaranteed as scratchBuffer.size >= maxScratchSize

		vkCmdBuildAccelerationStructuresKHR(commandBuffer, uint32_t(i - start), &buildInfos[start], &buildRangePtrs[start]);
		start = i;

		pipelineBarrier(commandBuffer, 0, 1, &scratchBarrier, 0, nullptr);
	}

	vkCmdResetQueryPool(commandBuffer, queryPool, 0, blas.size());
	vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, blas.size(), blas.data(), VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, 0);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));

	compactedSizes.resize(blas.size());
	VK_CHECK(vkGetQueryPoolResults(device, queryPool, 0, blas.size(), blas.size() * sizeof(VkDeviceSize), compactedSizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

	vkDestroyQueryPool(device, queryPool, 0);

	destroyBuffer(scratchBuffer, device);
}

void compactBLAS(VkDevice device, std::vector<VkAccelerationStructureKHR>& blas, const std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties)
{
	const size_t kAlignment = 256; // required by spec for acceleration structures

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	size_t totalCompactedSize = 0;
	std::vector<size_t> compactedOffsets(blas.size());

	for (size_t i = 0; i < blas.size(); ++i)
	{
		compactedOffsets[i] = totalCompactedSize;
		totalCompactedSize = (totalCompactedSize + compactedSizes[i] + kAlignment - 1) & ~(kAlignment - 1);
	}

	LOGI("BLAS compacted accelerationStructureSize: %.2f MB", double(totalCompactedSize) / 1e6);

	Buffer compactedBuffer;
	createBuffer(compactedBuffer, device, memoryProperties, totalCompactedSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	std::vector<VkAccelerationStructureKHR> compactedBlas(blas.size());

	for (size_t i = 0; i < blas.size(); ++i)
	{
		VkAccelerationStructureCreateInfoKHR accelerationInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		accelerationInfo.buffer = compactedBuffer.buffer;
		accelerationInfo.offset = compactedOffsets[i];
		accelerationInfo.size = compactedSizes[i];
		accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelerationInfo, nullptr, &compactedBlas[i]));
	}

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	for (size_t i = 0; i < blas.size(); ++i)
	{
		VkCopyAccelerationStructureInfoKHR copyInfo = { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
		copyInfo.src = blas[i];
		copyInfo.dst = compactedBlas[i];
		copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

		vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
	}

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));

	for (size_t i = 0; i < blas.size(); ++i)
	{
		vkDestroyAccelerationStructureKHR(device, blas[i], nullptr);
		blas[i] = compactedBlas[i];
	}
	destroyBuffer(blasBuffer, device);
	blasBuffer = compactedBuffer;
}

VkAccelerationStructureKHR buildTLAS(VkDevice device, Buffer& tlasBuffer, const std::vector<MeshDraw>& draws, const std::vector<VkAccelerationStructureKHR>& blas, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties)
{
	Buffer instances;
	createBuffer(instances, device, memoryProperties, sizeof(VkAccelerationStructureInstanceKHR) * draws.size(), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	std::vector<VkDeviceAddress> blasAddresses(blas.size());

	for (size_t i = 0; i < blas.size(); ++i)
	{
		VkAccelerationStructureDeviceAddressInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		info.accelerationStructure = blas[i];

		blasAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR(device, &info);
	}

	for (size_t i = 0; i < draws.size(); ++i)
	{
		const MeshDraw& draw = draws[i];
		assert(draw.meshIndex < blas.size());

		mat3 xform = transpose(glm::mat3_cast(draw.orientation)) * draw.scale;

		VkAccelerationStructureInstanceKHR instance = {};
		memcpy(instance.transform.matrix[0], &xform[0], sizeof(float) * 3);
		memcpy(instance.transform.matrix[1], &xform[1], sizeof(float) * 3);
		memcpy(instance.transform.matrix[2], &xform[2], sizeof(float) * 3);
		instance.transform.matrix[0][3] = draw.position.x;
		instance.transform.matrix[1][3] = draw.position.y;
		instance.transform.matrix[2][3] = draw.position.z;
		instance.instanceCustomIndex = i;
		instance.mask = 1 << draw.postPass;
		instance.flags = draw.postPass ? VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR : 0;
		instance.accelerationStructureReference = blasAddresses[draw.meshIndex];

		memcpy(static_cast<VkAccelerationStructureInstanceKHR*>(instances.data) + i, &instance, sizeof(VkAccelerationStructureInstanceKHR));
	}

	VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.data.deviceAddress = getBufferAddress(instances, device);

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	uint32_t primitiveCount = uint32_t(draws.size());

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

	LOGI("TLAS accelerationStructureSize: %.2f MB, scratchSize: %.2f MB", double(sizeInfo.accelerationStructureSize) / 1e6, double(sizeInfo.buildScratchSize) / 1e6);

	createBuffer(tlasBuffer, device, memoryProperties, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer scratch;
	createBuffer(scratch, device, memoryProperties, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR accelerationInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	accelerationInfo.buffer = tlasBuffer.buffer;
	accelerationInfo.size = sizeInfo.accelerationStructureSize;
	accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	VkAccelerationStructureKHR tlas = nullptr;
	VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelerationInfo, nullptr, &tlas));

	buildInfo.dstAccelerationStructure = tlas;
	buildInfo.scratchData.deviceAddress = getBufferAddress(scratch, device);

	VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
	buildRange.primitiveCount = primitiveCount;
	const VkAccelerationStructureBuildRangeInfoKHR* buildRangePtr = &buildRange;

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &buildRangePtr);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));

	destroyBuffer(scratch, device);
	destroyBuffer(instances, device);

	return tlas;
}

#if defined(_WIN32)
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
			shadingEnabled = !shadingEnabled;
			return;
		}
		if (key == GLFW_KEY_B)
		{
			shadowblurEnabled = !shadowblurEnabled;
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
	}
}
#endif

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

struct pcg32_random_t
{
	uint64_t state;
	uint64_t inc;
};

#define PCG32_INITIALIZER { 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL }

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

pcg32_random_t rngstate = PCG32_INITIALIZER;

double rand01()
{
	return pcg32_random_r(&rngstate) / double(1ull << 32);
}

uint32_t rand32()
{
	return pcg32_random_r(&rngstate);
}

#if defined(_WIN32)
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
	camera.orientation = glm::quatLookAt(front, up);
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

#if defined(_WIN32)
int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		LOGE("Usage: %s [mesh list]", argv[0]);
		return 1;
	}

// #if defined(VK_USE_PLATFORM_XLIB_KHR)
// 	// TODO: We could support both X11 and Wayland, but that requires some tweaks in swapchain handling
// 	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
// #endif

	int rc = glfwInit();
	assert(rc);

	VK_CHECK(volkInitialize());

	VkInstance instance = createInstance();
	assert(instance);

	volkLoadInstanceOnly(instance);

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
	bool raytracingSupported = false;

	for (const auto& ext : extensions)
	{
		meshShadingSupported = meshShadingSupported || strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0;
		raytracingSupported = raytracingSupported || strcmp(ext.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
	}

	meshShadingEnabled = meshShadingSupported;

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	uint32_t graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(graphicsFamily != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = createDevice(instance, physicalDevice, graphicsFamily, meshShadingEnabled, raytracingSupported);
	assert(device);

	volkLoadDevice(device);

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

	VkFence frameFence = createFence(device);
	assert(frameFence);

	VkQueue queue = 0;
	vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

	VkSampler textureSampler = createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	assert(textureSampler);

	VkSampler readSampler = createSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	assert(readSampler);

	VkSampler depthSampler = createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_REDUCTION_MODE_MIN);
	assert(depthSampler);

	static const size_t gbufferCount = 2;
	const VkFormat gbufferFormats[gbufferCount] = {
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_A2B10G10R10_UNORM_PACK32,
	};

	VkPipelineRenderingCreateInfo gbufferInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	gbufferInfo.colorAttachmentCount = gbufferCount;
	gbufferInfo.pColorAttachmentFormats = gbufferFormats;
	gbufferInfo.depthAttachmentFormat = depthFormat;

	ShaderSet shaders;
	bool rcs = loadShaders(shaders, device, argv[0], "shaders/");
	assert(rcs);

	Swapchain swapchain;
	createSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

	VkDescriptorSetLayout textureSetLayout = createDescriptorArrayLayout(device);

	VkPipelineCache pipelineCache = 0;

	Program debugtextProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["debugtext.comp"] }, sizeof(TextData));

	Program drawcullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["drawcull.comp"] }, sizeof(CullData));

	Program tasksubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["tasksubmit.comp"] }, 0);

	Program clustersubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["clustersubmit.comp"] }, 0);

	Program clustercullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["clustercull.comp"] }, sizeof(CullData));

	Program depthreduceProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["depthreduce.comp"] }, sizeof(vec4));

	Program meshProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaders["mesh.vert"], &shaders["mesh.frag"] }, sizeof(Globals), textureSetLayout);

	Program meshtaskProgram = {};
	Program clusterProgram = {};
	if (meshShadingEnabled)
	{
		meshtaskProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaders["meshlet.task"], &shaders["meshlet.mesh"], &shaders["mesh.frag"] }, sizeof(Globals), textureSetLayout);

		clusterProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaders["meshlet.mesh"], &shaders["mesh.frag"] }, sizeof(Globals), textureSetLayout);
	}

	Program blitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["blit.comp"] }, sizeof(vec4));
	Program shadeProgram = {};
	Program shadowProgram = {};
	Program shadowblurProgram = {};
	if (raytracingSupported)
	{
		shadeProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["shade.comp"] }, sizeof(ShadeData));
		shadowProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["shadow.comp"] }, sizeof(ShadowData));
		shadowblurProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaders["shadowblur.comp"] }, sizeof(vec4));
	}

	VkPipeline debugtextPipeline = 0;
	VkPipeline drawcullPipeline = 0;
	VkPipeline drawculllatePipeline = 0;
	VkPipeline taskcullPipeline = 0;
	VkPipeline taskculllatePipeline = 0;
	VkPipeline tasksubmitPipeline = 0;
	VkPipeline clustersubmitPipeline = 0;
	VkPipeline clustercullPipeline = 0;
	VkPipeline clusterculllatePipeline = 0;
	VkPipeline depthreducePipeline = 0;
	VkPipeline meshPipeline = 0;
	VkPipeline meshpostPipeline = 0;
	VkPipeline meshtaskPipeline = 0;
	VkPipeline meshtasklatePipeline = 0;
	VkPipeline meshtaskpostPipeline = 0;
	VkPipeline clusterPipeline = 0;
	VkPipeline clusterpostPipeline = 0;
	VkPipeline blitPipeline = 0;
	VkPipeline shadePipeline = 0;
	VkPipeline shadowPipeline = 0;
	VkPipeline shadowblurPipeline = 0;

	auto pipelines = [&]()
	{
		auto replace = [&](VkPipeline& pipeline, VkPipeline newPipeline)
		{
			if (pipeline)
				vkDestroyPipeline(device, pipeline, 0);
			assert(newPipeline);
			pipeline = newPipeline;
		};

		replace(debugtextPipeline, createComputePipeline(device, pipelineCache, debugtextProgram));
		replace(drawcullPipeline, createComputePipeline(device, pipelineCache, drawcullProgram, true, /* LATE= */ VK_FALSE, /* TASK= */ VK_FALSE));
		replace(drawculllatePipeline, createComputePipeline(device, pipelineCache, drawcullProgram, true, /* LATE= */ VK_TRUE, /* TASK= */ VK_FALSE));
		replace(taskcullPipeline, createComputePipeline(device, pipelineCache, drawcullProgram, true, /* LATE= */ VK_FALSE, /* TASK= */ VK_TRUE));
		replace(taskculllatePipeline, createComputePipeline(device, pipelineCache, drawcullProgram, true, /* LATE= */ VK_TRUE, /* TASK= */ VK_TRUE));

		replace(tasksubmitPipeline, createComputePipeline(device, pipelineCache, tasksubmitProgram));
		replace(clustersubmitPipeline, createComputePipeline(device, pipelineCache, clustersubmitProgram));
		replace(clustercullPipeline, createComputePipeline(device, pipelineCache, clustercullProgram, true, /* LATE= */ VK_FALSE));
		replace(clusterculllatePipeline, createComputePipeline(device, pipelineCache, clustercullProgram, true, /* LATE= */ VK_TRUE));
		replace(depthreducePipeline, createComputePipeline(device, pipelineCache, depthreduceProgram));
		replace(meshPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram));
		replace(meshpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram, true, /* LATE= */ VK_FALSE, /* TASK= */ VK_FALSE, /* POST= */ VK_TRUE));

		if (meshShadingSupported)
		{
			replace(meshtaskPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, /* useSpecializationConstants = */ true, /* LATE= */ VK_FALSE, /* TASK= */ VK_TRUE));
			replace(meshtasklatePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, /* useSpecializationConstants = */ true, /* LATE= */ VK_TRUE, /* TASK= */ VK_TRUE, /* POST= */ VK_FALSE));
			replace(meshtaskpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, /* useSpecializationConstants = */ true, /* LATE= */ VK_TRUE, /* TASK= */ VK_TRUE, /* POST= */ VK_TRUE));
			replace(clusterPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram));
			replace(clusterpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram, true, /* LATE= */ VK_FALSE, /* TASK= */ VK_FALSE, /* POST= */ VK_TRUE));
		}

		replace(blitPipeline, createComputePipeline(device, pipelineCache, blitProgram));
		if (raytracingSupported)
		{
			replace(shadePipeline, createComputePipeline(device, pipelineCache, shadeProgram));
			replace(shadowPipeline, createComputePipeline(device, pipelineCache, shadowProgram));
			replace(shadowblurPipeline, createComputePipeline(device, pipelineCache, shadowblurProgram));
		}
	};

	pipelines();

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

	Buffer scratch = {};
	createBuffer(scratch, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	Geometry geometry;
	std::vector<Material> materials;
	std::vector<MeshDraw> draws;
	std::vector<std::string> texturePaths;

	// material index 0 is always dummy
	materials.resize(1);
	materials[0].diffuseFactor = vec4(1);

	camera.position = { 0.f, 0.f, 0.f };
	camera.orientation = { 0.f, 0.f, 0.f, 1.f };
	camera.fovY = glm::radians(70.f);
	vec3 sunDirection = normalize(vec3(1.0f, 1.0f, 1.0f));

	bool sceneMode = false;
	bool fastMode = getenv("FAST") && atoi(getenv("FAST"));

	if (argc == 2)
	{
		const char* ext = strrchr(argv[1], '.');
		if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
		{
			glm::vec3 euler(0.f);
			if (!loadScene(geometry, materials, draws, texturePaths, camera, sunDirection, argv[1], meshShadingSupported, euler, fastMode))
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

	std::vector<Image> images;
	size_t imageMemory = 0;
	double imageTimer = glfwGetTime();

	for (size_t i = 0; i < texturePaths.size(); ++i)
	{
		Image image;
		if (!loadImage(image, device, physicalDevice, commandPool, commandBuffer, queue, memoryProperties, scratch, texturePaths[i].c_str()))
		{
			LOGE("Error: image %s failed to load", texturePaths[i].c_str());
			return 1;
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetImageMemoryRequirements(device, image.image, &memoryRequirements);
		imageMemory += memoryRequirements.size;

		images.push_back(image);
	}

	LOGI("Loaded %d textures (%.2f MB) in %.2f sec", double(imageMemory) / 1e6, int(images.size()), glfwGetTime() - imageTimer);

	uint32_t descriptorCount = uint32_t(texturePaths.size() + 1);
	std::pair<VkDescriptorPool, VkDescriptorSet> textureSet = createDescriptorArray(device, textureSetLayout, descriptorCount);

	for (size_t i = 0; i < texturePaths.size(); ++i)
	{
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageView = images[i].imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = textureSet.second;
		write.dstBinding = 0;
		write.dstArrayElement = uint32_t(i + 1);
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	if (!sceneMode)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (!loadMesh(geometry, argv[i], meshShadingSupported, fastMode))
			{
				LOGE("Error: mesh %s failed to load", argv[i]);
				return 1;
			}
		}
	}

	if (geometry.meshes.empty())
	{
		LOGE("Error: no meshes loaded!");
		return 1;
	}

	LOGI("Geometry: VB %.2f MB, IB %.2f MB, meshlets %.2f MB",
	    double(geometry.vertices.size() * sizeof(Vertex)) / 1e6,
	    double(geometry.indices.size() * sizeof(uint32_t)) / 1e6,
	    double(geometry.meshlets.size() * sizeof(Meshlet) + geometry.meshletVertexData.size() * sizeof(unsigned int)) / 1e6 + geometry.meshletIndexData.size() * sizeof(unsigned char) / 1e6);

	if (draws.empty())
	{
		rngstate.state = 0x42;

		uint32_t drawCount = 100'000;
		draws.resize(drawCount);

		float sceneRadius = 150;

		for (uint32_t i = 0; i < drawCount; ++i)
		{
			MeshDraw& draw = draws[i];

			size_t meshIndex = rand32() % geometry.meshes.size();
			const Mesh& mesh = geometry.meshes[meshIndex];

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

	float drawDistance = 2000.f;
	uint32_t meshletVisibilityCount = 0;
	uint32_t meshPostPasses = 0;

	for (size_t i = 0; i < draws.size(); ++i)
	{
		MeshDraw& draw = draws[i];
		const Mesh& mesh = geometry.meshes[draw.meshIndex];

		draw.meshletVisibilityOffset = meshletVisibilityCount;

		uint32_t meshletCount = 0;
		for (uint32_t i = 0; i < mesh.lodCount; ++i)
			meshletCount = std::max(meshletCount, mesh.lods[i].meshletCount);

		meshletVisibilityCount += meshletCount;
		meshPostPasses |= 1 << draw.postPass;
	}

	uint32_t meshletVisibilityBytes = (meshletVisibilityCount + 31) / 32 * sizeof(uint32_t);

	uint32_t raytracingBufferFlags =
	    raytracingSupported
	        ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	        : 0;

	Buffer mb = {};
	createBuffer(mb, device, memoryProperties, geometry.meshes.size() * sizeof(Mesh), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer mtb = {};
	createBuffer(mtb, device, memoryProperties, materials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	Buffer vb = {};
	createBuffer(vb, device, memoryProperties, geometry.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer ib = {};
	createBuffer(ib, device, memoryProperties, geometry.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer mlb = {};
	Buffer mvdb = {};
	Buffer midb = {};
	if (meshShadingEnabled)
	{
		createBuffer(mlb, device, memoryProperties, geometry.meshlets.size() * sizeof(Meshlet), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(mvdb, device, memoryProperties, geometry.meshletVertexData.size() * sizeof(unsigned int), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(midb, device, memoryProperties, geometry.meshletIndexData.size() * sizeof(unsigned char), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	uploadBuffer(device, commandPool, commandBuffer, queue, mb, scratch, geometry.meshes.data(), geometry.meshes.size() * sizeof(Mesh));
	uploadBuffer(device, commandPool, commandBuffer, queue, mtb, scratch, materials.data(), materials.size() * sizeof(Material));
	uploadBuffer(device, commandPool, commandBuffer, queue, vb, scratch, geometry.vertices.data(), geometry.vertices.size() * sizeof(Vertex));
	uploadBuffer(device, commandPool, commandBuffer, queue, ib, scratch, geometry.indices.data(), geometry.indices.size() * sizeof(uint32_t));

	if (meshShadingEnabled)
	{
		uploadBuffer(device, commandPool, commandBuffer, queue, mlb, scratch, geometry.meshlets.data(), geometry.meshlets.size() * sizeof(Meshlet));
		uploadBuffer(device, commandPool, commandBuffer, queue, mvdb, scratch, geometry.meshletVertexData.data(), geometry.meshletVertexData.size() * sizeof(unsigned int));
		uploadBuffer(device, commandPool, commandBuffer, queue, midb, scratch, geometry.meshletIndexData.data(), geometry.meshletIndexData.size() * sizeof(unsigned char));
	}

	Buffer db = {};
	createBuffer(db, device, memoryProperties, draws.size() * sizeof(MeshDraw), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer dvb = {};
	createBuffer(dvb, device, memoryProperties, draws.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	bool dvbCleared = false;

	Buffer dcb = {};
	createBuffer(dcb, device, memoryProperties, TASK_WGLIMIT * sizeof(MeshTaskCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer dccb = {};
	createBuffer(dccb, device, memoryProperties, 16, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// TODO: there's a way to implement cluster visibility persistence *without* using bitwise storage at all, which may be beneficial on the balance, so we should try that.
	// *if* we do that, we can drop meshletVisibilityOffset et al from everywhere
	Buffer mvb = {};
	bool mvbCleared = false;
	if (meshShadingSupported)
	{
		createBuffer(mvb, device, memoryProperties, meshletVisibilityBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	Buffer cib = {};
	Buffer ccb = {};
	if (meshShadingSupported)
	{
		createBuffer(cib, device, memoryProperties, CLUSTER_LIMIT * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(ccb, device, memoryProperties, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	uploadBuffer(device, commandPool, commandBuffer, queue, db, scratch, draws.data(), draws.size() * sizeof(MeshDraw));

	std::vector<VkAccelerationStructureKHR> blas;
	VkAccelerationStructureKHR tlas = nullptr;
	Buffer blasBuffer = {};
	Buffer tlasBuffer = {};
	if (raytracingSupported)
	{
		std::vector<VkDeviceSize> compactedSizes;
		buildBLAS(device, geometry.meshes, vb, ib, blas, compactedSizes, blasBuffer, commandPool, commandBuffer, queue, memoryProperties);
		compactBLAS(device, blas, compactedSizes, blasBuffer, commandPool, commandBuffer, queue, memoryProperties);

		tlas = buildTLAS(device, tlasBuffer, draws, blas, commandPool, commandBuffer, queue, memoryProperties);
		LOGI("Ray Tracing is supported!");
	}
	else
		LOGW("Ray Tracing is not supported, this may cause artifacts!");


	Image gbufferTargets[gbufferCount] = {};
	Image depthTarget = {};
	Image shadowTarget = {};
	Image shadowblurTarget = {};

	Image depthPyramid = {};
	VkImageView depthPyramidMips[16] = {};
	uint32_t depthPyramidWidth = 0;
	uint32_t depthPyramidHeight = 0;
	uint32_t depthPyramidLevels = 0;

	std::vector<VkImageView> swapchainImageViews(swapchain.imageCount);

	double frameCPUAvg = 0.0;
	double frameGPUAvg = 0.0;

	uint64_t frameIndex = 0;

	uint64_t timestampResults[20] = {};
	uint64_t pipelineResults[3] = {};

	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, mouse_callback);

	// Initialize GUI renderer
	const auto& guiRenderer = GuiRenderer::GetInstance();
	VkSurfaceCapabilitiesKHR surfaceCaps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);
	uint32_t imageCount = std::max(2u, surfaceCaps.minImageCount);

	VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &swapchainFormat;
	renderingInfo.depthAttachmentFormat = depthFormat;
	guiRenderer->Initialize(window, CURRENT_VK_VERSION, instance, physicalDevice, device, graphicsFamily, queue, renderingInfo, textureSet.first, swapchainFormat, imageCount);

	float lastFrame = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		guiRenderer->BeginFrame();
		ImVec2 windowSize = ImVec2(800, 600);
		ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

		// update camera position
		float currentFrame = glfwGetTime();
		float deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		float velocity = cameraSpeed * deltaTime;

		glm::vec3 front = camera.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 right = camera.orientation * glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 up = glm::cross(right, front);

		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) != GLFW_PRESS)
		{
			if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
				camera.position -= front * velocity;
			if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
				camera.position += front * velocity;
			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
				camera.position -= right * velocity;
			if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
				camera.position += right * velocity;
		}

		double frameCPUBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();

		if (reloadShaders && glfwGetTime() >= reloadShadersTimer)
		{
			bool changed = false;

			for (Shader& shader : shaders.shaders)
			{
				if (shader.module)
					vkDestroyShaderModule(device, shader.module, 0);

				std::vector<char> oldSpirv = std::move(shader.spirv);

				std::string spirvPath = "/shaders/" + shader.name + ".spv";
				rcs = loadShader(shader, device, argv[0], spirvPath.c_str());
				assert(rcs);

				changed |= oldSpirv != shader.spirv;
			}

			if (changed)
			{
				VK_CHECK(vkDeviceWaitIdle(device));
				pipelines();
			}

			reloadShadersTimer = glfwGetTime() + 1;
		}


		SwapchainStatus swapchainStatus = updateSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

		if (swapchainStatus == Swapchain_NotReady)
			continue;

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
		}

		uint32_t imageIndex = 0;
		VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain.swapchain, ~0ull, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
			continue; // attempting to render to an out-of-date swapchain would break semaphore synchronization
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
			vkCmdFillBuffer(commandBuffer, dvb.buffer, 0, sizeof(uint32_t) * draws.size(), 0);

			VkBufferMemoryBarrier2 fillBarrier = bufferBarrier(dvb.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
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

		mat4 view = glm::mat4_cast(camera.orientation);
		// view[3] = vec4(-camera.position, 1.0f);
		// view = inverse(view);
		//  view = glm::scale(glm::identity<glm::mat4>(), vec3(1, 1, 1)) * view;
		view = glm::lookAt(camera.position, camera.position + front, up);

		float znear = 0.1f;
		mat4 projection;
		if (enableDollyZoom)
		{
			float so = soRef - glm::abs(glm::dot(glm::normalize(front), camera.position - cameraOriginForDolly));
			projection = perspectiveProjectionDollyZoom(camera.fovY, float(swapchain.width) / float(swapchain.height), znear, so, soRef);
		}
		else
		{
			projection = perspectiveProjection(camera.fovY, float(swapchain.width) / float(swapchain.height), znear);
		}
		mat4 projectionT = transpose(projection);

		vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
		vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

		CullData cullData = {};
		cullData.view = view;
		cullData.P00 = projection[0][0];
		cullData.P11 = projection[1][1];
		cullData.znear = znear;
		cullData.zfar = drawDistance;
		cullData.frustum[0] = frustumX.x;
		cullData.frustum[1] = frustumX.z;
		cullData.frustum[2] = frustumY.y;
		cullData.frustum[3] = frustumY.z;
		cullData.drawCount = uint32_t(draws.size());
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
			uint32_t rasterizationStage =
			    taskSubmit
			        ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV | VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV
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
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

				DescriptorInfo pyramidDesc{ depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL };
				DescriptorInfo descriptors[] = { db.buffer, mb.buffer, dcb.buffer, dccb.buffer, dvb.buffer, pyramidDesc };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, drawcullProgram.updateTemplate, drawcullProgram.layout, 0, descriptors);

				vkCmdPushConstants(commandBuffer, drawcullProgram.layout, drawcullProgram.pushConstantStages, 0, sizeof(cullData), &passData);
				vkCmdDispatch(commandBuffer, getGroupCount(uint32_t(draws.size()), drawcullProgram.localSizeX), 1, 1);
			}

			if (taskSubmit)
			{
				VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(dccb.buffer,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

				pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, tasksubmitPipeline);

				DescriptorInfo descriptors[] = { dccb.buffer, dcb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, tasksubmitProgram.updateTemplate, tasksubmitProgram.layout, 0, descriptors);
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
			assert(clearColors.size() == gbufferCount);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

			vkCmdBeginQuery(commandBuffer, queryPoolPipeline, query, 0);

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

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, late ? clusterculllatePipeline : clustercullPipeline);

				DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mlb.buffer, mvb.buffer, pyramidDesc, cib.buffer, ccb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, clustercullProgram.updateTemplate, clustercullProgram.layout, 0, descriptors);

				CullData passData = cullData;
				passData.postPass = postPass;

				vkCmdPushConstants(commandBuffer, clustercullProgram.layout, clustercullProgram.pushConstantStages, 0, sizeof(cullData), &passData);
				vkCmdDispatchIndirect(commandBuffer, dccb.buffer, 4);

				VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(ccb.buffer,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

				pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, clustersubmitPipeline);

				DescriptorInfo descriptors2[] = { ccb.buffer, cib.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, clustersubmitProgram.updateTemplate, clustersubmitProgram.layout, 0, descriptors2);

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

			Globals passGlobals = globals;
			passGlobals.cullData.postPass = postPass;

			if (clusterSubmit)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPass >= 1 ? clusterpostPipeline : clusterPipeline);

				DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mvdb.buffer, midb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer, textureSampler, mtb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, clusterProgram.updateTemplate, clusterProgram.layout, 0, descriptors);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, clusterProgram.layout, 1, 1, &textureSet.second, 0, nullptr);

				vkCmdPushConstants(commandBuffer, clusterProgram.layout, clusterProgram.pushConstantStages, 0, sizeof(globals), &passGlobals);
				vkCmdDrawMeshTasksIndirectEXT(commandBuffer, ccb.buffer, 4, 1, 0);
			}
			else if (taskSubmit)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPass >= 1 ? meshtaskpostPipeline : late ? meshtasklatePipeline
				                                                                                                              : meshtaskPipeline);

				DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mvdb.buffer, midb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer, textureSampler, mtb.buffer };

				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshtaskProgram.updateTemplate, meshtaskProgram.layout, 0, descriptors);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshtaskProgram.layout, 1, 1, &textureSet.second, 0, nullptr);

				vkCmdPushConstants(commandBuffer, meshtaskProgram.layout, meshtaskProgram.pushConstantStages, 0, sizeof(globals), &passGlobals);

				vkCmdDrawMeshTasksIndirectEXT(commandBuffer, dccb.buffer, 4, 1, 0);
			}
			else
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPass >= 1 ? meshpostPipeline : meshPipeline);

				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, vb.buffer, DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), textureSampler, mtb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshProgram.updateTemplate, meshProgram.layout, 0, descriptors);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshProgram.layout, 1, 1, &textureSet.second, 0, nullptr);

				vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdPushConstants(commandBuffer, meshProgram.layout, meshProgram.pushConstantStages, 0, sizeof(globals), &passGlobals);
				vkCmdDrawIndexedIndirectCount(commandBuffer, dcb.buffer, offsetof(MeshDrawCommand, indirect), dccb.buffer, 0, uint32_t(draws.size()), sizeof(MeshDrawCommand));
			}

			vkCmdEndRendering(commandBuffer);

			vkCmdEndQuery(commandBuffer, queryPoolPipeline, query);

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

				vec4 reduceData = vec4(levelWidth, levelHeight, 0.f, 0.f);

				vkCmdPushConstants(commandBuffer, depthreduceProgram.layout, depthreduceProgram.pushConstantStages, 0, sizeof(reduceData), &reduceData);
				vkCmdDispatch(commandBuffer, getGroupCount(levelWidth, depthreduceProgram.localSizeX), getGroupCount(levelHeight, depthreduceProgram.localSizeY), 1);
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

		vkCmdResetQueryPool(commandBuffer, queryPoolPipeline, 0, 4);

		std::vector<VkClearColorValue> clearColors(gbufferCount);
		clearColors[0] = { 135.f / 255.f, 206.f / 255.f, 235.f / 255.f, 0.f }; 
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
		if (meshPostPasses >> 1)
		{
			// post cull: frustum + occlusion cull & fill extra objects
			cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 12, "post cull", /* late= */ true, /* postPass= */ 1);

			// post render: render extra objects
			render(/* late= */ true, clearColors, depthClear, 2, 14, "post render", /* postPass= */ 1);
		}

		VkImageMemoryBarrier2 blitBarriers[2 + gbufferCount] = {
			imageBarrier(swapchain.images[imageIndex],
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
			imageBarrier(depthTarget.image,
			    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    VK_IMAGE_ASPECT_DEPTH_BIT)
		};

		for (uint32_t i = 0; i < gbufferCount; ++i)
			blitBarriers[i + 2] = imageBarrier(gbufferTargets[i].image,
			    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(blitBarriers), blitBarriers);

		if (raytracingSupported && shadingEnabled)
		{
			uint32_t timestamp = 16;
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
			VkImageMemoryBarrier2 preshadowBarrier =
				imageBarrier(shadowTarget.image,
					0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

			pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &preshadowBarrier);

			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadowPipeline);
				DescriptorInfo descriptors[] = { { shadowTarget.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, tlas };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, shadowProgram.updateTemplate, shadowProgram.layout, 0, descriptors);

				ShadowData shadowData = {};
				shadowData.sunDirection = sunDirection;
				shadowData.sunJitter = shadowblurEnabled ? 1e-2f : 0;
				shadowData.inverseViewProjection = inverse(projection * view);
				shadowData.imageSize = vec2(float(swapchain.width), float(swapchain.height));
				vkCmdPushConstants(commandBuffer, shadowProgram.layout, shadowProgram.pushConstantStages, 0, sizeof(shadowData), &shadowData);
				vkCmdDispatch(commandBuffer, getGroupCount(swapchain.width, shadowProgram.localSizeX), getGroupCount(swapchain.height, shadowProgram.localSizeY), 1);
			}

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);

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
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadowblurPipeline);
				DescriptorInfo descriptors[] = { { blurTo.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, blurFrom.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };

				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, shadowblurProgram.updateTemplate, shadowblurProgram.layout, 0, descriptors);
				vec4 blurData = vec4(float(swapchain.width), float(swapchain.height), pass == 0 ? 1 : 0, 0);

				vkCmdPushConstants(commandBuffer, shadowblurProgram.layout, shadowblurProgram.pushConstantStages, 0, sizeof(blurData), &blurData);
				vkCmdDispatch(commandBuffer, getGroupCount(swapchain.width, shadowblurProgram.localSizeX), getGroupCount(swapchain.height, shadowblurProgram.localSizeY), 1);
			}

			VkImageMemoryBarrier2 postblurBarrier =
			    imageBarrier(shadowTarget.image,
			        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

			pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &postblurBarrier);
			
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 2);
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadePipeline);

				DescriptorInfo descriptors[] = { { swapchainImageViews[imageIndex], VK_IMAGE_LAYOUT_GENERAL }, { readSampler, gbufferTargets[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { readSampler, gbufferTargets[1].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { readSampler, depthTarget.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { readSampler, shadowTarget.imageView, VK_IMAGE_LAYOUT_GENERAL } };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, shadeProgram.updateTemplate, shadeProgram.layout, 0, descriptors);

				ShadeData shadeData = {};
				shadeData.cameraPosition = camera.position;
				shadeData.sunDirection = sunDirection;
				shadeData.inverseViewProjection = inverse(projection * view);
				shadeData.imageSize = vec2(float(swapchain.width), float(swapchain.height));

				vkCmdPushConstants(commandBuffer, shadeProgram.layout, shadeProgram.pushConstantStages, 0, sizeof(shadeData), &shadeData);
				vkCmdDispatch(commandBuffer, getGroupCount(swapchain.width, shadeProgram.localSizeX), getGroupCount(swapchain.height, shadeProgram.localSizeY), 1);
			}
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 3);
		}
		else
		{
			uint32_t timestamp = 16;
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 2);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blitPipeline);

			DescriptorInfo descriptors[] = { { swapchainImageViews[imageIndex], VK_IMAGE_LAYOUT_GENERAL }, { readSampler, gbufferTargets[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
			vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, blitProgram.updateTemplate, blitProgram.layout, 0, descriptors);

			vec4 blitData = vec4(float(swapchain.width), float(swapchain.height), 0, 0);
			vkCmdPushConstants(commandBuffer, blitProgram.layout, blitProgram.pushConstantStages, 0, sizeof(blitData), &blitData);
			vkCmdDispatch(commandBuffer, getGroupCount(swapchain.width, blitProgram.localSizeX), getGroupCount(swapchain.height, blitProgram.localSizeY), 1);

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 3);
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
		static double finalGPUTime = 0.0;

		if (debugGuiMode % 3)
		{
			auto debugtext = [&](int line, const char* format, ...)
#ifdef __GNUC__
			                     __attribute__((format(printf, 3, 4)))
#endif
			{
				TextData textData = {};
				textData.offsetX = 1;
				textData.offsetY = line + 2;
				textData.scale = 2;
				textData.color = 0xffffffff; 

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

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, debugtextPipeline);

			DescriptorInfo descriptors[] = { { swapchainImageViews[imageIndex], VK_IMAGE_LAYOUT_GENERAL } };
			vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, debugtextProgram.updateTemplate, debugtextProgram.layout, 0, descriptors);

			// debug text goes here!
			uint64_t triangleCount = pipelineResults[0] + pipelineResults[1] + pipelineResults[2];

			double frameGpuBegin = double(timestampResults[0]) * props.limits.timestampPeriod * 1e-6;
			double frameGpuEnd = double(timestampResults[1]) * props.limits.timestampPeriod * 1e-6;

			cullGPUTime = double(timestampResults[3] - timestampResults[2]) * props.limits.timestampPeriod * 1e-6;
			renderGPUTime = double(timestampResults[5] - timestampResults[4]) * props.limits.timestampPeriod * 1e-6;
			pyramidGPUTime = double(timestampResults[7] - timestampResults[6]) * props.limits.timestampPeriod * 1e-6;
			culllateGPUTime = double(timestampResults[9] - timestampResults[8]) * props.limits.timestampPeriod * 1e-6;
			renderlateGPUTime = double(timestampResults[11] - timestampResults[10]) * props.limits.timestampPeriod * 1e-6;
			cullpostGPUTime = double(timestampResults[13] - timestampResults[12]) * props.limits.timestampPeriod * 1e-6;
			renderpostGPUTime = double(timestampResults[15] - timestampResults[14]) * props.limits.timestampPeriod * 1e-6;
			shadowsGPUTime = double(timestampResults[17] - timestampResults[16]) * props.limits.timestampPeriod * 1e-6;
			shadowblurGPUTime = double(timestampResults[18] - timestampResults[17]) * props.limits.timestampPeriod * 1e-6;
			finalGPUTime = double(timestampResults[19] - timestampResults[18]) * props.limits.timestampPeriod * 1e-6;
			
			double trianglesPerSec = double(triangleCount) / double(frameGPUAvg * 1e-3);
			double drawsPerSec = double(draws.size()) / double(frameGPUAvg * 1e-3);

			debugtext(0, "%scpu: %.2f ms; gpu: %.2f ms", reloadShaders ? "R* " : "", frameCPUAvg, frameGPUAvg);

			if (debugGuiMode % 3 == 2)
			{
				debugtext(2, "cull: %.2f ms, pyramid: %.2f ms, render: %.2f ms, shadows: %.2f ms, shadow blur: %.2f ms, final: %.2f ms",
				    cullGPUTime + culllateGPUTime + cullpostGPUTime,
				    pyramidGPUTime,
				    renderGPUTime + renderlateGPUTime + renderpostGPUTime,
				    shadowsGPUTime,
					shadowblurGPUTime,
				    finalGPUTime);
				debugtext(3, "triangles %.2fM; %.1fB tri / sec, %.1fM draws / sec",
				    double(triangleCount) * 1e-6, trianglesPerSec * 1e-9, drawsPerSec * 1e-6);
				debugtext(5, "frustum culling %s, occlusion culling %s, level-of-detail %s",
				    cullingEnabled ? "ON" : "OFF", occlusionEnabled ? "ON" : "OFF", lodEnabled ? "ON" : "OFF");
				debugtext(6, "mesh shading %s, task shading %s, cluster occlusion culling %s",
				    taskSubmit ? "ON" : "OFF", taskSubmit && taskShadingEnabled ? "ON" : "OFF",
				    clusterOcclusionEnabled ? "ON" : "OFF");

				debugtext(8, "RT shading %s, shadow blur %s",
				    raytracingSupported && shadingEnabled ? "ON" : "OFF",
				    raytracingSupported && shadingEnabled && shadowblurEnabled ? "ON" : "OFF");
			}
		}

		VkImageMemoryBarrier2 presentBarrier = imageBarrier(swapchain.images[imageIndex],
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
		    0, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &presentBarrier);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 1);

		static bool bDisplaySettings = false;
		static bool bDisplayProfiling = false;
		static bool bDisplayScene = false;
		if (ImGui::BeginMainMenuBar())
		{
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
			ImGui::Checkbox("Enable Deferred Shading", &shadingEnabled);
			ImGui::Checkbox("Enable Shadow Blurring", &shadowblurEnabled);
			ImGui::Checkbox("Enable LoD", &lodEnabled);
			if (lodEnabled)
			{
				ImGui::SetNextItemWidth(100.f);
				ImGui::DragInt("Level Index(LoD)", &debugLodStep, 1, 0, 9);
			}
			ImGui::Checkbox("Enable Reload Shaders", &reloadShaders);

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
				ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camera.position.x, camera.position.y, camera.position.z);
				ImGui::Text("Camera Rotation: (Pitch %.2f, Yaw %.2f, Roll %.2f)", pitch, yaw, roll);
				ImGui::SetNextItemWidth(200.f);
				ImGui::DragFloat("Camera Moving Speed", &cameraSpeed, 0.01f, 0.0f, 10.f);
				if (ImGui::Checkbox("Enable Dolly Zoom", &enableDollyZoom))
				{
					// Update the camera origin for dolly zoom
					cameraOriginForDolly = camera.position;
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
		guiRenderer->RenderDrawData(commandBuffer, swapchainImageViews[imageIndex], { swapchain.width, swapchain.height });

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
		releaseSemaphoreInfo.semaphore = releaseSemaphore;
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
		presentInfo.pWaitSemaphores = &releaseSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain.swapchain;
		presentInfo.pImageIndices = &imageIndex;

		VK_CHECK_SWAPCHAIN(vkQueuePresentKHR(queue, &presentInfo));

		VK_CHECK(vkWaitForFences(device, 1, &frameFence, VK_TRUE, ~0ull));
		VK_CHECK(vkResetFences(device, 1, &frameFence));

		auto res = vkGetQueryPoolResults(device, queryPoolTimestamp, 0, COUNTOF(timestampResults), sizeof(timestampResults), timestampResults, sizeof(timestampResults[0]), VK_QUERY_RESULT_64_BIT);
		
		VK_CHECK(vkGetQueryPoolResults(device, queryPoolTimestamp, 0, COUNTOF(timestampResults), sizeof(timestampResults), timestampResults, sizeof(timestampResults[0]), VK_QUERY_RESULT_64_BIT));

		VK_CHECK(vkGetQueryPoolResults(device, queryPoolPipeline, 0, COUNTOF(pipelineResults), sizeof(pipelineResults), pipelineResults, sizeof(pipelineResults[0]), VK_QUERY_RESULT_64_BIT));

		double frameGPUBegin = double(timestampResults[0]) * props.limits.timestampPeriod * 1e-6;
		double frameGPUEnd = double(timestampResults[1]) * props.limits.timestampPeriod * 1e-6;
		double frameCPUEnd = glfwGetTime() * 1000.0;

		frameCPUAvg = frameCPUAvg * 0.9 + (frameCPUEnd - frameCPUBegin) * 0.1;
		frameGPUAvg = frameGPUAvg * 0.9 + (frameGPUEnd - frameGPUBegin) * 0.1;

		frameIndex++;
	}

	guiRenderer->Shutdown(device);

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

	vkDestroyDescriptorPool(device, textureSet.first, 0);

	for (Image& image : images)
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
		destroyBuffer(mvdb, device);
		destroyBuffer(midb, device);
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
	}

	destroyBuffer(ib, device);
	destroyBuffer(vb, device);
	destroyBuffer(scratch, device);

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	vkDestroyCommandPool(device, commandPool, 0);

	destroySwapchain(device, swapchain);

	vkDestroyQueryPool(device, queryPoolTimestamp, 0);
	vkDestroyQueryPool(device, queryPoolPipeline, 0);

	vkDestroyPipeline(device, meshPipeline, 0);
	vkDestroyPipeline(device, meshpostPipeline, 0);
	destroyProgram(device, meshProgram);
	{
		vkDestroyPipeline(device, meshtaskPipeline, 0);
		vkDestroyPipeline(device, meshtasklatePipeline, 0);
		vkDestroyPipeline(device, meshtaskpostPipeline, 0);
		destroyProgram(device, meshtaskProgram);
	}
	{
		vkDestroyPipeline(device, debugtextPipeline, 0);
		destroyProgram(device, debugtextProgram);
	}
	{
		vkDestroyPipeline(device, drawcullPipeline, 0);
		vkDestroyPipeline(device, drawculllatePipeline, 0);
		vkDestroyPipeline(device, taskcullPipeline, 0);
		vkDestroyPipeline(device, taskculllatePipeline, 0);
		destroyProgram(device, drawcullProgram);
	}
	{
		vkDestroyPipeline(device, tasksubmitPipeline, 0);
		destroyProgram(device, tasksubmitProgram);
	}
	{
		vkDestroyPipeline(device, clustersubmitPipeline, 0);
		destroyProgram(device, clustersubmitProgram);

		vkDestroyPipeline(device, clustercullPipeline, 0);
		vkDestroyPipeline(device, clusterculllatePipeline, 0);
		destroyProgram(device, clustercullProgram);

		vkDestroyPipeline(device, clusterPipeline, 0);
		vkDestroyPipeline(device, clusterpostPipeline, 0);
		destroyProgram(device, clusterProgram);
	}
	{
		vkDestroyPipeline(device, depthreducePipeline, 0);
		destroyProgram(device, depthreduceProgram);
	}

	vkDestroyPipeline(device, blitPipeline, 0);
	destroyProgram(device, blitProgram);

	if (raytracingSupported)
	{
		vkDestroyPipeline(device, shadePipeline, 0);
		destroyProgram(device, shadeProgram);
		
		vkDestroyPipeline(device, shadowPipeline, 0);
		destroyProgram(device, shadowProgram);

		vkDestroyPipeline(device, shadowblurPipeline, 0);
		destroyProgram(device, shadowblurProgram);
	}

	vkDestroyDescriptorSetLayout(device, textureSetLayout, 0);

	for (Shader& shader : shaders.shaders)
		if (shader.module)
			vkDestroyShaderModule(device, shader.module, 0);

	vkDestroySampler(device, textureSampler, 0);
	vkDestroySampler(device, readSampler, 0);
	vkDestroySampler(device, depthSampler, 0);

	vkDestroyFence(device, frameFence, 0);
	vkDestroySemaphore(device, acquireSemaphore, 0);
	vkDestroySemaphore(device, releaseSemaphore, 0);
	vkDestroySurfaceKHR(instance, surface, 0);
	glfwDestroyWindow(window);
	vkDestroyDevice(device, 0);

	if (debugCallback)
		vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);

	vkDestroyInstance(instance, 0);

	volkFinalize();
}

#elif defined(__ANDROID__)
#include <jni.h>
#include <android/native_window_jni.h> // ANativeWindow_fromSurface
#include <vulkan/vulkan.h>
#include <memory>

#include "renderer.h"

static ANativeWindow* g_window = nullptr;
static std::unique_ptr<Renderer> g_renderer;

extern "C" {

JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jobject surface) {

//	VkInstance instance = createInstance();
//	assert(instance);
//	volkLoadInstanceOnly(instance);

    g_window = ANativeWindow_fromSurface(env, surface);
    g_renderer = std::make_unique<Renderer>();
    g_renderer->InitVulkan(g_window);
}

JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeRender(JNIEnv* env, jobject thiz) {
    if (g_renderer) g_renderer->DrawFrame();
}

JNIEXPORT void JNICALL
Java_com_chzhang_kaleido_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    g_renderer.reset();
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}

}
#endif
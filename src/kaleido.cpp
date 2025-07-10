#include <iostream>
#include <stdio.h>
#include <algorithm>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VOLK_IMPLEMENTATION
#include <fast_obj.h>
#include <meshoptimizer.h>
#include <cgltf.h>

#include "common.h"
#include "device.h"
#include "swapchain.h"
#include "resources.h"
#include "texture.h"
#include "shaders.h"
#include "math.h"
#include "config.h"

bool meshShadingEnabled = true;
bool cullingEnabled = true;
bool lodEnabled = true;
bool occlusionEnabled = true;
bool clusterOcclusionEnabled = true;
bool taskShadingEnabled = false;

bool debugPyramid = false;
int debugPyramidLevel = 0;
int debugLodStep = 0;

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

struct alignas(16) MeshDraw
{
	vec3 position;
	float scale;
	quat orientation;

	uint32_t meshIndex;
	uint32_t vertexOffset;
	uint32_t meshletVisibilityOffset;
};

struct MeshDrawCommand
{
	uint32_t drawId;
	VkDrawIndirectCommand indirect; // 4 uint32_t
};

struct MeshTaskCommand
{
	uint32_t drawId;
	uint32_t taskOffset;
	uint32_t taskCount;
	uint32_t lateDrawVisibility;
	uint32_t meshletVisibilityOffset;
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
	float error;
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

struct alignas(16) CullData
{
	mat4 view;

	float P00, P11, znear, zfar; // symmetric projection parameters
	float frustum[4]; // data for left/right/top/bottom frustum planes
	float lodTarget; // lod target error at z=1
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint32_t drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
	int clusterOcclusionEnabled;
};

struct alignas(16) Globals
{
	mat4 projection;
	CullData cullData;
	float screenWidth, screenHeight;
};

struct alignas(16) DepthReduceData
{
	vec2 imageSize;
};

struct Camera
{
	vec3 position;
	quat orientation;
	float fovY;
};

size_t appendMeshlets(Geometry& result, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, bool fast = false)
{
	size_t max_vertices = MESH_MAXVTX;
	size_t max_triangles = MESH_MAXTRI;
	const float cone_weight = 0.5f;

	std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles));
	std::vector<unsigned int> meshletVertexData(meshlets.size() * max_vertices);
	std::vector<unsigned char> meshletIndexData(meshlets.size() * max_triangles * 3);

	if (fast)
		meshlets.resize(meshopt_buildMeshletsScan(meshlets.data(), meshletVertexData.data(), meshletIndexData.data(), indices.data(), indices.size(), vertices.size(), max_vertices, max_triangles));
	else
		meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertexData.data(), meshletIndexData.data(), indices.data(), indices.size(), &vertices[0].vx, vertices.size(), sizeof(Vertex), max_vertices, max_triangles, cone_weight));
	uint32_t meshletVertexOffset = uint32_t(result.meshletVertexData.size());
	uint32_t meshletIndexOffset = uint32_t(result.meshletIndexData.size());

	result.meshletVertexData.insert(result.meshletVertexData.end(), meshletVertexData.begin(), meshletVertexData.end());
	result.meshletIndexData.insert(result.meshletIndexData.end(), meshletIndexData.begin(), meshletIndexData.end());

	for (auto& meshlet : meshlets)
	{
		meshopt_optimizeMeshlet(&meshletVertexData[meshlet.vertex_offset], &meshletIndexData[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
		
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

	return meshlets.size();
}

bool loadObj(std::vector<Vertex>& vertices, const char* path)
{
	fastObjMesh* obj = fast_obj_read(path);
	if (!obj)
		return false;

	size_t index_count = 0;

	for (unsigned int i = 0; i < obj->face_count; ++i)
		index_count += 3 * (obj->face_vertices[i] - 2);

	vertices.resize(index_count);

	size_t vertex_offset = 0;
	size_t index_offset = 0;

	for (unsigned int i = 0; i < obj->face_count; ++i)
	{
		for (unsigned int j = 0; j < obj->face_vertices[i]; ++j)
		{
			fastObjIndex gi = obj->indices[index_offset + j];

			// triangulate polygon on the fly; offset-3 is always the first polygon vertex
			if (j >= 3)
			{
				vertices[vertex_offset + 0] = vertices[vertex_offset - 3];
				vertices[vertex_offset + 1] = vertices[vertex_offset - 1];
				vertex_offset += 2;
			}

			Vertex& v = vertices[vertex_offset++];

			v.vx = obj->positions[gi.p * 3 + 0];
			v.vy = obj->positions[gi.p * 3 + 1];
			v.vz = obj->positions[gi.p * 3 + 2];
			v.nx = uint8_t(obj->normals[gi.n * 3 + 0] * 127.f + 127.5f);
			v.ny = uint8_t(obj->normals[gi.n * 3 + 1] * 127.f + 127.5f);
			v.nz = uint8_t(obj->normals[gi.n * 3 + 2] * 127.f + 127.5f);
			v.tu = meshopt_quantizeHalf(obj->texcoords[gi.t * 2 + 0]);
			v.tv = meshopt_quantizeHalf(obj->texcoords[gi.t * 2 + 1]);
		}

		index_offset += obj->face_vertices[i];
	}
	assert(vertex_offset == index_count);

	fast_obj_destroy(obj);

	return true;
}

void appendMesh(Geometry& result, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, bool buildMeshlets, bool fast = false)
{
	std::vector<uint32_t> remap(indices.size());
	size_t uniqueVertices = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));

	meshopt_remapVertexBuffer(vertices.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap.data());
	meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());

	vertices.resize(uniqueVertices);

	if (fast)
		meshopt_optimizeVertexCacheFifo(indices.data(), indices.data(), indices.size(), vertices.size(), 16);
	else
		meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());

	meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));

	Mesh mesh = {};

	mesh.vertexOffset = uint32_t(result.vertices.size());
	mesh.vertexCount = uint32_t(vertices.size());

	result.vertices.insert(result.vertices.end(), vertices.begin(), vertices.end());

	std::vector<vec3> normals(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex& v = vertices[i];
		normals[i] = vec3(v.nx / 127.f - 1.f, v.ny / 127.f - 1.f, v.nz / 127.f - 1.f);
	}

	vec3 center = vec3(0.f);

	for (const auto& v : vertices)
		center += vec3(v.vx, v.vy, v.vz);
	center /= float(vertices.size());

	float radius = 0.f;
	for (const auto& v : vertices)
		radius = glm::max(radius, distance(center, vec3(v.vx, v.vy, v.vz)));

	mesh.center = center;
	mesh.radius = radius;

	float lodScale = meshopt_simplifyScale(&vertices[0].vx, vertices.size(), sizeof(Vertex));

	std::vector<uint32_t> lodIndices = indices;
	float lodError = 0.f;
	float normalWeights[3] = { 1.f, 1.f, 1.f };

	while (mesh.lodCount < COUNTOF(mesh.lods))
	{
		MeshLod& lod = mesh.lods[mesh.lodCount++];

		lod.indexOffset = uint32_t(result.indices.size());
		lod.indexCount = uint32_t(lodIndices.size());

		result.indices.insert(result.indices.end(), lodIndices.begin(), lodIndices.end());

		lod.meshletOffset = uint32_t(result.meshlets.size());
		lod.meshletCount = buildMeshlets ? uint32_t(appendMeshlets(result, vertices, lodIndices, fast)) : 0;

		lod.error = lodError * lodScale;
		if (mesh.lodCount < COUNTOF(mesh.lods))
		{
			// note: we're using the same value for all LODs; if this changes, we need to remove/change 95% exit criteria below
			const float maxError = 1e-1f;
			const unsigned int options = 0;

			size_t nextIndicesTarget = (size_t(lodIndices.size() * 0.65f) / 3) * 3;
			float nextError = 0.f;
			size_t nextIndices = meshopt_simplifyWithAttributes(lodIndices.data(), lodIndices.data(), lodIndices.size(), &vertices[0].vx, vertices.size(), sizeof(Vertex), &normals[0].x, sizeof(vec3), normalWeights, 3, NULL, nextIndicesTarget, maxError, options, &nextError);
			assert(nextIndices <= lodIndices.size());

			// we've reached the error bound
			if (nextIndices == lodIndices.size() || nextIndices == 0)
				break;

			// while we could keep this LOD, it's too close to the last one (and it can't go below that due to constant error bound above)
			if (nextIndices >= size_t(double(lodIndices.size()) * 0.95))
				break;

			lodIndices.resize(nextIndices);

			lodError = std::max(lodError, nextError); // important! since we start from last LOD, we need to accumulate the error

			if (fast)
				meshopt_optimizeVertexCacheFifo(lodIndices.data(), lodIndices.data(), lodIndices.size(), vertices.size(), 16);
			else
				meshopt_optimizeVertexCache(lodIndices.data(), lodIndices.data(), lodIndices.size(), vertices.size());
		}
	}

	// pad meshlets to 64 to allow shaders to over-read when running task shaders
	while (result.meshlets.size() % 32)
		result.meshlets.push_back(Meshlet());

	result.meshes.emplace_back(mesh);

}

bool loadMesh(Geometry& result, const char* path, bool buildMeshlets, bool fast = false)
{
	std::vector<Vertex> vertices;
	if (!loadObj(vertices, path))
		return false;

	std::vector<uint32_t> indices(vertices.size());

	for (size_t i = 0; i < indices.size(); ++i)

		indices[i] = uint32_t(i);

	appendMesh(result, vertices, indices, buildMeshlets, fast);
	return true;
}

void decomposeTransform(float translation[3], float rotation[4], float scale[3], const float* transform)
{
	float m[4][4] = {};
	memcpy(m, transform, 16 * sizeof(float));

	// extract translation from last row
	translation[0] = m[3][0];
	translation[1] = m[3][1];
	translation[2] = m[3][2];

	// compute determinant to determine handedness
	float det =
		m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
		m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

	float sign = (det < 0.f) ? -1.f : 1.f;

	// recover scale from axis lengths
	scale[0] = sqrtf(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]) * sign;
	scale[1] = sqrtf(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]) * sign;
	scale[2] = sqrtf(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]) * sign;

	// normalize axes to get a pure rotation matrix
	float rsx = (scale[0] == 0.f) ? 0.f : 1.f / scale[0];
	float rsy = (scale[1] == 0.f) ? 0.f : 1.f / scale[1];
	float rsz = (scale[2] == 0.f) ? 0.f : 1.f / scale[2];

	float r00 = m[0][0] * rsx, r10 = m[1][0] * rsy, r20 = m[2][0] * rsz;
	float r01 = m[0][1] * rsx, r11 = m[1][1] * rsy, r21 = m[2][1] * rsz;
	float r02 = m[0][2] * rsx, r12 = m[1][2] * rsy, r22 = m[2][2] * rsz;

	// "branchless" version of Mike Day's matrix to quaternion conversion
	int qc = r22 < 0 ? (r00 > r11 ? 0 : 1) : (r00 < -r11 ? 2 : 3);
	float qs1 = qc & 2 ? -1.f : 1.f;
	float qs2 = qc & 1 ? -1.f : 1.f;
	float qs3 = (qc - 1) & 2 ? -1.f : 1.f;

	float qt = 1.f - qs3 * r00 - qs2 * r11 - qs1 * r22;
	float qs = 0.5f / sqrtf(qt);

	rotation[qc ^ 0] = qs * qt;
	rotation[qc ^ 1] = qs * (r01 + qs1 * r10);
	rotation[qc ^ 2] = qs * (r20 + qs2 * r02);
	rotation[qc ^ 3] = qs * (r12 + qs3 * r21);
}

bool loadScene(Geometry& geometry, std::vector<MeshDraw>& draws, std::vector<std::string>& texturePaths, Camera& camera, const char* path, bool buildMeshlets, bool fast = false)
{
	double timer = glfwGetTime();

	cgltf_options options = {};
	cgltf_data* data = NULL;
	cgltf_result res = cgltf_parse_file(&options, path, &data);
	if (res != cgltf_result_success)
		return false;

	std::unique_ptr<cgltf_data, void(*)(cgltf_data*)> dataPtr(data, &cgltf_free);

	res = cgltf_load_buffers(&options, data, path);
	if (res != cgltf_result_success)
	{
		return false;
	}

	res = cgltf_validate(data);
	if (res != cgltf_result_success)
	{
		return false;
	}

	std::vector<std::pair<unsigned int, unsigned int>> primitives;

	for (size_t i = 0; i < data->meshes_count; ++i)
	{
		const cgltf_mesh& mesh = data->meshes[i];

		size_t meshOffset = geometry.meshes.size();

		for (size_t pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles || !prim.indices)
				continue;

			size_t vertexCount = prim.attributes[0].data->count;
			std::vector<Vertex> vertices(vertexCount);

			std::vector<float> scratch(vertexCount * 4);

			if (const cgltf_accessor* pos = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0))
			{
				assert(cgltf_num_components(pos->type) == 3);
				cgltf_accessor_unpack_floats(pos, scratch.data(), vertexCount * 3);

				for (size_t j = 0; j < vertexCount; ++j)
				{
					vertices[j].vx = scratch[j * 3 + 0];
					vertices[j].vy = scratch[j * 3 + 1];
					vertices[j].vz = scratch[j * 3 + 2];
				}
			}

			if (const cgltf_accessor* nrm = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0))
			{
				assert(cgltf_num_components(nrm->type) == 3);
				cgltf_accessor_unpack_floats(nrm, scratch.data(), vertexCount * 3);

				for (size_t j = 0; j < vertexCount; ++j)
				{
					vertices[j].nx = uint8_t(scratch[j * 3 + 0] * 127.f + 127.5f);
					vertices[j].ny = uint8_t(scratch[j * 3 + 1] * 127.f + 127.5f);
					vertices[j].nz = uint8_t(scratch[j * 3 + 2] * 127.f + 127.5f);
				}
			}

			if (const cgltf_accessor* tex = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0))
			{
				assert(cgltf_num_components(tex->type) == 2);
				cgltf_accessor_unpack_floats(tex, scratch.data(), vertexCount * 2);

				for (size_t j = 0; j < vertexCount; ++j)
				{
					vertices[j].tu = meshopt_quantizeHalf(scratch[j * 2 + 0]);
					vertices[j].tv = meshopt_quantizeHalf(scratch[j * 2 + 1]);
				}
			}

			std::vector<uint32_t> indices(prim.indices->count);
			cgltf_accessor_unpack_indices(prim.indices, indices.data(), 4, indices.size());
			appendMesh(geometry, vertices, indices, buildMeshlets, fast);
		}

		primitives.push_back(std::make_pair(unsigned(meshOffset), unsigned(geometry.meshes.size() - meshOffset)));
	}

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node* node = &data->nodes[i];

		if (node->mesh)
		{
			float matrix[16];
			cgltf_node_transform_world(node, matrix);

			float translation[3];
			float rotation[4];
			float scale[3];
			decomposeTransform(translation, rotation, scale, matrix);

			// TODO: better warnings for non-uniform or negative scale

			std::pair<unsigned int, unsigned int> range = primitives[cgltf_mesh_index(data, node->mesh)];

			for (unsigned int j = 0; j < range.second; ++j)
			{
				MeshDraw draw = {};
				draw.position = vec3(translation[0], translation[1], translation[2]);
				draw.scale = std::max(scale[0], std::max(scale[1], scale[2]));
				draw.orientation = quat(rotation[0], rotation[1], rotation[2], rotation[3]);
				draw.meshIndex = range.first + j;
				draw.vertexOffset = geometry.meshes[range.first + j].vertexOffset;

				draws.push_back(draw);
			}
		}

		if (node->camera)
		{
			float matrix[16];
			cgltf_node_transform_world(node, matrix);

			float translation[3];
			float rotation[4];
			float scale[3];
			decomposeTransform(translation, rotation, scale, matrix);

			assert(node->camera->type == cgltf_camera_type_perspective);

			camera.position = vec3(translation[0], translation[1], translation[2]);
			camera.orientation = quat(rotation[0], rotation[1], rotation[2], rotation[3]);
			camera.fovY = node->camera->data.perspective.yfov;
		}
	}

	for (size_t i = 0; i < data->textures_count; ++i)
	{
		cgltf_texture* texture = &data->textures[i];
		assert(texture->image);

		cgltf_image* image = texture->image;
		assert(image->uri);

		std::string ipath = path;
		std::string::size_type pos = ipath.find_last_of('/\\');
		if (pos == std::string::npos)
			ipath = "";
		else
			ipath = ipath.substr(0, pos + 1);

		std::string uri = image->uri;
		uri.resize(cgltf_decode_uri(&uri[0]));

		std::string::size_type dot = uri.find_last_of('.');
		if (dot != std::string::npos)
			uri.replace(dot, uri.size() - dot, ".dds");

		texturePaths.push_back(ipath + uri);
	}

	printf(LOGI("Loaded %s: %d meshes, %d draws, %d vertices in %.2f sec\n"),
		path, int(geometry.meshes.size()), int(draws.size()), int(geometry.vertices.size()),
		glfwGetTime() - timer);

	if (buildMeshlets)
	{
		unsigned int meshletVtxs = 0, meshletTris = 0;

		for (Meshlet& meshlet : geometry.meshlets)
		{
			meshletVtxs += meshlet.vertexCount;
			meshletTris += meshlet.triangleCount;
		}

		printf("Meshlets: %d meshlets, %d triangles, %d vertex refs\n", int(geometry.meshlets.size()), int(meshletTris), int(meshletVtxs));
	}

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
		if (key == GLFW_KEY_P)
		{
			debugPyramid = !debugPyramid;
			return;
		}
		if (key == GLFW_KEY_T)
		{
			taskShadingEnabled = !taskShadingEnabled;
		}
		if (debugPyramid && (key >= GLFW_KEY_0 && key <= GLFW_KEY_9))
		{
			debugPyramidLevel = key - GLFW_KEY_0;
			return;
		}
		else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
		{
			debugLodStep = key - GLFW_KEY_0;
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
	return (xorshifted >> rot) | (xorshifted << ((32-rot) & 31));
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

int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		printf(LOGE("Usage: %s [mesh list]\n"), argv[0]);
		return 1;
	}

#if defined(VK_USE_PLATFORM_XLIB_KHR)
	// TODO: We could support both X11 and Wayland, but that requires some tweaks in swapchain handling
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

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

	for (const auto& ext : extensions)
	{
		meshShadingSupported = meshShadingSupported || strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0;
	}

	meshShadingEnabled = meshShadingSupported;

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	uint32_t graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(graphicsFamily != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = createDevice(instance, physicalDevice, graphicsFamily, meshShadingEnabled);
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

	VkSampler depthSampler = createSampler(device, VK_SAMPLER_REDUCTION_MODE_MIN);
	assert(depthSampler);

	VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &swapchainFormat;
	renderingInfo.depthAttachmentFormat = depthFormat;

	Shader meshletTS = {};
	Shader meshletMS = {};
	if (meshShadingEnabled)
	{
		bool tc = loadShader(meshletTS, device, SHADER_PATH "meshlet.task.spv");
		assert(tc);

		bool rc = loadShader(meshletMS, device, SHADER_PATH "meshlet.mesh.spv");
		assert(rc);
	}

	Shader drawcullCS = {};
	{
		bool rc = loadShader(drawcullCS, device, SHADER_PATH "drawcull.comp.spv");
		assert(rc);
	}

	Shader depthreduceCS = {};
	{
		bool rc = loadShader(depthreduceCS, device, SHADER_PATH "depthreduce.comp.spv");
		assert(rc);
	}

	Shader tasksubmitCS = {};
	{
		bool rc = loadShader(tasksubmitCS, device, SHADER_PATH "tasksubmit.comp.spv");
		assert(rc);
	}

	Shader clustersubmitCS = {};
	{
		bool rc = loadShader(clustersubmitCS, device, SHADER_PATH "clustersubmit.comp.spv");
		assert(rc);
	}

	Shader clustercullCS = {};
	{
		bool rc = loadShader(clustercullCS, device, SHADER_PATH "clustercull.comp.spv");
		assert(rc);
	}

	Shader meshVS = {};
	{
		bool rc = loadShader(meshVS, device, SHADER_PATH "mesh.vert.spv");
		assert(rc);
	}

	Shader meshFS = {};
	{
		bool rc = loadShader(meshFS, device, SHADER_PATH "mesh.frag.spv");
		assert(rc);
	}

	Swapchain swapchain;
	createSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

	// TODO: this is critical for performance!
	VkPipelineCache pipelineCache = 0;

	Program drawcullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &drawcullCS }, sizeof(CullData));
	VkPipeline drawcullPipeline = createComputePipeline(device, pipelineCache, drawcullCS, drawcullProgram.layout, true, /* LATE= */ VK_FALSE, /* TASK= */ VK_FALSE);
	VkPipeline drawculllatePipeline = createComputePipeline(device, pipelineCache, drawcullCS, drawcullProgram.layout, true, /* LATE= */ VK_TRUE,  /* TASK= */ VK_FALSE);
	VkPipeline taskcullPipeline = createComputePipeline(device, pipelineCache, drawcullCS, drawcullProgram.layout, true, /* LATE= */ VK_FALSE, /* TASK= */ VK_TRUE);
	VkPipeline taskculllatePipeline = createComputePipeline(device, pipelineCache, drawcullCS, drawcullProgram.layout, true, /* LATE= */ VK_TRUE,  /* TASK= */ VK_TRUE);
	
	Program tasksubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &tasksubmitCS }, 0);
	VkPipeline tasksubmitPipeline = createComputePipeline(device, pipelineCache, tasksubmitCS, tasksubmitProgram.layout);

	Program clustersubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &clustersubmitCS }, 0);
	VkPipeline clustersubmitPipeline = createComputePipeline(device, pipelineCache, clustersubmitCS, clustersubmitProgram.layout);

	Program clustercullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &clustercullCS }, sizeof(CullData));
	VkPipeline clustercullPipeline = createComputePipeline(device, pipelineCache, clustercullCS, clustercullProgram.layout, true, /* LATE= */ VK_FALSE);
	VkPipeline clusterculllatePipeline = createComputePipeline(device, pipelineCache, clustercullCS, clustercullProgram.layout, true, /* LATE= */ VK_TRUE);

	Program depthreduceProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &depthreduceCS }, sizeof(DepthReduceData));
	VkPipeline depthreducePipeline = createComputePipeline(device, pipelineCache, depthreduceCS, depthreduceProgram.layout);

	Shaders shaders = { &meshVS, &meshFS };
	Program meshProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, shaders, sizeof(Globals));

	Program meshtaskProgram;
	Program clusterProgram;
	if (meshShadingEnabled)
	{
		meshtaskProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &meshletTS, &meshletMS, &meshFS }, sizeof(Globals));

		clusterProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &meshletMS, &meshFS }, sizeof(Globals));
	}

	VkPipeline meshPipeline = createGraphicsPipeline(device, pipelineCache, renderingInfo, shaders, meshProgram.layout);
	assert(meshPipeline);

	VkPipeline meshtaskPipeline = 0;
	VkPipeline meshtasklatePipeline = 0;
	VkPipeline clusterPipeline = 0;
	
	if (meshShadingEnabled)
	{
		meshtaskPipeline = createGraphicsPipeline(device, pipelineCache, renderingInfo, { &meshletTS, &meshletMS, &meshFS }, meshtaskProgram.layout, /* useSpecializationConstants = */ true, /* LATE= */ VK_FALSE, /* TASK= */ VK_TRUE);
		meshtasklatePipeline = createGraphicsPipeline(device, pipelineCache, renderingInfo, { &meshletTS, &meshletMS, &meshFS }, meshtaskProgram.layout, /* useSpecializationConstants = */ true, /* LATE= */ VK_TRUE, /* TASK= */ VK_TRUE);
		clusterPipeline = createGraphicsPipeline(device, pipelineCache, renderingInfo, { &meshletMS, &meshFS }, clusterProgram.layout);
		assert(meshtaskPipeline && meshtasklatePipeline && clusterPipeline);
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

	Buffer scratch = {};
	createBuffer(scratch, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	Geometry geometry;
	std::vector<MeshDraw> draws;
	std::vector<std::string> texturePaths;

	Camera camera;
	camera.position = { 0.f, 0.f, 0.f };
	camera.orientation = { 0.f, 0.f, 0.f, 1.f };
	camera.fovY = glm::radians(70.f);

	bool sceneMode = false;
	bool fastMode = getenv("FAST") && atoi(getenv("FAST"));

	if (argc == 2)
	{
		const char* ext = strrchr(argv[1], '.');
		if (ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0))
		{
			if (!loadScene(geometry, draws, texturePaths, camera, argv[1], meshShadingSupported, fastMode))
			{
				printf(LOGE("Error: scene %s failed to load\n"), argv[1]);
				return 1;
			}

			sceneMode = true;
		}
	}

	std::vector<Image> images;
	double imageTimer = glfwGetTime();

	for (size_t i = 0; i < texturePaths.size(); ++i)
	{
		Image image;
		if (!loadImage(image, device, commandPool, commandBuffer, queue, memoryProperties, scratch, texturePaths[i].c_str()))
		{
			printf(LOGE("Error: image %s failed to load\n"), texturePaths[i].c_str());
			return 1;
		}

		images.push_back(image);
	}

	printf(LOGI("Loaded %d textures in %.2f sec\n"), int(images.size()), glfwGetTime() - imageTimer);

	if (!sceneMode)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (!loadMesh(geometry, argv[i], meshShadingSupported, fastMode))
			{
				printf(LOGE("Error: mesh %s failed to load\n"), argv[i]);
				return 1;
			}
		}
	}

	if (geometry.meshes.empty())
	{
		printf(LOGE("Error: no meshes loaded!\n"));
		return 1;
	}

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
			draw.vertexOffset = mesh.vertexOffset;
		}
	}

	float drawDistance = 200;
	uint32_t meshletVisibilityCount = 0;

	for (size_t i = 0; i < draws.size(); ++i)
	{
		MeshDraw& draw = draws[i];
		const Mesh& mesh = geometry.meshes[draw.meshIndex];

		draw.meshletVisibilityOffset = meshletVisibilityCount;

		uint32_t meshletCount = 0;
		for (uint32_t i = 0; i < mesh.lodCount; ++i)
			meshletCount = std::max(meshletCount, mesh.lods[i].meshletCount);

		meshletVisibilityCount += meshletCount;
	}

	uint32_t meshletVisibilityBytes = (meshletVisibilityCount + 31) / 32 * sizeof(uint32_t);

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

	Buffer db = {};
	createBuffer(db, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Buffer dvb = {};
	createBuffer(dvb, device, memoryProperties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

	Image colorTarget = {};
	Image depthTarget = {};

	Image depthPyramid = {};
	VkImageView depthPyramidMips[16] = {};
	uint32_t depthPyramidWidth = 0;
	uint32_t depthPyramidHeight = 0;
	uint32_t depthPyramidLevels = 0;

	double frameCPUAvg = 0.0;
	double frameGPUAvg = 0.0;

	uint64_t frameIndex = 0;

	while (!glfwWindowShouldClose(window))
	{
		double frameCPUBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();
		
		SwapchainStatus swapchainStatus = updateSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

		if (swapchainStatus == Swapchain_NotReady)
			continue;

		if (swapchainStatus == Swapchain_Resized || !colorTarget.image)
		{
			if (colorTarget.image)
				destroyImage(colorTarget, device);
			if (depthTarget.image)
				destroyImage(depthTarget, device);

			if (depthPyramid.image)
			{
				for (uint32_t i = 0; i < depthPyramidLevels; ++i)
					vkDestroyImageView(device, depthPyramidMips[i], 0);
				destroyImage(depthPyramid, device);
			}

			createImage(colorTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, swapchainFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
			createImage(depthTarget, device, memoryProperties, swapchain.width, swapchain.height, 1, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		
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
		view[3] = vec4(camera.position, 1.0f);
		view = inverse(view);
		// view = glm::scale(glm::identity<glm::mat4>(), vec3(1, 1, 1)) * view;

		float znear = 1.f;
		mat4 projection = perspectiveProjection(camera.fovY, float(swapchain.width) / float(swapchain.height), znear);
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

		auto cull = [&](VkPipeline pipeline, uint32_t timestamp, const char* phase, bool late)
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

			VkBufferMemoryBarrier2 fillBarriers[] =
			{
				bufferBarrier(dcb.buffer,
					VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | rasterizationStage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT),
				bufferBarrier(dccb.buffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
			};

			pipelineBarrier(commandBuffer, 0, COUNTOF(fillBarriers), fillBarriers, 1, &pyramidBarrier);

			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

				DescriptorInfo pyramidDesc{depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL};
				DescriptorInfo descriptors[] = { db.buffer, mb.buffer, dcb.buffer, dccb.buffer, dvb.buffer, pyramidDesc };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, drawcullProgram.updateTemplate, drawcullProgram.layout, 0, descriptors);

				vkCmdPushConstants(commandBuffer, drawcullProgram.layout, drawcullProgram.pushConstantStages, 0, sizeof(cullData), &cullData);
				vkCmdDispatch(commandBuffer, getGroupCount(uint32_t(draws.size()), drawcullCS.localSizeX), 1, 1);
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
				
			VkBufferMemoryBarrier2 cullBarriers[] =
			{
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

		auto render = [&](bool late, const VkClearColorValue& colorClear, const VkClearDepthStencilValue& depthClear, uint32_t query, uint32_t timestamp, const char* phase)
		{
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

			vkCmdBeginQuery(commandBuffer, queryPoolPipeline, query, 0);

			if (clusterSubmit)
			{
				VkBufferMemoryBarrier2 prefillBarrier = bufferBarrier(ccb.buffer,
					VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
				pipelineBarrier(commandBuffer, 0, 1, &prefillBarrier, 0, nullptr);

				vkCmdFillBuffer(commandBuffer, ccb.buffer, 0, 4, 0);

				VkBufferMemoryBarrier2 fillBarriers[] =
				{
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

				vkCmdPushConstants(commandBuffer, clustercullProgram.layout, clustercullProgram.pushConstantStages, 0, sizeof(cullData), &cullData);
				vkCmdDispatchIndirect(commandBuffer, dccb.buffer, 4);

				VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(ccb.buffer,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

				pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, clustersubmitPipeline);

				DescriptorInfo descriptors2[] = { ccb.buffer, cib.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, clustersubmitProgram.updateTemplate, clustersubmitProgram.layout, 0, descriptors2);

				vkCmdDispatch(commandBuffer, 1, 1, 1);

				VkBufferMemoryBarrier2 cullBarriers[] =
				{
					bufferBarrier(cib.buffer,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT),
					bufferBarrier(ccb.buffer,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
				};

				pipelineBarrier(commandBuffer, 0, COUNTOF(cullBarriers), cullBarriers, 0, nullptr);
			}

			VkRenderingAttachmentInfo colorAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			colorAttachment.imageView = colorTarget.imageView;
			colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			colorAttachment.loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.clearValue.color = colorClear;

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
			passInfo.colorAttachmentCount = 1;
			passInfo.pColorAttachments = &colorAttachment;
			passInfo.pDepthAttachment = &depthAttachment;

			vkCmdBeginRendering(commandBuffer, &passInfo);
			
			VkViewport viewport = { 0, float(swapchain.height), float(swapchain.width), -float(swapchain.height), 0, 1 };
			VkRect2D scissor = { {0, 0}, {uint32_t(swapchain.width), uint32_t(swapchain.height)} };

			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			if (clusterSubmit)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, clusterPipeline);

				DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mvdb.buffer, midb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, clusterProgram.updateTemplate, clusterProgram.layout, 0, descriptors);

				vkCmdPushConstants(commandBuffer, clusterProgram.layout, clusterProgram.pushConstantStages, 0, sizeof(globals), &globals);
				vkCmdDrawMeshTasksIndirectEXT(commandBuffer, ccb.buffer, 4, 1, 0);
			}
			else if (taskSubmit)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, late ? meshtasklatePipeline : meshtaskPipeline);
				
				DescriptorInfo pyramidDesc(depthSampler, depthPyramid.imageView, VK_IMAGE_LAYOUT_GENERAL);
				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mvdb.buffer, midb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer };
				
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshtaskProgram.updateTemplate, meshtaskProgram.layout, 0, descriptors);

				vkCmdPushConstants(commandBuffer, meshtaskProgram.layout, meshtaskProgram.pushConstantStages, 0, sizeof(globals), &globals);
				
				vkCmdDrawMeshTasksIndirectEXT(commandBuffer, dccb.buffer, 4, 1, 0);
			}
			else
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

				DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, vb.buffer };
				vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, meshProgram.updateTemplate, meshProgram.layout, 0, descriptors);

				vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdPushConstants(commandBuffer, meshProgram.layout, meshProgram.pushConstantStages, 0, sizeof(globals), &globals);
				vkCmdDrawIndexedIndirectCount(commandBuffer, dcb.buffer, offsetof(MeshDrawCommand, indirect), dccb.buffer, 0, uint32_t(draws.size()), sizeof(MeshDrawCommand));
			}

			vkCmdEndRendering(commandBuffer);

			vkCmdEndQuery(commandBuffer, queryPoolPipeline, query);

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
		};

		auto pyramid = [&]()
		{
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 4);

			VkImageMemoryBarrier2 depthBarriers[] =
			{
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

				DepthReduceData reduceData = { vec2(levelWidth, levelHeight) };

				vkCmdPushConstants(commandBuffer, depthreduceProgram.layout, depthreduceProgram.pushConstantStages, 0, sizeof(reduceData), &reduceData);
				vkCmdDispatch(commandBuffer, getGroupCount(levelWidth, depthreduceCS.localSizeX), getGroupCount(levelHeight, depthreduceCS.localSizeY), 1);
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

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 5);
		};

		VkImageMemoryBarrier2 renderBeginBarriers[] =
		{
			imageBarrier(colorTarget.image,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL),
			imageBarrier(depthTarget.image,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_IMAGE_ASPECT_DEPTH_BIT),
		};

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(renderBeginBarriers), renderBeginBarriers);

		vkCmdResetQueryPool(commandBuffer, queryPoolPipeline, 0, 4);

		VkClearColorValue colorClear = { 48.f / 255.f, 10.f / 255.f, 36.f / 255.f, 1 };
		VkClearDepthStencilValue depthClear = { 0.f, 0 };

		// early cull: frustum cull & fill objects that *were* visible last frame
		cull(taskSubmit ? taskcullPipeline : drawcullPipeline, 2, "early cull", /* late= */ false);
		// early render: render objects that were visible last frame
		render(/* late= */ false, colorClear, depthClear, 0, 8, "early render");
		// depth pyramid generation
		pyramid();
		// late cull: frustum + occlusion cull & fill objects that were *not* visible last frame
		cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 6, "late cull", /* late= */ true);
		

		// late render: render objects that are visible this frame but weren't drawn in the early pass
		render(/* late= */ true, colorClear, depthClear, 1, 10, "late render");

		VkImageMemoryBarrier2 copyBarriers[] =
		{
			imageBarrier(colorTarget.image,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			imageBarrier(swapchain.images[imageIndex],
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			imageBarrier(depthPyramid.image,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL),
		};

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(copyBarriers), copyBarriers);

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

		VkImageMemoryBarrier2 presentBarrier = imageBarrier(swapchain.images[imageIndex],
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &presentBarrier);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, 1);
		
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


		VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, frameFence));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &releaseSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain.swapchain;
		presentInfo.pImageIndices = &imageIndex;

		VK_CHECK_SWAPCHAIN(vkQueuePresentKHR(queue, &presentInfo));

		VK_CHECK(vkWaitForFences(device, 1, &frameFence, VK_TRUE, ~0ull));
		VK_CHECK(vkResetFences(device, 1, &frameFence));

		uint64_t timestampResults[12] = {};
		VK_CHECK(vkGetQueryPoolResults(device, queryPoolTimestamp, 0, COUNTOF(timestampResults), sizeof(timestampResults), timestampResults, sizeof(timestampResults[0]), VK_QUERY_RESULT_64_BIT));

		uint64_t pipelineResults[2] = {};
		VK_CHECK(vkGetQueryPoolResults(device, queryPoolPipeline, 0, COUNTOF(pipelineResults), sizeof(pipelineResults), pipelineResults, sizeof(pipelineResults[0]), VK_QUERY_RESULT_64_BIT));

		uint64_t triangleCount = pipelineResults[0] + pipelineResults[1];

		double frameGPUBegin = double(timestampResults[0]) * props.limits.timestampPeriod * 1e-6;
		double frameGPUEnd = double(timestampResults[1]) * props.limits.timestampPeriod * 1e-6;
		double cullGPUTime = double(timestampResults[3] - timestampResults[2]) * props.limits.timestampPeriod * 1e-6;
		double pyramidGPUTime = double(timestampResults[5] - timestampResults[4]) * props.limits.timestampPeriod * 1e-6;
		double culllateGPUTime = double(timestampResults[7] - timestampResults[6]) * props.limits.timestampPeriod * 1e-6;

		double renderGPUTime = double(timestampResults[9] - timestampResults[8]) * props.limits.timestampPeriod * 1e-6;
		double renderlateGPUTime = double(timestampResults[11] - timestampResults[10]) * props.limits.timestampPeriod * 1e-6;
		
		double frameCPUEnd = glfwGetTime() * 1000.0;

		frameCPUAvg = frameCPUAvg * 0.9 + (frameCPUEnd - frameCPUBegin) * 0.1;
		frameGPUAvg = frameGPUAvg * 0.9 + (frameGPUEnd - frameGPUBegin) * 0.1;
		
		double trianglesPerSec = double(triangleCount) / double(frameGPUAvg * 1e-3);
		double modelsPerSec = double(draws.size()) / double(frameGPUAvg * 1e-3);

		char title[512];
		snprintf(title, sizeof(title), "mesh shading %s; task shading %s; frustum culling: %s; occlusion culling: %s; lod: %s, cluster OC: %s; cpu: %.2f ms; gpu %.2f ms (cull: %.2f ms, render: %.2f ms, pyramid: %.2f ms, cull late: %.2f, renderlate: %.2f ms); triangles %.2fM; %.1fB tri/sec,%.1fM models/sec",
			taskSubmit ? "ON" : "OFF",
			taskSubmit && taskShadingEnabled ? "ON" : "OFF",
			cullingEnabled ? "ON" : "OFF",
			occlusionEnabled ? "ON" : "OFF",
			lodEnabled ? "ON" : "OFF",
			clusterOcclusionEnabled ? "ON" : "OFF",
			frameCPUAvg, frameGPUAvg, cullGPUTime, renderGPUTime, pyramidGPUTime, culllateGPUTime, renderlateGPUTime, double(triangleCount) * 1e-6, trianglesPerSec * 1e-9, modelsPerSec * 1e-6);
		glfwSetWindowTitle(window, title);

		frameIndex++;
	}

	VK_CHECK(vkDeviceWaitIdle(device));	

	if (depthPyramid.image)
	{
		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
			vkDestroyImageView(device, depthPyramidMips[i], 0);

		destroyImage(depthPyramid, device);
	}

	for (Image& image : images)
	{
		destroyImage(image, device);
	}

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
		destroyBuffer(mvb, device);
		destroyBuffer(cib, device);
		destroyBuffer(ccb, device);
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
		vkDestroyPipeline(device, meshtaskPipeline, 0);
		vkDestroyPipeline(device, meshtasklatePipeline, 0);
		destroyProgram(device, meshtaskProgram);
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
		destroyProgram(device, clusterProgram);
	}
	{
		vkDestroyPipeline(device, depthreducePipeline, 0);
		destroyProgram(device, depthreduceProgram);
	}

	destroyShader(meshVS, device);
	destroyShader(meshFS, device);
	destroyShader(drawcullCS, device);
	destroyShader(depthreduceCS, device);
	destroyShader(tasksubmitCS, device);
	destroyShader(clustersubmitCS, device);
	destroyShader(clustercullCS, device);

	{
		destroyShader(meshletTS, device);
		destroyShader(meshletMS, device);
	}

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


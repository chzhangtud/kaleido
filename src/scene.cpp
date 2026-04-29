#include "scene.h"
#include "scene_transforms.h"
#include "common.h"
#include "config.h"

#include <glm/gtc/type_ptr.hpp>

#include <fast_obj.h>
#include <cgltf.h>
#include <meshoptimizer.h>
#include <time.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

static void appendMeshlet(Geometry& result, const meshopt_Meshlet& meshlet, const std::vector<vec3>& vertices, const std::vector<unsigned int>& meshlet_vertices, const std::vector<unsigned char>& meshlet_triangles, uint32_t baseVertex, bool lod0)
{
	size_t dataOffset = result.meshletdata.size();

	unsigned int minVertex = ~0u, maxVertex = 0;
	for (unsigned int i = 0; i < meshlet.vertex_count; ++i)
	{
		minVertex = std::min(meshlet_vertices[meshlet.vertex_offset + i], minVertex);
		maxVertex = std::max(meshlet_vertices[meshlet.vertex_offset + i], maxVertex);
	}

	bool shortRefs = maxVertex - minVertex < (1 << 16);

	for (unsigned int i = 0; i < meshlet.vertex_count; ++i)
	{
		unsigned int ref = meshlet_vertices[meshlet.vertex_offset + i] - minVertex;
		if (shortRefs && i % 2)
			result.meshletdata.back() |= ref << 16;
		else
			result.meshletdata.push_back(ref);
	}

	const unsigned int* indexGroups = reinterpret_cast<const unsigned int*>(&meshlet_triangles[0] + meshlet.triangle_offset);
	unsigned int indexGroupCount = (meshlet.triangle_count * 3 + 3) / 4;

	for (unsigned int i = 0; i < indexGroupCount; ++i)
		result.meshletdata.push_back(indexGroups[i]);

	if (lod0)
	{
		for (unsigned int i = 0; i < meshlet.vertex_count; ++i)
		{
			unsigned int vtx = meshlet_vertices[meshlet.vertex_offset + i];
			unsigned short hx = meshopt_quantizeHalf(vertices[vtx].x);
			unsigned short hy = meshopt_quantizeHalf(vertices[vtx].y);
			unsigned short hz = meshopt_quantizeHalf(vertices[vtx].z);

			result.meshletvtx0.push_back(hx);
			result.meshletvtx0.push_back(hy);
			result.meshletvtx0.push_back(hz);
			result.meshletvtx0.push_back(0);
		}
	}

	meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[meshlet.vertex_offset], &meshlet_triangles[meshlet.triangle_offset], meshlet.triangle_count, &vertices[0].x, vertices.size(), sizeof(vec3));

	Meshlet m = {};
	m.dataOffset = uint32_t(dataOffset);
	m.baseVertex = baseVertex + minVertex;
	m.triangleCount = meshlet.triangle_count;
	m.vertexCount = meshlet.vertex_count;
	m.shortRefs = shortRefs;
	m.center = vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
	m.radius = bounds.radius;
	m.coneAxis[0] = bounds.cone_axis_s8[0];
	m.coneAxis[1] = bounds.cone_axis_s8[1];
	m.coneAxis[2] = bounds.cone_axis_s8[2];
	m.coneCutoff = bounds.cone_cutoff_s8;

	result.meshlets.push_back(m);
}

static size_t appendMeshlets(Geometry& result, const std::vector<vec3>& vertices, std::vector<uint32_t>& indices, uint32_t baseVertex, bool lod0, bool fast, bool clrt)
{
	const size_t max_vertices = MESH_MAXVTX;
	const size_t min_triangles = MESH_MAXTRI / 4 * 4;
	const size_t max_triangles = MESH_MAXTRI;
	const float cone_weight = 0.25f;
	const float fill_weight = 0.5f;

	std::vector<meshopt_Meshlet> meshlets(indices.size() / 3);
	std::vector<unsigned int> meshlet_vertices(meshlets.size() * max_vertices);
	std::vector<unsigned char> meshlet_triangles(meshlets.size() * max_triangles * 3);

	if (fast)
		meshlets.resize(meshopt_buildMeshletsScan(meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(), indices.size(), vertices.size(), max_vertices, max_triangles));
	else if (clrt && lod0) // only use spatial algo for lod0 as this is the only lod that is used for raytracing
		meshlets.resize(meshopt_buildMeshletsSpatial(meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(), indices.size(), &vertices[0].x, vertices.size(), sizeof(vec3), max_vertices, min_triangles, max_triangles, fill_weight));
	else
		meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(), indices.size(), &vertices[0].x, vertices.size(), sizeof(vec3), max_vertices, max_triangles, cone_weight));

	for (auto& meshlet : meshlets)
	{
		meshopt_optimizeMeshlet(&meshlet_vertices[meshlet.vertex_offset], &meshlet_triangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
		appendMeshlet(result, meshlet, vertices, meshlet_vertices, meshlet_triangles, baseVertex, lod0);
	}

	return meshlets.size();
}

static bool loadObj(std::vector<Vertex>& vertices, const char* path)
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

			v.vx = meshopt_quantizeHalf(obj->positions[gi.p * 3 + 0]);
			v.vy = meshopt_quantizeHalf(obj->positions[gi.p * 3 + 1]);
			v.vz = meshopt_quantizeHalf(obj->positions[gi.p * 3 + 2]);
			v.tp = 0;
			v.np = (meshopt_quantizeSnorm(obj->normals[gi.n * 3 + 0], 10) + 511) |
			       (meshopt_quantizeSnorm(obj->normals[gi.n * 3 + 1], 10) + 511) << 10 |
			       (meshopt_quantizeSnorm(obj->normals[gi.n * 3 + 1], 10) + 511) << 20;
			v.tu = meshopt_quantizeHalf(obj->texcoords[gi.t * 2 + 0]);
			v.tv = meshopt_quantizeHalf(obj->texcoords[gi.t * 2 + 1]);
		}

		index_offset += obj->face_vertices[i];
	}
	assert(vertex_offset == index_count);

	fast_obj_destroy(obj);

	return true;
}

static void appendMesh(Geometry& result, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, bool buildMeshlets, bool fast, bool clrt)
{
	std::vector<uint32_t> remap(vertices.size());
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

	std::vector<vec3> positions(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex& v = vertices[i];
		positions[i] = vec3(meshopt_dequantizeHalf(v.vx), meshopt_dequantizeHalf(v.vy), meshopt_dequantizeHalf(v.vz));
	}

	if (positions.empty())
	{
		mesh.aabbMin = vec3(0.f);
		mesh.aabbMax = vec3(0.f);
	}
	else
	{
		vec3 bbmin = positions[0];
		vec3 bbmax = positions[0];
		for (const vec3& p : positions)
		{
			bbmin = glm::min(bbmin, p);
			bbmax = glm::max(bbmax, p);
		}
		mesh.aabbMin = bbmin;
		mesh.aabbMax = bbmax;
	}

	std::vector<vec3> normals(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex& v = vertices[i];
		normals[i] = vec3((v.np & 1023) / 511.f - 1.f, ((v.np >> 10) & 1023) / 511.f - 1.f, ((v.np >> 20) & 1023) / 511.f - 1.f);
	}

	vec3 center = vec3(0.f);

	for (const auto& v : positions)
		center += v;

	center /= float(vertices.size());

	float radius = 0.f;
	for (const auto& v : positions)
		radius = glm::max(radius, distance(center, v));

	mesh.center = center;
	mesh.radius = radius;

	float lodScale = meshopt_simplifyScale(&positions[0].x, vertices.size(), sizeof(vec3));

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
		lod.meshletCount = buildMeshlets ? uint32_t(appendMeshlets(result, positions, lodIndices, mesh.vertexOffset, &lod == mesh.lods, fast, clrt)) : 0;

		lod.error = lodError * lodScale;
		if (mesh.lodCount < COUNTOF(mesh.lods))
		{
			// note: we're using the same value for all LODs; if this changes, we need to remove/change 95% exit criteria below
			const float maxError = 1e-1f;
			const unsigned int options = 0;

			size_t nextIndicesTarget = (size_t(lodIndices.size() * 0.65f) / 3) * 3;
			float nextError = 0.f;
			size_t nextIndices = meshopt_simplifyWithAttributes(lodIndices.data(), lodIndices.data(), lodIndices.size(), &positions[0].x, vertices.size(), sizeof(vec3), &normals[0].x, sizeof(vec3), normalWeights, 3, NULL, nextIndicesTarget, maxError, options, &nextError);
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

	result.meshes.emplace_back(mesh);
}

bool loadMesh(Geometry& result, const char* path, bool buildMeshlets, bool fast, bool clrt)
{
	std::vector<Vertex> vertices;
	if (!loadObj(vertices, path))
		return false;

	std::vector<uint32_t> indices(vertices.size());

	for (size_t i = 0; i < indices.size(); ++i)

		indices[i] = uint32_t(i);

	appendMesh(result, vertices, indices, buildMeshlets, fast, clrt);
	return true;
}

static void decomposeTransform(float translation[3], float rotation[4], float scale[3], const float* transform)
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

static void loadVertices(std::vector<Vertex>& vertices, const cgltf_primitive& prim)
{
	size_t vertexCount = prim.attributes[0].data->count;
	std::vector<float> scratch(vertexCount * 4);

	if (const cgltf_accessor* pos = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0))
	{
		assert(cgltf_num_components(pos->type) == 3);
		cgltf_accessor_unpack_floats(pos, scratch.data(), vertexCount * 3);

		for (size_t j = 0; j < vertexCount; ++j)
		{
			vertices[j].vx = meshopt_quantizeHalf(scratch[j * 3 + 0]);
			vertices[j].vy = meshopt_quantizeHalf(scratch[j * 3 + 1]);
			vertices[j].vz = meshopt_quantizeHalf(scratch[j * 3 + 2]);
		}
	}

	if (const cgltf_accessor* nrm = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0))
	{
		assert(cgltf_num_components(nrm->type) == 3);
		cgltf_accessor_unpack_floats(nrm, scratch.data(), vertexCount * 3);

		for (size_t j = 0; j < vertexCount; ++j)
		{
			float nx = scratch[j * 3 + 0], ny = scratch[j * 3 + 1], nz = scratch[j * 3 + 2];

			vertices[j].np = (meshopt_quantizeSnorm(nx, 10) + 511) |
			                 (meshopt_quantizeSnorm(ny, 10) + 511) << 10 |
			                 (meshopt_quantizeSnorm(nz, 10) + 511) << 20;
		}
	}

	if (const cgltf_accessor* tan = cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0))
	{
		assert(cgltf_num_components(tan->type) == 4);
		cgltf_accessor_unpack_floats(tan, scratch.data(), vertexCount * 4);

		for (size_t j = 0; j < vertexCount; ++j)
		{
			float tx = scratch[j * 4 + 0], ty = scratch[j * 4 + 1], tz = scratch[j * 4 + 2];
			float tsum = fabsf(tx) + fabsf(ty) + fabsf(tz);
			float tu = tz >= 0 ? tx / tsum : (1 - fabsf(ty / tsum)) * (tx >= 0 ? 1 : -1);
			float tv = tz >= 0 ? ty / tsum : (1 - fabsf(tx / tsum)) * (ty >= 0 ? 1 : -1);

			vertices[j].tp = (meshopt_quantizeSnorm(tu, 8) + 127) | (meshopt_quantizeSnorm(tv, 8) + 127) << 8;
			vertices[j].np |= (scratch[j * 4 + 3] >= 0 ? 0 : 1) << 30;
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
}

// True if this material must render only in the transparency G-buffer pass (postPass == 1).
static bool MaterialNeedsTransparencyPass(const cgltf_material& material)
{
	if (material.alpha_mode != cgltf_alpha_mode_opaque)
		return true;
	if (material.has_transmission)
		return true;
	if (material.has_pbr_metallic_roughness)
	{
		const float a = material.pbr_metallic_roughness.base_color_factor[3];
		if (a < 1.0f - 1e-5f)
			return true;
	}
	if (material.has_pbr_specular_glossiness)
	{
		const float a = material.pbr_specular_glossiness.diffuse_factor[3];
		if (a < 1.0f - 1e-5f)
			return true;
	}
	return false;
}

static MaterialKey BuildPbrMaterialKey(const cgltf_material& material, int workflow)
{
	uint32_t alpha = (uint32_t)material.alpha_mode;
	if (alpha > (uint32_t)cgltf_alpha_mode_blend)
		alpha = (uint32_t)cgltf_alpha_mode_opaque;

	uint32_t wf = (uint32_t)workflow & 0xFu;

	return MaterialKey::Pack(MaterialType::PBR, wf, alpha & 3u, material.double_sided ? 1u : 0u, material.has_transmission ? 1u : 0u);
}

static PBRMaterial BuildPbrMaterial(const cgltf_data* data, const cgltf_material& material, int textureOffset)
{
	PBRMaterial result = PBRMaterial::CreateDefault();
	Material& mat = result.data;

	if (material.has_pbr_specular_glossiness)
	{
		if (material.pbr_specular_glossiness.diffuse_texture.texture)
			mat.albedoTexture = textureOffset + int(cgltf_texture_index(data, material.pbr_specular_glossiness.diffuse_texture.texture));

		mat.baseColorFactor = vec4(material.pbr_specular_glossiness.diffuse_factor[0], material.pbr_specular_glossiness.diffuse_factor[1], material.pbr_specular_glossiness.diffuse_factor[2], material.pbr_specular_glossiness.diffuse_factor[3]);

		if (material.pbr_specular_glossiness.specular_glossiness_texture.texture)
			mat.pbrTexture = textureOffset + int(cgltf_texture_index(data, material.pbr_specular_glossiness.specular_glossiness_texture.texture));

		mat.pbrFactor = vec4(material.pbr_specular_glossiness.specular_factor[0], material.pbr_specular_glossiness.specular_factor[1], material.pbr_specular_glossiness.specular_factor[2], material.pbr_specular_glossiness.glossiness_factor);
		mat.workflow = 2;
	}
	else if (material.has_pbr_metallic_roughness)
	{
		if (material.pbr_metallic_roughness.base_color_texture.texture)
			mat.albedoTexture = textureOffset + int(cgltf_texture_index(data, material.pbr_metallic_roughness.base_color_texture.texture));

		mat.baseColorFactor = vec4(material.pbr_metallic_roughness.base_color_factor[0], material.pbr_metallic_roughness.base_color_factor[1], material.pbr_metallic_roughness.base_color_factor[2], material.pbr_metallic_roughness.base_color_factor[3]);

		if (material.pbr_metallic_roughness.metallic_roughness_texture.texture)
			mat.pbrTexture = textureOffset + int(cgltf_texture_index(data, material.pbr_metallic_roughness.metallic_roughness_texture.texture));

		mat.pbrFactor = vec4(1, 1, material.pbr_metallic_roughness.metallic_factor, material.pbr_metallic_roughness.roughness_factor);
		mat.workflow = 1;
	}

	if (material.normal_texture.texture)
		mat.normalTexture = textureOffset + int(cgltf_texture_index(data, material.normal_texture.texture));

	if (material.emissive_texture.texture)
		mat.emissiveTexture = textureOffset + int(cgltf_texture_index(data, material.emissive_texture.texture));

	if (material.occlusion_texture.texture)
		mat.occlusionTexture = textureOffset + int(cgltf_texture_index(data, material.occlusion_texture.texture));

	mat.emissiveFactor[0] = material.emissive_factor[0];
	mat.emissiveFactor[1] = material.emissive_factor[1];
	mat.emissiveFactor[2] = material.emissive_factor[2];

	const float emissiveStrength = material.has_emissive_strength ? material.emissive_strength.emissive_strength : 1.f;
	mat.shadingParams = vec4(material.normal_texture.scale,
	    material.occlusion_texture.texture ? material.occlusion_texture.scale : 1.f,
	    material.alpha_cutoff,
	    emissiveStrength);
	mat.alphaMode = int32_t(material.alpha_mode);

	mat.transmissionTexture = 0;
	mat.transmissionFactor = 0.f;
	mat.ior = 1.5f;
	mat.doubleSided = material.double_sided ? 1 : 0;
	if (material.has_transmission)
	{
		mat.transmissionFactor = material.transmission.transmission_factor;
		if (material.transmission.transmission_texture.texture)
			mat.transmissionTexture = textureOffset + int(cgltf_texture_index(data, material.transmission.transmission_texture.texture));
	}
	if (material.has_ior)
		mat.ior = material.ior.ior;

	result.key = BuildPbrMaterialKey(material, mat.workflow);

	return result;
}

static void FillGltfDocumentOutline(cgltf_data* data, GltfDocumentOutline& out)
{
	out = GltfDocumentOutline{};
	out.loaded = true;

	out.nodes.resize(data->nodes_count);
	for (cgltf_size i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node& n = data->nodes[i];
		GltfNodeOutline& node = out.nodes[i];
		node.name = (n.name && n.name[0]) ? n.name : ("Node " + std::to_string(i));
		if (n.mesh)
			node.meshIndex = int32_t(cgltf_mesh_index(data, n.mesh));
		node.children.reserve(n.children_count);
		for (cgltf_size c = 0; c < n.children_count; ++c)
			node.children.push_back(uint32_t(cgltf_node_index(data, n.children[c])));
	}

	out.scenes.resize(data->scenes_count);
	for (cgltf_size s = 0; s < data->scenes_count; ++s)
	{
		const cgltf_scene& sc = data->scenes[s];
		GltfSceneOutline& sceneOut = out.scenes[s];
		sceneOut.name = (sc.name && sc.name[0]) ? sc.name : ("Scene " + std::to_string(s));
		sceneOut.rootNodes.reserve(sc.nodes_count);
		for (cgltf_size r = 0; r < sc.nodes_count; ++r)
			sceneOut.rootNodes.push_back(uint32_t(cgltf_node_index(data, sc.nodes[r])));
	}

	if (data->scene)
		out.defaultSceneIndex = int32_t(cgltf_scene_index(data, data->scene));
	else if (data->scenes_count > 0)
		out.defaultSceneIndex = 0;
	else
		out.defaultSceneIndex = -1;

	out.meshNames.resize(data->meshes_count);
	for (cgltf_size i = 0; i < data->meshes_count; ++i)
	{
		const cgltf_mesh& m = data->meshes[i];
		out.meshNames[i] = (m.name && m.name[0]) ? m.name : ("Mesh " + std::to_string(i));
	}
}

bool loadScene(Scene& scene, const char* path, bool buildMeshlets, glm::vec3& euler, bool fast, bool clrt)
{
	clock_t timer = clock();

	scene.gltfDocument = GltfDocumentOutline{};

#if defined(__ANDROID__)
	// define Android callback
	auto androidRead = [](const cgltf_memory_options*,
	                       const cgltf_file_options*,
	                       const char* filePath,
	                       cgltf_size* size,
	                       void** data) -> cgltf_result
	{
		AAsset* asset = AAssetManager_open(g_assetManager, filePath, AASSET_MODE_BUFFER);
		if (!asset)
		{
			LOGE("Failed to open asset: %s", filePath);
			return cgltf_result_file_not_found;
		}

		off_t assetSize = AAsset_getLength(asset);
		void* buffer = malloc(assetSize);
		if (!buffer)
		{
			AAsset_close(asset);
			return cgltf_result_out_of_memory;
		}

		int64_t bytesRead = AAsset_read(asset, buffer, assetSize);
		AAsset_close(asset);

		if (bytesRead != assetSize)
		{
			free(buffer);
			return cgltf_result_io_error;
		}

		*size = assetSize;
		*data = buffer;
		return cgltf_result_success;
	};

	auto androidRelease = [](const cgltf_memory_options*,
	                          const cgltf_file_options*,
	                          void* data)
	{
		free(data);
	};
#endif

	cgltf_options options = {};
#if defined(__ANDROID__)
	options.file.read = androidRead;
	options.file.release = androidRelease;
#endif

	cgltf_data* data = nullptr;
	cgltf_result res;

#if defined(WIN32)
	res = cgltf_parse_file(&options, path, &data);
#elif defined(__ANDROID__)
	// read gltf main file
	AAsset* model = AAssetManager_open(g_assetManager, path, AASSET_MODE_BUFFER);
	if (!model)
	{
		LOGE("Failed to open gltf file: %s", path);
		return false;
	}
	off_t size = AAsset_getLength(model);
	std::vector<uint8_t> buffer(size);
	AAsset_read(model, buffer.data(), size);
	AAsset_close(model);

	res = cgltf_parse(&options, buffer.data(), buffer.size(), &data);
#endif

	if (res != cgltf_result_success)
	{
		LOGE("Failed to load scene file: %s.", path);
		return false;
	}

	std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> dataPtr(data, &cgltf_free);

	// === use customized callback to load buffer===
	res = cgltf_load_buffers(&options, data, path);
	if (res != cgltf_result_success)
	{
		LOGE("Failed to load buffers for scene file: %s.", path);
		return false;
	}

	res = cgltf_validate(data);
	if (res != cgltf_result_success)
	{
		LOGE("Failed to validate data for scene file: %s.", path);
		return false;
	}

	scene.draws.clear();
	scene.animations.clear();
	scene.sceneTextures.clear();

	std::vector<std::pair<unsigned int, unsigned int>> primitives;
	std::vector<cgltf_material*> primitiveMaterials;

	size_t firstMeshOffset = scene.geometry.meshes.size();

	for (size_t i = 0; i < data->meshes_count; ++i)
	{
		const cgltf_mesh& mesh = data->meshes[i];

		size_t meshOffset = scene.geometry.meshes.size();

		for (size_t pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles || !prim.indices)
				continue;

			std::vector<Vertex> vertices(prim.attributes[0].data->count);
			loadVertices(vertices, prim);

			std::vector<uint32_t> indices(prim.indices->count);
			cgltf_accessor_unpack_indices(prim.indices, indices.data(), 4, indices.size());
			appendMesh(scene.geometry, vertices, indices, buildMeshlets, fast, clrt);
			primitiveMaterials.push_back(prim.material);
		}

		primitives.push_back(std::make_pair(unsigned(meshOffset), unsigned(scene.geometry.meshes.size() - meshOffset)));
	}

	assert(primitiveMaterials.size() + firstMeshOffset == scene.geometry.meshes.size());

	ClearSceneTransformData(scene);
	scene.transformNodes.resize(data->nodes_count);
	scene.drawsForNode.assign(data->nodes_count, {});

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node* node = &data->nodes[i];
		TransformNode& tn = scene.transformNodes[i];
		tn.parent = node->parent ? int32_t(cgltf_node_index(data, node->parent)) : -1;
		tn.children.clear();
		for (cgltf_size c = 0; c < node->children_count; ++c)
			tn.children.push_back(uint32_t(cgltf_node_index(data, node->children[c])));
		float localMat[16]{};
		cgltf_node_transform_local(node, localMat);
		tn.local = glm::make_mat4(localMat);
		tn.world = glm::mat4(1.f);
		tn.worldDirty = true;
		tn.visible = true;
	}

	scene.transformRootNodes.clear();
	{
		const cgltf_scene* activeScene = nullptr;
		if (data->scene)
			activeScene = data->scene;
		else if (data->scenes_count > 0)
			activeScene = &data->scenes[0];
		if (activeScene)
		{
			for (cgltf_size r = 0; r < activeScene->nodes_count; ++r)
				scene.transformRootNodes.push_back(uint32_t(cgltf_node_index(data, activeScene->nodes[r])));
		}
	}

	size_t materialOffset = scene.materialDb.Size();
	assert(materialOffset > 0); // index 0 = dummy materials
	scene.gltfMaterialBaseIndex = uint32_t(materialOffset);
	scene.gltfMaterialCount = uint32_t(data->materials_count);
	scene.gltfMaterialDefaults.clear();
	scene.gltfMaterialDefaults.reserve(data->materials_count);

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node* node = &data->nodes[i];

		if (node->mesh)
		{
			std::pair<unsigned int, unsigned int> range = primitives[cgltf_mesh_index(data, node->mesh)];

			for (unsigned int j = 0; j < range.second; ++j)
			{
				MeshDraw draw = {};
				draw.world = glm::mat4(1.f);
				draw.gltfNodeIndex = uint32_t(i);
				draw.meshIndex = range.first + j;

				cgltf_material* material = primitiveMaterials[range.first + j - firstMeshOffset];

				draw.materialIndex = material ? materialOffset + int(cgltf_material_index(data, material)) : 0;

				if (material && MaterialNeedsTransparencyPass(*material))
					draw.postPass = 1;

				const uint32_t drawIndex = uint32_t(scene.draws.size());
				scene.draws.push_back(draw);
				scene.drawsForNode[i].push_back(drawIndex);
			}
		}
	}

	FlushSceneTransforms(scene);

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node* node = &data->nodes[i];

		if (node->camera)
		{
			float matrix[16];
			memcpy(matrix, glm::value_ptr(scene.transformNodes[i].world), sizeof(matrix));

			float translation[3];
			float rotation[4];
			float scale[3];
			decomposeTransform(translation, rotation, scale, matrix);

			assert(node->camera->type == cgltf_camera_type_perspective);

			scene.camera.position = vec3(translation[0], translation[1], translation[2]);
			scene.camera.orientation = quat(rotation[0], rotation[1], rotation[2], rotation[3]);
			euler = glm::degrees(glm::eulerAngles(scene.camera.orientation));
			scene.camera.fovY = node->camera->data.perspective.yfov;
		}

		if (node->light && node->light->type == cgltf_light_type_directional)
		{
			const glm::mat4& W = scene.transformNodes[i].world;
			scene.sunDirection = vec3(W[2][0], W[2][1], W[2][2]);
		}
	}

	int textureOffset = 1 + int(scene.sceneTextures.size());

	for (size_t i = 0; i < data->materials_count; ++i)
	{
		cgltf_material* material = &data->materials[i];
		PBRMaterial pbrMaterial = BuildPbrMaterial(data, *material, textureOffset);
		scene.gltfMaterialDefaults.push_back(pbrMaterial);
		scene.materialDb.Add(std::make_unique<PBRMaterial>(std::move(pbrMaterial)));
	}
	scene.materialShaderGraphEnabled.assign(scene.materialDb.entries.size(), 0);
	scene.materialShaderGraphPath.assign(scene.materialDb.entries.size(), std::string{});
	scene.materialShaderGraphFloatParams.assign(scene.materialDb.entries.size(), std::vector<float>{});
	scene.materialShaderGraphAppliedEnabled = scene.materialShaderGraphEnabled;
	scene.materialShaderGraphAppliedPath = scene.materialShaderGraphPath;
	scene.materialShaderGraphAppliedFloatParams = scene.materialShaderGraphFloatParams;

	for (size_t i = 0; i < data->textures_count; ++i)
	{
		cgltf_texture* texture = &data->textures[i];
		assert(texture->image);

		cgltf_image* image = texture->image;
		SceneTextureSource tex;

		if (image->buffer_view)
		{
			const uint8_t* ptr = cgltf_buffer_view_data(image->buffer_view);
			const cgltf_size sz = image->buffer_view->size;
			if (!ptr || sz == 0)
			{
				LOGE("glTF texture %zu: image bufferView has no data (scene %s)", i, path);
			}
			else
			{
				tex.embedded.assign(ptr, ptr + sz);
			}
			tex.path = std::string(path) + " [embedded image " + std::to_string(i) + "]";
		}
		else if (image->uri)
		{
			fs::path scenePath = fs::path(path);
			fs::path basePath = scenePath.parent_path();

			std::string uri = image->uri;

			const char* prefix_png = "data:image/png;base64,";
			const char* prefix_jpg = "data:image/jpeg;base64,";
			if (strncmp(uri.c_str(), prefix_png, strlen(prefix_png)) == 0 || strncmp(uri.c_str(), prefix_jpg, strlen(prefix_jpg)) == 0)
			{
				tex.path = std::move(uri);
			}
			else
			{
				uri.resize(cgltf_decode_uri(&uri[0]));
				fs::path fullPath = basePath / uri;
				std::string texturePath = fullPath.string();
				std::replace(texturePath.begin(), texturePath.end(), '\\', '/');
				tex.path = std::move(texturePath);
			}
		}
		else
		{
			LOGE("glTF texture %zu: image has neither uri nor bufferView (scene %s)", i, path);
			tex.path = std::string(path) + " [invalid image " + std::to_string(i) + "]";
		}

		scene.sceneTextures.emplace_back(std::move(tex));
	}

	std::vector<cgltf_animation_sampler*> samplersT(data->nodes_count);
	std::vector<cgltf_animation_sampler*> samplersR(data->nodes_count);
	std::vector<cgltf_animation_sampler*> samplersS(data->nodes_count);

	for (size_t i = 0; i < data->animations_count; ++i)
	{
		cgltf_animation* anim = &data->animations[i];

		for (size_t j = 0; j < anim->channels_count; ++j)
		{
			cgltf_animation_channel* channel = &anim->channels[j];
			cgltf_animation_sampler* sampler = channel->sampler;

			if (!channel->target_node)
				continue;

			if (channel->target_path == cgltf_animation_path_type_translation)
				samplersT[cgltf_node_index(data, channel->target_node)] = sampler;
			else if (channel->target_path == cgltf_animation_path_type_rotation)
				samplersR[cgltf_node_index(data, channel->target_node)] = sampler;
			else if (channel->target_path == cgltf_animation_path_type_scale)
				samplersS[cgltf_node_index(data, channel->target_node)] = sampler;
		}
	}

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		if (!samplersR[i] && !samplersT[i] && !samplersS[i])
			continue;

		cgltf_accessor* input = 0;
		if (samplersT[i])
			input = samplersT[i]->input;
		else if (samplersR[i])
			input = samplersR[i]->input;
		else if (samplersS[i])
			input = samplersS[i]->input;

		if ((samplersT[i] && samplersT[i]->input->count != input->count) ||
		    (samplersR[i] && samplersR[i]->input->count != input->count) ||
		    (samplersS[i] && samplersS[i]->input->count != input->count))
		{
			LOGW("kipping animation for node %d due to mismatched sampler counts\n", int(i));
			continue;
		}

		if ((samplersT[i] && samplersT[i]->interpolation != cgltf_interpolation_type_linear) ||
		    (samplersR[i] && samplersR[i]->interpolation != cgltf_interpolation_type_linear) ||
		    (samplersS[i] && samplersS[i]->interpolation != cgltf_interpolation_type_linear))
		{
			LOGW("skipping animation for node %d due to mismatched sampler interpolation type.\n", int(i));
			continue;
		}

		if (input->count < 2)
		{
			LOGW("skipping animation for node %d with %d keyframes\n", int(i), int(input->count));
			continue;
		}

		std::vector<float> times(input->count);
		cgltf_accessor_unpack_floats(input, times.data(), times.size());

		Animation animation = {};
		animation.gltfNodeIndex = uint32_t(i);
		animation.startTime = times[0];
		animation.period = times[1] - times[0];

		std::vector<float> valuesR, valuesT, valuesS;

		if (samplersT[i])
		{
			valuesT.resize(samplersT[i]->output->count * 3);
			cgltf_accessor_unpack_floats(samplersT[i]->output, valuesT.data(), valuesT.size());
		}

		if (samplersR[i])
		{
			valuesR.resize(samplersR[i]->output->count * 4);
			cgltf_accessor_unpack_floats(samplersR[i]->output, valuesR.data(), valuesR.size());
		}

		if (samplersS[i])
		{
			valuesS.resize(samplersS[i]->output->count * 3);
			cgltf_accessor_unpack_floats(samplersS[i]->output, valuesS.data(), valuesS.size());
		}

		for (size_t j = 0; j < input->count; ++j)
		{
			cgltf_node nodeCopy = data->nodes[i];

			if (samplersT[i])
			{
				memcpy(nodeCopy.translation, &valuesT[j * 3], 3 * sizeof(float));
				nodeCopy.has_translation = true;
			}

			if (samplersR[i])
			{
				memcpy(nodeCopy.rotation, &valuesR[j * 4], 4 * sizeof(float));
				nodeCopy.has_rotation = true;
			}

			if (samplersS[i])
			{
				memcpy(nodeCopy.scale, &valuesS[j * 3], 3 * sizeof(float));
				nodeCopy.has_scale = true;
			}

			Keyframe kf = {};
			kf.translation = nodeCopy.has_translation ? vec3(nodeCopy.translation[0], nodeCopy.translation[1], nodeCopy.translation[2]) : vec3(0.f);
			kf.rotation = nodeCopy.has_rotation ? quat(nodeCopy.rotation[0], nodeCopy.rotation[1], nodeCopy.rotation[2], nodeCopy.rotation[3])
			                                      : quat(1.f, 0.f, 0.f, 0.f);
			kf.scale = nodeCopy.has_scale ? vec3(nodeCopy.scale[0], nodeCopy.scale[1], nodeCopy.scale[2]) : vec3(1.f, 1.f, 1.f);

			animation.keyframes.push_back(kf);
		}

		scene.animations.push_back(std::move(animation));
	}

	LOGI("Loaded %s: %d meshes, %d draws, %d animations, %d vertices in %.2f sec",
	    path, int(scene.geometry.meshes.size()), int(scene.draws.size()), int(scene.animations.size()), int(scene.geometry.vertices.size()),
	    double(clock() - timer) / CLOCKS_PER_SEC);

	if (buildMeshlets)
	{
		unsigned int meshletVtxs = 0, meshletTris = 0;

		for (Meshlet& meshlet : scene.geometry.meshlets)
		{
			meshletVtxs += meshlet.vertexCount;
			meshletTris += meshlet.triangleCount;
		}

		LOGI("Meshlets: %d meshlets, %d triangles, %d vertex refs", int(scene.geometry.meshlets.size()), int(meshletTris), int(meshletVtxs));
	}

	FillGltfDocumentOutline(data, scene.gltfDocument);

	return true;
}

void SortSceneDrawsByMaterialKey(Scene& scene)
{
	std::vector<MeshDraw>& draws = scene.draws;
	const std::vector<MaterialKey>& keys = scene.materialDb.materialKeys;
	const size_t n = draws.size();
	if (n <= 1)
		return;

	auto materialKeyForDraw = [&](uint32_t materialIndex) -> MaterialKey
	{
		if (materialIndex >= keys.size())
			return MaterialKey::DefaultPbrOpaque();
		return keys[materialIndex];
	};

	std::vector<uint32_t> order(n);
	for (size_t i = 0; i < n; ++i)
		order[i] = uint32_t(i);

	std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
	    { return materialKeyForDraw(draws[a].materialIndex) < materialKeyForDraw(draws[b].materialIndex); });

	std::vector<MeshDraw> sorted(n);
	for (size_t k = 0; k < n; ++k)
		sorted[k] = draws[order[k]];
	draws.swap(sorted);

	// Remap draw indices per glTF node after reordering draws.
	std::vector<std::vector<uint32_t>> newDrawsForNode(scene.transformNodes.size());
	for (uint32_t newIdx = 0; newIdx < uint32_t(draws.size()); ++newIdx)
	{
		const uint32_t nodeIdx = draws[newIdx].gltfNodeIndex;
		if (nodeIdx < newDrawsForNode.size())
			newDrawsForNode[nodeIdx].push_back(newIdx);
	}
	scene.drawsForNode.swap(newDrawsForNode);
}

void RebuildMaterialDrawBatches(Scene& scene)
{
	std::vector<MeshDraw>& draws = scene.draws;
	const std::vector<MaterialKey>& keys = scene.materialDb.materialKeys;

	scene.drawBatches.clear();

	if (draws.empty())
		return;

	auto materialKeyForDraw = [&](uint32_t materialIndex) -> MaterialKey
	{
		if (materialIndex >= keys.size())
			return MaterialKey::DefaultPbrOpaque();
		return keys[materialIndex];
	};

	size_t batchStart = 0;
	while (batchStart < draws.size())
	{
		const MaterialKey key = materialKeyForDraw(draws[batchStart].materialIndex);
		size_t batchEnd = batchStart + 1;
		while (batchEnd < draws.size() && materialKeyForDraw(draws[batchEnd].materialIndex) == key)
			++batchEnd;

		DrawBatch batch{};
		batch.materialKey = key;
		batch.firstDraw = uint32_t(batchStart);
		batch.drawCount = uint32_t(batchEnd - batchStart);
		scene.drawBatches.push_back(batch);

		batchStart = batchEnd;
	}

	LOGI("Material draw batches: %d batches for %d draws", int(scene.drawBatches.size()), int(draws.size()));
}

Scene::Scene(const char* _path)
    : path(_path)
{
}

std::optional<uint32_t> Scene::GltfMaterialIndexToMaterialIndex(uint32_t gltfMatIdx) const
{
	if (gltfMatIdx >= gltfMaterialCount)
		return std::nullopt;
	const uint64_t materialIndex = uint64_t(gltfMaterialBaseIndex) + uint64_t(gltfMatIdx);
	if (materialIndex >= materialDb.entries.size())
		return std::nullopt;
	return uint32_t(materialIndex);
}

void CollectDrawIndicesInNodeSubtree(const Scene& scene, const uint32_t rootNode, std::vector<uint32_t>& outDraws)
{
	outDraws.clear();
	if (scene.transformNodes.empty() || rootNode >= scene.transformNodes.size())
		return;

	std::vector<uint32_t> stack;
	stack.push_back(rootNode);
	while (!stack.empty())
	{
		const uint32_t n = stack.back();
		stack.pop_back();

		if (n < scene.drawsForNode.size())
		{
			for (const uint32_t d : scene.drawsForNode[n])
			{
				if (d < scene.draws.size())
					outDraws.push_back(d);
			}
		}

		if (n >= scene.transformNodes.size())
			continue;
		for (const uint32_t c : scene.transformNodes[n].children)
		{
			if (c < scene.transformNodes.size())
				stack.push_back(c);
		}
	}
}

void WorldAabbFromMeshAndDraw(const Mesh& mesh, const glm::mat4& world, glm::vec3& outMin, glm::vec3& outMax)
{
	const glm::vec3 bmin(mesh.aabbMin.x, mesh.aabbMin.y, mesh.aabbMin.z);
	const glm::vec3 bmax(mesh.aabbMax.x, mesh.aabbMax.y, mesh.aabbMax.z);
	const glm::vec3 corners[8] = {
		glm::vec3(bmin.x, bmin.y, bmin.z),
		glm::vec3(bmax.x, bmin.y, bmin.z),
		glm::vec3(bmin.x, bmax.y, bmin.z),
		glm::vec3(bmax.x, bmax.y, bmin.z),
		glm::vec3(bmin.x, bmin.y, bmax.z),
		glm::vec3(bmax.x, bmin.y, bmax.z),
		glm::vec3(bmin.x, bmax.y, bmax.z),
		glm::vec3(bmax.x, bmax.y, bmax.z),
	};

	bool first = true;
	for (const glm::vec3& c : corners)
	{
		const glm::vec4 wp = world * glm::vec4(c, 1.f);
		const glm::vec3 p = glm::vec3(wp.x, wp.y, wp.z) / wp.w;
		if (first)
		{
			outMin = outMax = p;
			first = false;
		}
		else
		{
			outMin = glm::min(outMin, p);
			outMax = glm::max(outMax, p);
		}
	}
}

bool UnionWorldAabbForDraws(const Scene& scene, const std::vector<uint32_t>& drawIndices, glm::vec3& outMin, glm::vec3& outMax)
{
	bool have = false;
	for (const uint32_t di : drawIndices)
	{
		if (di >= scene.draws.size())
			continue;
		const MeshDraw& d = scene.draws[di];
		if (d.meshIndex >= scene.geometry.meshes.size())
			continue;
		const Mesh& mesh = scene.geometry.meshes[d.meshIndex];
		glm::vec3 mn, mx;
		WorldAabbFromMeshAndDraw(mesh, d.world, mn, mx);
		if (!have)
		{
			outMin = mn;
			outMax = mx;
			have = true;
		}
		else
		{
			outMin = glm::min(outMin, mn);
			outMax = glm::max(outMax, mx);
		}
	}
	return have;
}
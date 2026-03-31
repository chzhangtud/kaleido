#pragma once

#include "math.h"
#include <stdint.h>
#include <vector>
#include <string>
#include "common.h"
#include "resources.h"

struct alignas(16) Meshlet
{
	vec3 center;
	float radius;
	int8_t coneAxis[3];
	int8_t coneCutoff;

	uint32_t dataOffset; // dataOffset..dataOffset+vertexCount-1 stores vertex indices, we store indices packed in 4b units after that
	uint32_t baseVertex;
	uint8_t vertexCount;
	uint8_t triangleCount;
	uint8_t shortRefs;
	uint8_t padding;
};

struct alignas(16) Material
{
	int albedoTexture;
	int normalTexture;
	int pbrTexture;
	int emissiveTexture;

	vec4 baseColorFactor;
	vec4 pbrFactor; // MR: (unused, unused, metallic, roughness), SG: (specular.rgb, glossiness)
	vec3 emissiveFactor;
	int workflow;   // 0: default, 1: metallic-roughness, 2: specular-glossiness fallback
};

struct alignas(16) MeshDraw
{
	vec3 position;
	float scale;
	quat orientation;

	uint32_t meshIndex;
	uint32_t meshletVisibilityOffset;
	uint32_t postPass;
	uint32_t flags;

	uint32_t materialIndex;
};

struct Vertex
{
	uint16_t vx, vy, vz;
	uint16_t tp; // packed tangent: 8-8 octahedral
	uint32_t np; // packed normal: 10-10-10-2 vector + bitangent sign
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
	std::vector<uint32_t> meshletdata;
	std::vector<uint16_t> meshletvtx0; // 4 position components per vertex referenced by meshlets in lod 0, packed tightly

	std::vector<Mesh> meshes;
};

struct Camera
{
	vec3 position;
	quat orientation;
	float fovY;
	float znear;
};

struct Keyframe
{
	vec3 translation;
	float scale;
	quat rotation;
};

struct Animation
{
	uint32_t drawIndex;

	float startTime;
	float period;
	std::vector<Keyframe> keyframes;
};

struct Scene
{
	Scene(const char* _path);
	Geometry geometry;
	std::vector<Material> materials;
	std::vector<MeshDraw> draws;
	std::vector<Animation> animations;
	std::vector<std::string> texturePaths;
	vec3 sunDirection{ 1.0f };
	uint32_t meshletVisibilityCount{ 0u };
	std::pair<VkDescriptorPool, VkDescriptorSet> textureSet{};
	std::vector<Image> images;

	std::string path;
	float drawDistance{ 1000.f };
	uint32_t meshPostPasses = 0;

	Camera camera;
};

bool loadMesh(Geometry& result, const char* path, bool buildMeshlets, bool fast = false, bool clrt = false);
bool loadScene(Geometry& geometry, std::vector<Material>& materials, std::vector<MeshDraw>& draws, std::vector<std::string>& texturePaths, std::vector<Animation>& animations, Camera& camera, vec3& sunDirection, const char* path, bool buildMeshlets, glm::vec3& euler, bool fast = false, bool clrt = false);
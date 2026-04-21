#pragma once

#include "math.h"
#include <glm/gtc/matrix_transform.hpp>
#include "material_types.h"
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
#include "common.h"
#include "resources.h"
#include <cstddef>

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

// std430: mat4 at offset 0, then 4 u32 groups - verify with static_assert after padding settles.
struct alignas(16) MeshDraw
{
	glm::mat4 world{ 1.f }; // column-major, object to world

	uint32_t meshIndex = 0;
	uint32_t meshletVisibilityOffset = 0;
	uint32_t postPass = 0;
	uint32_t flags = 0;

	uint32_t materialIndex = 0;
	uint32_t gltfNodeIndex = 0;
	uint32_t pad0 = 0;
	uint32_t pad1 = 0;
};

static_assert(sizeof(MeshDraw) % 16 == 0, "MeshDraw must stay 16-byte aligned for GPU");
static_assert(sizeof(MeshDraw) == 96, "MeshDraw std430 size must match shaders");
static_assert(offsetof(MeshDraw, world) == 0, "MeshDraw.world offset must match std430");
static_assert(offsetof(MeshDraw, meshIndex) == 64, "MeshDraw.meshIndex offset must match std430");
static_assert(offsetof(MeshDraw, materialIndex) == 80, "MeshDraw.materialIndex offset must match std430");
static_assert(offsetof(MeshDraw, gltfNodeIndex) == 84, "MeshDraw.gltfNodeIndex offset must match std430");

inline glm::mat4 MeshDrawWorldFromUniformTRS(const glm::vec3& position, float scale, const glm::quat& orientation)
{
	return glm::translate(glm::mat4(1.f), position) * glm::mat4_cast(orientation) * glm::scale(glm::mat4(1.f), glm::vec3(scale));
}

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

// glTF textures: file path / data URI in path, or embedded PNG-JPEG bytes (GLB bufferView) in embedded.
struct SceneTextureSource
{
	std::string path;
	std::vector<uint8_t> embedded;
};

// Captured from cgltf for editor UI (scene node tree + mesh names for labels).
struct GltfNodeOutline
{
	std::string name;
	int32_t meshIndex = -1;
	std::vector<uint32_t> children;
};

struct GltfSceneOutline
{
	std::string name;
	std::vector<uint32_t> rootNodes;
};

struct GltfDocumentOutline
{
	bool loaded = false;
	std::vector<GltfSceneOutline> scenes;
	int32_t defaultSceneIndex = -1;
	std::vector<GltfNodeOutline> nodes;
	std::vector<std::string> meshNames;
};

struct Scene
{
	Scene(const char* _path);
	Geometry geometry;
	MaterialDatabase materialDb;
	std::vector<MeshDraw> draws;
	std::vector<DrawBatch> drawBatches;
	std::vector<Animation> animations;
	std::vector<SceneTextureSource> sceneTextures;
	GltfDocumentOutline gltfDocument;
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
bool loadScene(Geometry& geometry, MaterialDatabase& materialDb, std::vector<MeshDraw>& draws, std::vector<SceneTextureSource>& sceneTextures, std::vector<Animation>& animations, Camera& camera, vec3& sunDirection, const char* path, bool buildMeshlets, glm::vec3& euler, bool fast = false, bool clrt = false, GltfDocumentOutline* outGltfDocument = nullptr);

void SortSceneDrawsByMaterialKey(Scene& scene);
void RebuildMaterialDrawBatches(Scene& scene);
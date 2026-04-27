#pragma once

#include "math.h"
#include <glm/gtc/matrix_transform.hpp>
#include "material_types.h"
#include <stdint.h>
#include <optional>
#include <vector>
#include <string>
#include <memory>
#include "common.h"
#include "resources.h"
#include <cstddef>
#include <cstring>

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

// std430 layout for GPU mesh buffer (must match src/shaders/mesh.h struct Mesh).
struct alignas(16) GpuMeshStd430
{
	vec3 center;
	float radius;

	uint32_t vertexOffset;
	uint32_t vertexCount;
	uint32_t lodCount;
	uint32_t placeHolder;

	MeshLod lods[8];
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

	// CPU-only object-space bounds (not uploaded to GPU Mesh std430; see GpuMeshStd430).
	vec3 aabbMin{};
	vec3 aabbMax{};
};

static_assert(sizeof(GpuMeshStd430) == 192, "GpuMeshStd430 must match GLSL Mesh std430 size");
static_assert(sizeof(Mesh) > sizeof(GpuMeshStd430), "Mesh carries extra CPU fields after GpuMeshStd430 prefix");
static_assert(offsetof(Mesh, aabbMin) == sizeof(GpuMeshStd430), "GpuMesh prefix must alias Mesh for upload memcpy");

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
	vec3 translation{};
	quat rotation{ 1.f, 0.f, 0.f, 0.f };
	vec3 scale{ 1.f, 1.f, 1.f };
};

struct Animation
{
	uint32_t gltfNodeIndex = 0;

	float startTime = 0.f;
	float period = 0.f;
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

struct TransformNode
{
	int32_t parent = -1;
	std::vector<uint32_t> children;
	glm::mat4 local{ 1.f };
	glm::mat4 world{ 1.f };
	bool worldDirty = true;
	bool visible = true;
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
	std::vector<TransformNode> transformNodes;
	std::vector<uint32_t> transformRootNodes;
	std::vector<std::vector<uint32_t>> drawsForNode;
	uint32_t gltfMaterialBaseIndex = 0;
	uint32_t gltfMaterialCount = 0;
	std::vector<PBRMaterial> gltfMaterialDefaults;
	bool transformsGpuDirty = false;
	std::optional<uint32_t> uiSelectedGltfNode;
	std::optional<uint32_t> uiSelectedMaterialIndex;
	// Editor overlays (default off for image regression; see docs/superpowers/specs/2026-04-26-scene-tree-selection-outline-aabb-design.md §8).
	bool uiEnableSelectionOutline = false;
	bool uiShowSelectedSubtreeAabb = false;
	bool uiVisualizeRenderGraph = false;
	bool uiRenderGraphWindowOpen = false;
	int uiRenderGraphViewMode = 0; // 0 = live, 1 = imported
	int uiRenderGraphGraphMode = 0; // 0 = simple(pass->pass), 1 = full(pass/resource)
	std::string uiRenderGraphImportedPath;
	vec3 sunDirection{ 1.0f };
	uint32_t meshletVisibilityCount{ 0u };
	std::pair<VkDescriptorPool, VkDescriptorSet> textureSet{};
	std::vector<Image> images;

	std::string path;
	float drawDistance{ 1000.f };
	uint32_t meshPostPasses = 0;

	Camera camera;

	std::optional<uint32_t> GltfMaterialIndexToMaterialIndex(uint32_t gltfMatIdx) const;
};

bool loadMesh(Geometry& result, const char* path, bool buildMeshlets, bool fast = false, bool clrt = false);
bool loadScene(Scene& scene, const char* path, bool buildMeshlets, glm::vec3& euler, bool fast = false, bool clrt = false);

void CollectDrawIndicesInNodeSubtree(const Scene& scene, uint32_t rootNode, std::vector<uint32_t>& outDraws);
void WorldAabbFromMeshAndDraw(const Mesh& mesh, const glm::mat4& world, glm::vec3& outMin, glm::vec3& outMax);
bool UnionWorldAabbForDraws(const Scene& scene, const std::vector<uint32_t>& drawIndices, glm::vec3& outMin, glm::vec3& outMax);

inline GpuMeshStd430 PackGpuMeshStd430(const Mesh& m)
{
	GpuMeshStd430 g;
	memcpy(&g, &m, sizeof(GpuMeshStd430));
	return g;
}

void SortSceneDrawsByMaterialKey(Scene& scene);
void RebuildMaterialDrawBatches(Scene& scene);
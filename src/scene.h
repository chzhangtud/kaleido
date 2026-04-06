#pragma once

#include "math.h"
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
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

enum class MaterialType : uint32_t
{
	PBR = 0,
};

// Stable batching/sort key: material class, workflow, alpha mode, double-sided, transmission (extensible upper bits reserved).
struct MaterialKey
{
	uint64_t packed = 0;

	static MaterialKey Pack(MaterialType type, uint32_t workflow, uint32_t alphaMode, uint32_t doubleSided, uint32_t transmission)
	{
		const uint64_t t = (uint64_t)(uint32_t)type & 0xFFu;
		const uint64_t w = (uint64_t)(workflow & 0xFu) << 8;
		const uint64_t a = (uint64_t)(alphaMode & 3u) << 12;
		const uint64_t d = (uint64_t)(doubleSided & 1u) << 14;
		const uint64_t tr = (uint64_t)(transmission & 1u) << 15;
		MaterialKey k;
		k.packed = t | w | a | d | tr;
		return k;
	}

	// Opaque MR, single-sided, no transmission (matches glTF defaults / dummy slot 0).
	static MaterialKey DefaultPbrOpaque() { return Pack(MaterialType::PBR, 1u, 0u, 0u, 0u); }

	bool operator==(MaterialKey o) const { return packed == o.packed; }
	bool operator!=(MaterialKey o) const { return packed != o.packed; }
	bool operator<(MaterialKey o) const { return packed < o.packed; }
};

class MaterialClass
{
public:
	virtual ~MaterialClass() = default;
	virtual Material ToGpuMaterial() const = 0;
	virtual MaterialType GetType() const = 0;
	virtual MaterialKey GetMaterialKey() const = 0;
};

class PBRMaterial final : public MaterialClass
{
public:
	Material data{};
	MaterialKey key = MaterialKey::DefaultPbrOpaque();

	Material ToGpuMaterial() const override
	{
		return data;
	}

	MaterialType GetType() const override
	{
		return MaterialType::PBR;
	}

	MaterialKey GetMaterialKey() const override
	{
		return key;
	}

	static PBRMaterial CreateDefault();
};

struct MaterialDatabase
{
	std::vector<std::unique_ptr<MaterialClass>> entries;
	std::vector<Material> gpuMaterials;
	std::vector<MaterialKey> materialKeys;

	void Clear();
	uint32_t Add(std::unique_ptr<MaterialClass> material);
	size_t Size() const;
};

// CPU-side batch metadata: consecutive draws in scene.draws sharing the same MaterialKey (draws must be material-sorted).
struct DrawBatch
{
	MaterialKey materialKey{};
	uint32_t firstDraw = 0;
	uint32_t drawCount = 0;
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
	MaterialDatabase materialDb;
	std::vector<MeshDraw> draws;
	std::vector<DrawBatch> drawBatches;
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
bool loadScene(Geometry& geometry, MaterialDatabase& materialDb, std::vector<MeshDraw>& draws, std::vector<std::string>& texturePaths, std::vector<Animation>& animations, Camera& camera, vec3& sunDirection, const char* path, bool buildMeshlets, glm::vec3& euler, bool fast = false, bool clrt = false);

void SortSceneDrawsByMaterialKey(Scene& scene);
void RebuildMaterialDrawBatches(Scene& scene);
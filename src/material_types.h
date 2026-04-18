#pragma once

#include "math.h"
#include <stdint.h>
#include <cstddef>
#include <memory>
#include <vector>

// GPU SSBO layout: must match `struct Material` in src/shaders/mesh.h (std430).
struct alignas(16) Material
{
	int32_t albedoTexture;
	int32_t normalTexture;
	int32_t pbrTexture;
	int32_t emissiveTexture;
	int32_t occlusionTexture;
	int32_t texturePad[3];

	vec4 baseColorFactor;
	// MR: (unused, unused, metallic, roughness), SG: (specular.rgb, glossiness)
	vec4 pbrFactor;
	float emissiveFactor[3];
	// 0: default, 1: metallic-roughness, 2: specular-glossiness fallback
	int32_t workflow;

	// x: glTF normal texture scale, y: occlusion strength, z: alpha cutoff (MASK), w: emissive strength (KHR)
	vec4 shadingParams;
	int32_t alphaMode; // 0 opaque, 1 mask, 2 blend (cgltf_alpha_mode)
	int32_t transmissionTexture; // 0 = none (same convention as other texture indices)
	float transmissionFactor;
	float ior; // KHR_materials_ior, default 1.5 when unspecified
};

static_assert(sizeof(Material) == 112, "Material size must match GLSL std430 (see shaders/mesh.h)");

enum class MaterialType : uint32_t
{
	PBR = 0,
};

// Stable batching/sort key: material class, workflow, alpha mode, double-sided, transmission (extensible upper bits reserved).
struct MaterialKey
{
	uint64_t packed = 0;

	static MaterialKey Pack(MaterialType type, uint32_t workflow, uint32_t alphaMode, uint32_t doubleSided, uint32_t transmission);
	static MaterialKey DefaultPbrOpaque();

	bool operator==(MaterialKey o) const { return packed == o.packed; }
	bool operator!=(MaterialKey o) const { return packed != o.packed; }
	bool operator<(MaterialKey o) const { return packed < o.packed; }
};
// Pack / DefaultPbrOpaque definitions live in material_types.cpp (same bit layout as pre-refactor scene.h).

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

	Material ToGpuMaterial() const override;
	MaterialType GetType() const override;
	MaterialKey GetMaterialKey() const override;

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

// CPU-side batch metadata: consecutive draws sharing the same MaterialKey (draws must be material-sorted).
struct DrawBatch
{
	MaterialKey materialKey{};
	uint32_t firstDraw = 0;
	uint32_t drawCount = 0;
};

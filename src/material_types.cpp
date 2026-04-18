#include "material_types.h"
#include <assert.h>

MaterialKey MaterialKey::Pack(MaterialType type, uint32_t workflow, uint32_t alphaMode, uint32_t doubleSided, uint32_t transmission)
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

MaterialKey MaterialKey::DefaultPbrOpaque()
{
	return Pack(MaterialType::PBR, 1u, 0u, 0u, 0u);
}

PBRMaterial PBRMaterial::CreateDefault()
{
	PBRMaterial material{};
	material.data.baseColorFactor = vec4(1);
	material.data.pbrFactor = vec4(1, 1, 0, 1);
	material.data.workflow = 1;
	material.data.shadingParams = vec4(1.f, 1.f, 0.5f, 1.f);
	material.data.alphaMode = 0;
	material.data.transmissionTexture = 0;
	material.data.transmissionFactor = 0.f;
	material.data.ior = 1.5f;
	return material;
}

Material PBRMaterial::ToGpuMaterial() const { return data; }
MaterialType PBRMaterial::GetType() const { return MaterialType::PBR; }
MaterialKey PBRMaterial::GetMaterialKey() const { return key; }

void MaterialDatabase::Clear()
{
	entries.clear();
	gpuMaterials.clear();
	materialKeys.clear();
}

uint32_t MaterialDatabase::Add(std::unique_ptr<MaterialClass> material)
{
	assert(material);
	materialKeys.push_back(material->GetMaterialKey());
	gpuMaterials.push_back(material->ToGpuMaterial());
	entries.push_back(std::move(material));
	return uint32_t(gpuMaterials.size() - 1);
}

size_t MaterialDatabase::Size() const
{
	return gpuMaterials.size();
}

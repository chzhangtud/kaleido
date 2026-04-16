# PBR Material Semantic Layer + Encoder + Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split glTF PBR material handling into a testable semantic description, a deterministic GPU encoder, and locked CPU/GPU layout contracts, without changing rendered output until explicitly adding features.

**Architecture:** Introduce `PbrSurfaceDescription` (CPU-only semantics) and `DecodeGltfPbrMaterial` / `EncodePbrToGpuMaterial` so `scene.cpp` only orchestrates I/O. Extract `Material` / `MaterialDatabase` / `MaterialKey` into `material_types.{h,cpp}` so unit tests link a tiny target without Vulkan. Keep `MaterialKey` derivation inside the encoder path to avoid drift from `Material` fields.

**Tech stack:** C++17, CMake 3.10+, glm (via `math.h`), cgltf (decoder + existing loader), existing `static_assert` on `Material` size.

---

## File map (create / modify)

| File | Role |
|------|------|
| `src/material_types.h` | `Material`, `MaterialType`, `MaterialKey`, `MaterialClass`, `PBRMaterial`, `MaterialDatabase`, `DrawBatch` declarations (moved from `scene.h`). |
| `src/material_types.cpp` | `PBRMaterial::CreateDefault`, `MaterialDatabase::{Clear,Add,Size}` (moved from `scene.cpp`). |
| `src/scene.h` | `#include "material_types.h"`; remove duplicated material definitions; keep `MeshDraw`, `Geometry`, `Scene`, loaders. |
| `src/scene.cpp` | Thin `BuildPbrMaterial`: decode → encode → `PBRMaterial`; remove inlined field rules once encoder exists. |
| `src/pbr_surface_description.h` | Plain structs/enums for semantic layer (no cgltf include). |
| `src/gltf_pbr_decoder.h` | `bool DecodeGltfPbrMaterial(const cgltf_data* data, const cgltf_material& material, int textureOffset, PbrSurfaceDescription& out);` |
| `src/gltf_pbr_decoder.cpp` | Decoder implementation (moved logic from current `BuildPbrMaterial`). |
| `src/gpu_material_encoder.h` | `void EncodePbrToGpuMaterial(const PbrSurfaceDescription& in, Material& outGpu, MaterialKey& outKey);` |
| `src/gpu_material_encoder.cpp` | Encoder + key packing from semantics only. |
| `docs/superpowers/gbuffer-and-material-encoding.md` | Human-readable G-buffer + MR/SG notes (shader consumer list). |
| `tests/CMakeLists.txt` | Optional test target when `KALEIDO_BUILD_TESTS=ON`. |
| `tests/material_contract_test.cpp` | Layout + `MaterialKey` + batch invariant tests. |
| `tests/material_encoder_test.cpp` | Golden-byte tests for encoder (after encoder exists). |
| `tests/material_decoder_test.cpp` | Decoder tests using tiny in-memory cgltf graphs (after decoder exists). |
| `CMakeLists.txt` | `option(KALEIDO_BUILD_TESTS ...)` + `add_subdirectory(tests)` guarded by option. |

**Shader maintenance checklist (no code moves):** when `Material` changes, update `src/shaders/mesh.h`, `src/shaders/mesh.frag.glsl`, `src/shaders/shadow.comp.glsl`, `src/shaders/transparency_blend.frag.glsl`, `src/shaders/transmission_resolve.comp.glsl`.

---

### Task 1: Extract `material_types` from `scene`

**Files:**
- Create: `src/material_types.h`
- Create: `src/material_types.cpp`
- Modify: `src/scene.h` (delete material block; add `#include "material_types.h"` before any type that uses `MaterialKey` / `DrawBatch`)
- Modify: `src/scene.cpp` (remove `PBRMaterial::CreateDefault` and `MaterialDatabase::*` bodies; add `#include` as needed)

- [ ] **Step 1: Create `src/material_types.h`**

Cut from `scene.h` (lines 26–131 approximately: from the `Material` comment through `DrawBatch` struct) into the new header. Top of file:

```cpp
#pragma once

#include "math.h"
#include <stdint.h>
#include <cstddef>
#include <memory>
#include <string>
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
	vec4 pbrFactor;
	float emissiveFactor[3];
	int32_t workflow;

	vec4 shadingParams;
	int32_t alphaMode;
	int32_t transmissionTexture;
	float transmissionFactor;
	float ior;
};

static_assert(sizeof(Material) == 112, "Material size must match GLSL std430 (see shaders/mesh.h)");

enum class MaterialType : uint32_t
{
	PBR = 0,
};

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

struct DrawBatch
{
	MaterialKey materialKey{};
	uint32_t firstDraw = 0;
	uint32_t drawCount = 0;
};
```

Do not define `MaterialKey::Pack` / `DefaultPbrOpaque` in the header; define them only in `material_types.cpp` (match previous `scene.h` behavior exactly).

- [ ] **Step 2: Create `src/material_types.cpp`**

```cpp
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
```

- [ ] **Step 3: Edit `src/scene.h`** — Remove the moved block; add `#include "material_types.h"` near the top (after `math.h` is fine). Ensure `Scene` still has `MaterialDatabase materialDb` and `std::vector<DrawBatch> drawBatches`.

- [ ] **Step 4: Edit `src/scene.cpp`** — Delete the old `PBRMaterial::CreateDefault`, `MaterialDatabase::*` definitions (now in `material_types.cpp`).

- [ ] **Step 5: Build**

Run (PowerShell, from repo root):

```powershell
Set-Location "d:\CMakeRepos\kaleido\build"
cmake --build . --config Debug --target kaleido_standalone
```

Expected: **0 errors**; linker includes new `material_types.cpp` via existing `GLOB_RECURSE` of `src/*.cpp`.

- [ ] **Step 6: Commit**

```powershell
Set-Location "d:\CMakeRepos\kaleido"
git add src/material_types.h src/material_types.cpp src/scene.h src/scene.cpp
git commit -m "refactor: extract material_types from scene"
```

---

### Task 2: CMake test gate + contract test executable

**Files:**
- Modify: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/material_contract_test.cpp`

- [ ] **Step 1: Append to root `CMakeLists.txt` (after `option(BUILD_KALEIDO_EDITOR...)` line)**

```cmake
option(KALEIDO_BUILD_TESTS "Build kaleido_material_tests (no Vulkan link)" OFF)

if(KALEIDO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Create `tests/CMakeLists.txt`**

```cmake
add_executable(kaleido_material_tests
    material_contract_test.cpp
    ${CMAKE_SOURCE_DIR}/src/material_types.cpp
)

target_include_directories(kaleido_material_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(kaleido_material_tests PRIVATE
    ${KALEIDO_COMMON_DEFINITIONS}
)

target_link_libraries(kaleido_material_tests PRIVATE glm)

if(MSVC)
    target_compile_options(kaleido_material_tests PRIVATE /W4)
endif()

add_test(NAME kaleido_material_contract COMMAND kaleido_material_tests)
```

- [ ] **Step 3: Create `tests/material_contract_test.cpp`**

```cpp
#include "material_types.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#define TEST_CHECK(cond, msg) \
	do \
	{ \
		if (!(cond)) \
		{ \
			return fail(msg); \
		} \
	} while (0)

#include <cstdio>

static int fail(const char* msg)
{
	// Tests: stderr only (harness), not engine LOG macros.
	fputs(msg, stderr);
	fputc('\n', stderr);
	return 1;
}

static int test_material_size_align()
{
	TEST_CHECK(sizeof(Material) == 112u, "Material sizeof");
	TEST_CHECK(alignof(Material) == 16u, "Material alignas(16)");
	return 0;
}

static int test_material_offsets()
{
	TEST_CHECK(offsetof(Material, albedoTexture) == 0u, "offsetof albedoTexture");
	TEST_CHECK(offsetof(Material, baseColorFactor) == 32u, "offsetof baseColorFactor");
	TEST_CHECK(offsetof(Material, emissiveFactor) == 64u, "offsetof emissiveFactor");
	TEST_CHECK(offsetof(Material, workflow) == 76u, "offsetof workflow");
	TEST_CHECK(offsetof(Material, shadingParams) == 80u, "offsetof shadingParams");
	TEST_CHECK(offsetof(Material, alphaMode) == 96u, "offsetof alphaMode");
	TEST_CHECK(offsetof(Material, transmissionTexture) == 100u, "offsetof transmissionTexture");
	TEST_CHECK(offsetof(Material, transmissionFactor) == 104u, "offsetof transmissionFactor");
	TEST_CHECK(offsetof(Material, ior) == 108u, "offsetof ior");
	return 0;
}

static int test_material_key_pack()
{
	MaterialKey a = MaterialKey::DefaultPbrOpaque();
	MaterialKey b = MaterialKey::Pack(MaterialType::PBR, 1u, 0u, 0u, 0u);
	TEST_CHECK(a == b, "DefaultPbrOpaque vs Pack");
	MaterialKey mask = MaterialKey::Pack(MaterialType::PBR, 1u, 1u, 0u, 0u);
	TEST_CHECK(mask.packed != a.packed, "alpha mode should change key");
	return 0;
}

static int test_draw_batch_ranges()
{
	std::vector<MaterialKey> keys = {
		MaterialKey::DefaultPbrOpaque(),
		MaterialKey::DefaultPbrOpaque(),
		MaterialKey::Pack(MaterialType::PBR, 1u, 2u, 0u, 0u),
	};
	std::vector<DrawBatch> batches;
	MaterialKey current = keys[0];
	uint32_t first = 0;
	for (uint32_t i = 0; i <= keys.size(); ++i)
	{
		const bool end = (i == keys.size());
		const MaterialKey k = end ? MaterialKey{} : keys[i];
		if (end || k.packed != current.packed)
		{
			DrawBatch b;
			b.materialKey = current;
			b.firstDraw = first;
			b.drawCount = i - first;
			batches.push_back(b);
			if (!end)
			{
				current = k;
				first = i;
			}
		}
	}
	TEST_CHECK(batches.size() == 2u, "batch count");
	TEST_CHECK(batches[0].drawCount == 2u, "first batch count");
	TEST_CHECK(batches[1].drawCount == 1u, "second batch count");
	return 0;
}

int main()
{
	if (test_material_size_align()) return 1;
	if (test_material_offsets()) return 1;
	if (test_material_key_pack()) return 1;
	if (test_draw_batch_ranges()) return 1;
	return 0;
}
```

- [ ] **Step 4: Configure and build tests**

```powershell
Set-Location "d:\CMakeRepos\kaleido\build"
cmake .. -DKALEIDO_BUILD_TESTS=ON
cmake --build . --config Debug --target kaleido_material_tests
```

Expected: **Build succeeds**.

- [ ] **Step 5: Run tests**

```powershell
.\Debug\kaleido_material_tests.exe
echo $LASTEXITCODE
```

Expected: **exit code 0**.

```powershell
ctest -C Debug -R kaleido_material_contract --output-on-failure
```

Expected: **Passed**.

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt tests/
git commit -m "test: add material layout contract tests (KALEIDO_BUILD_TESTS)"
```

---

### Task 3: Document G-buffer and shader consumers

**Files:**
- Create: `docs/superpowers/gbuffer-and-material-encoding.md`

- [ ] **Step 1: Write the doc** with these mandatory sections (fill with quotes from current shaders when writing):

1. **G-buffer from `mesh.frag.glsl`:** RT0 = `tosrgb(albedo.rgb)`, A = emissive scale encoding; RT1 = oct-encoded normal + roughness + metallic; `gbufferMaterialIndex` in post pass.
2. **`final.comp` lighting:** reads roughness/metallic from G-buffer; Disney diffuse + GGX specular (cite line range).
3. **MR vs SG on GPU:** MR uses texture G/B for roughness/metallic; SG path uses `pbrFactor` and gloss → roughness heuristic (cite `mesh.frag.glsl`).
4. **Consumer list:** `mesh.frag.glsl`, `shadow.comp.glsl`, `transparency_blend.frag.glsl`, `transmission_resolve.comp.glsl`.

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/gbuffer-and-material-encoding.md
git commit -m "docs: describe g-buffer and material field consumers"
```

---

### Task 4: Add `PbrSurfaceDescription` (semantic layer)

**Files:**
- Create: `src/pbr_surface_description.h`

- [ ] **Step 1: Create header** (no cgltf)

```cpp
#pragma once

#include "math.h"
#include <stdint.h>

enum class PbrWorkflow : int32_t
{
	Default = 0,
	MetallicRoughness = 1,
	SpecularGlossiness = 2,
};

struct PbrSurfaceDescription
{
	int32_t albedoTexture = 0;
	int32_t normalTexture = 0;
	int32_t pbrTexture = 0;
	int32_t emissiveTexture = 0;
	int32_t occlusionTexture = 0;

	vec4 baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
	vec4 pbrFactor{ 1.f, 1.f, 0.f, 1.f };
	float emissiveFactor[3]{ 0.f, 0.f, 0.f };
	PbrWorkflow workflow = PbrWorkflow::MetallicRoughness;

	vec4 shadingParams{ 1.f, 1.f, 0.5f, 1.f };
	int32_t alphaMode = 0;
	bool doubleSided = false;
	bool hasTransmission = false;

	int32_t transmissionTexture = 0;
	float transmissionFactor = 0.f;
	float ior = 1.5f;
};
```

- [ ] **Step 2: Build `kaleido_standalone`** (sanity)

Expected: **unchanged** — header not used yet.

- [ ] **Step 3: Commit**

```bash
git add src/pbr_surface_description.h
git commit -m "feat: add PbrSurfaceDescription semantic struct"
```

---

### Task 5: Implement glTF decoder

**Files:**
- Create: `src/gltf_pbr_decoder.h`
- Create: `src/gltf_pbr_decoder.cpp`
- Modify: `src/scene.cpp` (replace body of `BuildPbrMaterial` with decode + temporary direct copy to `Material` until Task 6)

- [ ] **Step 1: Add `src/gltf_pbr_decoder.h`**

```cpp
#pragma once

#include "pbr_surface_description.h"

struct cgltf_data;
struct cgltf_material;

bool DecodeGltfPbrMaterial(const cgltf_data* data, const cgltf_material& material, int textureOffset, PbrSurfaceDescription& out);
```

- [ ] **Step 2: Implement `src/gltf_pbr_decoder.cpp`** — Move the logic from existing `BuildPbrMaterial` in `scene.cpp` (lines handling SG, MR, normal, emissive, occlusion, emissive strength, transmission, IOR, `shadingParams`, `alphaMode`, `doubleSided`, `has_transmission`) into `DecodeGltfPbrMaterial`. Map `cgltf_alpha_mode_*` to `int32_t` as today. Set `out.doubleSided = material.double_sided != 0`; `out.hasTransmission = material.has_transmission != 0`. Do **not** compute `MaterialKey` here (encoder owns key in Task 6).

- [ ] **Step 3: Replace `static PBRMaterial BuildPbrMaterial(...)` in `scene.cpp`** with:

```cpp
#include "gltf_pbr_decoder.h"

static PBRMaterial BuildPbrMaterial(const cgltf_data* data, const cgltf_material& material, int textureOffset)
{
	PbrSurfaceDescription desc{};
	if (!DecodeGltfPbrMaterial(data, material, textureOffset, desc))
		return PBRMaterial::CreateDefault();

	PBRMaterial result = PBRMaterial::CreateDefault();
	Material& mat = result.data;
	mat.albedoTexture = desc.albedoTexture;
	mat.normalTexture = desc.normalTexture;
	mat.pbrTexture = desc.pbrTexture;
	mat.emissiveTexture = desc.emissiveTexture;
	mat.occlusionTexture = desc.occlusionTexture;
	mat.baseColorFactor = desc.baseColorFactor;
	mat.pbrFactor = desc.pbrFactor;
	mat.emissiveFactor[0] = desc.emissiveFactor[0];
	mat.emissiveFactor[1] = desc.emissiveFactor[1];
	mat.emissiveFactor[2] = desc.emissiveFactor[2];
	mat.workflow = static_cast<int32_t>(desc.workflow);
	mat.shadingParams = desc.shadingParams;
	mat.alphaMode = desc.alphaMode;
	mat.transmissionTexture = desc.transmissionTexture;
	mat.transmissionFactor = desc.transmissionFactor;
	mat.ior = desc.ior;
	result.key = BuildPbrMaterialKey(material, mat.workflow);
	return result;
}
```

Decoder must return `true` for all paths the old function handled (only return `false` if you add explicit validation later).

- [ ] **Step 4: Build** `kaleido_standalone` Debug.

- [ ] **Step 5: Commit**

```bash
git add src/gltf_pbr_decoder.h src/gltf_pbr_decoder.cpp src/scene.cpp
git commit -m "refactor: decode glTF PBR into PbrSurfaceDescription"
```

---

### Task 6: Implement GPU encoder + wire key from semantics

**Files:**
- Create: `src/gpu_material_encoder.h`
- Create: `src/gpu_material_encoder.cpp`
- Modify: `src/scene.cpp` (`BuildPbrMaterial` calls encoder; remove `BuildPbrMaterialKey` usage from material path or reimplement `BuildPbrMaterialKey` atop `PbrSurfaceDescription` in encoder file)

- [ ] **Step 1: Add `src/gpu_material_encoder.h`**

```cpp
#pragma once

#include "material_types.h"
#include "pbr_surface_description.h"

void EncodePbrToGpuMaterial(const PbrSurfaceDescription& in, Material& outGpu, MaterialKey& outKey);
```

- [ ] **Step 2: Implement `src/gpu_material_encoder.cpp`**

```cpp
#include "gpu_material_encoder.h"

static MaterialKey KeyFromDescription(const PbrSurfaceDescription& in)
{
	const uint32_t wf = static_cast<uint32_t>(in.workflow) & 0xFu;
	uint32_t alpha = (uint32_t)in.alphaMode;
	if (alpha > 2u)
		alpha = 2u;
	const uint32_t ds = in.doubleSided ? 1u : 0u;
	const uint32_t tr = in.hasTransmission ? 1u : 0u;
	return MaterialKey::Pack(MaterialType::PBR, wf, alpha, ds, tr);
}

void EncodePbrToGpuMaterial(const PbrSurfaceDescription& in, Material& outGpu, MaterialKey& outKey)
{
	outGpu = Material{};
	outGpu.albedoTexture = in.albedoTexture;
	outGpu.normalTexture = in.normalTexture;
	outGpu.pbrTexture = in.pbrTexture;
	outGpu.emissiveTexture = in.emissiveTexture;
	outGpu.occlusionTexture = in.occlusionTexture;
	outGpu.baseColorFactor = in.baseColorFactor;
	outGpu.pbrFactor = in.pbrFactor;
	outGpu.emissiveFactor[0] = in.emissiveFactor[0];
	outGpu.emissiveFactor[1] = in.emissiveFactor[1];
	outGpu.emissiveFactor[2] = in.emissiveFactor[2];
	outGpu.workflow = static_cast<int32_t>(in.workflow);
	outGpu.shadingParams = in.shadingParams;
	outGpu.alphaMode = in.alphaMode;
	outGpu.transmissionTexture = in.transmissionTexture;
	outGpu.transmissionFactor = in.transmissionFactor;
	outGpu.ior = in.ior;
	outGpu.texturePad[0] = 0;
	outGpu.texturePad[1] = 0;
	outGpu.texturePad[2] = 0;
	outKey = KeyFromDescription(in);
}
```

Initialize `outGpu` fields to match `PBRMaterial::CreateDefault` for any field not set by glTF (decoder should already mirror previous behavior).

- [ ] **Step 3: Simplify `BuildPbrMaterial` in `scene.cpp`**

```cpp
static PBRMaterial BuildPbrMaterial(const cgltf_data* data, const cgltf_material& material, int textureOffset)
{
	PbrSurfaceDescription desc{};
	if (!DecodeGltfPbrMaterial(data, material, textureOffset, desc))
		return PBRMaterial::CreateDefault();

	PBRMaterial result{};
	EncodePbrToGpuMaterial(desc, result.data, result.key);
	return result;
}
```

- [ ] **Step 4: Remove or inline `BuildPbrMaterialKey` in `scene.cpp`** if unused; grep for `BuildPbrMaterialKey` and delete dead code.

- [ ] **Step 5: Build + run** `kaleido_standalone` on a known glTF (e.g. chess or street scene). Expected: **no visual regression** (manual).

- [ ] **Step 6: Commit**

```bash
git add src/gpu_material_encoder.h src/gpu_material_encoder.cpp src/scene.cpp
git commit -m "feat: encode PbrSurfaceDescription to GPU Material + MaterialKey"
```

---

### Task 7: Golden tests for encoder

**Files:**
- Create: `tests/material_encoder_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add test file** comparing `EncodePbrToGpuMaterial` to `PBRMaterial::CreateDefault()` for default input, and one MR case (metallic 0.5, roughness 0.25, workflow MR, opaque).

Example core:

```cpp
#include "gpu_material_encoder.h"
#include "material_types.h"
#include "pbr_surface_description.h"

#include <cstring>

#include <cstdio>

static int fail(const char* m)
{
	fputs(m, stderr);
	fputc('\n', stderr);
	return 1;
}

static int test_default_matches_create_default()
{
	PbrSurfaceDescription in{};
	Material gpu{};
	MaterialKey key{};
	EncodePbrToGpuMaterial(in, gpu, key);

	PBRMaterial ref = PBRMaterial::CreateDefault();
	TEST_CHECK(std::memcmp(&gpu, &ref.data, sizeof(Material)) == 0, "default gpu bytes");
	TEST_CHECK(key == ref.key, "default key");
	return 0;
}
```

Fill in `TEST_CHECK` macro same as Task 2.

- [ ] **Step 2: Append source file to `tests/CMakeLists.txt` `add_executable` list:**

```cmake
add_executable(kaleido_material_tests
    material_contract_test.cpp
    material_encoder_test.cpp
    ${CMAKE_SOURCE_DIR}/src/material_types.cpp
    ${CMAKE_SOURCE_DIR}/src/gpu_material_encoder.cpp
)
```

- [ ] **Step 3: Build and run** `kaleido_material_tests`. Fix default `PbrSurfaceDescription` if decoder defaults differ from `CreateDefault` until tests encode the same bytes.

- [ ] **Step 4: Commit**

```bash
git add tests/material_encoder_test.cpp tests/CMakeLists.txt
git commit -m "test: golden Material bytes for encoder defaults"
```

---

### Task 8: Decoder tests (minimal cgltf)

**Files:**
- Create: `tests/material_decoder_test.cpp`
- Modify: `tests/CMakeLists.txt` (add cpp + link `cgltf`)

- [ ] **Step 1: Build a one-material `cgltf_data` in memory** using `cgltf_alloc` defaults: allocate `materials_count = 1`, set `has_pbr_metallic_roughness = true`, set factors and `alpha_mode`. Call `DecodeGltfPbrMaterial` with `textureOffset = 1` and assert texture indices when non-null textures are set (optional: skip texture index test if setting textures is too verbose—then only test factors/workflow).

Minimal pattern:

```cpp
#include "gltf_pbr_decoder.h"
#include <cgltf.h>
#include <cstring>

static int test_mr_factors()
{
	cgltf_options opts{};
	cgltf_data* data = nullptr;
	// Prefer loading a tiny embedded JSON/GLB fixture from tests/data/...
	// If adding a file fixture: commit tests/data/min_mr.gltf and use cgltf_parse_file.
	return 0;
}
```

**Concrete minimal path:** add `tests/data/min_mr.gltf` (valid minimal glTF 2.0 with one mesh and one MR material, metallicFactor 0.25, roughnessFactor 0.75). Load with `cgltf_parse_file`, then `DecodeGltfPbrMaterial(data, data->materials[0], 0, desc)` and `TEST_CHECK` `desc.pbrFactor.z == 0.25f` etc.

- [ ] **Step 2: Update `tests/CMakeLists.txt`**

```cmake
target_link_libraries(kaleido_material_tests PRIVATE glm cgltf)
```

- [ ] **Step 3: Run tests** — all pass.

- [ ] **Step 4: Commit**

```bash
git add tests/material_decoder_test.cpp tests/data/min_mr.gltf tests/CMakeLists.txt
git commit -m "test: glTF MR decoder fixture"
```

---

### Task 9 (optional): Scalar BRDF reference parity

**Files:**
- Create: `tests/brdf_reference_test.cpp`
- Create: `src/brdf_reference.h` + `src/brdf_reference.cpp` (only if shared with future tools)

- [ ] **Step 1:** Port **one** scalar path from `final.comp.glsl` (e.g. `NdotL = NdotV = NdotH = VdotH = 0.5`, `roughness = 0.25`, `metallic = 0.0`) into C++ `double` and assert `specularBRDF` and `diffuseBRDF` channel within `1e-4` of the shader math (replicate the same formulas literally).

- [ ] **Step 2:** Add to `kaleido_material_tests` sources, build, run, commit `test: add scalar BRDF reference parity`.

---

## Plan self-review (spec coverage)

| Spec section | Tasks |
|--------------|-------|
| Phase 0 test + contract | Task 1–2, Task 3 doc |
| Phase 1 decoder | Task 4–5, Task 8 |
| Phase 2 encoder | Task 6–7 |
| Phase 3 batch invariants | Task 2 (`test_draw_batch_ranges`); extend later with extracted `RebuildDrawBatchesFromKeys` in `gpu_material_encoder.cpp` if you want parity with `RebuildMaterialDrawBatches` |
| Shader consumer list | Task 3 |
| Phase 4 extensions | Not scheduled; repeat Tasks 4→6→7 per extension |

**Placeholder scan:** None intentional; Task 8 requires committing a real `min_mr.gltf` (generate with glTF sample or hand-write minimal JSON).

---

**Plan complete and saved to `docs/superpowers/plans/2026-04-16-pbr-material-semantic-encoder.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — Dispatch a fresh subagent per task, review between tasks, fast iteration. **REQUIRED SUB-SKILL:** superpowers:subagent-driven-development.

**2. Inline Execution** — Execute tasks in this session using executing-plans with checkpoints. **REQUIRED SUB-SKILL:** superpowers:executing-plans.

**Which approach?**

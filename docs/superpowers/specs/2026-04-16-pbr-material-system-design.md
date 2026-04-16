# PBR Material System Design (Semantic Layer + Encoder + Contract)

**Status:** Adopted (main line)  
**Date:** 2026-04-16  
**Scope:** CPU-side material pipeline from glTF to GPU `Material` SSBO, layout contract with GLSL, batching keys, and test strategy. Out of scope: material graphs, arbitrary shader graphs, full GPU-driven queue refactor (covered by separate renderer generalization work).

---

## 1. Context

The repository already has:

- A packed GPU struct `Material` (C++ in `src/scene.h`, mirror in `src/shaders/mesh.h`) with `static_assert` on size.
- `MaterialClass`, `PBRMaterial`, `MaterialDatabase`, `MaterialKey`, `DrawBatch`, and draw sorting / batch rebuild in the runtime path.
- glTF material construction embedded in `src/scene.cpp` (`BuildPbrMaterial`, `BuildPbrMaterialKey`, etc.).
- Deferred shading: G-buffer write in `src/shaders/mesh.frag.glsl`, lighting in `src/shaders/final.comp.glsl`; ray-traced transparency shadows read `Material` in `src/shaders/shadow.comp.glsl`.

The attached external plan *Renderer 通用化路线* remains directionally valid for GPU-driven scaling, but its “phase A” material notes are partially superseded by the types above. This spec focuses on **making the material subsystem testable and evolvable** without changing the overall renderer roadmap.

---

## 2. Goals

1. **Separation of concerns:** glTF semantics, GPU packing, and sort/batch keys are owned by distinct units with narrow interfaces.
2. **Incremental delivery:** each milestone keeps **pixel-identical** output unless explicitly expanding features.
3. **Test-driven friendly:** dominant tests run on CPU (no Vulkan required) for decode/encode/key invariants; a **thin** layer of render probes or golden frames later.
4. **Contract safety:** a single place defines and verifies std430 layout expectations shared by C++ and GLSL consumers.

## 3. Non-goals (YAGNI)

- Node-based material graphs or user-defined shader linking in this iteration.
- Replacing the deferred G-buffer format solely for testing (debug modes may be used for probes only).
- Mandating CI golden images on multiple GPUs in the first iteration.

---

## 4. Architecture (Route 2)

### 4.1 Layers

| Layer | Responsibility | Typical location (suggested) |
|-------|------------------|------------------------------|
| **Semantic** | glTF-meaningful state: workflow (MR/SG), factors, texture slot indices after remapping, alpha mode, double-sided, transmission, IOR, normal/occlusion scales, etc. | e.g. `PbrSurfaceDescription` in a header + decoder `.cpp` |
| **Encoder** | Maps semantic state → packed `Material` and derives `MaterialKey` (and later `ShaderPermutationKey` if split). Pure, deterministic. | e.g. `gpu_material_encoder.cpp` |
| **Contract** | `Material` layout, versioning policy, static asserts, optional offset tests; documents which shaders read which fields. | `scene.h` / `mesh.h` + tests |
| **Runtime DB** | Storage, upload order, `MaterialDatabase::Add` (existing). | existing `scene.h` |

**Data flow:** `cgltf_material` → **Decoder** → `PbrSurfaceDescription` → **Encoder** → `Material` + `MaterialKey` → `PBRMaterial` / `MaterialDatabase`.

### 4.2 Relationship to batching

`MaterialKey` must be derivable from the same semantic inputs as the encoder to avoid drift. Longer term, split:

- **SortKey:** coarse ordering for draw sorting and batch boundaries.
- **ShaderPermutationKey:** fine-grained shader variant / specialization (when introduced).

Initially both may still pack into one `uint64` with documented bit layout; tests must cover bit packing.

### 4.3 Shader consumers (maintenance list)

Any change to `Material` or sampling rules must be checked against at least:

- `src/shaders/mesh.frag.glsl`
- `src/shaders/shadow.comp.glsl`
- Other buffers that index `materials[meshDraw.materialIndex]` (search `Materials` / `materials[` in `src/shaders/`).

---

## 5. Phased implementation

### Phase 0 — Test harness and contract

- Add an optional CMake target for tests (project-local), without forcing `BUILD_TESTING` on all of `external/`.
- Contract tests: `sizeof` / `alignof` / `offsetof` for C++ `Material`; document parity with `mesh.h` (manual or generated single source of truth is a follow-up decision).
- Document current G-buffer encoding and MR/SG interpretation (including known SG approximation).

**Exit:** tests compile and run with zero GPU; no visual change.

### Phase 1 — Decoder extraction

- Move glTF → semantic struct logic out of `scene.cpp` into a dedicated decoder module; `BuildPbrMaterial` calls decoder then assigns (still manually or via thin wrapper).
- Table-driven tests: minimal materials (MR default, MR + textures, SG path, alpha modes, double-sided, transmission + IOR).

**Exit:** golden or hashed `Material` bytes match pre-refactor for fixed fixtures.

### Phase 2 — Encoder

- Implement `EncodeGpuMaterial(PbrSurfaceDescription) -> Material` and centralized `MaterialKey` computation from semantics.
- Remove duplicated field assignment from the loader; `PBRMaterial` holds `data` + `key` produced by encoder.

**Exit:** same golden bytes; expanded unit tests for edge factors.

### Phase 3 — Key / pipeline alignment (prep)

- Document and test invariants: sorted draws produce valid `DrawBatch` ranges; `materialIndex` bounds.
- If shader variants appear, introduce `ShaderPermutationKey` without changing sort behavior first.

### Phase 4 — Feature extensions (per extension)

For each new glTF extension (e.g. clearcoat): extend semantic struct → tests → layout decision (extend `Material` vs side buffer) → shader → optional render probe.

---

## 6. Testing strategy

### 6.1 Pyramid

1. **Unit (primary):** decoder branches, encoder output, `MaterialKey` packing, batch invariants.
2. **Contract:** C++/GLSL struct agreement; list of shader consumers updated in PR checklist.
3. **Reference math (recommended):** BRDF / factor combinations mirrored in C++ for a small set of vectors (complements shader tests, very stable).
4. **Render (thin):** one minimal scene, fixed resolution, **TAA and other temporal effects disabled**, optional `gbufferDebugMode` or explicit probe pass; compare PNG with tolerance or SSIM. Run on a **designated** machine or CI job first; do not block all PRs on multi-GPU golden until stable.

### 6.2 Flakiness controls for render tests

- Disable temporal accumulation and fixed-seed noise where applicable.
- Prefer **pixel probes** over full-frame where possible.
- Pin GPU/driver only for jobs that enforce golden images; keep CPU tests the default gate.

---

## 7. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Layout drift between C++ and GLSL | Encoder single entry point + contract tests + PR checklist of shader files |
| SG vs MR semantic mismatch | Document approximation in spec; encoder tests encode explicit expected behavior |
| RT shadow path diverges from raster | Shared semantic tests; when changing alpha/transmission, add shadow-specific fixture if needed |
| `Material` struct growth | Version field or side SSBO policy decided before first breaking layout change |

---

## 8. Success criteria

- New material fields or glTF extensions can be added with **decoder + encoder tests first**, then shaders.
- Refactors of `scene.cpp` loading do not silently change `Material` bytes for standard fixtures.
- Renderer generalization (GPU-driven queues) can consume **stable** `MaterialKey` and GPU layout documentation without re-deriving rules from shaders.

---

## 9. References (in-repo)

- `src/scene.h` — `Material`, `MaterialDatabase`, `MaterialKey`, `PBRMaterial`, `DrawBatch`
- `src/scene.cpp` — `BuildPbrMaterial`, `BuildPbrMaterialKey`, `SortSceneDrawsByMaterialKey`, `RebuildMaterialDrawBatches`
- `src/shaders/mesh.h`, `src/shaders/mesh.frag.glsl`, `src/shaders/final.comp.glsl`, `src/shaders/shadow.comp.glsl`
- `CMakeLists.txt` — `BUILD_TESTING OFF` today; project-local test option to be added under Phase 0

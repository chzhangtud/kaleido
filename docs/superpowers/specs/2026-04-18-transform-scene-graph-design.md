# Transform + Scene Graph Design (glTF Node Hierarchy → GPU `mat4`)

**Status:** Adopted (confirmed 2026-04-18)  
**Date:** 2026-04-18  
**Scope:** Runtime scene graph mirroring glTF nodes, per-node local transforms, dirty propagation of cached world transforms, per-primitive draw records carrying a **full world `mat4`** for shading and culling, animation driving **nodes** (not baked per-draw world with uniform scale), and editor hierarchy selection + local TRS editing.  
**Out of scope (this iteration):** Skinning, morph targets, physics joints, multi-file asset merging, undo/redo stack, serialization of editor overrides back to glTF.

---

## 1. Context

Current behavior (see `src/scene.cpp`, `src/scene.h`, `src/shaders/mesh.h`):

- Each glTF mesh primitive becomes one `Mesh` in `Geometry`.
- For each node that references a mesh, `cgltf_node_transform_world` computes a **baked world matrix**, then `decomposeTransform` feeds `MeshDraw` with `vec3 position`, **scalar `scale`** (max of non-uniform scale), and `quat orientation`.
- Vertex shader (`src/shaders/mesh.vert.glsl`) applies uniform scale + quaternion rotation around the origin, then adds translation — **not** equivalent to arbitrary glTF `mat4` / non-uniform scale.
- `GltfDocumentOutline` already stores a **node tree** (names, children, `meshIndex`) for ImGui; it does not own transforms or selection.
- Animations store `Animation::drawIndex` and keyframes with baked world TRS compressed to uniform scale (`scene.cpp` / `renderer.cpp`).

This spec replaces the baked-uniform path with **Route 1** from brainstorming: **node table (local transform) + primitive draw table (GPU world matrix)**, aligned with Filament-style separation of Transform vs renderable.

---

## 2. Goals

1. **Preserve glTF hierarchy** at runtime: one logical record per glTF node (transform + graph links), independent of mesh/primitive count.
2. **Correct world transform** for rendering: each draw carries **`mat4 world`** (column-major, GLM/GLSL consistent) supporting `has_matrix`, non-uniform scale, and composed parent chains.
3. **Dirty caching:** changing a node’s **local** transform marks its **subtree** world caches dirty; evaluation propagates from roots in a deterministic order and writes updated **`world` matrices into draws** referenced by that subtree.
4. **Animations** target **glTF node indices** (local TRS channels); runtime updates node locals, then uses the same dirty/evaluate/sync path as the editor.
5. **Editor:** hierarchy view with **node selection** and **inspector** for local translation / rotation / scale; edits write node locals and trigger dirty propagation.
6. **Downstream correctness:** culling (mesh + meshlet), raster passes, transparency pass, and ray-tracing instance transforms consume the same **`mat4`** semantics.

## 3. Non-goals (YAGNI)

- Full generic ECS framework (no mandated EnTT-style registry); a **focused** `Scene` sub-structure is enough.
- Automatic **gizmo** manipulation in the viewport (can be backlog); **numeric** TRS in inspector satisfies MVP.
- **Saving** edited scenes to disk (separate export spec).
- **GPU skinning** / **morph** weights (document extension points only).

---

## 4. Architecture

### 4.1 Two-layer model

| Layer | Responsibility |
|-------|------------------|
| **Transform graph** | `N` entries aligned with glTF node order (`0 .. nodes_count-1`). Each stores `parent` index (`-1` for roots within the loaded asset), ordered `children`, **local** transform (`mat4 local` — either copied from `has_matrix` or composed from TRS), `worldDirty`, cached `mat4 world`. Optional: `visible` flag (default true). |
| **Draw instances** | One `MeshDraw` per **triangle primitive** (current mesh splitting). Stores **GPU fields** including `mat4 world`, existing `meshIndex`, `materialIndex`, `postPass`, `flags`, `meshletVisibilityOffset`, and **`uint32_t gltfNodeIndex`** for CPU-side mapping (editor, animation, diagnostics). |

**Flow:** local change → mark dirty subtree → `EvaluateWorldTransforms()` → for each node whose `world` changed, copy `world` to **all** `MeshDraw` entries with matching `gltfNodeIndex` → upload or map `MeshDraw` SSBO.

### 4.2 Matrix conventions

- **CPU:** `glm::mat4`, **column-major**, vector as column: `v_world = world * vec4(v_object, 1)`.
- **GLSL `mesh.h`:** `mat4 world` with the same multiplication order as GLM.
- **Normals:** `mat3 normalMat = transpose(inverse(mat3(world)))` computed in the vertex stage for MVP (acceptable cost for typical draw counts; revisit with `worldIT` packing if profiling demands).

### 4.3 Bounding volumes under non-uniform scale

Mesh and meshlet centers/radii are authored in **object space**. After world becomes a full `mat4`:

- **World center:** `vec3 c_w = (world * vec4(mesh.center, 1)).xyz` (GLSL) / `glm::vec3(world * glm::vec4(center,1))` (CPU).
- **World radius (conservative):** `radius_w = mesh.radius * max(length(world[0].xyz), length(world[1].xyz), length(world[2].xyz))` where `world[i]` denotes the **i-th column** of the linear part (same convention as GLM column access). Document this as the **MVP conservative bound**; tighter OBB transforms are backlog.

Cone axis for meshlet backface culling: transform direction with `mat3(world)` normalized (non-uniform scale slightly distorts cone tests; acceptable MVP; flag for future “normal matrix * axis”).

### 4.4 Ray tracing instance matrix

`fillInstanceRT` (`src/scenert.cpp`) must build `VkTransformMatrixKHR` from the same **`mat4 world`** used by rasterization (object → world for BLAS in object space). Replace the current `transpose(mat3_cast(q))*scale` derivation with **documented extraction** of the affine 3×4 from `glm::mat4` into Vulkan’s row packing (`matrix[3][4]` layout per Vulkan spec). Add a short comment in code referencing the column→row conversion to prevent sign/transpose bugs.

---

## 5. Dirty semantics (normative)

**Local edit** (loader init, animation sample, editor commit):

1. Set `worldDirty = true` on the edited node.
2. DFS/BFS from that node: for every **descendant**, set `worldDirty = true`.
3. Do **not** require marking ancestors dirty (their cached `world` is unchanged); world recomputation always multiplies **parent world** (which must already be clean when visiting children if traversal is parent-before-child).

**Evaluation** (single entry point, e.g. `Scene::FlushTransforms()`):

1. Build a stable order: **breadth-first from scene roots** (default glTF scene’s `rootNodes`, same order as glTF), or explicit topological sort by parent depth increasing. **Normative choice:** sort nodes by **tree depth** ascending (roots depth 0), tie-break by glTF index ascending — guarantees parent evaluated before child for a tree.
2. For each node `i` in order:
   - If `parent == -1`, `world = local` (or multiply by optional editor root transform if introduced later).
   - Else `world = parentWorld * local`.
   - If `worldDirty` was false **and** `world` equals previous cached matrix (optional fast path), skip; else mark `world` updated.
3. Clear `worldDirty` on all visited nodes after successful write.

**Sync to draws:** maintain `std::vector<uint32_t> drawsPerNode` (flat list: pairs `(nodeIndex, drawIndex)` or range offsets per node) populated at load. On any node whose `world` changed this frame, copy `world` into each attached `MeshDraw`.

---

## 6. Loading (`loadScene`)

1. Append geometry/materials/textures as today.
2. Allocate `TransformNode` array with `data->nodes_count`.
3. For each glTF node `i`, fill:
   - `parent` index via `cgltf_node.parent` (already resolved after `cgltf_load_buffers` + validate),
   - `children` list,
   - `local` matrix: if `has_matrix`, copy 16 floats column-major; else compose from translation / rotation / scale using the same math as `cgltf_node_transform_local` (call `cgltf_node_transform_local` into a float array, then assign to `glm::mat4`).
4. For each node with `mesh`, for each valid triangle primitive, push `MeshDraw` with `gltfNodeIndex = i`, material/mesh indices as today, **`world` initially identity placeholder**.
5. Run **one** `FlushTransforms()` (or equivalent) before returning so draws are valid.
6. `FillGltfDocumentOutline` remains for UI labels; optionally extend outline with `gltfNodeIndex` redundant check (same index as vector position).

**Cameras / lights:** keep using `cgltf_node_transform_world` at load **or** read from evaluated graph once transforms are live — pick one implementation to avoid drift (spec prefers **read evaluated node `world`** after first flush for consistency).

---

## 7. Animation

- Replace `Animation::drawIndex` with **`uint32_t gltfNodeIndex`** (or `nodeIndex`).
- Keyframes store **local** `vec3 translation`, `quat rotation`, `vec3 scale` (not uniform); optionally store `mat4` if channel uses matrix (rare); MVP assumes TRS channels only — if only matrix animations appear, **skip** with warning (same as today’s skip patterns) unless matrix sampling is implemented.
- Runtime (`renderer.cpp`): each tick, interpolate locals, write into `TransformNode[gltfNodeIndex].local`, mark dirty subtree from that node, rely on shared flush before GPU upload.

**Migration note:** remove dependency on `nodeDraws[i]` pointing at “first primitive draw”; animation always moves the **node**, which updates **all** primitives under that node.

---

## 8. Editor (`renderer.cpp` ImGui)

- **Selection state:** `optional<uint32_t> selectedGltfNodeIndex` stored on `Scene` or `VulkanContext` (whichever already hosts UI scratch state — implementation plan chooses one home to avoid globals).
- **Hierarchy:** reuse `DrawGltfDocumentTree` / `DrawGltfOutlineNode`; replace `BulletText`/`TreeNodeEx` leaf behavior with `ImGui::Selectable` or tree flags `ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth` and `ImGuiTreeNodeFlags_Selected` when `nodeIdx == selected`.
- **Inspector:** when selection active, show:
  - Local TRS fields (`DragFloat3`, quaternion as Euler **degrees** for editing with `glm::eulerAngles` + `glm::angleAxis` round-trip documented as approximate),
  - “Reset to file” (reload locals from last loaded cgltf snapshot — optional MVP: restart from stored copy of initial locals in `TransformNode::localBindPose`).

**`has_matrix` nodes:** MVP shows a read-only note “Matrix node — edit as TRS not supported” **or** decompose once at load and treat as TRS thereafter (lossy). **Normative MVP:** **decompose once at load** into TRS for inspector editing; log warning if decomposition is numerically ill-conditioned.

---

## 9. Shader / pipeline consumer list (must stay in sync)

Any change to `MeshDraw` layout or world usage must update:

- `src/shaders/mesh.h` (`MeshDraw`)
- `src/shaders/mesh.vert.glsl`
- `src/shaders/meshlet.mesh.glsl`, `src/shaders/meshlet.task.glsl`
- `src/shaders/mesh.frag.glsl`, `src/shaders/transparency_blend.frag.glsl`
- `src/shaders/drawcull.comp.glsl`
- `src/shaders/clustercull.comp.glsl`
- `src/shaders/shadow.comp.glsl`
- `src/scene.h` (C++ mirror struct + `static_assert(sizeof)` parity tests if added)
- `src/renderer.cpp` (buffer size, animation updates, any CPU culling that mirrors GPU)
- `src/scenert.cpp` / `src/scenert.h` (`fillInstanceRT`)
- `src/kaleido_runtime.cpp` (random draw generator, empty scene bootstrap)

---

## 10. Testing strategy

1. **Compile-time:** `static_assert(sizeof(MeshDraw) % 16 == 0)` plus, after layout stabilizes, an exact `sizeof` equality check documented against `mesh.h` std430 packing (include explicit padding fields in `MeshDraw` until C++ and GLSL sizes match).
2. **CPU smoke test (optional CMake target):** build two-node chain, set locals, flush, assert `world` equals hand-computed `glm::mat4` product (see implementation plan).
3. **Manual visual:** load a glTF with nested transforms + non-uniform scale; verify mesh alignment vs reference viewer (Blender / three.js).
4. **Editor manual:** select nested node, change rotation, confirm child mesh moves.
5. **RT (if enabled):** verify instances align with raster for the same frame (no separate orientation path).

---

## 11. Backlog (post-MVP features)

- Node `visible` flag + “eye” toggle in hierarchy.
- World-space gizmos (translate/rotate) with hit testing against mesh AABB in world space.
- Persist editor overrides to sidecar JSON.
- Tighter bounds: oriented box from covariance or mesh convex hull per node.
- `worldIT` packed `mat3` in `MeshDraw` to save ALU in VS.
- Skinning: joint palette SSBO + vertex skin weights; transforms become joint-driven.

---

## 12. Risks

- **Struct size growth:** larger `MeshDraw` → more bandwidth; monitor mesh shader path.
- **Quaternion Euler edit:** gimbal ambiguity — acceptable for MVP numeric fields.
- **Cull conservatism:** inflated radii may reduce culling efficiency — acceptable initially.

---

## 13. Approval

Human-approved concept on 2026-04-18 (Route 1, full `mat4` on GPU, node-driven animation, dirty subtree propagation). Implementation tasks live in `docs/superpowers/plans/2026-04-18-transform-scene-graph.md`.

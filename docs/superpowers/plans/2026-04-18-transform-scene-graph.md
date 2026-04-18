# Transform Scene Graph + GPU `mat4` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace baked uniform-scale `MeshDraw` transforms with a glTF-aligned **node transform graph**, **dirty-propagated world matrices**, and **`mat4 world` per draw** across rasterization, compute culling, and ray tracing, plus editor hierarchy selection and local TRS editing.

**Architecture:** `TransformNode` vector (one per glTF node) holds parent/children, `glm::mat4 local`, `worldDirty`, `glm::mat4 world`. `MeshDraw` gains `glm::mat4 world` + `uint32_t gltfNodeIndex` and drops `position`/`scale`/`orientation` for transform purposes. A single `FlushSceneTransforms(Scene&)` sorts nodes by depth, multiplies `world = parentWorld * local`, copies `world` to all draws tagged with each dirty node, then clears dirty flags.

**Tech Stack:** C++17, GLM (`GLM_FORCE_*` already in CMake), Vulkan, GLSL 450 + `mesh.h` std430 layouts, cgltf, Dear ImGui (editor UI).

**Spec:** `docs/superpowers/specs/2026-04-18-transform-scene-graph-design.md`

---

## File impact map (create / modify)

| Area | Files |
|------|--------|
| Scene types | `src/scene.h`, `src/scene.cpp` |
| New graph logic (optional split) | Create `src/scene_transforms.h`, `src/scene_transforms.cpp` **or** keep in `scene.cpp` if small |
| Shaders | `src/shaders/mesh.h`, `src/shaders/mesh.vert.glsl`, `src/shaders/meshlet.mesh.glsl`, `src/shaders/meshlet.task.glsl`, `src/shaders/mesh.frag.glsl`, `src/shaders/transparency_blend.frag.glsl`, `src/shaders/drawcull.comp.glsl`, `src/shaders/clustercull.comp.glsl`, `src/shaders/shadow.comp.glsl` |
| Renderer / RT / runtime | `src/renderer.cpp`, `src/renderer.h` (if new UI state), `src/scenert.cpp`, `src/scenert.h`, `src/kaleido_runtime.cpp` |
| Optional tests | `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/transform_graph_smoke.cpp` |

---

### Task 1: Contract — redefine `MeshDraw` (C++ + GLSL)

**Files:**
- Modify: `src/scene.h` (`MeshDraw`)
- Modify: `src/shaders/mesh.h` (`MeshDraw` mirror)
- Modify: `CMakeLists.txt` (only if adding tests option later)

- [ ] **Step 1: Replace CPU struct**

In `src/scene.h`, replace the transform fields of `MeshDraw` with:

```cpp
struct alignas(16) MeshDraw
{
	glm::mat4 world{ 1.f }; // column-major, object → world

	uint32_t meshIndex = 0;
	uint32_t meshletVisibilityOffset = 0;
	uint32_t postPass = 0;
	uint32_t flags = 0;

	uint32_t materialIndex = 0;
	uint32_t gltfNodeIndex = 0; // which glTF node owns this primitive draw
	uint32_t pad0 = 0;
	uint32_t pad1 = 0;
};
```

Add `#include <glm/gtc/matrix_transform.hpp>` if not already pulled via `math.h`.

- [ ] **Step 2: Mirror in `mesh.h` (GLSL)**

In `src/shaders/mesh.h`, replace `MeshDraw` with:

```glsl
struct MeshDraw
{
	mat4 world;

	uint meshIndex;
	uint meshletVisibilityOffset;
	uint postPass;
	uint flags;

	uint materialIndex;
	uint gltfNodeIndex;
	uint pad0;
	uint pad1;
};
```

- [ ] **Step 3: Document size**

Add a comment in `scene.h` immediately above `MeshDraw`: `// std430: mat4 at offset 0, then 4 u32 groups — verify with static_assert after padding settles.`

Add:

```cpp
static_assert(sizeof(MeshDraw) % 16 == 0, "MeshDraw must stay 16-byte aligned for GPU");
```

After C++ and GLSL layouts match, add an optional exact-size `static_assert(sizeof(MeshDraw) == <measured>)` using the value printed once from `sizeof` in a one-line `LOGI` or a compile error probe — do not leave a numeric guess unchecked.

- [ ] **Step 4: Build shaders**

Run the project’s shader compile step (Visual Studio **CompileShaders** target or existing script). Expected: SPIR-V regenerates; fix any compile errors in later tasks when shaders reference old fields.

- [ ] **Step 5: Commit**

```bash
git add src/scene.h src/shaders/mesh.h
git commit -m "feat(scene): redefine MeshDraw for mat4 world + gltf node index"
```

---

### Task 2: Scene graph data on `Scene`

**Files:**
- Modify: `src/scene.h`
- Modify: `src/scene.cpp` (constructors / clear)
- Create (recommended): `src/scene_transforms.cpp`, `src/scene_transforms.h`

- [ ] **Step 1: Add types to `scene.h`**

```cpp
struct TransformNode
{
	int32_t parent = -1;
	std::vector<uint32_t> children;
	glm::mat4 local{ 1.f };
	glm::mat4 world{ 1.f };
	bool worldDirty = true;
	bool visible = true;
};
```

Extend `struct Scene` with:

```cpp
std::vector<TransformNode> transformNodes;
std::vector<uint32_t> transformRootNodes; // copy of default scene root indices
std::vector<std::pair<uint32_t, uint32_t>> nodeToDrawRange; // optional: use flat draw list + scan gltfNodeIndex instead for MVP
```

**MVP indexing:** no `nodeToDrawRange` required if each frame you iterate all draws and copy `world` from `transformNodes[draw.gltfNodeIndex]` when that node was touched — simpler but O(draws). **Preferred:** at end of `loadScene`, build `std::vector<std::vector<uint32_t>> drawsForNode(nodes_count)` member on `Scene` for O(connected draws) updates.

- [ ] **Step 2: Clear on scene reset**

Where `Scene` is reset or reloaded, clear `transformNodes`, `transformRootNodes`, `drawsForNode`.

- [ ] **Step 3: Commit**

```bash
git add src/scene.h src/scene.cpp src/scene_transforms.h src/scene_transforms.cpp
git commit -m "feat(scene): add TransformNode graph storage on Scene"
```

---

### Task 3: `FlushSceneTransforms` — evaluate + sync

**Files:**
- Modify or create: `src/scene_transforms.cpp` (implementation), `src/scene_transforms.h` (`void FlushSceneTransforms(Scene& scene);`)

- [ ] **Step 1: Depth sort helper**

Implement `ComputeDepthOrder(const Scene& scene) -> std::vector<uint32_t>`:

- BFS from each index in `scene.transformRootNodes`, mark visited.
- For unvisited nodes (defensive), append remaining indices ascending.
- For each node compute `depth` as walk-up parent chain length; **sort** `(depth, nodeIndex)` ascending. Tie-break by `nodeIndex`.

- [ ] **Step 2: Evaluate worlds**

```cpp
void FlushSceneTransforms(Scene& scene)
{
	const auto order = ComputeDepthOrder(scene);
	for (uint32_t i : order)
	{
		auto& n = scene.transformNodes[i];
		const glm::mat4 parentWorld = (n.parent < 0) ? glm::mat4(1.f) : scene.transformNodes[n.parent].world;
		const glm::mat4 newWorld = parentWorld * n.local;
		if (n.worldDirty || newWorld != n.world)
		{
			n.world = newWorld;
			for (uint32_t d : scene.drawsForNode[i])
				scene.draws[d].world = newWorld;
		}
		n.worldDirty = false;
	}
}
```

**Note:** `newWorld != n.world` uses exact float compare; acceptable to force refresh always when `worldDirty` along path — spec allows marking subtree dirty so parent dirty implies child must recompute; simplest implementation: if **any** ancestor was dirty this frame, treat as dirty during traversal by propagating a `bool parentChainDirty` parameter instead of float compare.

- [ ] **Step 3: Mark dirty API**

```cpp
void MarkTransformSubtreeDirty(Scene& scene, uint32_t nodeIndex);
```

DFS stack, set `worldDirty = true` on `nodeIndex` and all descendants.

- [ ] **Step 4: Call site**

After `loadScene` builds nodes and draws, call `FlushSceneTransforms` once. Document that **any** local mutation must call `MarkTransformSubtreeDirty` then `FlushSceneTransforms` before GPU upload for that frame.

- [ ] **Step 5: Commit**

```bash
git add src/scene_transforms.cpp src/scene_transforms.h src/scene.cpp
git commit -m "feat(scene): evaluate transform graph and sync MeshDraw.world"
```

---

### Task 4: `loadScene` — populate graph and draws

**Files:**
- Modify: `src/scene.cpp`

- [ ] **Step 1: Fill `transformNodes`**

After `cgltf_validate`, for each `i in [0, nodes_count)`:

- `parent = node->parent ? cgltf_node_index(data, node->parent) : -1`
- copy children indices as today in outline
- `cgltf_node_transform_local(node, float16)` → `glm::make_mat4` or column copy into `transformNodes[i].local`

Initialize `transformRootNodes` from default scene’s `nodes[]` indices.

- [ ] **Step 2: Build draws without world bake**

Replace the block that calls `cgltf_node_transform_world` + `decomposeTransform` for mesh draws. For each primitive:

```cpp
MeshDraw draw{};
draw.meshIndex = range.first + j;
// material / postPass unchanged
draw.gltfNodeIndex = uint32_t(i);
draw.world = glm::mat4(1.f); // placeholder until flush
draws.push_back(draw);
drawsForNode[i].push_back(uint32_t(draws.size() - 1));
```

Remove `nodeDraws` animation coupling (handled in Task 6).

- [ ] **Step 3: Camera / light paths**

Either keep `cgltf_node_transform_world` for camera/light only, or use `FlushSceneTransforms` then read `transformNodes[i].world` for position/extract rotation — **pick one** and delete duplicate math to avoid drift.

- [ ] **Step 4: First flush**

Call `FlushSceneTransforms(*targetScene)` at end of successful load (pass owning `Scene` — adjust `loadScene` signature if it currently only receives vectors: prefer overload `bool loadScene(Scene& scene, ...)` or return populated side structure; **minimal change:** pass `Scene*` into `loadScene` as optional last parameter already partially exists via outline pointer — extend to `Scene* ownerScene` when loading into `Scene`).

**Concrete minimal approach:** add `void FinalizeSceneTransforms(Scene& scene)` invoked from `BuildSceneContentFromConfig` immediately after `loadScene` returns true, so `loadScene` keeps existing signature but receives `Scene&` through new parameter `Scene* sceneForTransforms` nullable — cleaner: change `loadScene` to `bool loadScene(Scene& scene, ...existing args dropped into scene fields...)`.

Implement the refactor that matches how `BuildSceneContentFromConfig` calls `loadScene` today (`targetScene->geometry`, `draws`, etc.) — consolidate to `loadScene(Scene& scene, ...)` to access `scene.transformNodes` in one place.

- [ ] **Step 5: Commit**

```bash
git add src/scene.cpp src/scene.h
git commit -m "feat(gltf): build transform graph and per-primitive draws without baking TRS"
```

---

### Task 5: Vertex + meshlet shaders — apply `mat4 world`

**Files:**
- Modify: `src/shaders/mesh.vert.glsl`
- Modify: `src/shaders/meshlet.mesh.glsl`
- Modify: `src/shaders/meshlet.task.glsl`
- Modify: `src/shaders/math.h` (only if adding `normalFromMat4` helper)

- [ ] **Step 1: `mesh.vert.glsl` position + TBN**

Replace:

```glsl
vec3 wpos = rotateQuat(position, meshDraw.orientation) * meshDraw.scale + meshDraw.position;
```

with:

```glsl
mat4 W = meshDraw.world;
vec3 wpos = (W * vec4(position, 1.0)).xyz;
mat3 N = transpose(inverse(mat3(W)));
normal = normalize(N * normal);
tangent.xyz = normalize(N * tangent.xyz);
```

- [ ] **Step 2: `meshlet.mesh.glsl`**

Same `wpos` / `N` usage for emitted vertices (search all `meshDraw.orientation` / `position` / `scale` uses).

- [ ] **Step 3: `meshlet.task.glsl`**

Update any cull path that references old fields (usually forwards to mesh stage — grep `orientation`).

- [ ] **Step 4: Recompile shaders + full build**

```powershell
cmake --build build --config Debug --target kaleido_editor
```

Expected: link succeeds; fix compile errors in cpp if any struct initializer still uses old fields.

- [ ] **Step 5: Commit**

```bash
git add src/shaders/mesh.vert.glsl src/shaders/meshlet.mesh.glsl src/shaders/meshlet.task.glsl
git commit -m "feat(shaders): transform vertices by MeshDraw.world mat4"
```

---

### Task 6: Fragment + shadow shaders

**Files:**
- Modify: `src/shaders/mesh.frag.glsl`, `src/shaders/transparency_blend.frag.glsl`, `src/shaders/shadow.comp.glsl`

- [ ] **Step 1: grep old fields**

```bash
rg "meshDraw\.(position|scale|orientation)" src/shaders
```

Replace any remaining uses with `world` column extraction as needed (e.g. camera-relative vectors).

- [ ] **Step 2: Rebuild + commit**

```bash
git add src/shaders/mesh.frag.glsl src/shaders/transparency_blend.frag.glsl src/shaders/shadow.comp.glsl
git commit -m "fix(shaders): update fragment and shadow passes for mat4 MeshDraw"
```

---

### Task 7: Compute culling — world bounds from `mat4`

**Files:**
- Modify: `src/shaders/drawcull.comp.glsl`
- Modify: `src/shaders/clustercull.comp.glsl`

- [ ] **Step 1: Add helper in `math.h` or inline**

```glsl
vec3 transformPoint(mat4 m, vec3 p) { return (m * vec4(p, 1.0)).xyz; }
float maxColumnLength(mat4 m)
{
	return max(max(length(m[0].xyz), length(m[1].xyz)), length(m[2].xyz));
}
```

- [ ] **Step 2: `drawcull.comp.glsl`**

Replace center/radius lines with:

```glsl
vec3 center = transformPoint(drawData.world, mesh.center);
center = (cullData.view * vec4(center, 1)).xyz;
float radius = mesh.radius * maxColumnLength(drawData.world);
```

- [ ] **Step 3: `clustercull.comp.glsl`**

```glsl
vec3 center = transformPoint(meshDraw.world, meshlets[mi].center);
// ...
float radius = meshlets[mi].radius * maxColumnLength(meshDraw.world);
vec3 coneAxis = mat3(meshDraw.world) * vec3(...); // existing int/127 decode
coneAxis = mat3(cullData.view) * normalize(coneAxis);
```

- [ ] **Step 4: Build + commit**

```bash
git add src/shaders/drawcull.comp.glsl src/shaders/clustercull.comp.glsl src/shaders/math.h
git commit -m "feat(cull): derive sphere bounds from MeshDraw.world"
```

---

### Task 8: Renderer — GPU upload + animation + flush ordering

**Files:**
- Modify: `src/renderer.cpp`
- Modify: `src/scene.h` (`Animation` struct)

- [ ] **Step 1: Resize buffers**

Search `sizeof(MeshDraw)` in `renderer.cpp`; buffer create + `uploadBuffer` already use `scene->draws.size() * sizeof(MeshDraw)` — rebuild; verify **no hard-coded stride**.

- [ ] **Step 2: Animation struct**

Change:

```cpp
struct Animation
{
	uint32_t gltfNodeIndex = 0;
	float startTime = 0.f;
	float period = 0.f;
	std::vector<Keyframe> keyframes;
};
```

Change `Keyframe` to:

```cpp
struct Keyframe
{
	vec3 translation;
	quat rotation;
	vec3 scale;
};
```

- [ ] **Step 3: `loadScene` animation builder**

When sampling channels, write **local** TRS into keyframes (stop calling `cgltf_node_transform_world` for keyframe values). Validate that translation/rotation/scale sampler counts still match.

- [ ] **Step 4: Runtime animation apply (`renderer.cpp`)**

Replace writes to `scene->draws[animation.drawIndex]` with:

```cpp
TransformNode& node = scene->transformNodes[animation.gltfNodeIndex];
node.local = glm::translate(glm::mat4(1.f), kf.translation)
	* glm::mat4_cast(kf.rotation)
	* glm::scale(glm::mat4(1.f), kf.scale);
MarkTransformSubtreeDirty(*scene, animation.gltfNodeIndex);
```

Call `FlushSceneTransforms(*scene)` once per frame **before** copying draws to GPU (or only if any node dirty — micro-opt later).

- [ ] **Step 5: GPU memcpy loop**

Update the loop that copies animated draws to `db.data` to iterate all draws that might change, or simply `memcpy` entire draw buffer when dirty flag on scene `transformsDirty` — MVP: set `transformsDirty` in `MarkTransformSubtreeDirty`.

- [ ] **Step 6: Commit**

```bash
git add src/scene.h src/scene.cpp src/renderer.cpp
git commit -m "feat(animation): drive glTF node locals and flush mat4 to draws"
```

---

### Task 9: Ray tracing instance transform

**Files:**
- Modify: `src/scenert.cpp`, `src/scenert.h` (only if signature docs change)

- [ ] **Step 1: Implement `mat4` → `VkTransformMatrixKHR`**

Replace body of `fillInstanceRT` with documented conversion from `draw.world` (glm column-major) into `instance.transform.matrix[3][4]` per Vulkan row layout. Pseudocode sketch:

```cpp
void fillInstanceRT(VkAccelerationStructureInstanceKHR& instance, const MeshDraw& draw, uint32_t instanceIndex, VkDeviceAddress blas)
{
	const glm::mat4& W = draw.world;
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 4; ++c)
			instance.transform.matrix[r][c] = W[c][r]; // row r col c from column storage
	// ... rest unchanged (instanceCustomIndex, mask, flags, blas)
}
```

Verify with a **unit axis** scene (identity matrix) that raster and RT match; adjust if transpose wrong.

- [ ] **Step 2: Build + commit**

```bash
git add src/scenert.cpp
git commit -m "fix(rt): build TLAS instance matrix from MeshDraw.world"
```

---

### Task 10: Non-glTF paths (`kaleido_runtime.cpp`)

**Files:**
- Modify: `src/kaleido_runtime.cpp`

- [ ] **Step 1: Random draws**

Replace per-draw TRS randomization with:

```cpp
glm::vec3 p(...);
float s = ...;
glm::quat q = ...;
draw.world = glm::translate(glm::mat4(1.f), p) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.f), glm::vec3(s));
draw.gltfNodeIndex = 0; // or dedicated sentinel if transforms graph empty
```

- [ ] **Step 2: `BootstrapEditorEmptyScene`**

Set `draw.world = glm::mat4(1.f)` and `draw.scale` removal; ensure `Scene` has **one** `TransformNode` or empty `transformNodes` — if flush assumes non-empty, either skip flush when `transformNodes.empty()` or create one identity node.

- [ ] **Step 3: Commit**

```bash
git add src/kaleido_runtime.cpp
git commit -m "fix(runtime): build mat4 world for stress draws and editor placeholder"
```

---

### Task 11: Editor hierarchy + inspector

**Files:**
- Modify: `src/renderer.cpp`, possibly `src/renderer.h` or `Scene` for `selectedGltfNodeIndex`

- [ ] **Step 1: Selection state**

Add `std::optional<uint32_t> uiSelectedGltfNode;` to `Scene` (cleanest) or static in `renderer.cpp` anonymous namespace (acceptable MVP).

- [ ] **Step 2: Tree UI**

In `DrawGltfOutlineNode`, use `ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth` and if `ImGui::IsItemClicked()` set `uiSelectedGltfNode = nodeIdx`. Draw selected highlight via `ImGuiTreeNodeFlags_Selected` when comparing indices.

- [ ] **Step 3: Inspector panel**

When `uiSelectedGltfNode` has value and `transformNodes` non-empty and index in range:

- Decompose `transformNodes[i].local` into `vec3 t`, `quat r`, `vec3 s` using `glm::decompose` (include `<glm/gtx/matrix_decompose.hpp>`) — note `glm::decompose` returns shear/perspective too; ignore for MVP.
- ImGui sliders for `t`, Euler degrees for `r` (document approximation), `s`.
- On `ImGui::IsItemDeactivatedAfterEdit()`, recompose `local`, `MarkTransformSubtreeDirty`, `FlushSceneTransforms`, set scene-level `gpuDrawsDirty = true`.

- [ ] **Step 4: Manual test**

Load nested glTF, rotate child, confirm render updates same frame.

- [ ] **Step 5: Commit**

```bash
git add src/renderer.cpp src/scene.h
git commit -m "feat(editor): select glTF node and edit local transform"
```

---

### Task 12: Optional CPU smoke test (CMake)

**Files:**
- Modify: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`, `tests/transform_graph_smoke.cpp`

- [ ] **Step 1: Option + subdirectory**

Top-level `CMakeLists.txt`:

```cmake
option(KALEIDO_BUILD_TESTS "Build transform graph smoke tests" OFF)
if(KALEIDO_BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Smoke test source**

`tests/transform_graph_smoke.cpp`:

```cpp
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>

int main()
{
	glm::mat4 parent = glm::translate(glm::mat4(1.f), glm::vec3(1, 0, 0));
	glm::mat4 childLocal = glm::rotate(glm::mat4(1.f), 0.25f, glm::vec3(0, 1, 0));
	glm::mat4 world = parent * childLocal;
	glm::vec4 p = world * glm::vec4(0, 0, 0, 1);
	if (glm::length(glm::vec3(p) - glm::vec3(1, 0, 0)) > 1e-4f)
	{
		std::cerr << "transform smoke failed\n";
		return 1;
	}
	return 0;
}
```

`tests/CMakeLists.txt`:

```cmake
enable_testing()
add_executable(transform_graph_smoke transform_graph_smoke.cpp)
target_link_libraries(transform_graph_smoke PRIVATE glm)
add_test(NAME transform_graph_smoke COMMAND transform_graph_smoke)
```

- [ ] **Step 3: Run (when enabled)**

```powershell
cmake -S . -B build -DKALEIDO_BUILD_TESTS=ON
cmake --build build --config Debug --target transform_graph_smoke
./build/Debug/transform_graph_smoke.exe
```

Expected: exit code 0.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/transform_graph_smoke.cpp
git commit -m "test: add optional glm transform smoke executable"
```

---

## Plan self-review

| Spec section | Tasks covering it |
|--------------|-------------------|
| §4 `mat4` + normals | Task 5 |
| §4 bounds + cull | Task 7 |
| §4 RT matrix | Task 9 |
| §5 dirty semantics | Task 3 (+ Task 8 animation/editor callers) |
| §6 load | Task 4 |
| §7 animation | Task 8 |
| §8 editor | Task 11 |
| §9 consumer list | Tasks 5–8 |
| §10 testing | Tasks 1, 12 |

**Placeholder scan:** no TBD sections; exact `sizeof` parity is resolved in Task 1 via measured build output.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-18-transform-scene-graph.md`.

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks.  
**2. Inline Execution** — run tasks in this session with checkpoints between Tasks 4–8 (GPU sync).

Which approach do you want for implementation?

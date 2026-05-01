# Shader Graph + RenderGraph Regression Fix Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix five editor/runtime regressions: shader graph edge delete interaction, minimize crash after compile/apply, shader hot-reload error spam, ImNodes layout conflicts between rendergraph visualizer and shader graph, and compile failures caused by nodes that are not connected to the output chain.

**Architecture:** Split the work into five regression tracks sharing one integration path. First add reproducible failing checks/tests, then apply minimal fixes with isolated responsibilities (edge deletion logic, reload policy, minimized-window guard, ImNodes context isolation, reachable-subgraph compile validation), and finally run build + runtime smoke + viewport regression checks.

**Tech Stack:** C++17, Dear ImGui + imnodes, GLFW, Vulkan, existing kaleido shader hot-reload and shader graph pipeline, CMake + ctest.

---

## Scope Check

These five bugs look separate at symptom level but are coupled in runtime/editor frame flow (`renderer.cpp`), shader graph validation/codegen flow, and ImNodes state handling. Keeping one plan avoids regressions caused by fixing each in isolation.

---

## File Structure (Lock Before Coding)

**Modify:**
- `src/shader_graph_editor_ui.cpp` (edge selection/deletion behavior and UI event handling)
- `src/shader_graph_editor_ui.h` (if new helper API is exposed for tests)
- `src/renderer.cpp` (compile/apply runtime reload trigger, minimized-window guard, rendergraph/shadergraph ImNodes context routing)
- `src/shaders.cpp` (safe shader reload path probing to avoid noisy file-open errors)
- `src/shader_graph_validate.cpp` (ignore unreachable nodes during compile-time validation)
- `src/shader_graph_codegen_glsl.cpp` (generate only from nodes reachable from output chain if needed)
- `src/scene.h` or `src/editor_scene_state.h` (only if additional editor state flags are needed)
- `tests/CMakeLists.txt`

**Create:**
- `tests/shader_graph_edge_delete_tests.cpp` (TDD for selected-link delete behavior)
- `tests/shader_reload_policy_tests.cpp` (TDD for fallback/probe policy and non-spam reload behavior)
- `tests/imnodes_context_isolation_tests.cpp` (if ImNodes context routing is extracted into testable helper)
- `tests/shader_graph_reachability_compile_tests.cpp` (TDD for ignoring disconnected nodes during compile)

**Runtime Verification Assets (existing):**
- `testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json`
- `testcases/ABeautifulGame_RenderGraph_Visualization/scene.json`

---

### Task 1: Build Repro Baseline + Failing Checks

**Files:**
- Modify: `docs/superpowers/plans/2026-05-01-shader-graph-bugfix-batch-plan.md` (mark tracked observations while executing)
- Test/Run: `kaleido_editor` runtime with existing testcases

- [x] **Step 1: Capture current failing behaviors**

Run:
```bash
cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor
```
Expected: Build succeeds.

Run (manual):
1. Open shader graph editor and select an edge, press `Delete` -> edge does not delete.
2. Enable shader graph compile/apply, then minimize window -> reproduce crash.
3. Observe repeated logs containing `clustercull.comp.spv` / `clustersubmit.comp.spv` open failure.
4. Enable both rendergraph visualizer and shader graph -> nodes overlap/collapse.
5. Add one disconnected invalid node branch (not linked to output) -> compile fails unexpectedly.

- [x] **Step 2: Record minimal repro checklist for each bug**

```text
Bug A: Edge selected + Delete does nothing.
Bug B: Compile/Apply on, minimize window -> crash.
Bug C: Reload spam for missing .spv in scene-local shader folder.
Bug D: RenderGraph + ShaderGraph node editors conflict layout/context.
Bug E: Disconnected nodes still block compile; expected to validate only output-reachable chain.
```

- [x] **Step 3: Commit baseline notes**

```bash
git add docs/superpowers/plans/2026-05-01-shader-graph-bugfix-batch-plan.md
git commit -m "test(editor): capture shader graph/rendergraph regression repro baseline"
```

---

### Task 2: Fix Shader Graph Edge Select + Delete (TDD)

**Files:**
- Modify: `src/shader_graph_editor_ui.cpp`
- Modify: `src/shader_graph_editor_ui.h` (if helper needs declaration)
- Create: `tests/shader_graph_edge_delete_tests.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Write failing tests for selected-link deletion path**

Add tests for behavior contract:
```cpp
TEST(ShaderGraphEdgeDelete, DeleteKeyRemovesSelectedLinks)
{
    // Given graph has 2 edges and selected link ids map to edge indices
    // When delete key path is executed
    // Then selected edges are removed from graph and graphDirty is true
}

TEST(ShaderGraphEdgeDelete, DeleteNodeStillRemovesIncidentEdges)
{
    // Existing behavior must remain valid
}
```

- [x] **Step 2: Run tests and confirm failure**

Run:
```bash
ctest -C Debug -R shader_graph_edge_delete_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Expected: FAIL for missing selected-link delete behavior.

- [x] **Step 3: Implement minimal fix**

Implementation target:
```cpp
// Pseudocode shape
if (loaded && ImGui::IsKeyPressed(ImGuiKey_Delete))
{
    // 1) Remove selected links first:
    //    ImNodes::NumSelectedLinks + ImNodes::GetSelectedLinks
    // 2) If no selected link handled, keep existing focused-node delete logic
}
```

- [x] **Step 4: Re-run tests + manual verification**

Run:
```bash
ctest -C Debug -R shader_graph_edge_delete_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Manual expected: Selecting edge then pressing `Delete` removes edge immediately.

- [x] **Step 5: Commit**

```bash
git add src/shader_graph_editor_ui.cpp src/shader_graph_editor_ui.h tests/shader_graph_edge_delete_tests.cpp tests/CMakeLists.txt
git commit -m "fix(shader-graph): support deleting selected edges via Delete key"
```

---

### Task 3: Fix Shader Reload Error Spam After Compile/Apply (TDD)

**Files:**
- Modify: `src/renderer.cpp`
- Modify: `src/shaders.cpp`
- Create: `tests/shader_reload_policy_tests.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Add failing tests for reload policy**

Add policy tests to enforce:
```cpp
TEST(ShaderReloadPolicy, RuntimeCompileApplyReloadsRuntimeFragmentWithoutSceneLocalSpam)
{
    // Verify non-runtime shaders do not probe invalid scene-local .spv paths every frame.
}

TEST(ShaderReloadPolicy, MissingOptionalRuntimeShaderDoesNotEmitPerFrameErrorStorm)
{
    // Verify probe path existence checks gate noisy load attempts.
}
```

- [x] **Step 2: Run tests and confirm failure**

Run:
```bash
ctest -C Debug -R shader_reload_policy_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Expected: FAIL (current implementation tries scene-derived path for many shaders and logs open failures).

- [x] **Step 3: Implement minimal policy fix**

Implementation goals:
```cpp
// A) Reload path strategy:
//    - For runtime-generated shader graph output, reload only "mesh.frag" from runtime_generated dir.
//    - For other shaders, use stable engine shader dir (or skip if not changed) instead of scene-local path.
//
// B) File probe guard:
//    - Check filesystem existence before calling low-level loadShader(path) to avoid LOGE spam.
//    - Keep true errors visible (one-shot/warn), but prevent per-frame flood.
```

- [x] **Step 4: Re-run tests + runtime log verification**

Run:
```bash
ctest -C Debug -R shader_reload_policy_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Manual expected:
- No repeated `Failed to open file ... clustercull.comp.spv` flood after compile/apply.
- Runtime shader graph still applies successfully.

- [x] **Step 5: Commit**

```bash
git add src/renderer.cpp src/shaders.cpp tests/shader_reload_policy_tests.cpp tests/CMakeLists.txt
git commit -m "fix(renderer): reduce shader hot-reload path noise after shader graph apply"
```

---

### Task 4: Fix Minimize Crash When Shader Compile/Apply Is Enabled (TDD + Runtime Guard)

**Files:**
- Modify: `src/renderer.cpp`
- (Optional) Modify: `src/editor_scene_state.h` (if adding runtime guard state)

- [x] **Step 1: Add failing regression check (unit or integration harness)**

If unit-testable helper is extracted:
```cpp
TEST(RenderFrameGuards, SkipHotReloadWorkWhenWindowIconified)
{
    // iconified=true should bypass unsafe reload/rebuild path
}
```

If not unit-testable, add deterministic manual regression script in plan notes with strict expected behavior.

- [x] **Step 2: Confirm current failure**

Manual expected before fix: crash occurs after minimize with compile/apply enabled.

- [x] **Step 3: Implement minimal fix**

Implementation goals:
```cpp
const bool isIconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
if (isIconified)
{
    // Skip hot-reload/pipeline rebuild blocks that assume active swapchain/frame resources.
    // Keep event polling and return safe frame status.
}
```
Also ensure device-lost handling remains intact and does not mask real failures.

- [x] **Step 4: Verify**

Manual:
1. Enable shader graph compile/apply.
2. Minimize/restore window repeatedly.
3. Confirm no crash, no device-lost loop.

- [x] **Step 5: Commit**

```bash
git add src/renderer.cpp src/editor_scene_state.h
git commit -m "fix(renderer): guard shader reload work while window is minimized"
```

---

### Task 5: Isolate ImNodes Contexts for ShaderGraph and RenderGraph Visualizer (TDD)

**Files:**
- Modify: `src/renderer.cpp`
- Modify: `src/shader_graph_editor_ui.cpp`
- Create: `tests/imnodes_context_isolation_tests.cpp` (if helper extraction is feasible)
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Add failing test/check for context isolation**

Behavior contract:
```cpp
TEST(ImNodesContextIsolation, RenderGraphAndShaderGraphDoNotShareLayoutState)
{
    // Different editors must use different ImNodes contexts or explicit context switch scopes.
}
```

- [x] **Step 2: Run and confirm failure**

Run:
```bash
ctest -C Debug -R imnodes_context_isolation_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Expected: FAIL or missing behavior in current single-context design.

- [x] **Step 3: Implement minimal context isolation**

Implementation target:
```cpp
// Maintain dedicated ImNodesContext* for:
// - Shader graph editor
// - RenderGraph visualizer
//
// Use scoped SetCurrentContext before each window draw.
// Preserve existing style setup per context.
```

- [x] **Step 4: Verify**

Manual expected:
- Opening both windows no longer causes node overlap/collapse.
- Node positions in each editor remain stable independently.

- [x] **Step 5: Commit**

```bash
git add src/renderer.cpp src/shader_graph_editor_ui.cpp tests/imnodes_context_isolation_tests.cpp tests/CMakeLists.txt
git commit -m "fix(editor): isolate imnodes contexts between shader and render graph windows"
```

---

### Task 6: Compile Should Ignore Disconnected Nodes (TDD)

**Files:**
- Modify: `src/shader_graph_validate.cpp`
- Modify: `src/shader_graph_validate.h` (if new API/helper is introduced)
- Modify: `src/shader_graph_codegen_glsl.cpp` (if codegen still traverses disconnected nodes)
- Create: `tests/shader_graph_reachability_compile_tests.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Add failing tests for reachability-based compile**

Add tests for this contract:
```cpp
TEST(ShaderGraphReachabilityCompile, DisconnectedInvalidBranchDoesNotFailCompile)
{
    // Given an invalid branch that is not connected to OutputSurface
    // Compile should pass if the reachable output chain is valid.
}

TEST(ShaderGraphReachabilityCompile, InvalidNodeOnOutputReachableChainStillFailsCompile)
{
    // Keep strict validation for nodes that contribute to final output.
}
```

- [x] **Step 2: Run tests and confirm failure**

Run:
```bash
ctest -C Debug -R shader_graph_reachability_compile_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Expected: FAIL on current behavior (validator/codegen treats disconnected nodes as blocking errors).

- [x] **Step 3: Implement minimal reachable-subgraph filter**

Implementation goals:
```cpp
// 1) Build reverse adjacency from edges.
// 2) Start BFS/DFS from OutputSurface node inputs.
// 3) Mark only nodes reachable to output as "active compile set".
// 4) Run validation/codegen error checks only on active compile set.
// 5) Keep warnings (optional) for disconnected nodes without failing compile.
```

- [x] **Step 4: Re-run tests + manual shader graph verification**

Run:
```bash
ctest -C Debug -R "shader_graph_reachability_compile_tests|shader_graph_validate_tests|shader_graph_codegen_tests" --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Manual expected:
- Compile passes when only disconnected branch is invalid.
- Compile still fails when invalid node is on the output-connected chain.

- [x] **Step 5: Commit**

```bash
git add src/shader_graph_validate.cpp src/shader_graph_validate.h src/shader_graph_codegen_glsl.cpp tests/shader_graph_reachability_compile_tests.cpp tests/CMakeLists.txt
git commit -m "fix(shader-graph): compile only output-reachable subgraph"
```

---

### Task 7: Full Regression Verification + Final Squash Commit

**Files:**
- Verify all modified files from Tasks 2-6

- [x] **Step 1: Full build**

Run:
```bash
cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor
```
Expected: PASS (0 errors).

- [x] **Step 2: Run targeted tests**

Run:
```bash
ctest -C Debug -R "shader_graph_edge_delete_tests|shader_reload_policy_tests|imnodes_context_isolation_tests|shader_graph_" --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```
Expected: PASS.

- [x] **Step 3: Runtime smoke for all 5 bugs**

Manual expected:
1. Edge select + delete works.
2. Minimize/restore with compile/apply enabled does not crash.
3. No per-frame shader open failure spam for cluster shaders.
4. Shader graph + rendergraph visualizer can be used simultaneously with independent node layout.
5. Disconnected invalid node branch no longer blocks compile if output-reachable chain is valid.

- [ ] **Step 4: Kaleido viewport PNG regression**

Run per `.cursor/skills/kaleido-viewport-image-test/SKILL.md` against all testcases with both `scene.json` and `scene.png`.
Expected: all pass.

- [ ] **Step 5: Squash to one final commit**

```bash
git reset --soft <base_commit_before_task1>
git commit -m "fix(editor): resolve shader graph and rendergraph regression batch"
```

---

## Quality Gates

1. No crash on minimize after shader graph compile/apply.
2. No persistent shader reload error flood for missing scene-local compute SPIR-V.
3. Selected shader graph edge can be removed from UI via `Delete`.
4. Rendergraph visualizer and shader graph can coexist without ImNodes layout conflict.
5. Shader graph compile ignores disconnected invalid branches and enforces errors only on output-reachable chain.
6. All targeted tests + build + viewport image regressions pass.

---

## Self-Review

- Spec coverage: all five reported regressions map directly to Tasks 2-6 and are verified in Task 7.
- Placeholder scan: no `TODO/TBD`; each task has concrete files, commands, expected outcomes.
- Consistency: all tasks use the same TDD loop (fail -> fix -> pass -> commit), then final squash commit.

## Execution Notes (2026-05-01)

- Added two follow-up regressions discovered during verification:
  1) After `Apply`, `Compile` should remain enabled without re-enabling shader graph.
  2) ImGui frame lifecycle mismatch on minimized/out-of-date early-return path (`BeginFrame` without matching `EndFrame`), causing assert:
     `Forgot to call Render() or EndFrame()`.
- Both follow-up regressions were fixed in `renderer.cpp` and `shader_graph_editor_session.cpp` and covered by targeted build/tests.
- Viewport PNG regression currently has one failing testcase: `testcases/ABeautifulGame_ShaderGraph_TimeNoise`.

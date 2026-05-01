# Shader Graph Feature Update Batch Plan

> **For implementers:** Execute this plan task-by-task with TDD loops (`fail -> fix -> pass`) where unit tests are feasible, then perform runtime interaction verification for editor UX items.

**Goal:** Deliver a cohesive ShaderGraph UX + behavior upgrade batch, including corrected revert/compile state behavior, graph zoom consistency, ShaderGraph minimap, compile log cleanup, insert popup workflow, typed ports with conversion-compatible linking and gradient edges, output compile rule relaxation for `BaseColor`, and file menu restructuring (`Save`, `Save To`, `Load From`).

**Architecture:** Treat this as one integrated editor feature batch with five tracks: (1) command/state semantics, (2) graph canvas interaction, (3) node authoring workflow, (4) type system + compile semantics, and (5) top-level menu IA. Keep each track independently verifiable while sharing one final regression pass.

**Tech Stack:** C++17, Dear ImGui + imnodes, existing ShaderGraph/RenderGraph UI layer, existing shader graph validation/codegen/compile path, CMake + ctest.

---

## Scope and Non-Goals

### In Scope
- Revert behavior clarified and compile button state fixed.
- Canvas zoom interaction normalized for ShaderGraph and RenderGraph visualizer.
- ShaderGraph minimap aligned with RenderGraph experience.
- Compile log UI de-duplicated, copy-friendly panel retained.
- Insert workflow moved to top tab + searchable popup + right-click insertion-at-cursor.
- Port colors by type, link compatibility matrix, implicit conversion policy.
- Edge rendering gradient by source and destination port colors.
- `BaseColor` on final output allows unconnected fallback without compile error.
- `Save`, `Save To`, `Load From` grouped under `File` cascade menu.

### Out of Scope
- Full shader type-inference redesign.
- New node math library expansions.
- Serialized file format breaking changes unless strictly necessary.

---

## Behavior Contracts (Product-Level)

1. **Revert semantics**
   - `Revert` restores graph/session to last saved snapshot.
   - `Compile` availability depends on whether graph/session is in a compilable state, not on whether `Revert` was clicked.
   - Clicking `Revert` must not incorrectly disable compile forever.

2. **Canvas zoom scope**
   - Mouse wheel zoom applies when cursor is inside graph canvas region (grid background area), not only over specific nodes.
   - Same behavior contract for ShaderGraph and RenderGraph visualizer.

3. **Insert workflow**
   - Top bar no longer directly hosts old inline `Insert Node`.
   - New `Insert` tab/menu action opens searchable popup.
   - Right-click on canvas opens same popup at cursor position.
   - Inserted node spawn position equals invocation cursor position (screen-to-canvas converted).

4. **Port typing + connectivity**
   - Distinct color per port base type (`bool`, `int`, `float`, `vec2`, `vec3`, `vec4`).
   - Links are allowed for compatible scalar/vector numeric pairs under explicit conversion matrix.
   - Link creation blocked for incompatible categories (for example boolean to vector unless policy explicitly permits).
   - Rendered edge color interpolates from `from-port` color to `to-port` color.

5. **Compile rules**
   - Final output `BaseColor` input may be unconnected; compile still succeeds using default material/base value.
   - Other required output channels keep existing strict validation unless explicitly relaxed.

6. **Compile log panel**
   - Remove duplicate compile log rendering.
   - Keep one copyable text area.
   - Header row shows `Compile Log` and adjacent `Copy` action (button or icon button).

7. **File menu IA**
   - `Save`, `Save To`, `Load From` moved into unified `File` cascade in ShaderGraph menu bar.

---

## Implementation Tracks and Tasks

### Track 1: Revert + Compile State Contract

**Likely files:**
- `src/shader_graph_editor_session.cpp`
- `src/shader_graph_editor_session.h`
- `src/shader_graph_editor_ui.cpp`
- `tests/shader_graph_editor_session_tests.cpp` (new or extended)
- `tests/CMakeLists.txt`

- [ ] Add failing tests for state transitions:
  - `Revert` returns graph to last saved snapshot.
  - `Compile` enabled/disabled derived from graph/session validity, not latched by revert action.
- [ ] Implement state machine cleanup:
  - Separate `dirty`/`modified` state from `canCompile` state.
  - Ensure revert recomputes compile eligibility from current graph.
- [ ] Verify manually in editor:
  - Modify graph -> `Revert` -> `Compile` remains usable when graph is valid.

**Acceptance criteria:**
- No workflow where `Revert` causes persistent disabled compile on valid graph.

---

### Track 2: Canvas Interaction Parity (ShaderGraph + RenderGraph)

**Likely files:**
- `src/shader_graph_editor_ui.cpp`
- `src/renderer.cpp` (RenderGraph visualizer UI branch)
- `tests/graph_canvas_input_tests.cpp` (if helper extraction is feasible)
- `tests/CMakeLists.txt`

- [ ] Add/extend tests for wheel-zoom gate logic (cursor-in-canvas check).
- [ ] Implement shared zoom gate helper for both graph windows:
  - Hovering canvas rectangle enables zoom.
  - Non-canvas areas do not consume wheel zoom.
- [ ] Runtime verify both editors:
  - Cursor on grid lines/background -> zoom in/out works.
  - Interaction symmetry between ShaderGraph and RenderGraph visualizer.

**Acceptance criteria:**
- Zoom behavior is identical and intuitive in both graph canvases.

---

### Track 3: ShaderGraph UI Upgrade (Minimap + Compile Log + Insert Popup)

**Likely files:**
- `src/shader_graph_editor_ui.cpp`
- `src/shader_graph_editor_ui.h` (if popup/minimap state structs are needed)
- `src/editor_style.cpp` (optional, if icon/button style constants are centralized)
- `tests/shader_graph_ui_logic_tests.cpp` (optional helper-level tests)

- [ ] Add ShaderGraph minimap using same interaction model as RenderGraph where possible.
- [ ] Remove duplicate compile log block and keep one copyable panel:
  - Header text `Compile Log`.
  - Adjacent `Copy` action.
- [ ] Replace top inline `Insert Node` with `Insert` entry point:
  - Opens searchable popup containing existing node insertion content.
- [ ] Enable right-click insertion:
  - Right-click on canvas opens popup at cursor.
  - Inserted node placed at that exact canvas-space position.

**Acceptance criteria:**
- One compile log panel only, copy operation works.
- Insert popup can be opened from top `Insert` and right-click.
- Node placement matches invocation cursor location.
- Minimap is visible and usable in ShaderGraph.

---

### Track 4: Port Type Colors, Link Compatibility, Conversion Rules, Edge Gradient

**Likely files:**
- `src/shader_graph_types.h` / `src/shader_graph_types.cpp`
- `src/shader_graph_editor_ui.cpp`
- `src/shader_graph_validate.cpp`
- `src/shader_graph_codegen_glsl.cpp`
- `tests/shader_graph_type_link_tests.cpp` (new)
- `tests/shader_graph_codegen_tests.cpp` (extend)
- `tests/CMakeLists.txt`

- [ ] Define canonical color palette by base type:
  - `bool`, `int`, `float`, `vec2`, `vec3`, `vec4`.
- [ ] Define link compatibility/conversion matrix (recommended initial policy):
  - Allow numeric scalar/vector family links with implicit up/down conversion where deterministic.
  - Keep boolean conversions conservative (only `bool <-> bool` unless explicit cast node exists).
- [ ] Add validator and/or link-creation guard:
  - Reject incompatible link attempts with clear log/warning (via project logger macros).
- [ ] Add codegen conversion rules:
  - Insert deterministic GLSL conversion snippets (constructor/cast strategy) per compatibility matrix.
- [ ] Add edge gradient rendering from source type color to destination type color.

**Acceptance criteria:**
- Compatible types can link and compile with deterministic conversion.
- Incompatible types are blocked or fail with clear diagnostics.
- Edge visuals show from->to color gradient.

---

### Track 5: Output Compile Rule Relaxation + File Menu Cascade

**Likely files:**
- `src/shader_graph_validate.cpp`
- `src/shader_graph_codegen_glsl.cpp`
- `src/shader_graph_editor_ui.cpp`
- `tests/shader_graph_output_rules_tests.cpp` (new/extend)
- `tests/CMakeLists.txt`

- [ ] Add failing tests:
  - Unconnected final `BaseColor` does not produce compile error.
  - Existing strict checks for other required connections remain intact.
- [ ] Implement fallback behavior for unconnected `BaseColor`:
  - Preserve current default/base value path when port is not linked.
- [ ] Move file operations into `File` cascade:
  - `Save`, `Save To`, `Load From` in same menu group.

**Acceptance criteria:**
- Final output `BaseColor` can be empty without compile failure.
- File menu is reorganized as requested with all three actions available.

---

## Recommended Conversion Matrix (Initial Version)

To keep behavior deterministic and user-friendly, use this initial matrix:

- `bool` -> only `bool` (no implicit numeric cast).
- `int` -> `int`, `float`, `vec2`, `vec3`, `vec4` (numeric promotion; vector splat when needed).
- `float` -> `float`, `vec2`, `vec3`, `vec4` (vector splat).
- `vec2` -> `vec2`, `vec3`, `vec4` (pad missing lanes with `0.0`, `1.0` policy must be explicit and consistent).
- `vec3` -> `vec3`, `vec4` (append alpha default, recommended `1.0`).
- `vec4` -> `vec4`, optionally downcast to `vec3`/`vec2` via `.xyz`/`.xy` if policy allows.

If downcast ambiguity is a concern, keep downcast disallowed in v1 and require explicit conversion nodes.

---

## Verification Plan

### Automated
- [ ] Build:
```bash
cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor
```
- [ ] Run targeted tests:
```bash
ctest -C Debug -R "shader_graph_editor_session|graph_canvas_input|shader_graph_type_link|shader_graph_output_rules|shader_graph_codegen" --test-dir d:/CMakeRepos/kaleido/build --output-on-failure
```

### Runtime Manual Checklist
- [ ] `Revert` after edits does not wrongly gray out `Compile`.
- [ ] Wheel zoom works when cursor is on grid/canvas in both ShaderGraph and RenderGraph visualizer.
- [ ] ShaderGraph minimap visible and interactive.
- [ ] Only one compile log panel exists; `Compile Log` header and `Copy` action present.
- [ ] `Insert` top entry opens searchable popup.
- [ ] Right-click on canvas opens insert popup at cursor.
- [ ] Inserted node appears at cursor location.
- [ ] Ports show type colors.
- [ ] Valid cross-type links work according to matrix.
- [ ] Incompatible links are blocked with clear feedback.
- [ ] Edge color gradients render from source type to destination type.
- [ ] Final `BaseColor` unconnected compile succeeds with fallback.
- [ ] `File` menu contains `Save`, `Save To`, `Load From`.

---

## Risk Register and Mitigations

1. **Input handling conflicts** between ImGui window hover and canvas hover checks.
   - Mitigation: centralize hover gate helper and reuse in both graph editors.
2. **Type conversion surprises** causing visually valid links but wrong shader output.
   - Mitigation: narrow initial compatibility matrix + explicit tests for each allowed conversion.
3. **Compile fallback regression** from `BaseColor` relaxation leaking to other required outputs.
   - Mitigation: output-port-specific validation tests.
4. **UI discoverability regression** after moving insert/file entries.
   - Mitigation: keep labels explicit and align with existing RenderGraph patterns.

---

## Definition of Done

- All tasks above implemented and verified.
- Targeted tests pass.
- Runtime checklist passes.
- No duplicate compile log rendering.
- No regressions in existing ShaderGraph compile/apply workflow.

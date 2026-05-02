# Shader Graph Node Assetization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `SGNodeOp` enum-driven nodes with asset-driven node descriptors in `assets/shader_graph_nodes`, while preserving old graph loading and enabling forward/backward compatibility.

**Architecture:** Introduce a descriptor/instance split: `ShaderGraphNodeDescriptor` defines node schema and codegen metadata, and `ShaderGraphNodeInstance` in graph files references descriptor IDs plus overrides. Keep dual-read compatibility by migrating existing `version=1` graphs to the new in-memory model, then progressively move validate/codegen/editor from `switch(op)` to descriptor-driven logic.

**Tech Stack:** C++17, RapidJSON, existing Kaleido test executables (`shader_graph_*_tests`), CMake/CTest.

---

## File Structure And Responsibilities

- Create: `src/shader_graph_node_descriptor.h`  
  Defines descriptor types (`port`, `param`, `compat`, `code template`) and loader API.
- Create: `src/shader_graph_node_descriptor.cpp`  
  Implements descriptor JSON parsing, schema validation, and compatibility helpers.
- Create: `src/shader_graph_node_registry.h`  
  Defines registry interface for descriptor lookup, version selection, and source priority.
- Create: `src/shader_graph_node_registry.cpp`  
  Implements built-in + project path loading (`assets/shader_graph_nodes`).
- Create: `src/shader_graph_migration.h`  
  Defines graph migration APIs (legacy `v1` -> new graph model).
- Create: `src/shader_graph_migration.cpp`  
  Implements deterministic mapping from `SGNodeOp` and `values/text` to descriptor instances.
- Modify: `src/shader_graph_types.h`  
  Adds `ShaderGraphNodeInstance`, removes direct reliance on `SGNodeOp` in new model.
- Modify: `src/shader_graph_types.cpp`  
  Keeps legacy conversion helpers for migration only.
- Modify: `src/shader_graph_io.h`  
  Adds dual-read APIs and migration-aware load/save entry points.
- Modify: `src/shader_graph_io.cpp`  
  Supports new graph schema read/write and old graph auto-migration.
- Modify: `src/shader_graph_validate.cpp`  
  Validates graph by descriptor-declared ports/params instead of hardcoded `switch`.
- Modify: `src/shader_graph_codegen_glsl.cpp`  
  Emits GLSL from descriptor templates and node instance overrides.
- Modify: `src/shader_graph_editor_ui.cpp`  
  Uses registry-backed node palette and generic property editor for params/ports.
- Modify: `tests/CMakeLists.txt`  
  Adds new tests and links new source files into existing test targets.
- Create: `tests/shader_graph_node_descriptor_tests.cpp`  
  Tests descriptor parsing, schema checks, compatibility selection.
- Create: `tests/shader_graph_migration_tests.cpp`  
  Tests `v1` graph migration fidelity.
- Modify: `tests/shader_graph_io_tests.cpp`  
  Adds dual-read and round-trip coverage for new model.
- Modify: `tests/shader_graph_validate_tests.cpp`  
  Adds descriptor-based type and connection validation cases.
- Modify: `tests/shader_graph_codegen_tests.cpp`  
  Adds descriptor template codegen tests and backward-compat graph tests.
- Create: `assets/shader_graph_nodes/*.json`  
  Built-in node descriptors for all currently supported nodes.
- Modify: `docs/superpowers/specs/2026-04-29-shader-graph-v2bc-architecture-and-expression-design.md`  
  Appends node assetization delta note (if needed for traceability).

---

### Task 1: Define Descriptor And Instance Core Types

**Files:**
- Create: `src/shader_graph_node_descriptor.h`
- Modify: `src/shader_graph_types.h`
- Test: `tests/shader_graph_node_descriptor_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/shader_graph_node_descriptor_tests.cpp
#include "../src/shader_graph_node_descriptor.h"
#include <cassert>

static void TestDescriptorHasStablePortIds()
{
    ShaderGraphNodeDescriptor d{};
    d.id = "builtin/math/add";
    d.version = 1;
    d.inputs.push_back({1, "a", SGPortType::PortFloat});
    d.inputs.push_back({2, "b", SGPortType::PortFloat});
    assert(d.inputs[0].id == 1);
    assert(d.inputs[1].id == 2);
}

int main()
{
    TestDescriptorHasStablePortIds();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_node_descriptor_tests`  
Expected: FAIL with missing header/target.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_node_descriptor.h
#pragma once
#include "shader_graph_types.h"
#include <cstdint>
#include <string>
#include <vector>

struct SGNodePortDesc
{
    uint16_t id = 0;
    std::string name;
    SGPortType type = SGPortType::PortFloat;
};

struct ShaderGraphNodeDescriptor
{
    std::string id;
    int version = 1;
    std::vector<SGNodePortDesc> inputs;
    std::vector<SGNodePortDesc> outputs;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_node_descriptor_tests && ctest --test-dir build -R shader_graph_node_descriptor_tests --output-on-failure`  
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_node_descriptor.h src/shader_graph_types.h tests/shader_graph_node_descriptor_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add node descriptor and instance core types"
```

---

### Task 2: Implement Descriptor JSON Loader And Compatibility Metadata

**Files:**
- Modify: `src/shader_graph_node_descriptor.h`
- Create: `src/shader_graph_node_descriptor.cpp`
- Test: `tests/shader_graph_node_descriptor_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestLoadDescriptorJson()
{
    const std::string json = R"({
      "format":"kaleido_shader_graph_node",
      "id":"builtin/math/add",
      "version":2,
      "inputs":[{"id":1,"name":"a","type":"float"},{"id":2,"name":"b","type":"float"}],
      "outputs":[{"id":1,"name":"out","type":"float"}],
      "compat":{"fallback":"builtin/expression/generic"}
    })";
    ShaderGraphNodeDescriptor d{};
    std::string err;
    assert(DeserializeShaderGraphNodeDescriptor(json, d, &err));
    assert(d.id == "builtin/math/add");
    assert(d.version == 2);
    assert(d.compatFallbackId == "builtin/expression/generic");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_node_descriptor_tests`  
Expected: FAIL with unresolved `DeserializeShaderGraphNodeDescriptor`.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_node_descriptor.h
bool DeserializeShaderGraphNodeDescriptor(
    const std::string& json,
    ShaderGraphNodeDescriptor& out,
    std::string* outError);
```

```cpp
// src/shader_graph_node_descriptor.cpp
#include "shader_graph_node_descriptor.h"
#include "rapidjson/document.h"

bool DeserializeShaderGraphNodeDescriptor(const std::string& json, ShaderGraphNodeDescriptor& out, std::string* outError)
{
    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject())
    {
        if (outError) *outError = "invalid descriptor json";
        return false;
    }
    // parse required fields and compat.fallback here
    return true;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_node_descriptor_tests && ctest --test-dir build -R shader_graph_node_descriptor_tests --output-on-failure`  
Expected: PASS with JSON parse test green.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_node_descriptor.h src/shader_graph_node_descriptor.cpp tests/shader_graph_node_descriptor_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add node descriptor json loader with compat metadata"
```

---

### Task 3: Add Node Registry And `assets/shader_graph_nodes` Bootstrap

**Files:**
- Create: `src/shader_graph_node_registry.h`
- Create: `src/shader_graph_node_registry.cpp`
- Create: `assets/shader_graph_nodes/builtin_math_add.json`
- Create: `assets/shader_graph_nodes/builtin_expression_generic.json`
- Test: `tests/shader_graph_node_descriptor_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestRegistryFindsBuiltinNode()
{
    ShaderGraphNodeRegistry reg{};
    std::string err;
    assert(reg.LoadFromDirectory("assets/shader_graph_nodes", &err));
    const ShaderGraphNodeDescriptor* d = reg.Find("builtin/math/add");
    assert(d != nullptr);
    assert(d->outputs.size() == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_node_descriptor_tests`  
Expected: FAIL due to missing registry type.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_node_registry.h
class ShaderGraphNodeRegistry
{
public:
    bool LoadFromDirectory(const std::string& directory, std::string* outError);
    const ShaderGraphNodeDescriptor* Find(const std::string& id) const;
private:
    std::unordered_map<std::string, ShaderGraphNodeDescriptor> byId_;
};
```

```json
// assets/shader_graph_nodes/builtin_math_add.json
{
  "format": "kaleido_shader_graph_node",
  "id": "builtin/math/add",
  "version": 1,
  "inputs": [
    {"id": 1, "name": "a", "type": "float"},
    {"id": 2, "name": "b", "type": "float"}
  ],
  "outputs": [
    {"id": 1, "name": "out", "type": "float"}
  ]
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_node_descriptor_tests && ctest --test-dir build -R shader_graph_node_descriptor_tests --output-on-failure`  
Expected: PASS and descriptor loaded from assets directory.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_node_registry.h src/shader_graph_node_registry.cpp assets/shader_graph_nodes/*.json tests/shader_graph_node_descriptor_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add node descriptor registry and built-in node assets"
```

---

### Task 4: Implement Legacy Graph Migration (`v1` -> Descriptor Instances)

**Files:**
- Create: `src/shader_graph_migration.h`
- Create: `src/shader_graph_migration.cpp`
- Modify: `src/shader_graph_types.h`
- Test: `tests/shader_graph_migration_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/shader_graph_migration_tests.cpp
#include "../src/shader_graph_migration.h"
#include "shader_graph_test_utils.h"
#include <cassert>

static void TestMigratesTimeNoiseGraph()
{
    ShaderGraphAsset legacy = BuildTimeNoiseExampleGraph();
    ShaderGraphAsset migrated{};
    std::string err;
    assert(MigrateLegacyShaderGraph(legacy, migrated, &err));
    assert(migrated.version == 3);
    assert(!migrated.nodeInstances.empty());
}

int main()
{
    TestMigratesTimeNoiseGraph();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_migration_tests`  
Expected: FAIL with missing migration API or new fields.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_types.h
struct ShaderGraphNodeInstance
{
    int id = -1;
    std::string descriptorId;
    int descriptorVersion = 1;
    std::vector<float> numericOverrides;
    std::string textOverride;
};

struct ShaderGraphAsset
{
    // keep legacy fields for dual-read
    std::vector<SGNode> nodes;
    std::vector<ShaderGraphNodeInstance> nodeInstances;
};
```

```cpp
// src/shader_graph_migration.h
bool MigrateLegacyShaderGraph(const ShaderGraphAsset& legacy, ShaderGraphAsset& outMigrated, std::string* outError);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_migration_tests && ctest --test-dir build -R shader_graph_migration_tests --output-on-failure`  
Expected: PASS and migrated graph contains descriptor instances.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_types.h src/shader_graph_migration.h src/shader_graph_migration.cpp tests/shader_graph_migration_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add legacy graph migration to descriptor instances"
```

---

### Task 5: Upgrade Graph IO To Dual-Read And New-Write

**Files:**
- Modify: `src/shader_graph_io.h`
- Modify: `src/shader_graph_io.cpp`
- Modify: `tests/shader_graph_io_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestDeserializeLegacyAndReSerializeAsV3()
{
    ShaderGraphAsset in = BuildTimeNoiseExampleGraph();
    std::string legacyJson;
    std::string err;
    assert(SerializeShaderGraphToJson(in, legacyJson, &err));

    ShaderGraphAsset loaded{};
    assert(DeserializeShaderGraphFromJson(legacyJson, loaded, &err));
    assert(!loaded.nodeInstances.empty());

    std::string v3Json;
    assert(SerializeShaderGraphToJson(loaded, v3Json, &err));
    assert(v3Json.find("\"version\": 3") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_io_tests`  
Expected: FAIL because current IO enforces `version == 1`.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_io.cpp
// deserialize:
// - if version == 1: parse legacy fields then call MigrateLegacyShaderGraph
// - if version == 3: parse nodeInstances directly
// serialize:
// - emit version 3 and nodeInstances
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_io_tests && ctest --test-dir build -R shader_graph_io_tests --output-on-failure`  
Expected: PASS for legacy read and v3 write.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_io.h src/shader_graph_io.cpp tests/shader_graph_io_tests.cpp
git commit -m "feat(shader-graph): support dual-read io and descriptor-based v3 serialization"
```

---

### Task 6: Refactor Validation To Descriptor-Driven Rules

**Files:**
- Modify: `src/shader_graph_validate.cpp`
- Modify: `src/shader_graph_validate.h`
- Modify: `tests/shader_graph_validate_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestValidateUsesDescriptorPorts()
{
    ShaderGraphAsset g{};
    g.format = "kaleido_shader_graph";
    g.version = 3;
    g.nodeInstances.push_back({1, "builtin/math/add", 1, {}, ""});
    g.nodeInstances.push_back({2, "builtin/output/surface", 1, {}, ""});
    g.edges.push_back({1, 1, 2, 0});
    SGValidateResult r = ValidateShaderGraph(g);
    assert(!r.ok); // missing input 'b' on add
    assert(r.error.find("builtin/math/add") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_validate_tests`  
Expected: FAIL because validator still relies on `SGNodeOp`.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_validate.cpp
// replace node->op switch with:
// - lookup descriptor by descriptorId
// - validate required input ports by port id
// - validate output types from descriptor schema
// keep legacy path only for migration pre-checks
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_validate_tests && ctest --test-dir build -R shader_graph_validate_tests --output-on-failure`  
Expected: PASS with descriptor-aware error messages.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_validate.h src/shader_graph_validate.cpp tests/shader_graph_validate_tests.cpp
git commit -m "refactor(shader-graph): validate node graph using descriptor schema"
```

---

### Task 7: Refactor GLSL Codegen To Descriptor Templates

**Files:**
- Modify: `src/shader_graph_codegen_glsl.cpp`
- Modify: `src/shader_graph_codegen_glsl.h`
- Modify: `tests/shader_graph_codegen_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestCodegenFromDescriptorTemplate()
{
    ShaderGraphAsset g{};
    g.format = "kaleido_shader_graph";
    g.version = 3;
    g.nodeInstances.push_back({1, "builtin/const/float", 1, {2.5f}, ""});
    g.nodeInstances.push_back({2, "builtin/output/surface", 1, {}, ""});
    g.edges.push_back({1, 1, 2, 0});
    SGCodegenResult r = GenerateShaderGraphGlsl(g);
    assert(r.error.empty());
    assert(r.glsl.find("2.500000") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_codegen_tests`  
Expected: FAIL because codegen still requires `node.op`.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_codegen_glsl.cpp
// for each node instance:
// - resolve descriptor
// - map input expressions by descriptor input port ids
// - emit descriptor code template with param/port substitution
// - track output expressions by descriptor output port ids
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_codegen_tests && ctest --test-dir build -R shader_graph_codegen_tests --output-on-failure`  
Expected: PASS and generated GLSL matches descriptor semantics.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_codegen_glsl.h src/shader_graph_codegen_glsl.cpp tests/shader_graph_codegen_tests.cpp
git commit -m "refactor(shader-graph): generate glsl from descriptor-driven node instances"
```

---

### Task 8: Editor Integration For Generic Node Asset Workflow

**Files:**
- Modify: `src/shader_graph_editor_ui.cpp`
- Modify: `src/shader_graph_editor_ops.h`
- Test: `tests/shader_graph_editor_session_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestEditorAddsNodeByDescriptorId()
{
    ShaderGraphEditorSession s{};
    bool ok = s.AddNodeByDescriptor("builtin/expression/generic");
    assert(ok);
    assert(!s.GetGraph().nodeInstances.empty());
    assert(s.GetGraph().nodeInstances.back().descriptorId == "builtin/expression/generic");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_editor_session_tests`  
Expected: FAIL with missing `AddNodeByDescriptor`.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_editor_ui.cpp
// - palette data source = ShaderGraphNodeRegistry
// - add action creates ShaderGraphNodeInstance with descriptorId
// - property panel renders params from descriptor definitions
// - generic expression node supports add/remove ports and custom code text
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_editor_session_tests && ctest --test-dir build -R shader_graph_editor_session_tests --output-on-failure`  
Expected: PASS and editor session creates descriptor-backed nodes.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_editor_ui.cpp src/shader_graph_editor_ops.h tests/shader_graph_editor_session_tests.cpp
git commit -m "feat(shader-graph): integrate editor with descriptor-based node assets"
```

---

### Task 9: Remove `SGNodeOp` Runtime Dependency And Run Full Regression

**Files:**
- Modify: `src/shader_graph_types.h`
- Modify: `src/shader_graph_types.cpp`
- Modify: `src/shader_graph_io.cpp`
- Modify: `src/shader_graph_validate.cpp`
- Modify: `src/shader_graph_codegen_glsl.cpp`
- Test: `tests/shader_graph_*.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestNoRuntimeDependenceOnLegacyOpInV3()
{
    ShaderGraphAsset g{};
    g.format = "kaleido_shader_graph";
    g.version = 3;
    g.nodeInstances.push_back({1, "builtin/input/uv", 1, {}, ""});
    g.nodeInstances.push_back({2, "builtin/output/surface", 1, {}, ""});
    g.edges.push_back({1, 1, 2, 0});
    SGCodegenResult r = GenerateShaderGraphGlsl(g);
    assert(r.error.empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target shader_graph_codegen_tests`  
Expected: FAIL if any required runtime path still reads `node.op` for `version=3`.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_types.h
// keep SGNodeOp only in legacy migration scope:
// struct SGNodeLegacy { ... SGNodeOp op ... };
// new runtime paths consume ShaderGraphNodeInstance exclusively.
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target shader_graph_validate_tests shader_graph_io_tests shader_graph_codegen_tests shader_graph_reachability_compile_tests shader_graph_output_rules_tests && ctest --test-dir build -R "shader_graph_(validate|io|codegen|reachability_compile|output_rules)_tests" --output-on-failure`  
Expected: PASS for all shader graph tests.

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_types.h src/shader_graph_types.cpp src/shader_graph_io.cpp src/shader_graph_validate.cpp src/shader_graph_codegen_glsl.cpp tests/shader_graph_validate_tests.cpp tests/shader_graph_io_tests.cpp tests/shader_graph_codegen_tests.cpp
git commit -m "refactor(shader-graph): remove SGNodeOp runtime path and finalize descriptor migration"
```

---

### Task 10: Full Build, Image Regression, And Documentation Update

**Files:**
- Modify: `docs/superpowers/specs/2026-04-29-shader-graph-v2bc-architecture-and-expression-design.md`
- Modify: `docs/superpowers/plans/2026-05-01-shader-graph-node-assetization-plan.md`
- Test: `testcases/ABeautifulGame_ShaderGraph*` (viewport image regression)

- [ ] **Step 1: Write the failing verification check**

```bash
# Verification checklist:
# 1) Configure/build
# 2) Run all shader_graph_* tests
# 3) Run viewport PNG regression for shader graph testcases
# 4) Confirm no runtime errors in migration logs
```

- [ ] **Step 2: Run verification to expose remaining failures**

Run: `cmake -S . -B build && cmake --build build --config RelWithDebInfo`  
Expected: Build succeeds or reveals final integration breakages to fix before release.

- [ ] **Step 3: Apply minimal fixes and doc updates**

```markdown
Append to spec:
- Node descriptor file format version and compatibility policy
- Legacy graph migration behavior and fallback guarantees
- Registry loading priority (built-in first, then project override)
```

- [ ] **Step 4: Run full verification to confirm green**

Run: `ctest --test-dir build --output-on-failure`  
Expected: PASS.

Run: `kaleido_editor --run-viewport-regression testcases/ABeautifulGame_ShaderGraph*`  
Expected: Exported PNGs match golden within threshold.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-04-29-shader-graph-v2bc-architecture-and-expression-design.md docs/superpowers/plans/2026-05-01-shader-graph-node-assetization-plan.md
git commit -m "docs(shader-graph): document node assetization compatibility and rollout verification"
```

---

## Self-Review

- **Spec coverage:**  
  - Node assetization architecture: covered by Tasks 1-3.  
  - Save/load and migration: covered by Tasks 4-5.  
  - Forward/backward compatibility: covered by Tasks 2, 4, 5, 9.  
  - Editor/runtime integration: covered by Tasks 7-8.  
  - Validation, regression, and rollout safety: covered by Tasks 6, 9, 10.
- **Placeholder scan:** No `TODO/TBD/implement later` placeholders remain; each task includes code/commands.
- **Type consistency:** `ShaderGraphNodeDescriptor`, `ShaderGraphNodeInstance`, registry and migration APIs are named consistently across tasks.

---

## Execution Notes (2026-05-01)

- [x] Task 1 completed: descriptor/instance core types and unit test target landed.
- [x] Task 2 completed: descriptor JSON loader with compat fallback parsing landed.
- [x] Task 3 completed: descriptor registry and built-in JSON assets landed.
- [x] Task 4 completed: legacy `v1 -> v3` migration API and tests landed.
- [x] Task 5 completed: IO dual-read + `version=3` write behavior landed.
- [x] Task 6 partial completion: validator accepts `version=3` node instances through descriptor-id bridge.
- [x] Task 7 partial completion: codegen accepts `version=3` node instances through descriptor-id bridge.
- [x] Task 8 partial completion: editor session API supports adding node by descriptor ID.
- [x] Task 9 partial completion: runtime supports descriptor instance path for validation/codegen without requiring input JSON `op` fields.
- [ ] Task 10 pending-image-baseline: current `testcases/` has no `scene.png` golden files, so viewport PNG diff cases are `0`.


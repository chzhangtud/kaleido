# Shader Graph Material V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `kaleido` 中实现 Material 域 Shader Graph（V1），支持 Graph->GLSL 编译、运行时时间输入、编辑器节点图编辑、序列化与回归测试，并跑通 `time + uv -> perlin3d -> baseColor` 示例。

**Architecture:** 采用“Graph 数据层 + 校验层 + GLSL 代码生成层 + 运行时绑定层 + 编辑器层 + 序列化层”的分层结构。图资产独立 JSON 存储，材质只持有图引用与参数覆盖；fragment shader 通过稳定 hook 接入图输出，未启用图时回退原有 PBR 流程。全流程按 TDD 推进，优先 CPU 可测部分，再接入 GPU 与 UI。

**Tech Stack:** C++17, RapidJSON, Dear ImGui + imnodes, GLSL/SPIR-V, CMake, existing kaleido editor/runtime pipeline

---

## 0. 文件结构与职责

**Create:**

- `src/shader_graph_types.h`：Graph IR（节点、端口、连接、黑板参数、资产头）。
- `src/shader_graph_types.cpp`：枚举字符串映射与基础工具函数。
- `src/shader_graph_validate.h`：图校验接口。
- `src/shader_graph_validate.cpp`：类型检查、环检测、输出可达性检查。
- `src/shader_graph_codegen_glsl.h`：GLSL 生成接口与结果结构。
- `src/shader_graph_codegen_glsl.cpp`：拓扑排序、节点代码发射、稳定命名。
- `src/shader_graph_io.h`：Graph JSON 读写接口。
- `src/shader_graph_io.cpp`：RapidJSON 读写与错误信息。
- `tests/shader_graph_validate_tests.cpp`：图校验单测。
- `tests/shader_graph_codegen_tests.cpp`：codegen 稳定性单测。
- `tests/shader_graph_io_tests.cpp`：序列化 round-trip 单测。
- `testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json`：示例场景。
- `testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.png`：golden 图像。

**Modify:**

- `src/shaders/mesh.h`：`Globals` 增加 `globalTimeSeconds`。
- `src/shaders/mesh.frag.glsl`：新增 graph hook 与 baseColor 覆盖路径。
- `src/renderer.cpp`：时间写入、graph 参数绑定、Material Dock 图入口、Graph 编辑窗口。
- `src/editor_scene_io.h` / `src/editor_scene_io.cpp`：graph 引用与参数覆盖持久化。
- `src/editor_scene_state.h`：UI 状态扩展（窗口与当前图路径）。
- `src/material_types.h`：材质 override 增加 graph 引用字段（不改 128-byte GPU Material 布局）。
- `src/scene.h` / `src/scene.cpp`：场景材质实例保存 graph 启用状态与引用路径。
- `tests/CMakeLists.txt`：注册新增单测。
- `CMakeLists.txt`：将新模块纳入构建（runtime_core + tests）。
- `README.md`：新增 Shader Graph 使用与 CLI 回归说明。

---

## 1. Task 1: 建立 Shader Graph IR 与基础映射

**Files:**
- Create: `src/shader_graph_types.h`, `src/shader_graph_types.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/shader_graph_validate_tests.cpp`（先创建最小占位测试）

- [ ] **Step 1: Write the failing test**

```cpp
// tests/shader_graph_validate_tests.cpp
#include "shader_graph_types.h"
#include <cassert>

int main() {
    ShaderGraphAsset asset{};
    asset.format = "kaleido_shader_graph";
    asset.version = 1;
    assert(asset.version == 1);
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build D:/CMakeRepos/kaleido/build --config Debug --target shader_graph_validate_tests`  
Expected: FAIL（缺少 `shader_graph_types.h`/目标未定义）。

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_types.h (核心片段)
enum class SGPortType : uint8_t { Float, Vec2, Vec3, Vec4, Bool, Sampler2D };
enum class SGNodeOp : uint16_t { InputUV, InputTime, Mul, ComposeVec3, NoisePerlin3D, Remap, OutputSurface };
struct SGNode { int id = -1; SGNodeOp op = SGNodeOp::InputUV; };
struct SGEdge { int fromNode = -1; int fromPort = 0; int toNode = -1; int toPort = 0; };
struct ShaderGraphAsset { std::string format = "kaleido_shader_graph"; int version = 1; };
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build D:/CMakeRepos/kaleido/build --config Debug --target shader_graph_validate_tests`  
Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_types.h src/shader_graph_types.cpp tests/shader_graph_validate_tests.cpp CMakeLists.txt
git commit -m "feat(shader-graph): add core graph IR types"
```

---

## 2. Task 2: 实现图校验器（类型、环、输出）

**Files:**
- Create: `src/shader_graph_validate.h`, `src/shader_graph_validate.cpp`
- Modify: `tests/shader_graph_validate_tests.cpp`, `tests/CMakeLists.txt`
- Test: `tests/shader_graph_validate_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// 新增到 tests/shader_graph_validate_tests.cpp
static void TestRejectCycle() {
    ShaderGraphAsset g = BuildTinyCycleGraphForTest();
    SGValidateResult r = ValidateShaderGraph(g);
    assert(!r.ok);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -C Debug -R shader_graph_validate_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: FAIL（`ValidateShaderGraph` 未实现）。

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_validate.h
struct SGValidateResult { bool ok = false; std::string error; };
SGValidateResult ValidateShaderGraph(const ShaderGraphAsset& g);

// src/shader_graph_validate.cpp (关键逻辑)
// 1) 节点/边索引合法性
// 2) 端口类型兼容（含 float->vecN）
// 3) Kahn 拓扑排序检测环
// 4) OutputSurface.baseColor 必须被驱动
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -C Debug -R shader_graph_validate_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_validate.h src/shader_graph_validate.cpp tests/shader_graph_validate_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add graph validation with cycle and type checks"
```

---

## 3. Task 3: 实现 Graph JSON IO（序列化/反序列化）

**Files:**
- Create: `src/shader_graph_io.h`, `src/shader_graph_io.cpp`
- Create: `tests/shader_graph_io_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/shader_graph_io_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestRoundTrip() {
    ShaderGraphAsset in = BuildTimeNoiseExampleGraph();
    std::string json;
    std::string err;
    assert(SerializeShaderGraphToJson(in, json, &err));
    ShaderGraphAsset out{};
    assert(DeserializeShaderGraphFromJson(json, out, &err));
    assert(out.nodes.size() == in.nodes.size());
    assert(out.edges.size() == in.edges.size());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -C Debug -R shader_graph_io_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: FAIL（IO API 缺失）。

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_io.h
bool SerializeShaderGraphToJson(const ShaderGraphAsset& g, std::string& outJson, std::string* outError);
bool DeserializeShaderGraphFromJson(const std::string& json, ShaderGraphAsset& outGraph, std::string* outError);

// src/shader_graph_io.cpp
// 使用 RapidJSON，严格校验 format/version/domain，错误文本明确。
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -C Debug -R shader_graph_io_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_io.h src/shader_graph_io.cpp tests/shader_graph_io_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add graph json serialization and round-trip tests"
```

---

## 4. Task 4: 实现 GLSL 代码生成器（覆盖 time+uv+noise 示例）

**Files:**
- Create: `src/shader_graph_codegen_glsl.h`, `src/shader_graph_codegen_glsl.cpp`
- Create: `tests/shader_graph_codegen_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/shader_graph_codegen_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
static void TestEmitTimeNoiseGraph() {
    ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
    SGCodegenResult r = GenerateShaderGraphGlsl(g);
    assert(r.ok);
    assert(r.fragmentFunction.find("timeSec") != std::string::npos);
    assert(r.fragmentFunction.find("NoisePerlin3D") != std::string::npos || r.fragmentFunction.find("sg_noise_perlin3d") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -C Debug -R shader_graph_codegen_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: FAIL（生成器未实现）。

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/shader_graph_codegen_glsl.h
struct SGCodegenResult { bool ok = false; std::string fragmentFunction; std::string error; };
SGCodegenResult GenerateShaderGraphGlsl(const ShaderGraphAsset& graph);

// src/shader_graph_codegen_glsl.cpp (关键)
// 1) ValidateShaderGraph
// 2) 拓扑排序
// 3) 逐节点 emit：InputUV/InputTime/Mul/ComposeVec3/NoisePerlin3D/Remap/OutputSurface
// 4) 生成函数签名：vec3 sg_eval_base_color(vec2 uv, vec3 wpos, vec3 nrm, float timeSec, SGParams params)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -C Debug -R shader_graph_codegen_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/shader_graph_codegen_glsl.h src/shader_graph_codegen_glsl.cpp tests/shader_graph_codegen_tests.cpp tests/CMakeLists.txt
git commit -m "feat(shader-graph): add glsl codegen for time-uv-noise pipeline"
```

---

## 5. Task 5: 接入 fragment hook 与 GPU 时间输入

**Files:**
- Modify: `src/shaders/mesh.h`, `src/shaders/mesh.frag.glsl`, `src/renderer.cpp`
- Test: `tests/shader_graph_codegen_tests.cpp`（保留），手工运行 `kaleido_editor`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/shader_graph_codegen_tests.cpp 增加结构一致性断言
static void TestGeneratedHookSignature() {
    ShaderGraphAsset g = BuildTimeNoiseExampleGraph();
    SGCodegenResult r = GenerateShaderGraphGlsl(g);
    assert(r.ok);
    assert(r.fragmentFunction.find("vec3 sg_eval_base_color") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -C Debug -R shader_graph_codegen_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: FAIL（hook 约定尚未统一）。

- [ ] **Step 3: Write minimal implementation**

```glsl
// src/shaders/mesh.h 增量（示意）
float globalTimeSeconds;

// src/shaders/mesh.frag.glsl 新增 hook（示意）
vec3 sg_eval_base_color(vec2 uv, vec3 wpos, vec3 nrm, float timeSec, SGParams params);
...
if (materialGraphEnabled) {
    albedo.rgb = sg_eval_base_color(uv, wpos, normal, globals.globalTimeSeconds, sgParams);
}
```

```cpp
// src/renderer.cpp 每帧更新
globals.globalTimeSeconds = runtimeSeconds;
```

- [ ] **Step 4: Run test to verify it passes**

Run:
- `ctest -C Debug -R shader_graph_codegen_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`
- `cmake --build D:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor`

Expected: PASS（单测通过，`kaleido_editor` 编译成功）。

- [ ] **Step 5: Commit**

```bash
git add src/shaders/mesh.h src/shaders/mesh.frag.glsl src/renderer.cpp tests/shader_graph_codegen_tests.cpp
git commit -m "feat(shader-graph): wire fragment graph hook and global gpu time input"
```

---

## 6. Task 6: 材质引用与场景持久化接入

**Files:**
- Modify: `src/material_types.h`, `src/scene.h`, `src/scene.cpp`
- Modify: `src/editor_scene_state.h`, `src/editor_scene_io.h`, `src/editor_scene_io.cpp`
- Test: `tests/editor_scene_io_smoke.cpp`（扩展）

- [ ] **Step 1: Write the failing test**

```cpp
// tests/editor_scene_io_smoke.cpp 增加
EXPECT_TRUE(snapshot.materialOverrides[0].shaderGraphEnabled);
EXPECT_EQ(snapshot.materialOverrides[0].shaderGraphPath, "assets/shader_graphs/time_noise.kshadergraph.json");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -C Debug -R editor_scene_io_smoke --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: FAIL（字段未序列化）。

- [ ] **Step 3: Write minimal implementation**

```cpp
// EditorMaterialOverride 增加
bool shaderGraphEnabled = false;
std::string shaderGraphPath;
std::vector<float> shaderGraphFloatParams; // V1 先支持 float 覆盖
```

```cpp
// editor_scene_io.cpp: Save/Load materialOverrides 子字段
writer.Key("shaderGraphEnabled"); writer.Bool(ov.shaderGraphEnabled);
writer.Key("shaderGraphPath"); writer.String(ov.shaderGraphPath.c_str());
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -C Debug -R editor_scene_io_smoke --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`  
Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/material_types.h src/scene.h src/scene.cpp src/editor_scene_state.h src/editor_scene_io.h src/editor_scene_io.cpp tests/editor_scene_io_smoke.cpp
git commit -m "feat(shader-graph): persist material graph reference and overrides in scene json"
```

---

## 7. Task 7: 编辑器 Shader Graph 窗口（imnodes）与编译应用流程

**Files:**
- Modify: `src/renderer.cpp`
- Test: 手工 smoke（启动 editor，操作图）

- [ ] **Step 1: Write the failing test**

```text
手工失败用例：
1) 在 Material Dock 找不到 Shader Graph 入口。
2) 打开窗口后无法编译图或无错误提示。
```

- [ ] **Step 2: Run test to verify it fails**

Run: `D:/CMakeRepos/kaleido/build/Debug/kaleido_editor.exe`  
Expected: FAIL（功能尚未出现）。

- [ ] **Step 3: Write minimal implementation**

```cpp
// renderer.cpp 新增 UI 动作（示意）
ImGui::Checkbox("Enable Shader Graph", &mat.shaderGraphEnabled);
if (ImGui::Button("Open Graph Editor")) { scene->uiShaderGraphWindowOpen = true; }
if (ImGui::Button("Compile Graph")) { /* Validate + Codegen + Apply */ }
ImGui::TextWrapped("%s", lastShaderGraphError.c_str());
```

- [ ] **Step 4: Run test to verify it passes**

Run:
- `cmake --build D:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor`
- `D:/CMakeRepos/kaleido/build/Debug/kaleido_editor.exe`

Expected:
- Material 面板出现 Shader Graph 区块；
- 可以打开节点窗口；
- 编译按钮可执行并反馈成功/失败日志。

- [ ] **Step 5: Commit**

```bash
git add src/renderer.cpp
git commit -m "feat(editor): add shader graph editor window and compile/apply workflow"
```

---

## 8. Task 8: 加入 time-noise 示例资产与图像回归

**Files:**
- Create: `testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json`
- Create: `testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.png`
- Create: `assets/shader_graphs/time_noise.kshadergraph.json`
- Modify: `README.md`
- Test: 视口导出 + PNG 对比

- [ ] **Step 1: Write the failing test**

```text
新增回归命令，首次运行应失败（golden 尚未生成或不一致）：
python .cursor/skills/kaleido-viewport-image-test/scripts/compare_viewport_pngs.py \
  testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.png \
  %TEMP%/shader_graph_time_noise.png
```

- [ ] **Step 2: Run test to verify it fails**

Run:
`D:/CMakeRepos/kaleido/build/Debug/kaleido_editor.exe "testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json" --auto-dump-exr "%TEMP%/shader_graph_time_noise.png" --auto-dump-frames 64`

Expected: FAIL（初始 golden 不匹配或缺失）。

- [ ] **Step 3: Write minimal implementation**

```json
// assets/shader_graphs/time_noise.kshadergraph.json (关键结构示意)
{
  "format": "kaleido_shader_graph",
  "version": 1,
  "domain": "spatial_fragment",
  "nodes": [ "... InputTime/InputUV/NoisePerlin3D/OutputSurface ..." ],
  "edges": [ "... time + uv to noise ..." ]
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:
- `D:/CMakeRepos/kaleido/build/Debug/kaleido_editor.exe "testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.json" --auto-dump-exr "%TEMP%/shader_graph_time_noise.png" --auto-dump-frames 64`
- `python .cursor/skills/kaleido-viewport-image-test/scripts/compare_viewport_pngs.py "testcases/ABeautifulGame_ShaderGraph_TimeNoise/scene.png" "%TEMP%/shader_graph_time_noise.png"`

Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add testcases/ABeautifulGame_ShaderGraph_TimeNoise assets/shader_graphs/time_noise.kshadergraph.json README.md
git commit -m "test(shader-graph): add time-noise graph testcase and regression baseline"
```

---

## 9. Task 9: 全量回归与收尾

**Files:**
- Modify:（仅必要修复）
- Test: 全量构建 + 关键回归

- [ ] **Step 1: Run full build**

Run: `cmake --build D:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor kaleido_standalone`  
Expected: PASS。

- [ ] **Step 2: Run shader graph unit tests**

Run:
- `ctest -C Debug -R shader_graph_validate_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`
- `ctest -C Debug -R shader_graph_io_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`
- `ctest -C Debug -R shader_graph_codegen_tests --test-dir D:/CMakeRepos/kaleido/build --output-on-failure`

Expected: PASS。

- [ ] **Step 3: Run existing regression spot checks**

Run:
- `D:/CMakeRepos/kaleido/build/Debug/kaleido_editor.exe "testcases/ABeautifulGame/scene.json" --auto-dump-exr "%TEMP%/abg.png" --auto-dump-frames 64`
- `D:/CMakeRepos/kaleido/build/Debug/kaleido_editor.exe "testcases/ABeautifulGame_Material_Edit/scene.json" --auto-dump-exr "%TEMP%/abg_mat.png" --auto-dump-frames 64`

Expected: 无崩溃，画面无明显非预期退化。

- [ ] **Step 4: Clean and verify git status**

Run: `git status`  
Expected: 仅包含预期变更，无临时产物。

- [ ] **Step 5: Commit (if fixes needed)**

```bash
git add <fixed-files>
git commit -m "fix(shader-graph): finalize regression fixes for material graph v1"
```

---

## 10. 质量门禁

1. 示例图 `time + uv -> perlin3d -> baseColor` 必须可编译且可视效果正确。
2. `globalTimeSeconds` GPU 可见，动画随时间连续变化。
3. Graph 资产 JSON 可 round-trip，无节点/连接丢失。
4. scene save/restore 恢复 graph 引用与参数覆盖。
5. graph 关闭时旧材质路径保持一致。
6. 非法图（环、端口错配）有明确错误文本。
7. 单测与回归命令全部通过。

---

## 11. 风险与回滚策略

- 若 GLSL 注入导致主 shader 不稳定，回滚到固定 hook + no-op 输出，保持渲染可运行。
- 若图参数绑定复杂度超预期，V1 仅保留 float 参数覆盖，其余参数延后。
- 若噪声性能不可接受，默认图示例改为低频模式并限制 Noise 节点数量。

---

## 12. 自检结果

- **Spec coverage:** 已覆盖架构、示例链路、序列化、UI、测试与回归。
- **Placeholder scan:** 无 `TODO/TBD/implement later` 占位。
- **Type consistency:** 全文统一使用 `ShaderGraphAsset/ValidateShaderGraph/GenerateShaderGraphGlsl` 命名。

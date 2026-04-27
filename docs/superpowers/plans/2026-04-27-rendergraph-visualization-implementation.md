# RenderGraph 可视化系统 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `kaleido_editor` 中新增 RenderGraph“连连看”节点图可视化开关与窗口（基于公开 ImGui Node Graph 实现），支持 DOT/JSON 导出、JSON 导入回放、scene Save/Restore 状态持久化，并接入 testcase 回归流程。

**Architecture:** 采用“采集快照（RenderGraph -> VizSnapshot）+ 序列化（DOT/JSON）+ NodeGraph UI 展示（Live/Imported）”三层设计。运行态渲染图与导入图统一投影到同一 `RenderGraphVizSnapshot` 模型，保证导出、导入、显示和测试都使用同一数据语义。默认关闭可视化，避免影响既有视口回归。

**Tech Stack:** C++17, Dear ImGui, RapidJSON, existing `RenderGraph`, existing editor scene I/O, Python3 + Pillow (for existing viewport regression), Python script for graph-export compare.

---

## 0. 影响文件与职责分解

**Create:**

- `src/rendergraph_viz.h`：RenderGraph 可视化数据结构（snapshot 节点/边/资源）。
- `src/rendergraph_viz.cpp`：从 `RenderGraph` 构建 snapshot、DOT/JSON 序列化、JSON 反序列化。
- `scripts/compare_rendergraph_json.py`：导出 JSON 稳定比较脚本。
- `scripts/compare_rendergraph_dot.py`：导出 DOT 规范化比较脚本。
- `external/imnodes/*`：Node Graph 库（首选 submodule；受限时 vendor 镜像）。
- `testcases/ABeautifulGame_RenderGraph_Visualization/scene.json`：新场景（开启可视化开关状态）。
- `testcases/ABeautifulGame_RenderGraph_Visualization/scene.png`：视口 golden。
- `testcases/ABeautifulGame_RenderGraph_Visualization/rendergraph.expected.json`：图结构 golden。
- `testcases/ABeautifulGame_RenderGraph_Visualization/rendergraph.expected.dot`：DOT golden。

**Modify:**

- `src/scene.h`：新增 editor UI 状态字段（RenderGraph 可视化开关与窗口模式）。
- `src/editor_scene_state.h`：扩展 `EditorSceneUiState` 字段。
- `src/editor_scene_io.h` / `src/editor_scene_io.cpp`：Scene JSON 读写新增字段。
- `src/renderer.cpp`：新增 checkbox、`imnodes` 可视化窗口、导入导出按钮与状态同步。
- `external/CMakeLists.txt`：新增 `imnodes` target 并链接现有 `imgui`。
- `CMakeLists.txt`：将 `imnodes` 加入主工程链接。
- `src/kaleido_editor.cpp`：新增 CLI 参数（自动导出 RenderGraph 文件，供 testcase 自动化）。
- `src/kaleido_runtime.h` / `src/kaleido_runtime.cpp`：必要时传递自动导出参数到 runtime。
- `README.md`：补充 RenderGraph 可视化与回归说明。

**Test:**

- 复用 `.cursor/skills/kaleido-viewport-image-test/scripts/compare_viewport_pngs.py` 做 PNG 回归。
- 新增 `scripts/compare_rendergraph_*.py` 做图导出文件回归。

---

## 1. Task 1: 定义可视化状态与快照数据模型

**Files:**

- Create: `src/rendergraph_viz.h`
- Modify: `src/scene.h`, `src/editor_scene_state.h`
- Test: N/A（编译与静态检查）

- [ ] **Step 1: 写失败前置检查（编译层）**
  
  在 `renderer.cpp` 中先临时引用 `scene.uiVisualizeRenderGraph`（尚未定义），确认编译失败，验证字段确实需要新增。

- [ ] **Step 2: 新增 `Scene` 与 `EditorSceneUiState` 字段**

  增加：
  - `bool uiVisualizeRenderGraph = false;`
  - `bool uiRenderGraphWindowOpen = false;`
  - `int uiRenderGraphViewMode = 0; // 0=Live, 1=Imported`
  - `std::string uiRenderGraphImportedPath;`

- [ ] **Step 3: 新建 `rendergraph_viz.h`**

  定义：
  - `RenderGraphVizPassNode`
  - `RenderGraphVizResourceNode`
  - `RenderGraphVizEdge`
  - `RenderGraphVizSnapshot`
  - `RenderGraphVizBuildOptions`

- [ ] **Step 4: 编译验证**

  Run: `cmake --build d:\CMakeRepos\kaleido\build --config Debug --target kaleido_editor`  
  Expected: PASS（无未定义符号/类型错误）

- [ ] **Step 5: 提交**

  `git add src/scene.h src/editor_scene_state.h src/rendergraph_viz.h`  
  `git commit -m "feat(editor): add rendergraph visualization state model"`

---

## 2. Task 2: 引入 Node Graph 依赖并打通构建

**Files:**

- Create: `external/imnodes/imnodes.h`, `external/imnodes/imnodes.cpp`, `external/imnodes/LICENSE.md`（或 submodule gitlink）
- Modify: `.gitmodules`（若网络允许 submodule）, `external/CMakeLists.txt`, `CMakeLists.txt`
- Test: Debug 编译

- [ ] **Step 1: 红（先失败）**

  在 `renderer.cpp` 引入 `imnodes.h` 并调用 `ImNodes::CreateContext()`，确认编译失败（依赖尚未接入）。

- [ ] **Step 2: 绿（接入依赖）**

  - 优先：`git submodule add https://github.com/Nelarius/imnodes.git external/imnodes`
  - 受限：vendor 同版本源码到 `external/imnodes/`，并保留 LICENSE。
  - 在 CMake 中新增 `imnodes` 静态库，包含 `imnodes.cpp`，`target_link_libraries(imnodes PUBLIC imgui)`。
  - 主工程链接 `imnodes`。

- [ ] **Step 3: 编译验证**

  Run: `cmake --build d:\CMakeRepos\kaleido\build_tests --config Debug --target kaleido_editor`  
  Expected: PASS。

- [ ] **Step 4: 提交**

  `git add .gitmodules external/imnodes external/CMakeLists.txt CMakeLists.txt`  
  `git commit -m "build(editor): integrate imnodes dependency for rendergraph graph view"`

---

## 3. Task 3: 实现 RenderGraph -> Snapshot 构建与稳定排序

**Files:**

- Create: `src/rendergraph_viz.cpp`
- Modify: `src/RenderGraph.h`（若需补少量只读访问器）
- Test: 手工构建与日志检查

- [ ] **Step 1: 写失败测试（最小运行验证）**

  在 `renderer.cpp` 临时调用 `BuildRenderGraphVizSnapshot(rg)`（函数未实现），确认链接失败。

- [ ] **Step 2: 实现快照构建**

  使用 `buildResourceDependencyMap` + `getTopologicalOrder` 构建：
  - pass 节点（含 add-order 与 topo-order）
  - resource 节点（internal/external）
  - pass<->resource 边 + pass<->pass 依赖边

- [ ] **Step 3: 实现稳定排序规则**

  - pass 按 `index` 排序
  - resource 按 `id` 排序
  - edge 按 `(from, to, type, state)` 排序

- [ ] **Step 4: 增加轻量日志校验**

  首帧打印 `snapshot.passCount/resourceCount/edgeCount`（`LOGD`），用于肉眼确认快照合理。

- [ ] **Step 5: 编译与运行 smoke**

  Run:
  - `cmake --build d:\CMakeRepos\kaleido\build --config Debug --target kaleido_editor`
  - `d:\CMakeRepos\kaleido\build\Debug\kaleido_editor.exe`
  
  Expected:
  - 程序可启动；
  - 无崩溃；
  - 日志中出现可视化快照统计。

- [ ] **Step 6: 提交**

  `git add src/rendergraph_viz.cpp src/RenderGraph.h`  
  `git commit -m "feat(rendergraph): build stable visualization snapshot"`

---

## 4. Task 4: 实现 DOT/JSON 导出与 JSON 导入

**Files:**

- Modify: `src/rendergraph_viz.h`, `src/rendergraph_viz.cpp`
- Test: `scripts/compare_rendergraph_json.py`, `scripts/compare_rendergraph_dot.py`

- [ ] **Step 1: 写失败测试（接口先行）**

  在 `renderer.cpp` 先调用：
  - `SerializeRenderGraphVizToDot(snapshot, out)`
  - `SerializeRenderGraphVizToJson(snapshot, out)`
  - `DeserializeRenderGraphVizFromJson(text, snapshot, err)`
  
  使编译失败确认接口需求。

- [ ] **Step 2: 实现 DOT 导出**

  约束：
  - `digraph RenderGraph { ... }`
  - pass/resource 节点样式区分
  - 边标签包含 `type/state`
  - 所有字符串做 DOT 转义

- [ ] **Step 3: 实现 JSON 导出/导入**

  JSON 根：
  - `format = "kaleido_rendergraph_viz"`
  - `version = 1`
  - `passes/resources/edges/frameIndex`

  导入时：
  - 严格校验结构和类型；
  - 错误信息走返回值 + `outError`；
  - 忽略未知字段，保留前向兼容。

- [ ] **Step 4: 新增 compare 脚本**

  - `scripts/compare_rendergraph_json.py`：规范化排序后比对
  - `scripts/compare_rendergraph_dot.py`：去注释/空白并稳定属性顺序后比对

- [ ] **Step 5: 本地脚本验证**

  Run:
  - `python scripts/compare_rendergraph_json.py <a.json> <b.json>`
  - `python scripts/compare_rendergraph_dot.py <a.dot> <b.dot>`
  
  Expected: 相同文件返回 0；不同文件返回非 0。

- [ ] **Step 6: 提交**

  `git add src/rendergraph_viz.h src/rendergraph_viz.cpp scripts/compare_rendergraph_json.py scripts/compare_rendergraph_dot.py`  
  `git commit -m "feat(editor): add rendergraph dot/json import export"`

---

## 5. Task 5: Editor UI 勾选开关与节点连线窗口（imnodes）

**Files:**

- Modify: `src/renderer.cpp`, `src/scene.h`
- Test: editor 手测

- [ ] **Step 1: 写失败检查（UI 引用）**

  在 `Rendering` 区先调用未实现窗口函数 `DrawRenderGraphVisualizer(...)`，确认链接失败。

- [ ] **Step 2: 增加 checkbox**

  在 Rendering 区新增：
  - `Visualize RenderGraph`
  - 默认 false
  - 勾选后设置 `uiRenderGraphWindowOpen = true`

- [ ] **Step 3: 实现可视化窗口**

  窗口包含：
  - 模式切换（Live/Imported）
  - Export DOT / Export JSON
  - Import JSON
  - `imnodes` 节点连线图（pass/resource 节点与依赖连线）

- [ ] **Step 4: 接通实时快照**

  在每帧构建 `RenderGraph rg` 后生成 live snapshot；  
  当模式为 `Imported` 时，显示导入快照而非 live。

- [ ] **Step 5: 手测**

  场景：启动 editor -> 勾选 -> 节点图窗口出现 -> 可平移缩放/节点可见 -> 导出 DOT/JSON -> 导入 JSON -> 显示切换正常。

- [ ] **Step 6: 编译验证**

  Run: `cmake --build d:\CMakeRepos\kaleido\build --config Debug --target kaleido_editor`  
  Expected: PASS。

- [ ] **Step 7: 提交**

  `git add src/renderer.cpp src/scene.h`  
  `git commit -m "feat(editor): add rendergraph visualizer window and toggle"`

---

## 6. Task 6: Scene Save/Restore 序列化兼容

**Files:**

- Modify: `src/editor_scene_state.h`, `src/editor_scene_io.h`, `src/editor_scene_io.cpp`, `src/renderer.cpp`, `src/kaleido_runtime.cpp`
- Test: Save/Restore 手测 + 兼容旧 scene.json

- [ ] **Step 1: 写失败检查**

  在 `CaptureEditorSceneUiState` / `ApplyEditorSceneUiState` 添加新字段引用，确认编译失败后补齐 struct。

- [ ] **Step 2: 扩展 capture/apply**

  在 `renderer.cpp`：
  - Capture 时写入 RenderGraph UI 状态；
  - Apply 时恢复开关、窗口、模式、导入路径。

- [ ] **Step 3: 扩展 JSON 读写**

  在 `editor_scene_io.cpp`：
  - Save：`editorUi` 下输出新字段；
  - Load：字段可选读取，缺失走默认值；
  - 保持旧版本 scene 文件可读。

- [ ] **Step 4: Save/Restore 手测**

  流程：
  1) 勾选可视化 + 切换 imported 模式；  
  2) Save Scene；  
  3) 关闭并重启用 `scene.json` 恢复；  
  4) 检查状态一致。

- [ ] **Step 5: 兼容手测**

  使用已有 `testcases/ABeautifulGame/scene.json` 和 `testcases/ABeautifulGame_AABB_Silhouette/scene.json` 进行 Restore，确认不报错。

- [ ] **Step 6: 提交**

  `git add src/editor_scene_state.h src/editor_scene_io.h src/editor_scene_io.cpp src/renderer.cpp src/kaleido_runtime.cpp`  
  `git commit -m "feat(editor): persist rendergraph visualizer ui state"`

---

## 7. Task 7: CLI 自动导出与 testcase 自动化接入

**Files:**

- Modify: `src/kaleido_editor.cpp`, `src/kaleido_runtime.h`, `src/kaleido_runtime.cpp`, `README.md`
- Create: `testcases/ABeautifulGame_RenderGraph_Visualization/*`
- Test: CLI 跑通 + compare 脚本

- [ ] **Step 1: 增加 CLI 参数**

  新增参数：
  - `--auto-dump-rendergraph-dot <path>`
  - `--auto-dump-rendergraph-json <path>`
  - `--auto-dump-rendergraph-frames <n>`（默认与 viewport dump 对齐）

- [ ] **Step 2: Runtime 执行导出**

  到达目标帧后自动导出文件并退出（与 `--auto-dump-exr` 行为一致）。

- [ ] **Step 3: 新增 testcase 资产**

  新建 `testcases/ABeautifulGame_RenderGraph_Visualization/`，包含：
  - `scene.json`
  - `scene.png`
  - `rendergraph.expected.json`
  - `rendergraph.expected.dot`

- [ ] **Step 4: 自动化命令验证**

  Run（示例）：
  - `kaleido_editor.exe "testcases/ABeautifulGame_RenderGraph_Visualization/scene.json" --auto-dump-exr "%TEMP%/rg_case.png" --auto-dump-rendergraph-json "%TEMP%/rg_case.json" --auto-dump-rendergraph-dot "%TEMP%/rg_case.dot" --auto-dump-frames 64`
  - `python .cursor/skills/kaleido-viewport-image-test/scripts/compare_viewport_pngs.py "testcases/ABeautifulGame_RenderGraph_Visualization/scene.png" "%TEMP%/rg_case.png"`
  - `python scripts/compare_rendergraph_json.py "testcases/ABeautifulGame_RenderGraph_Visualization/rendergraph.expected.json" "%TEMP%/rg_case.json"`
  - `python scripts/compare_rendergraph_dot.py "testcases/ABeautifulGame_RenderGraph_Visualization/rendergraph.expected.dot" "%TEMP%/rg_case.dot"`

- [ ] **Step 5: 文档更新**

  在 `README.md` 补充 RenderGraph 可视化和自动导出参数说明。

- [ ] **Step 6: 提交**

  `git add src/kaleido_editor.cpp src/kaleido_runtime.h src/kaleido_runtime.cpp README.md testcases/ABeautifulGame_RenderGraph_Visualization`  
  `git commit -m "test(editor): add rendergraph visualization testcase and automation"`

---

## 8. Task 8: 全量回归与收尾

**Files:**

- Modify: （仅必要修复）
- Test: 全量构建 + 已有 testcase + 新 testcase

- [ ] **Step 1: Debug 构建**

  Run: `cmake --build d:\CMakeRepos\kaleido\build --config Debug --target kaleido_editor`  
  Expected: PASS。

- [ ] **Step 2: 既有 testcase 视口回归（不应漂移）**

  至少覆盖：
  - `testcases/ABeautifulGame`
  - `testcases/ABeautifulGame_AABB_Silhouette`
  - `testcases/ABeautifulGame_Material_Edit`

- [ ] **Step 3: 新 testcase 三重回归**

  - PNG compare 通过；
  - JSON graph compare 通过；
  - DOT graph compare 通过。

- [ ] **Step 4: 清理临时日志与产物**

  - 删除临时 debug 输出；
  - 不提交 `%TEMP%` 产物。

- [ ] **Step 5: 最终提交（若按用户要求压缩）**

  若执行阶段产生多提交，最后按团队策略 `squash` 为 1 条（只在确认尚未 push 时执行）。

---

## 9. 质量门禁（必须同时满足）

1. `Visualize RenderGraph` checkbox 可控且默认关闭。
2. Live/Imported 模式可切换且逻辑清晰。
2.1 RenderGraph 窗口展示“连连看”节点图（非纯文本列表）。
3. DOT 可由通用工具（Graphviz）打开。
4. JSON 可成功导入并回放。
5. Save/Restore 恢复可视化相关 UI 状态。
6. 既有 testcase 的 PNG 回归无非预期偏差。
7. 新 testcase 的 PNG + JSON + DOT 三重比对通过。
8. 同一 testcase 连续运行 3 次，JSON/DOT compare 结果稳定一致。

---

## 10. 风险与回滚

- 风险：导出内容包含不稳定字段导致回归波动。  
  对策：规范化排序 + compare 脚本忽略非确定性字段。
- 风险：可视化 UI 带来额外帧耗。  
  对策：默认关闭，且仅在窗口开启时构建详细布局缓存。
- 风险：CLI 自动导出流程和现有 auto dump 冲突。  
  对策：统一到同一帧触发点，按固定顺序导出并一次性退出。
- 风险：导入无效 JSON 导致 UI 状态污染。  
  对策：导入失败时保留上次快照并弹错误信息，不覆盖当前有效状态。
- 风险：目标机器无法 `git submodule add` GitHub。  
  对策：使用 vendor 镜像目录保持 API 与目录一致，后续网络恢复后可替换为 submodule。

---

## 11. 自检结果

- Spec 覆盖项均有对应任务（UI、导出、导入、序列化、测试）。
- 无 `TODO/TBD` 占位符。
- 任务粒度可执行，路径与命令均可直接落地。

---

## 12. 修订记录

- **R1**：确定模块分层、文件清单与主干任务拆分。
- **R2**：补充 CLI 自动导出、testcase 三重回归链路。
- **R3**：补充稳定性门禁（连续 3 次一致）、导入失败保护和收尾策略。
- **R4**：升级为 imnodes 节点连线可视化路线，并加入 submodule/vendor 双路径集成。

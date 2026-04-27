# RenderGraph 可视化系统设计（Editor）

**状态：** Draft (R3, self-reviewed)  
**日期：** 2026-04-27  
**对应计划：** `docs/superpowers/plans/2026-04-27-rendergraph-visualization-implementation.md`

---

## 1. 背景与目标

当前仓库已有 `RenderGraph` 的核心能力：

- 在 `RenderGraph::buildResourceDependencyMap` 中可以得到 pass 与 resource 的生产/消费关系；
- 在 `RenderGraph::getTopologicalOrder` 中可以得到执行拓扑顺序；
- 在 `renderer.cpp` 中每帧都会构建并执行 `RenderGraph`。

但当前缺少可交互的图结构可视化与可导出的通用图数据，导致以下问题：

- 开发者很难在 editor 中快速定位 pass/resource 依赖关系；
- 排障时只能看日志，无法获得结构化图形；
- 无法将图数据交给 Graphviz/网络图工具做离线分析。

本设计目标：

1. 在 editor 中新增开关（checkbox）控制是否显示 RenderGraph 可视化。
2. 当开关开启时，可在独立窗口以“连连看”节点图方式展示 RenderGraph（节点 + 连线 + 元数据）。
3. 图数据支持导出为通用格式（至少 DOT；并提供 JSON 供再次导入）。
4. 支持导入（反序列化）先前导出的 JSON 快照并回放可视化。
5. 将可视化开关与窗口状态纳入场景快照（Save/Restore）。
6. 规划可落地 testcase 与现有视口图像回归流程的接入方式。

---

## 2. 范围

### 2.1 本次范围（In Scope）

- Editor UI：
  - Rendering 面板新增 `Visualize RenderGraph` checkbox。
  - 新增 `RenderGraph Visualizer` 窗口（可停靠/可隐藏）。
  - 用 Node Graph 交互（拖拽、缩放、平移）显示 pass/resource 节点和连线。
  - 提供 `Export DOT`、`Export JSON`、`Import JSON` 按钮。
- Runtime 数据采集：
  - 每帧从 `RenderGraph` 抓取 pass/resource 关系，构建只读快照。
  - 可切换“实时帧”与“导入快照”两种显示模式。
- 序列化/反序列化：
  - Editor scene JSON 增加 `renderGraphVisualization` UI 状态字段。
  - RenderGraph 独立导出文件支持 DOT 和 JSON 两种格式。
- 依赖集成：
  - 首选引入公开仓库 `imnodes`（https://github.com/Nelarius/imnodes）作为 Node Graph 实现。
  - 若执行环境导致 `git submodule add` 不可用，可临时采用 vendor 镜像，并保持目录与 API 与上游一致，后续可无缝切回 submodule。
- 测试：
  - 视口回归保证“默认关闭可视化”时不改变已有 golden。
  - 新增 RenderGraph 可视化 testcase（以导出文件比对为主，PNG 为辅）。

### 2.2 非目标（Out of Scope）

- 不实现可视化窗口内“拖拽改图”并反写运行时图结构。
- 不在本期引入复杂自动布局引擎（本期使用确定性轻量布局 + Node Graph 交互）。
- 不覆盖跨进程实时远程调试（仅本地编辑器内可视化与导出）。

---

## 2.3 本轮设计假设（按“中途不提问”约束）

- 假设本次可以在 editor 中新增一个独立 ImGui 窗口，不要求覆盖到 3D 视口像素内容中。
- 假设“通用格式”至少满足 DOT（Graphviz 生态可读）；JSON 作为可回放与自动化比对格式。
- 假设 testcase 接入以“导出图结构文件比对”为主判据，视口 PNG 仅用于防回归兜底。
- 假设 Scene JSON 的版本号可能与其他并行特性竞争，因此采用“字段前向兼容优先，版本号从主线分配”策略。

---

## 3. 方案对比（3 选 1）

### 方案 A（推荐）：`imnodes` 节点图 + DOT/JSON 双格式导出

- **核心思路**
  - 使用 `imnodes` 在 ImGui 窗口中绘制节点连线图（按拓扑分层布局初始位置）。
  - 导出 `DOT`（给 Graphviz）和 `JSON`（用于导入回放与自动化测试）。
- **优点**
  - 用户直接获得“连连看”交互体验（拖拽/缩放/平移）；
  - 与现有 ImGui 技术栈一致，依赖可控；
  - DOT 通用性强，用户可直接用 Graphviz/VSCode 插件打开；
  - JSON 便于稳定比对和回放。
- **缺点**
  - 需要新增一个第三方依赖（submodule 或 vendor）；
  - 需要维护节点 ID/连线 ID 映射与稳定布局逻辑。

### 方案 B：只导出，不内置可视化

- **核心思路**
  - Editor 只负责导出 DOT/JSON，不做图形渲染。
- **优点**
  - 实现最轻；
  - 不增加 UI 复杂度。
- **缺点**
  - 不满足“在 editor 中勾选后可视化”的核心诉求；
  - 调试链路断裂（需要频繁切外部工具）。

### 方案 C：引入更重型图可视化库（除 imnodes 外）

- **核心思路**
  - 集成第三方布局/渲染库，提供更高级交互。
- **优点**
  - 视觉效果和交互潜力最佳。
- **缺点**
  - 依赖和构建复杂度显著上升；
  - 与现有仓库“轻依赖”风格不一致。

### 结论

采用 **方案 A**。它在满足“连连看”可视化诉求的同时，保留导入导出与回归稳定性。

---

## 4. 总体设计

### 4.1 新增模块

1. `RenderGraphVizSnapshot`（数据快照层）
   - 从 `RenderGraph` 提取节点、边、resource usage、拓扑顺序。
   - 提供稳定排序，确保导出结果可复现。
2. `RenderGraphVizSerializer`（序列化层）
   - `ToDot(snapshot)`；
   - `ToJson(snapshot)`；
   - `FromJson(json)`（导入回放）。
3. `RenderGraphVisualizerWindow`（UI 层）
   - 渲染 `imnodes` 节点图（pass/resource 两类节点）；
   - 承载导入导出交互。

### 4.2 数据模型（导出 JSON v1）

根对象：

- `format`: `"kaleido_rendergraph_viz"`
- `version`: `1`
- `frameIndex`: uint64
- `passes`: []
- `resources`: []
- `edges`: []

`passes[i]`：

- `id`（稳定字符串，建议 pass name + add-order index）
- `name`
- `index`
- `topoOrder`

`resources[i]`：

- `id`（`tex:<id>` 或 `ext:<name>`）
- `kind`（`internal_texture` / `external_texture`）
- `name`

`edges[i]`：

- `from`
- `to`
- `type`（`pass_to_resource` / `resource_to_pass` / `pass_to_pass`）
- `state`（可选，记录 `ResourceState` 文本）

### 4.3 DOT 导出规范

- 使用有向图：`digraph RenderGraph { ... }`
- pass 节点与 resource 节点使用不同 shape/color。
- 边标签记录读写关系与状态（如 `read:ShaderRead`）。
- 对节点 ID 做转义，确保 Graphviz 可解析。

---

## 5. Editor UI 设计

### 5.1 开关与入口

- 在 `Rendering` 折叠区新增：
  - `ImGui::Checkbox("Visualize RenderGraph", &scene.uiVisualizeRenderGraph);`
- 勾选后展示 `RenderGraph Visualizer` 窗口；取消勾选则隐藏窗口。

### 5.2 可视化窗口内容

- 顶部工具栏：
  - `Mode`: `Live` / `Imported`
  - `Export DOT`
  - `Export JSON`
  - `Import JSON`
  - `Freeze`（冻结当前 Live 快照）
- 主体区域：
  - 主区域：`imnodes` 连线图（节点拖拽、平移、缩放）。
  - 信息区：选中节点详情（pass index、topo、resource kind）与统计信息。

### 5.3 默认行为

- 默认关闭（保证不影响现有测试基线）。
- 即使开启，也只影响 ImGui UI，不改变渲染主通路结果。

---

## 6. Save/Restore 与兼容

### 6.1 Scene JSON 扩展

在 `editorUi` 下新增：

- `visualizeRenderGraph`（bool）
- `renderGraphVisualizerWindowOpen`（bool，可选）
- `renderGraphVisualizerMode`（`"live"` / `"imported"`，可选）
- `renderGraphVisualizerImportedPath`（string，可选）

### 6.2 版本兼容策略

- 若当前 `scene.json` 版本仍为 v4：
  - 读取缺失字段时使用默认值（全 false / live / 空路径）。
- 若后续已有别的功能占用 v5：
  - 本特性随主线版本号前进，字段保持向后兼容解析。

---

## 7. 测试策略与 testcase 接入

### 7.1 回归原则

1. 默认关闭可视化时，既有 `testcases/*/scene.png` 回归结果不应变化。
2. 可视化功能正确性主要通过导出文件比对（结构化数据更稳定）。
3. 视口 PNG 回归用于兜底验证“功能引入未污染主画面”。

### 7.2 新 testcase 目录建议

新增目录：

- `testcases/ABeautifulGame_RenderGraph_Visualization/scene.json`
- `testcases/ABeautifulGame_RenderGraph_Visualization/scene.png`
- `testcases/ABeautifulGame_RenderGraph_Visualization/rendergraph.expected.json`
- `testcases/ABeautifulGame_RenderGraph_Visualization/rendergraph.expected.dot`

其中：

- `scene.json` 中 `editorUi.visualizeRenderGraph = true`（用于保存场景后恢复开关状态）；
- `scene.png` 依旧是视口基线图；
- `rendergraph.expected.*` 用于导出结果比对。

### 7.3 自动化比对建议

- 新增脚本 `scripts/compare_rendergraph_json.py`：
  - 忽略非稳定字段（如时间戳）；
  - 对节点/边按稳定键排序后对比。
- 新增脚本 `scripts/compare_rendergraph_dot.py`：
  - 规范化空白与属性顺序后再比对。

---

## 8. 风险与缓解

- **风险：图布局过于拥挤**
  - 缓解：提供“列表模式”与“仅显示 pass->pass 边”过滤开关。
- **风险：导出结果不稳定导致测试抖动**
  - 缓解：导出前统一排序（pass index、resource id、edge tuple）。
- **风险：UI 开关被误保存导致基线波动**
  - 缓解：默认值为 false；回归场景固定写入 false（除专用 testcase）。
- **风险：导入 JSON 与当前 runtime 图不一致引起误解**
  - 缓解：窗口醒目显示当前模式（Live/Imported）和来源路径。

---

## 9. 验收标准

1. Editor 中新增 checkbox，勾选后能显示 RenderGraph 可视化窗口。
2. 可导出 DOT 与 JSON，且 DOT 能被通用工具（Graphviz）打开。
3. 可导入 JSON 并在窗口正确回放图结构。
4. Save/Restore 后可视化 UI 状态可恢复。
5. 新 testcase 的导出结果比对通过。
6. 既有 testcase 视口回归在默认设置下无非预期变化。
7. 导出 JSON 在同一输入场景下连续 3 次结果一致（除允许忽略字段外）。
8. 导出 DOT 可被 `dot -Tsvg` 成功解析（返回码 0）。
9. 节点图以“连连看”样式显示（至少可见 pass/resource 节点及其连线）。

---

## 10. 自检结论

- 无 `TODO/TBD` 占位符。
- 设计覆盖了 UI、导出、导入、序列化兼容、测试接入五个核心点。
- 将“图正确性”从不稳定图像比对转为结构化文件比对，降低回归脆弱性。

---

## 11. 修订记录

- **R1**：确定功能闭环（checkbox + 可视化 + DOT 导出）与基础数据结构。
- **R2**：补充 JSON 导入回放、Scene Save/Restore 字段与兼容策略。
- **R3**：补充 testcase 接入方案、自动化比对脚本策略与验收标准量化。
- **R4**：将可视化形态升级为 `imnodes` 节点连线图，并加入 submodule/vendor 集成策略。

# Shader Graph V2A（UX Alignment）设计说明

**状态：** Draft (R1, self-reviewed)  
**日期：** 2026-04-29  
**对应计划：** `docs/superpowers/plans/2026-04-29-shader-graph-v2a-ux-alignment-implementation.md`

---

## 1. 背景与目标

`kaleido` 已有 Shader Graph Material V1（图资产、基础节点、GLSL 注入、时间输入、序列化与回归链路）。  
你要求后续方向采用 D 路线：先让体验层更接近 `gltf2models`，再推进架构与扩展能力。

本阶段（V2A）目标：

1. 提升编辑体验与工作流效率，使其“使用感”接近成熟商业节点编辑器。
2. 不改变 V1 的核心运行时语义和兼容约束，优先做高收益低风险改进。
3. 为 V2B/V2C（多阶段图与表达式扩展）预留 UI 与数据模型入口。

---

## 2. 非抄袭与借鉴边界

### 2.1 可借鉴

- 通用交互范式：节点分类、搜索、拖拽连接、属性面板、编译日志、错误定位。
- 通用数据建模思想：图/节点/端口/连接/参数分层，稳定 ID 与可序列化。
- 通用工程模式：可回放错误、可导入导出、可回归测试。

### 2.2 明确禁止

- 不复制 `gltf2models` 的类名、枚举名、字段命名、文件组织与 API 形态。
- 不复刻其专有节点集合顺序、默认值细节、UI 文案和视觉布局细节。
- 不迁移或改写其受版权保护代码片段。

### 2.3 约束落地

- `kaleido` 延续自有命名（`ShaderGraphAsset`, `SGNodeOp`, `SGPortType` 等）。
- 文档与代码中显式记录“参考来源为高层设计理念，不复用实现细节”。
- 每项新增能力都附带 `kaleido` 现有架构适配理由。

---

## 3. 范围（V2A）

### 3.1 In Scope

- Graph Editor 交互改进：
  - 节点库分组与关键字搜索；
  - 快捷键（复制/粘贴/删除/框选）；
  - 错误节点高亮与自动定位；
  - 连接创建时即时类型提示。
- Material Dock 工作流改进：
  - `Compile` / `Apply` / `Revert` 状态机明确化；
  - Dirty 状态与未保存提醒；
  - 最近图资产（Recent Graphs）入口。
- 编译反馈改进：
  - 多层级日志（Error/Warning/Info）；
  - 错误与节点 ID 绑定；
  - 一键跳转到问题节点。
- 可维护性改进：
  - 将 `renderer.cpp` 中图编辑逻辑拆为独立编辑器模块。

### 3.2 Out of Scope

- 多 shader stage 图（vertex/fragment/light）。
- 动态端口表达式节点（可变输入输出）。
- 新增复杂运行时节点（Fresnel/高级噪声族等）。

---

## 4. 方案对比（V2A）

### 方案 A（推荐）：UI/交互优先，运行时零侵入

- 仅升级编辑器体验与状态机，不改变 codegen 与 runtime binding。
- 优点：交付快、回归风险低、对现有资产兼容性最优。
- 缺点：功能上限仍受 V1 节点能力限制。

### 方案 B：同时补一批节点

- 在 UI 改造同时追加节点集。
- 优点：用户感知更强。
- 缺点：范围膨胀，难以隔离回归来源。

### 方案 C：先重构底层再做 UI

- 先抽象图核心库，再做交互。
- 优点：长期最整洁。
- 缺点：短期价值慢，不符合“先 A 后 B/C”节奏。

**结论：采用方案 A。**

---

## 5. 总体设计

### 5.1 模块划分

1. `shader_graph_editor_ui.h/.cpp`
   - 节点库、画布、属性区、日志区渲染。
2. `shader_graph_editor_session.h/.cpp`
   - 编辑会话态、Dirty 标记、选中态、最近文件。
3. `shader_graph_compile_report.h/.cpp`
   - 编译消息结构、消息分级、节点跳转绑定。
4. `renderer.cpp`（薄适配）
   - 仅保留入口调用与 runtime 资源桥接。

### 5.2 Compile/Apply/Revert 状态机

- `Clean`：当前编辑态与已应用态一致。
- `Dirty`：编辑有改动未编译。
- `CompiledNotApplied`：编译通过但未应用到运行时。
- `Applied`：编译并应用成功。
- `CompileFailed`：最近一次编译失败。

状态迁移遵循单向明确规则，避免“看起来成功但未生效”。

### 5.3 错误定位模型

- `SGCompileMessage`：
  - `severity`（Error/Warning/Info）
  - `nodeId`（可空）
  - `text`
  - `phase`（validate/codegen/runtime）
- 当 `nodeId` 存在时，日志点击可触发画布定位与高亮。

---

## 6. 数据与持久化扩展

在不破坏 V1 图资产格式的前提下，新增可选 `editorMeta`：

- `lastView`（画布缩放与偏移）
- `collapsedGroups`（节点库折叠状态）
- `recentlyUsedNodeOps`（最近使用节点类型）

兼容规则：

- 缺失 `editorMeta` 不影响图执行。
- 新字段读取失败时忽略并记录 warning。

---

## 7. 测试策略

### 7.1 单元测试

- 状态机迁移测试（所有合法/非法迁移）。
- 编译消息关联测试（nodeId 跳转映射）。

### 7.2 集成测试

- 打开图 -> 修改 -> 编译失败 -> 定位节点 -> 修复 -> 编译成功 -> Apply。
- `Recent Graphs` 与 `Revert` 行为正确。

### 7.3 回归

- 既有 `time_noise.kshadergraph.json` 可无修改加载。
- 关闭 Graph 时渲染路径不变。

---

## 8. 风险与缓解

- **风险：** UI 状态膨胀导致 `renderer.cpp` 更难维护。  
  **缓解：** 强制拆分 editor 模块，`renderer.cpp` 仅保留桥接。

- **风险：** 错误定位与节点 ID 不一致。  
  **缓解：** 编译管线统一使用 graph stable node id，不使用临时索引。

- **风险：** 状态机复杂导致按钮行为不可预测。  
  **缓解：** 先写状态机测试，再实现 UI 绑定。

---

## 9. 验收标准

1. 用户可通过搜索快速添加节点，支持分类浏览。
2. 编译错误可定位到具体节点并高亮。
3. `Compile` / `Apply` / `Revert` 行为与状态机一致。
4. 图编辑逻辑从 `renderer.cpp` 分离为独立模块。
5. 现有 V1 图资产可兼容加载并运行。

---

## 10. 自检

- 无 `TODO/TBD` 占位项。
- 设计重点聚焦“体验对齐”，未越界到 B/C 功能扩展。
- 已加入借鉴/非抄袭边界和工程落地约束。

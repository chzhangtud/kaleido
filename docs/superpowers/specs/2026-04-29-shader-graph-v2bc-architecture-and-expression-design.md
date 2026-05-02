# Shader Graph V2B/V2C（Architecture + Expression）设计说明

**状态：** Draft (R1, self-reviewed)  
**日期：** 2026-04-29  
**前置：** `docs/superpowers/specs/2026-04-29-shader-graph-v2a-ux-alignment-design.md`  
**对应计划：** `docs/superpowers/plans/2026-04-29-shader-graph-v2bc-architecture-and-expression-implementation.md`

---

## 1. 目标

在 V2A 完成体验对齐后，本阶段推进“更像成熟 shader graph”的核心能力：

- **V2B（Architecture）**：多阶段图能力（Vertex/Fragment，预留 Light）与 Varying 管理。
- **V2C（Expression）**：受控表达式节点（局部表达式 + 全局表达式）与可配置端口。

目标是形成 `kaleido` 自主体系，不复制外部工程实现细节。

---

## 2. 非抄袭架构原则

1. **语义借鉴，不做 API 镜像**：采用多阶段图 + varying 的行业通用思想，但命名与结构自定义。
2. **自有数据协议**：继续使用 `kaleido_shader_graph` 格式并引入版本升级，不引入外部类型名。
3. **受控扩展**：表达式节点在 `kaleido` 安全边界内运行（禁不安全语句、限制资源访问）。

---

## 3. 范围

### 3.1 In Scope

- Graph Domain 从单 `spatial_fragment` 扩展为多 stage：
  - `vertex`
  - `fragment`
  - `light`（接口预留，可不在本期完全开放）
- Stage 间 varying：
  - 声明、类型校验、命名冲突检测；
  - `SetVarying` / `GetVarying` 节点。
- Expression 节点：
  - 自定义输入输出端口（受限类型集）；
  - 局部表达式体；
  - 全局表达式片段（helper function 常量）。
- 新图版本与迁移：
  - V1 -> V2 升级器（最小可行）。

### 3.2 Out of Scope

- 任意脚本执行与循环/分支自由控制流。
- 跨图函数调用与外部 include。
- 运行时热更新跨进程同步。

---

## 4. 方案对比

### 方案 A（推荐）：Vertex+Fragment + 受控 Expression（渐进开放）

- 先完成双阶段图、varying、表达式最小子集。
- 优点：风险可控，能覆盖 80% 常见需求。
- 缺点：Light stage 与高级语法需要下一轮补齐。

### 方案 B：一次性开放三阶段与完整表达式

- 优点：功能最强。
- 缺点：测试矩阵爆炸，难以保证稳定交付。

### 方案 C：仅表达式，不做多阶段

- 优点：实现快。
- 缺点：会把本应结构化的 stage 问题转嫁给表达式，后续维护差。

**结论：采用方案 A。**

---

## 5. 架构设计

### 5.1 数据模型

新增概念：

- `SGStage`：`Vertex`, `Fragment`, `LightReserved`
- `SGSubGraph`：每个 stage 一张 DAG
- `SGVaryingDecl`：`name + type + interpolation(optional)`

`ShaderGraphAsset`（v2）结构：

- `version: 2`
- `stages: { vertex: {...}, fragment: {...} }`
- `varyings: []`
- `blackboard: []`
- `editorMeta: { ... }`（可选）

### 5.2 校验层扩展

- stage 内继续做 DAG 校验、类型校验、输出完备性校验。
- stage 间新增：
  - `SetVarying` 与 `GetVarying` 名称对应校验；
  - 类型一致性校验；
  - 未赋值 varying 检测。

### 5.3 Codegen 扩展

- 生成阶段函数：
  - `sg_eval_vertex(...)`
  - `sg_eval_fragment(...)`
- varying 声明统一生成并注入稳定 hook 区域。
- expression 节点通过模板化发射，变量名统一前缀，避免冲突。

### 5.4 Runtime 接入

- Vertex hook：允许 UV/position/normal 预处理输出 varying。
- Fragment hook：读取 varying 并影响 `baseColor`（后续可扩展到更多 surface 属性）。

---

## 6. Expression 节点约束设计

### 6.1 节点能力

- 可定义 N 个输入端口与 M 个输出端口。
- 每个端口类型仅限：`float/vec2/vec3/vec4/bool`。
- 表达式正文仅允许白名单函数与运算符。

### 6.2 安全与稳定性

- 禁止访问未声明标识符。
- 禁止全局副作用语句。
- 禁止采样器声明与资源绑定操作（V2C 不开放）。
- 编译时错误必须返回“端口 + 语句片段 + 原因”。

### 6.3 全局表达式

- 用于声明辅助函数/常量片段。
- 仅在图编译产物中注入一次。
- 重名函数冲突时报错并阻止应用。

---

## 7. 迁移与兼容

- V1 文件（`version=1`）加载后自动包裹为：
  - 所有节点进入 `fragment` stage；
  - varying 为空。
- 迁移后可选择另存为 v2。
- 保持“无法迁移时回退旧逻辑，不崩溃”。

---

## 8. 测试策略

### 8.1 单元测试

- 多 stage 校验（varying 类型不匹配、未赋值、重名）。
- expression 解析与白名单约束测试。
- v1->v2 升级器测试。

### 8.2 集成测试

- Vertex 修改 UV -> Fragment 读取 varying -> 影响 noise 频率。
- Expression 节点替代一段数学节点链并生成稳定代码。

### 8.3 图像回归

- 新增 `ABeautifulGame_ShaderGraph_V2_StageVarying`。
- 新增 `ABeautifulGame_ShaderGraph_V2_Expression`。

---

## 9. 风险与缓解

- **风险：** 多阶段引入后调试复杂度上升。  
  **缓解：** 编译产物分段输出并支持 stage 级日志过滤。

- **风险：** expression 过于自由导致不可控。  
  **缓解：** 严格白名单与静态分析，不符合即拒绝。

- **风险：** 迁移器错误导致旧资产不可用。  
  **缓解：** 迁移前后 round-trip + 失败回退路径。

---

## 10. 验收标准

1. 支持 `vertex + fragment` 双 stage 图并可编译运行。
2. varying 声明/赋值/读取链路完整，类型错误有可读报错。
3. expression 节点支持可配置端口与受控表达式发射。
4. v1 资产可迁移加载，不破坏既有场景。
5. 新增 V2 testcase 图像回归与单测通过。

---

## 11. 自检

- 无 `TODO/TBD` 占位项。
- 已覆盖架构、表达式约束、迁移、测试和风险。
- 与 V2A 分层清晰，便于按阶段执行。

---

## 12. 2026-05-01 Node Assetization Delta

- Introduce `ShaderGraphNodeDescriptor` JSON schema and runtime `ShaderGraphNodeInstance` model for graph `version=3`.
- Keep backward compatibility by dual-read IO:
  - Load `version=1` legacy `nodes` and auto-migrate to in-memory `nodeInstances`.
  - Save as `version=3` with `nodeInstances` payload.
- Migration policy:
  - Preserve node ID, edges, numeric/text payload.
  - Map legacy op to descriptor IDs using `builtin/legacy/<SGNodeOpName>`.
  - Keep `nodes` only as compatibility runtime bridge during rollout.
- Registry loading priority:
  - Built-in descriptors are loaded from `assets/shader_graph_nodes`.
  - Project override path support is reserved for follow-up incremental work.

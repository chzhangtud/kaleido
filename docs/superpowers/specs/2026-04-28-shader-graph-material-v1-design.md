# Shader Graph（Material V1）设计说明

**状态：** Draft (R1, self-reviewed)  
**日期：** 2026-04-28  
**对应计划：** `docs/superpowers/plans/2026-04-28-shader-graph-material-v1-implementation.md`

---

## 1. 背景与目标

当前 `kaleido` 已具备：

- 完整的 PBR 材质数据结构与上传链路（`Material` / `MaterialDatabase`）；
- 编辑器材质面板（Material Dock）；
- Node Graph 第三方能力（`imnodes`，已用于 RenderGraph 可视化）；
- 场景 Save/Restore JSON 的状态持久化机制；
- Shader 重新加载与 SPIR-V 编译管线。

但当前材质编辑仍是“参数面板 + 固定着色路径”，无法表达程序化材质逻辑（例如时间驱动噪声）。

本设计目标：

1. 在编辑器中引入 **Material Domain 的 Shader Graph**（V1）。
2. 图编译结果是 **可编译 GLSL 代码片段**，并接入现有着色流程，而不是只写回静态材质参数。
3. 支持核心示例：`time + uv -> 3D noise -> baseColor`。
4. 完成图资产序列化/反序列化、场景引用持久化、回退兼容。

---

## 2. 设计边界（V1）

### 2.1 In Scope

- 域：仅 `Spatial/Fragment` 材质域。
- 图模型：有向无环图（DAG），单输入端口单连接。
- 节点类型（首批）：
  - 输入：`InputUV`, `InputTime`, `InputWorldPos`, `InputNormal`
  - 常量/参数：`Float`, `Vec2`, `Vec3`, `Vec4`, `ParamFloat`, `ParamVec*`
  - 数学：`Add`, `Sub`, `Mul`, `Div`, `Sin`, `Cos`, `Frac`, `Lerp`, `Remap`, `Saturate`
  - 组合：`ComposeVec3`, `SplitVec*`
  - 程序噪声：`NoisePerlin3D`
  - 输出：`OutputSurface`（至少 `baseColor`）
- 代码生成：Graph -> GLSL 函数片段 -> 注入 `mesh.frag.glsl` 既定 hook。
- 持久化：
  - 独立 Graph 资产 JSON；
  - Scene/Material override 中保存 graph 引用与参数覆盖。

### 2.2 Out of Scope

- 多域图（Canvas/Particle/Fog/Sky）。
- 用户自定义脚本节点。
- 运行时在线改图并跨进程热同步。
- 自动布局算法（仅提供基础重排和手动布局）。
- 复杂控制流节点（循环/分支图级语义）。

---

## 3. 参考与取舍（Godot 架构映射）

参考 Godot `modules/visual_shader` 的核心思路：

- 图容器持有 `nodes + connections`；
- 连接时做端口类型检查与环检测；
- 通过拓扑递归生成 GLSL 代码；
- 通过资源属性系统持久化节点与连接。

对 `kaleido` 的落地取舍：

- 保留“图 + 连接 + 校验 + 代码生成”骨架；
- 简化为单域、单输出目标，降低首次接入成本；
- 序列化采用仓库现有 JSON 风格（RapidJSON），避免引入新资源系统；
- UI 直接复用已有 `imnodes` 交互模型与工程依赖。

---

## 4. 总体架构

### 4.1 模块划分

1. **Graph Data Layer**
   - 文件建议：`src/shader_graph_types.h/.cpp`
   - 责任：节点、端口、连接、参数黑板、资产元数据定义。

2. **Validation Layer**
   - 文件建议：`src/shader_graph_validate.h/.cpp`
   - 责任：节点合法性、端口类型校验、环检测、输出完备性校验。

3. **Codegen Layer**
   - 文件建议：`src/shader_graph_codegen_glsl.h/.cpp`
   - 责任：拓扑排序、变量命名、节点级 GLSL 发射、生成 hook 函数。

4. **Runtime Binding Layer**
   - 修改：`src/shaders/mesh.h`, `src/renderer.cpp`, `src/shaders/mesh.frag.glsl`
   - 责任：时间参数上传、graph 参数绑定、graph 输出接入 shading 路径。

5. **Editor Layer**
   - 修改：`src/renderer.cpp`（Material Dock + Graph Window）
   - 责任：图编辑、编译、报错显示、参数调节、导入导出。

6. **Serialization Layer**
   - 新增：`src/shader_graph_io.h/.cpp`
   - 修改：`src/editor_scene_io.cpp/.h`
   - 责任：graph JSON 读写、scene 引用持久化与兼容。

### 4.2 数据流

Editor Graph -> Validate -> GLSL Snippet -> Shader Rebuild -> Runtime Upload -> Fragment Evaluate -> GBuffer BaseColor

---

## 5. 数据模型与序列化

### 5.1 Graph 资产 JSON（v1）

```json
{
  "format": "kaleido_shader_graph",
  "version": 1,
  "domain": "spatial_fragment",
  "entry": "material_surface",
  "nodes": [
    { "id": 10, "op": "InputUV", "params": {}, "ui": { "x": 120, "y": 80 } }
  ],
  "edges": [
    { "fromNode": 10, "fromPort": 0, "toNode": 30, "toPort": 0 }
  ],
  "blackboard": [
    { "name": "TimeSpeed", "type": "float", "default": 0.5 }
  ]
}
```

### 5.2 Scene/Material 引用扩展

在 material override 项增加字段：

- `shaderGraphEnabled: bool`
- `shaderGraphPath: string`
- `shaderGraphParamOverrides: object`

兼容规则：

- 缺字段 -> 默认关闭图材质；
- 路径无效或反序列化失败 -> 记录日志并回退旧材质逻辑；
- 版本不匹配 -> 拒绝加载并输出可读错误。

---

## 6. 类型系统与连接规则

### 6.1 端口类型

- `float`
- `vec2`
- `vec3`
- `vec4`
- `bool`
- `sampler2D`

### 6.2 兼容矩阵（V1）

- 允许：
  - `float -> vec2/vec3/vec4`（splat）
  - `vec3 -> vec4`（`w=1`，仅在明确允许端口）
- 禁止：
  - `vecN -> float` 隐式降维
  - `sampler2D` 与数值端口互连

### 6.3 图结构约束

- 不允许自环；
- 不允许形成环；
- 同一输入端口最多 1 条连接；
- `OutputSurface.baseColor` 必须可达（最终可编译条件之一）。

---

## 7. GLSL 生成策略

### 7.1 生成结构

每个图编译为函数：

```glsl
vec3 sg_eval_base_color(vec2 uv, vec3 wpos, vec3 nrm, float timeSec, SGParams params);
```

其中 `SGParams` 由 blackboard 映射。

### 7.2 注入点

在 `mesh.frag.glsl` 中新增稳定 hook：

- 在 `albedo` 计算前调用 `sg_eval_base_color(...)`；
- 若当前材质启用图，则覆盖 `albedo.rgb`；
- 未启用图时不改变现有逻辑。

### 7.3 命名与稳定性

- 节点临时变量命名：`sg_n<nodeId>_p<portId>`；
- 生成顺序由拓扑排序 + nodeId 次序稳定化；
- 相同输入图得到字节稳定输出（用于回归比对）。

---

## 8. 运行时绑定设计

### 8.1 Time 输入

在 `Globals` push constant 扩展：

- `float globalTimeSeconds`

CPU 侧每帧写入，从运行时累计秒数得到。

### 8.2 Graph 参数上传

V1 采用单独参数缓冲（建议小型 UBO/SSBO）：

- 每材质实例持有参数块；
- 参数名在编译阶段映射到稳定槽位；
- scene restore 时可恢复覆盖值。

### 8.3 材质选择逻辑

- `material.shaderGraphEnabled == true` 且 graph 编译成功：走图输出；
- 否则回退 `Material.baseColorFactor` + 贴图原逻辑。

---

## 9. 编辑器交互设计

Material Dock 新增 `Shader Graph` 面板：

- `Enable Shader Graph`（复选框）
- `Open Graph Editor`
- `Compile` / `Apply` / `Revert`
- `Import` / `Export`
- `Last Compile Error`（多行日志）

Graph Editor 窗口：

- 左侧节点库；
- 中央 `imnodes` 画布；
- 右侧节点参数与 blackboard；
- 底部 compile 日志与统计（node/edge/count）。

---

## 10. 样例链路（验收主线）

目标：`time + uv -> perlin3d -> baseColor`

标准节点连接：

1. `InputUV` -> `Mul(UVScale)`
2. `InputTime` -> `Mul(TimeSpeed)`
3. `ComposeVec3(uv.x, uv.y, t)` -> `NoisePerlin3D`
4. `Remap(-1..1 -> 0..1)` -> `ComposeVec3`
5. `OutputSurface.baseColor`

可见效果：

- 画面出现随时间流动的灰度噪声底色；
- 调整 `UVScale` 与 `TimeSpeed` 可实时改变频率和速度。

---

## 11. 测试策略

### 11.1 单元测试（CPU）

- 图校验：
  - 非法端口类型连接应失败；
  - 环检测应失败；
  - 缺失输出应失败。
- 代码生成：
  - 同图多次生成结果一致；
  - 示例图生成包含预期符号（`timeSec`, `uv` 等）。

### 11.2 集成测试（Editor/Runtime）

- 打开示例场景，加载 graph，编译成功；
- 导出视口 PNG，与 golden 比对阈值内通过；
- 关闭 graph 后结果回归旧路径，不影响既有 testcase。

### 11.3 序列化测试

- graph JSON round-trip 无信息丢失；
- scene save/restore 后 graph 引用和参数覆盖一致。

---

## 12. 风险与缓解

- **风险：** Perlin3D 噪声开销较高。  
  **缓解：** V1 限制节点数；后续支持噪声 LOD 或纹理噪声替代。

- **风险：** shader 注入导致主 shader 易碎。  
  **缓解：** 固定 hook 接口、自动生成文件隔离、严格编译日志。

- **风险：** 参数绑定与材质索引错位。  
  **缓解：** 编译产物保存参数布局 hash；运行时校验布局版本。

- **风险：** 序列化升级破坏老文件。  
  **缓解：** `format + version` 严格校验，提供回退分支。

---

## 13. 验收标准

1. 可在编辑器创建并编辑 Shader Graph（Material 域）。
2. `time + uv -> noise -> baseColor` 示例可成功编译并运行。
3. `globalTimeSeconds` 已进入 GPU，图内可读取。
4. graph 资产支持导入导出与 round-trip。
5. scene save/restore 可恢复 graph 引用与参数覆盖。
6. graph 关闭时既有渲染路径不变，回归用例无非预期漂移。
7. 非法图会给出可读错误，不崩溃。

---

## 14. 里程碑建议

- **M1**：Graph IR + 验证器 + JSON IO（无 UI）
- **M2**：GLSL codegen + runtime hook + time 输入
- **M3**：Editor 图编辑器 + 编译应用流程
- **M4**：参数覆盖 + save/restore + 回归测试资产

---

## 15. 自检

- 已覆盖：架构、数据模型、类型规则、代码生成、运行时绑定、UI、序列化、测试、风险、验收。
- 无 `TODO/TBD` 占位项。
- 示例链路可直接映射为测试与实现入口。

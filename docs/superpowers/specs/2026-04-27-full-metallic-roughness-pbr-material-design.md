# 编辑器完整 Metallic-Roughness PBR 材质支持（设计）

**状态：** Draft (R3, self-reviewed)  
**日期：** 2026-04-27  
**对应计划：** `docs/superpowers/plans/2026-04-27-full-metallic-roughness-pbr-material-plan.md`

---

## 1. 背景与目标

当前仓库已经具备以下能力：

- glTF 材质经 `BuildPbrMaterial` 进入 `MaterialDatabase`，并在 `mesh.frag.glsl` / `final.comp.glsl` 形成 MR 近似 PBR 流程。
- 编辑器已有 `Material` 折叠区，可编辑基础因子并触发 `materialDb.gpuDirty`，支持 `scene.json` v4 `materialOverrides` 的保存/恢复。
- 序列化是全量结构写入（`baseColorFactor/pbrFactor/emissiveFactor`），尚未覆盖全部“完整 MR + 扩展”字段。

本设计目标：在不引入材质图系统的前提下，实现 **工程内完整 Metallic-Roughness PBR 参数链路**：

1. Shader 侧形成一致、可验证的 MR 主流程；
2. Material dock 暴露完整目标参数；
3. `scene.json` 持久化/恢复覆盖完整参数，并对旧版本向后兼容；
4. 默认未使用覆写时，不改变现有 testcase 像素基线。

---

## 2. 范围定义

### 2.1 本次纳入参数（必须）

以 glTF MR 核心参数 + 当前引擎已接入扩展为边界：

- 核心 MR：
  - `baseColorFactor` / `baseColorTexture`
  - `metallicFactor` / `roughnessFactor` / `metallicRoughnessTexture`
  - `normalTexture` + `normalScale`
  - `occlusionTexture` + `occlusionStrength`
  - `emissiveFactor` / `emissiveTexture`
  - `alphaMode` / `alphaCutoff`
  - `doubleSided`
- 扩展（当前已接入）：
  - `transmissionFactor` / `transmissionTexture`
  - `ior`
  - `emissiveStrength`（映射到 `shadingParams.w`）

### 2.2 本次非目标

- 贴图资源路径替换、纹理重绑定 UI（仅显示槽位索引，不做资产替换工作流）。
- 新增 glTF 扩展（clearcoat, sheen, specular 等）。
- 从编辑器反写 glTF 资产文件本体。

### 2.3 参数语义与显示单位（规范）

| 参数 | 来源/落点 | UI 范围 | 语义约束 |
|---|---|---|---|
| Base Color | `baseColorFactor` | RGBA 0..1 | sRGB 交互、线性存储保持现有路径 |
| Metallic | `pbrFactor.z` | 0..1 | 仅 MR 工作流 |
| Roughness | `pbrFactor.w` | 0.045..1 | 下限 0.045 与 shader 一致 |
| Normal Scale | `shadingParams.x` | 0..4 | 0 表示禁用法线扰动 |
| Occlusion Strength | `shadingParams.y` | 0..1 | 0=无 AO，1=全量 AO |
| Alpha Mode | `alphaMode` | Opaque/Mask/Blend | 改动会影响排序 key |
| Alpha Cutoff | `shadingParams.z` | 0..1 | 仅 Mask 模式实际生效 |
| Emissive | `emissiveFactor` | RGB 0..32 | HDR 允许 >1 |
| Emissive Strength | `shadingParams.w` | 0..32 | KHR_emissive_strength 语义 |
| Transmission | `transmissionFactor` | 0..1 | >0 参与透明路径 |
| IOR | `ior` | 1.0..2.5 | 默认 1.5 |
| Double Sided | key 输入 | bool | 改动会影响排序 key |

---

## 3. 设计决策

### 3.1 完整性标准

采用“工程内完整”标准：  
在现有渲染架构（GBuffer + final + transparency/transmission）下，把纳入范围参数全部打通到 **UI -> CPU 材质 -> GPU 材质 -> Shader 使用/旁路 -> 序列化恢复**。

### 3.2 稳定主键与映射

- 序列化键继续使用 `gltfMaterialIndex`，避免 `materialDb` 绝对下标在未来多模型装载中漂移。
- 应用覆写通过 `Scene::GltfMaterialIndexToMaterialIndex` 映射，越界或类型不匹配仅 `LOGW` 跳过。

### 3.3 材质排序与 key

- 本次允许编辑会影响 `MaterialKey` 的字段（`alphaMode`、`doubleSided`、transmission presence/workflow 相关项）。
- 当 key 相关字段变化时，必须触发：
  - 对应 material 的 `MaterialKey` 重算；
  - `SortSceneDrawsByMaterialKey`；
  - `RebuildMaterialDrawBatches`；
  - 相关 GPU draw buffer 标记更新（与 transform/material 脏链路一致）。

### 3.4 写时校正策略

- 所有 dock 输入先写入临时值，再进行 clamp/枚举规范化，最后一次性提交到 `PBRMaterial::data`。
- 对会改变 key 的字段，采用“变更前 key vs 变更后 key”对比，避免每次滑条拖动都全量重排。

### 3.5 并发与一致性边界

- 编辑器所有材质编辑发生在主线程 UI 帧内，与渲染提交使用现有帧同步，不新增跨线程共享写。
- `materialDb.gpuDirty` 仅作为“下一帧上传”信号，不承载业务语义；业务语义由 `PBRMaterial::data` 与 override 结构保证。

---

## 4. 端到端数据流

1. **加载阶段**
   - `loadScene` 读取 glTF，构建 `PBRMaterial`、`MaterialKey`、`gltfMaterialDefaults`。
2. **恢复阶段**
   - `LoadEditorSceneSnapshot` 解析 `materialOverrides`；
   - `ApplyEditorMaterialOverrides` 写回 `PBRMaterial::data`，并同步 `gpuMaterials`；
   - 标记 `materialDb.gpuDirty`，在渲染帧上传。
3. **编辑阶段**
   - `DrawMaterialDock` 对选中节点 material 进行参数编辑；
   - UI 改动后更新 `PBRMaterial::data` + `gpuMaterials` + 脏标记；
   - 若影响 key，同步重算 key 与 draw 排序批次。
4. **保存阶段**
   - `CaptureEditorMaterialOverrides` 相对 `gltfMaterialDefaults` 进行差分；
   - 仅输出变化材质条目，写入 `scene.json` v5。

---

## 5. Material Dock 交互设计

### 5.1 结构

- 继续放置于 `DrawGltfDocumentTree` 下方 `CollapsingHeader("Material")`。
- 对节点上多个材质，使用 combo 选择正在编辑的 `materialIndex`。

### 5.2 参数分组

- Base
  - Base Color
  - Alpha Mode / Alpha Cutoff
  - Double Sided
- MR
  - Metallic
  - Roughness
- Detail
  - Normal Scale
  - Occlusion Strength
- Emissive
  - Emissive Color
  - Emissive Strength
- Transmission
  - Transmission Factor
  - IOR

### 5.3 可编辑策略

- 若某参数无对应纹理，仍允许编辑 factor。
- `workflow != 1` 的材质显示只读提示（后续再扩展 SG 编辑），本次重点是 MR 完整链路。

### 5.4 UX 细则

- 所有会触发排序重排的字段在 UI 上显示轻量提示（例如 “may reorder draws”）。
- 对高频拖动字段（roughness/metallic/normalScale）启用较小步进和输入框双模式，减少误触。
- 当节点材质列表超过 1 个时，默认保持上次选中的 `materialIndex`，若该材质失效则回落到首项。

---

## 6. Shader 行为一致性

### 6.1 `mesh.frag.glsl`

- MR 路径继续使用 glTF 约定：`roughness = pbrFactor.w * pbrSample.g`，`metallic = pbrFactor.z * pbrSample.b`。
- `alphaMode/alphaCutoff` 在 Opaque/Post 通道判定保持一致。
- `emissiveStrength` 通过 `shadingParams.w` 乘到 emissive 分量。

### 6.2 `final.comp.glsl`

- 保持当前 GGX + Disney diffuse 主体；
- 补充与 transmission 的耦合约束说明（由 transparency/transmission resolve 路径接管折射贡献，final 保持不重复计入）。

约束清单：

- `roughness` 下限与 `mesh.frag.glsl` 保持一致（0.045）。
- `metallic` 始终 clamp 到 `[0,1]`，避免极值造成 BRDF 不稳定。
- emissive 仍走独立加法项，不受 transmission 路径二次衰减。

### 6.3 transmission 相关通道

- transmission 开启时，`gbufferMaterialIndex` 及后续 pass 行为保持当前契约，确保 UI 修改 `transmissionFactor` 可见。

---

## 7. 序列化与兼容

### 7.1 文件版本

- `kaleido_editor_scene` 升级为 `version: 5`。
- v3/v4 读取继续支持：
  - 缺失新字段时使用旧默认值；
  - `materialOverrides` 缺失视为“无覆写”。

### 7.2 `materialOverrides` 新结构

每条 override 至少包含：

- `gltfMaterialIndex`
- `baseColorFactor`
- `pbrFactor`
- `emissiveFactor`
- `normalScale`
- `occlusionStrength`
- `alphaMode`
- `alphaCutoff`
- `doubleSided`
- `transmissionFactor`
- `ior`
- `emissiveStrength`

保存策略采用差分输出：仅当任一字段相对 `gltfMaterialDefaults` 发生变化时落盘。

### 7.3 JSON 兼容策略细节

- v5 写入时固定输出已知字段，不输出运行时中间态。
- 读取 v5 时：
  - 缺失字段按“当前 glTF 默认值”填充，而非硬编码常量；
  - 额外未知字段直接忽略，确保前向兼容。
- 读取 v3/v4 时：
  - 若存在旧 `materialOverrides`，先映射可用公共字段；
  - 新字段保持默认（来自 glTF 解析结果）。

---

## 8. 错误处理与鲁棒性

- `gltfMaterialIndex` 越界：`LOGW` 并忽略该条覆盖，不中断加载。
- 条目非 PBR 类型：`LOGW` 并忽略。
- JSON 字段类型错误：
  - 强类型错误（如数组长度错误）返回加载失败并给出路径化错误信息；
  - 未知字段忽略。

建议新增日志关键字（便于检索）：

- `EditorMaterialOverride/ParseError`
- `EditorMaterialOverride/InvalidIndex`
- `EditorMaterialOverride/NonPbrEntry`

---

## 9. 验收标准

1. 对 MR 材质，dock 中可编辑范围参数全部可见且即时生效。
2. 修改 `alphaMode/doubleSided/transmission` 等 key 相关参数时，渲染顺序和批次保持正确，无闪烁或错误混合。
3. Save/Restore 后参数一致；v3/v4 老场景可正常加载。
4. 不含覆写的 testcase 与既有 golden 图像一致（在回归阈值内）。
5. 新增 material-edit testcase 覆写后出图与新 golden 一致。

补充可量化验收：

6. 参数往返误差（Save -> Load）在 `1e-4` 内（float 字段）。
7. 修改单材质 key 字段后，`drawsForNode` 依然能正确映射到同一 glTF 节点集合（仅 draw 顺序变化）。

---

## 10. 风险与缓解

- 风险：key 改动后未重建 draw 批次导致渲染错误。  
  缓解：统一入口处理 key 改动并补充回归测试。
- 风险：过多字段导致 UI 易错。  
  缓解：分组展示 + tooltip + 合理 clamp。
- 风险：序列化升级破坏旧文件。  
  缓解：版本分支解析 + fixture 覆盖 v3/v4/v5。
- 风险：参数含义在 UI 与 shader 间出现“同名异义”。  
  缓解：以 `Material` 字段为单一语义源，spec 与代码注释同源维护。

---

## 11. 修订记录

- R1：建立完整范围、链路、参数集与版本升级方案。
- R2：补充参数语义表、写时校正策略、JSON 兼容细则与量化验收指标。
- R3：补充并发一致性边界、UX 细则、shader 约束清单与日志规范。

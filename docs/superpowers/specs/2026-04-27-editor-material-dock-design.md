# 编辑器：Material Dock 与 PBR 参数持久化（设计）

**状态：** 提议中（2026-04-27）  
**日期：** 2026-04-27  
**实现计划：** `docs/superpowers/plans/2026-04-27-editor-material-dock.md`  
**依赖：** 现有 `Scene` / `MaterialDatabase` / `PBRMaterial`（`src/material_types.h`）、`loadScene`（`src/scene.cpp`）、编辑器 UI（`src/renderer.cpp` 中 `DrawGltfDocumentTree`）、场景快照 IO（`src/editor_scene_io.cpp`）。

**相关规约：** `docs/superpowers/specs/2026-04-16-pbr-material-system-design.md`（GPU `Material` 布局与 `MaterialKey` 语义）。

---

## 1. 背景

用户希望在 **kaleido_editor** 中增加 **Material Dock（材质面板）**：

- 在选中已加载 glTF 中的**某个 mesh 相关节点**时，展示其关联的**材质**及可编辑参数（至少覆盖已接入渲染管线的 PBR 因子，如 **base color**、**roughness**、**metallic** 等；若某参数引擎尚未使用，则不在本迭代 UI 中暴露）。
- 修改应反映到实时渲染，并随 **`kaleido_editor_scene` 的 `scene.json`** 序列化/反序列化，以便保存/恢复。

引擎已具备：

- glTF 场景树与 `uiSelectedGltfNode` 点击选择（`DrawGltfOutlineNode`）。
- 每 draw 的 `materialIndex` 在加载时满足：`draw.materialIndex = materialOffset + cgltf_material_index`（`materialOffset` 为当前文件写入 `MaterialDatabase` 前的大小；单文件场景下 `gltfMaterialIndex` 与 `cgltf` 中 `materials` 数组下标一致）。
- `PBRMaterial` 的 CPU 态 `data` 与 `Material`（GPU）及 `PBRMaterial::key`（用于 `MaterialKey` 与 draw 排序，不随仅因子微调而变化）。

**缺口：** 左侧仅有 Scene 树与节点 TRS 检查器，**无**材质检查器；`EditorSceneSnapshot` 中**无**材质覆写；材质缓冲 **仅场景加载时上传一次**，**无**运行时从 CPU 改因子后的同步路径。

---

## 2. 目标（规范）

### 2.1 UI：Material Dock

1. 在**编辑器**侧栏中增加**可折叠的「Material / 材质」区块**（与「Assets」内 Scene 树**同级或紧邻**，避免打断现有布局；不强制本迭代接入 ImGui DockSpace，后续可再演进为可停靠窗口）。
2. 当**无 glTF 加载**或**无选中节点**时，显示简短提示（例如“请加载 .gltf/.glb 并选择带 mesh 的节点”），不崩溃。
3. 当选中**某一 glTF 节点** `N` 时，收集**该节点**（及其若实现上采用「子树内全部 draw」策略时的**子树**，见 3.1）上挂接的全部 **draw**；提取其中**不重复的** `materialIndex` 列表。若**多于一个**材质，使用 **ImGui 下拉框或列表** 切换「当前正在编辑的 `materialIndex`」。
4. 对**当前**选中的可编辑 `PBRMaterial`（`MaterialDatabase::entries` 中对应下标，且类型为 PBR / `PBRMaterial`）展示**仅**当前着色器/CPU 已用到且**值得编辑**的字段。本迭代**建议最小集**（与 `Material` / `BuildPbrMaterial` 一致）：

   - **工作流 1（Metallic-Roughness）**（`PBRMaterial::data.workflow == 1`）：`baseColorFactor`（RGBA）、`pbrFactor.z`（metallic）、`pbrFactor.w`（roughness）、`emissiveFactor[3]`（以及可选 `shadingParams.w` emissive strength 若与 glTF 扩展一致已存在）。  
   - **工作流 2（Specular-Glossiness 回退）**（`workflow == 2`）：`baseColorFactor`、`pbrFactor` 的 spec/gloss 语义、相应 emissive。  
   - 若 `alphaMode` / `transmission` 等**改变 `MaterialKey` 或管线分类** 的项在本迭代不编辑，则**不要**在 UI 暴露，避免与 `SortSceneDrawsByMaterialKey` 不一致；仅因子类编辑不触发 key 重算为真时方可后续开放。

5. 控件使用 ImGui 常规控件（`ColorEdit4`、`DragFloat` 等）；**sRGB**：`baseColor` 使用 **ImGuiColorEditFlags** 的线性/sRGB 约定与现有 PBR/最终着色一致（与 glTF 因子存储一致，必要时在规约中固定一种并在实现注释中写清）。

6. 每次用户修改，更新 **`PBRMaterial::data`**，调用 **`ToGpuMaterial()`** 回写 `materialDb.gpuMaterials[i]`，并置 **`MaterialDatabase` 或 `Scene` 级材质 GPU 脏标记**，由渲染器在下一帧**上传**对应 **SSBO** 子区间或整块（实现权衡见计划）。

### 2.2 持久化：scene.json

1. 扩展 **`kaleido_editor_scene` 文件格式**（**版本号递增至 4**），在根对象中增加**可选**字段，例如 `materialOverrides`（数组）：

   - 数组成员至少包含**稳定主键** `gltfMaterialIndex`（`uint32`，对应当前已加载的 **主 glTF 资源** 的 `cgltf_data::materials` 下标）。
   - 可编辑字段的 JSON 形态与 C++ 类型明确对应，例如：  
     `baseColorFactor`: `[r,g,b,a]`，`metallicFactor`，`roughnessFactor`，`emissiveFactor`: `[r,g,b]`，**仅**当与默认值不同或显式要覆盖时才写出（**可选**「稀疏存储」以减小 diff）。

2. **加载顺序：** `loadScene` 成功并完成 `MaterialDatabase` 与 draws 后，在应用 `transforms` / `editorUi` 等**之前或之后**（固定一种顺序并文档化；建议：**先** 完整构建场景，**再** 按 `gltfMaterialIndex` 将覆写**应用**到 `entries[materialIndex]` 与 `gpuMaterials[materialIndex]`，并 **reload 后上传材质缓冲**）：

   - `materialIndex = 当前模型加载时写入 DB 的 material 段基址 + gltfMaterialIndex`（对单 `modelPath` 的编辑器场景，基址 = 加载后 dummy 与前置资源之后连续段的起点；**必须在代码中单点计算**，避免硬编码。）

3. **保存：** `SaveEditorSceneSnapshot` 从当前 `MaterialDatabase` 中，对每个有**覆写记录**的 glTF 材质索引，序列化**当前** `PBRMaterial::data` 中属于「可编辑因子」的字段。  
   - **本迭代策略（推荐）：** 在 **Scene 或独立结构** 中维护 `std::unordered_map<uint32_t /*gltfMaterialIndex*/, MaterialOverrideBlob>` 表示**相对 glTF 初始加载**的差异；首次加载 glTF 时可为每个 glTF 材质**快照**初始 `PBRMaterial` 用于比较，**仅**将非默认/用户已改的条目写入 JSON。  
   - 简化版（可接受但 diff 大）：**总是**写出用户打开 dock 后编辑过的索引列表（**脏集合**），即使值与源文件相同。

4. **向后兼容：** 解析 **v3** 文件时 `materialOverrides` 缺失 = 无覆写；**v4** 读者必须**忽略**未知字段。旧测试用 **golden** 在未写 `materialOverrides` 时**行为与像素**不变。

### 2.3 与回归测试/CI

- 默认不启用材质覆写 的**新建** testcase 或**既有** `scene.png` 对比：在「未保存覆写、未开 dock」下 **像素**与**现有 golden** 一致。  
- 可新增**单独** testcase：`scene.json` 含**一条** `materialOverrides` 的已知覆写，golden **有目的地**更新一次（在计划中单独任务执行）。

---

## 3. 需要补充的产品与技术决策

### 3.1 选中一个节点、多个图元/材质

- **MVP 推荐：** 仅对**直接挂在该节点**上的 draw 集合 `drawsForNode[N]` 收集 `materialIndex`；若**空**则提示「此节点无 mesh 图元」。
- **增强（可选后续）：** 用 `CollectDrawIndicesInNodeSubtree` 对**子树**并集，与 AABB/描边语义一致。本 spec 采用 **MVP 仅本节点**；若产品坚持「子树内全部材质」，在实现计划中改为子树并更新本段。

### 3.2 视口点击 mesh

- 当前选择路径主要是 **树点击**。**视口射线拾取** 未在引擎内统一提供，**本 spec 不纳入**；作为「后续」在计划中记为**可选**的 Phase 2。

### 3.3 贴图（纹理）槽位

- 仅**因子**与 **emissive** 等**标量/向量** 纳入 v1。  
- 不在本迭代做「换贴图/换路径」的完整资源管线；若 JSON 中需**禁用**某 texture，属于**非目标**，避免与 `SceneTextureSource` 加载顺序纠缠。

### 3.4 `MaterialKey` 与排序

- 本迭代**仅**允许编辑不改变 `PBRMaterial::key` 的因子（`BuildPbrMaterial` 在加载时设置的 key 保持不变）。  
- 若未来允许改 `alphaMode` / 双面等，**必须** `SortSceneDrawsByMaterialKey` + `RebuildMaterialDrawBatches` 并**完整测试**。

### 3.5 多场景 / 多 glTF 合并

- 当前 `EditorSceneSnapshot` 为**单** `modelPath`。**`gltfMaterialIndex` 在单资源模型内唯一**；多资源若将来合并加载，**必须**为每条资源增加**命名空间**前缀（**非**本迭代范围）。

---

## 4. 方案对比

| 方案 | 内容 | 优点 | 缺点 |
|------|------|------|------|
| **A（推荐）** | 以 **`gltfMaterialIndex`** 为 JSON 主键；运行时映射到 `materialIndex`；Scene 内维护**覆写/差异** | 与 `cgltf` 稳定对应；不依赖 `MaterialDatabase` 全局下标在合并资产时的变化；易解释 | 需记录每个文件的 `materialBaseIndex` 或在加载时注册映射 |
| **B** | 以 **`MaterialDatabase` 绝对下标** 存 JSON | 实现直接 | 合并多 glTF 或资源顺序变更容易**断引用** |
| **C** | 以 **材质名** 字符串 | 人类可读 | glTF 名称**不保证唯一**；改名即丢失 |

**推荐方案 A。**

---

## 5. 主要模块与数据流

1. **加载 glTF** → 构建 `materialDb`、`draws`；记 `gltfMaterialIndex → materialIndex` 映射表（**仅编辑器/Scene 或 Runtime 的 Editor 扩展结构**中）。  
2. **加载 `scene.json`**（若 v4 含 `materialOverrides`）→ 按 `gltfMaterialIndex` 应用覆写 → `gpuMaterials` 同步 → **上传**材质缓冲。  
3. **用户**在树中选中节点 → `DrawMaterialDock` 读取 `drawsForNode` 与 `materialIndex` 列表 → 改 `PBRMaterial::data` → 脏 → **每帧或按需**上传。  
4. **Save Scene** → 序列化 `materialOverrides`（或等价名）**与** 现有 `format/version`。

---

## 6. 错误处理

- 无效的 `gltfMaterialIndex`（越界或模型材质数变少）：**记录 LOGW**；跳过该项，不崩溃。  
- 非 `PBRMaterial` 的条目：UI **跳过**或显示只读说明。

---

## 7. 验收标准

1. 在 Windows `kaleido_editor` 中加载测试 glTF，选中带 mesh 的节点，在 Material 区块可见 **base color / roughness / metallic**（在 MR 工作流或引擎映射下），修改后视口**立即**变化。  
2. Save 为 v4 `scene.json`，重开/Restore 后值一致。  
3. 未使用材质覆写时，**视口图像回归**（如仓库内 kaleido 视口 PNG 流程）与**既有 golden** 无**意外** diff。

---

## 8. 非目标（YAGNI）

- 在 Material Dock 中编辑**纹理路径**、**法线贴图**切换、**KHR 扩展** 全量。  
- 与 glTF **导出**回写 `.gltf` 文件。  
- 多视口/多机网络同步。  
- ImGui 真正的 **DockSpace 多列停靠** 可在后续单独迭代。

---

## 9. 审批与后续

本设计经批准确认后，按 `docs/superpowers/plans/2026-04-27-editor-material-dock.md` 分任务实现，并在各阶段执行计划中列出的**构建与视口/像素**验证。

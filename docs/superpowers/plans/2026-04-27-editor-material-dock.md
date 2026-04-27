# 编辑器：Material Dock 与 PBR 参数持久化（实现计划）

> **给实现者：** 按任务分段落地；以 `- [ ]` 子步骤**跟踪**进度。  
> **设计规约（Spec）：** `docs/superpowers/specs/2026-04-27-editor-material-dock-design.md`

**目标：** 在 **kaleido_editor** 左侧（或等效）增加 **Material** 可折叠区；根据 **glTF 场景树**选中节点，展示并编辑其关联 **PBR** 因子；将覆写**持久化**到 `kaleido_editor_scene` **v4**；在编辑后**同步** GPU 材质 **SSBO**。实现过程中**持续**可编译、并在关键节点做**手测/视口回归**。

**技术栈（不变）：** C++17、GLM、Vulkan、GLSL 450、Dear ImGui、RapidJSON（`src/editor_scene_io.cpp`）、现有 `MaterialDatabase` / `PBRMaterial`（`src/material_types.h`）。

---

## 文件影响范围（预计）

| 区域 | 文件 / 位置 |
|------|-------------|
| 场景/材质状态与脏标记 | `src/scene.h`（或 `src/editor_material_state.h` 新建）、`src/scene.cpp` 或 `src/kaleido_runtime.cpp` 应用覆写处 |
| 材质库 API | `src/material_types.h`, `src/material_types.cpp`（如 `SetGpuMaterialDirty`、或 `SyncMaterialToGpu` 辅助） |
| 渲染器：上传、ImGui | `src/renderer.h`, `src/renderer.cpp` |
| 场景快照 | `src/editor_scene_state.h`, `src/editor_scene_io.h`, `src/editor_scene_io.cpp` |
| 运行时应用快照 | `src/kaleido_runtime.cpp`（`LoadEditorSceneSnapshot` 后）、`src/renderer.cpp` 中 `CaptureEditorSceneUiState` / `ApplyEditorSceneUiState` 成对扩展或新增 `Capture/Apply` 材质覆写 |
| 回归/说明 | 按 spec 选做：新 `testcases/` 或仅手测 + 视口脚本 |

---

## 任务 0：基线确认（不提交也可，建议记录）

- [ ] **0.1** 在 `src/scene.cpp` 的 `loadScene` 中**再次确认** `draw.materialIndex` 与 `cgltf_material_index` 关系式，并**写下** `materialIndex = base + gltfMaterialIndex` 的 `base` 在**你的实现**中如何单点求出（与 spec 第 2.2 节一致）。  
- [ ] **0.2** 构建 `kaleido_editor`（或本仓库对编辑器的标准 target 名）**通过**。  
- [ ] **0.3**（可选）对**未改动的** `testcases/*/scene.json` 跑一次**视口 PNG 对比**（见仓库内 kaleido 视口图像技能），**建立**本分支的**基线**（主分支或当前 HEAD），后续任务结束后对比**无预期外**变化。

**测试门槛：** 0.2 通过；0.3 为强烈建议的回归基线。

---

## 任务 1：数据模型与「glTF 材质索引 → materialIndex」映射

**文件：** `src/scene.h`（与 `struct Scene` 或并列 Editor 扩展结构）、`src/scene.cpp` 中 `loadScene` 结束处

- [ ] **1.1** 在 `loadScene` 成功解析当前 glTF 后，保存本文件材料在 `MaterialDatabase` 中的**起始**下标 `materialBaseIndex`（在将本文件 `materials` 压入 DB **之前**的 `Size()`，与现有 `materialOffset` 逻辑对齐）及 `gltfMaterialCount`（`data->materials_count`）。  
- [ ] **1.2** 提供查询：`uint32_t Scene::GltfMaterialIndexToMaterialIndex(uint32_t gltfMatIdx) const` 或**自由函数**，越界时返回**可判失败**的 optional / 0 + 标志（与 spec 错误处理一致）。  
- [ ] **1.3** 若采用「相对默认值的覆写 diff」方案，在首次加载后**复制**每 glTF 材质对应的初始 `PBRMaterial` 快照到 `std::vector<PBRMaterial>` 或仅 factor 的轻量 struct，供 Save 时**差分**（若时间紧，可**延迟**到任务 4，本任务**至少**留好 `materialBaseIndex`）。

**测试门槛：** 单元/断言：对单测 glTF（或现有 testcase），`GltfMaterialIndexToMaterialIndex(0) == materialBaseIndex`（在 dummy 之后的第一段**依实现**可能需调整；以实际 `MaterialDatabase` 布局为准，**可写**临时 `LOGD` 手测后删除）。

- [ ] **1.4** 中间构建：`cmake --build` **通过**。

---

## 任务 2：GPU 材质缓冲运行时更新

**文件：** `src/material_types.h`, `src/material_types.cpp`, `src/renderer.h`, `src/renderer.cpp`

- [ ] **2.1** 增加**脏标记**（例如 `Scene::materialsGpuDirty` 或 `MaterialDatabase` 内 bool + 可选 `std::vector<uint8_t> dirtyMask`），在 **PBR** 因子被改时置位。  
- [ ] **2.2** 在**编辑器**主循环中，若脏：将 `entries[i]->ToGpuMaterial()` 写回 `gpuMaterials[i]`（与 Add 时顺序一致），并**上传**到现有 **material SSBO**（`mtb`）。  
  - **最小实现：** 整包 `vkCmdCopyBuffer` / 已有 `uploadBuffer` 路径**全量**重传（易正确，可接受性能）。  
  - **优化（可选）：** 仅 `vkCmdUpdateBuffer` 或部分 staging 更新被改下标。  
- [ ] **2.3** 确保**仅**在 `kaleido_editor` / 改材质路径执行，避免**非编辑器**多付一次每帧上传（用 `RuntimeLaunchMode::EditorViewport` 或等价 flag 门控）。

**测试门槛：** 手测：用临时代码或断点，改一个 `baseColorFactor` 后下一帧**draw** 颜色变化；**无** validation layer 新错误。

- [ ] **2.4** 本任务结束后再 `cmake --build`；若项目有**自动化** smoke，跑一遍。

---

## 任务 3：ImGui — Material Dock

**文件：** `src/renderer.cpp`（从 `DrawGltfDocumentTree` 或 `kaleido editor` 窗体中**抽** `DrawMaterialDock(Scene&)` 减少嵌套）

- [ ] **3.1** 在「kaleido editor」左侧面板中，在 `Assets` 的 **Scene 树**附近增加 **CollapsingHeader**「**Material**」（或中文「材质」与现有 UI 语言一致）。  
- [ ] **3.2** 当 `uiSelectedGltfNode` 有值、且 `drawsForNode[ni]` 非空，收集**唯一** `materialIndex` 列表；多材质时 `ImGui::Combo` 选当前**编辑目标**。  
- [ ] **3.3** 对 `entries[materialIndex]` 做 `dynamic_cast` 到 `PBRMaterial`；非空则按 `workflow` 显示：  
  - MR：`ColorEdit4` 对 `baseColorFactor`；`DragFloat` metallic / roughness 写 `pbrFactor.z` / `pbrFactor.w`；`emissiveFactor` 三通道等。  
  - SG：按 `BuildPbrMaterial` 与 `Material` 注释**对称**显示（可引用 `material_types.h` 注释）。  
- [ ] **3.4** 任意 ImGui 改值成功时：写回 `PBRMaterial::data`、**置 2.1 脏标记**。  
- [ ] **3.5** 若 `drawsForNode` 为空或节点无 mesh，**TextDisabled** 提示。

**测试门槛：** 手测：选节点、改颜色、**立即**在视口看到变化；**切换**节点后材质列表与值**正确**。

- [ ] **3.6** 构建通过。

---

## 任务 4：EditorSceneSnapshot 与 JSON v4

**文件：** `src/editor_scene_state.h`, `src/editor_scene_io.h`, `src/editor_scene_io.cpp`, `src/renderer.cpp`（Save 按钮路径），`src/kaleido_runtime.cpp`（启动加载）

- [ ] **4.1** 在 `EditorSceneSnapshot` 中增加**覆写**表示（如 `std::vector<…> materialOverrides` 或 `map`），每元素含 `gltfMaterialIndex` + 各因子字段（**与 JSON 一一对应**）。  
- [ ] **4.2** `SaveEditorSceneSnapshot`：根 `version` 改为 **4**；写 `materialOverrides` 数组（RapidJSON，与现有风格一致）。  
- [ ] **4.3** `LoadEditorSceneSnapshot`：读 v3（无 `materialOverrides` = 无操作）；读 v4 时解析数组，**不要**在 IO 里直接改 `MaterialDatabase`（**仅**填 `EditorSceneSnapshot`）。  
- [ ] **4.4** 在 `kaleido_runtime` 中，在**场景** `loadScene` 与 `ApplyEditorSceneUiState` 等**约定顺序**上，调用 **`ApplyMaterialOverridesFromSnapshot(Scene&, const Snapshot&)`**（名字自定），将覆写写入 `PBRMaterial` 并 **置 2.1 脏**、首帧**上传**。  
- [ ] **4.5** `CaptureEditorScene*`**保存**：从 `Scene` 的覆写/差异或「当前 `gpuMaterials` 与**初始**快照的 diff」生成写盘内容。  
- [ ] **4.6** 处理无效 `gltfMaterialIndex`：LOGW + 忽略。

**测试门槛：** 手测：改材质 → Save → 新进程 **Restore** → 与保存前**一致**；v3 旧档仍能加载。

- [ ] **4.7** 构建通过；**若有** JSON 样例，对 `testcases` 中**不**加 `materialOverrides` 的档跑**视口对比**，确认**与任务 0 基线**一致。

---

## 任务 5：视口/像素与收尾

- [ ] **5.1** 确认**默认**（不编辑材质、不保存覆写）下 `debugGuiMode` 等与 CI **一致**时，**黄金图**不漂移；若有漂移，对照 spec 第 2.3 节排查。  
- [ ] **5.2**（可选）新增**最小** testcase：专用 `scene.json` v4 + 一条 `materialOverrides`，更新一次**有意**的 `scene.png` golden。  
- [ ] **5.3** 删除临时 `LOGD`、**英文**仅用于需持久化的注释/用户不可见 `LOGE` 外，遵守仓库**注释英语**与 **LOGI/LOGW/LOGE** 规范。

**测试门槛：** 5.1 必过；5.2 按产品需要。

---

## 任务间「不断测试」约定（本计划强约束）

1. **每完成一个编号任务**（0–5）至少：对应 target **Release/Debug** 构建**成功**。  
2. 任务 2 之后、任务 4 之后：**必做**手测 **Save/Restore** 与**视口**一眼回归。  
3. 全任务结束前：对**主 testcase** 跑**视口 PNG 对比**（与仓库现成流程一致），**无解释**的失败**不合并**。

---

## 建议的提交分段（Conventional Commits，英文 subject）

1. `feat(editor): add material base index and GPU material dirty upload`  
2. `feat(editor): add ImGui material dock for PBR factors`  
3. `feat(editor): persist material overrides in editor scene v4`  
4. `test: …`（若新增 golden / 脚本）  

**不必**在一条提交里压全部功能，便于 bisect；最终合并前可按团队习惯 squash。

---

## 风险与回滚

- **风险：** 全量重传大材质缓冲每帧**性能**差 → 先用**仅脏时**上传；仍差则**按**子区间。  
- **回滚：** 以 feature flag 包住 `DrawMaterialDock` 与 v4 写入（**不推荐**长期双轨；**短期**可便于 bisect）。

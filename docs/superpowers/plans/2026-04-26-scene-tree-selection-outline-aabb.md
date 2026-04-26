# 场景树：子树选区描边、TRS 与 AABB（实现计划）

> **给实现者：** 按任务分段落地；以 `- [ ]` 子步骤**跟踪**进度。  
> **设计规约（Spec）：** `docs/superpowers/specs/2026-04-26-scene-tree-selection-outline-aabb-design.md`  
> **前置工作：** 须先或在与本文**任务 A** 并行期间完成 `docs/superpowers/plans/2026-04-18-transform-scene-graph.md` 所述内容（**变换图**、每 **draw** 的 **`mat4`** 世界矩阵、**编辑器**侧基础能力）。

**目标：** 为选中的 glTF 节点子树做**轮廓/描边**，在编辑器中实现**连续**的局部 **T/R/S（平移/旋转/缩放）**，在 **CPU** 中维护每 mesh 的 **AABB**，对选中子树在**世界空间**的包围盒**并集**做可选的**视口**线框叠加。

**结构摘要：**  
**CPU：** 在 `transformNodes` 上配合 `drawsForNode` 用 **BFS/DFS** 求子树内 **draw 索引**；`Mesh` 上增加**对象空间**的 `aabbMin` / `aabbMax`；对每个 **draw** 的包围盒的 **八** 个角用 `draw.world` 变到**世界**再**取并集**。  
**GPU：** 增加**第二套**图形管线/第二**遍**光栅，采用**反壳（inverted hull）**（**沿法线外推** + **前向面剔除**）；在 `Globals` 或 **push constant** 中标出「**描边子模式**」。  
**界面：** `ImGui::DragFloat3` 等每帧/「已改」时写回；**复选框** 控制 AABB 显示；以缓存的 **视 × 投** 矩阵在视口上用 `ImDrawList` 画线。

**技术栈（不变）：** C++17、GLM、Vulkan、GLSL 450、Dear ImGui、现有 `src/renderer.cpp` 中**渲染图**与通道编排。

---

## 文件影响范围

| 区域 | 文件 |
|------|--------|
| CPU 包围 + 子树工具 | `src/scene.h`, `src/scene.cpp`，可新建 `src/scene_bounds.h` / `src/scene_bounds.cpp` 或写在 `scene.cpp` 内 |
| 描边相关着色器 | `src/shaders/mesh.h`（只扩 `Globals`/PC，**不要**在 GPU 的 `Mesh` 里加 AABB）, `mesh.vert.glsl`, `meshlet.mesh.glsl`, `mesh.frag.glsl`（或极小的描边用 frag） |
| 渲染器 / ImGui / 各趟 | `src/renderer.cpp`, `src/renderer.h` |
| 测试 / 回归 | `tests/`，或手测 + `.cursor` 视口 PNG 脚本 |

---

## 任务 A：前置（变换 + 上传）

- [ ] **A1** 确认 `loadScene` 会构建 `transformNodes`、`drawsForNode`，且 `draws[di].gltfNodeIndex`、`uiSelectedGltfNode` 所用下标与 `gltfDocument.nodes` 顺序**一致**。
- [ ] **A2** 确认 `MarkTransformSubtreeDirty` 会置位 `transformsGpuDirty`，且 `renderer.cpp` 在**脏**时上传 `scene->draws`（及**开启**光线追踪时 **TLAS** 相关缓冲）。
- [ ] **A3** 若仍有**数据路径**未接入变换图，在实作描边或 AABB 前，按 `2026-04-18` 计划中**任务 1–4、8、10** 先补齐。

**（可选）提交信息：** 仅在**代码注释**中增加**交叉引文**时用英文，例如：`docs: cross-link editor outline prerequisites`；**非**必须单独成提交。

---

## 任务 B：每 mesh 的 CPU AABB + 子树辅助

**文件：** `src/scene.h`, `src/scene.cpp`, 若拆分则 `src/scene_bounds.h`, `src/scene_bounds.cpp`

- [ ] **B1** 仅**在 C++** 的 `struct Mesh` 上增加 `aabbMin`、`aabbMax`（见设计第 5 节）。如需对齐可写 `static_assert` 或注释；**不要**改 GLSL 的 `Mesh`。
- [ ] **B2** 在 `appendMesh`（或等价处）得到反量化 `positions` 后，对该 mesh 全部顶点做 `min`/`max` 并写入 `aabbMin` / `aabbMax`。
- [ ] **B3** 实现 `CollectDrawIndicesInNodeSubtree`（设计第 6 节）：用栈访问每个后代节点并拼接 `drawsForNode[i]`（对索引做防御性检查）。
- [ ] **B4** 实现 `WorldAabbFromMeshAndDraw` 与 `UnionWorldAabbForDraws`（或一个返回 bool+min+max 的 `ComputeSelectedSubtreeWorldAabb`）。
- [ ] **B5** 工程**可编译**（如 `cmake --build … --target kaleido_editor`）。

**建议的 Git 说明（英文）：** `feat(scene): add mesh AABB and subtree world bounds helpers`

---

## 任务 C：ImGui — 连续 TRS

**文件：** `src/renderer.cpp`

- [ ] **C1** 在 `DrawGltfDocumentTree` 下检查器（或抽出的辅助函数）中，在 **平移/旋转(度)/缩放** 控件变化时，用当前 `translation`、由欧拉转成的四元数、`scale` 重组 `TransformNode::local`。
- [ ] **C2** 每次上述变化调用 `MarkTransformSubtreeDirty(scene, ni)` 与 `FlushSceneTransforms(scene)`；不要**仅**依赖 `IsItemDeactivatedAfterEdit`。在需要时同时使用 `if (ImGui::DragFloat3(...))` 等。
- [ ] **C3** 手测：未松开鼠标前即看到 mesh 随拖拽更新。

**建议的 Git 说明（英文）：** `fix(editor): apply local TRS during drag, not only on release`

---

## 任务 D：选区「描边」子通道（反壳 inverted hull）

**文件：** `src/shaders/mesh.h`, `mesh.vert.glsl`, `meshlet.mesh.glsl`, `mesh.frag.glsl`（或变体）, `src/renderer.cpp`（+ 可能 `src/renderer.h`）

- [ ] **D1** 在 `mesh.h` 的 `Globals` 与 C++ 中镜像的 `Globals` 增加 `selectionOutlinePass` 与宽度（或标量），**或** 扩 push constant；**记录** 现有 push 区大小与对齐。
- [ ] **D2** **顶点级**：在**描边**子模式下按**设计文档**约定**空间**做 `法线 * 线宽` 等偏移；**法向**与 `MeshDraw.world` 一致变换。
- [ ] **D3** 挂接**新** 图形**管线**（或动态状态对象）：**剔除前向面**、深度比较与**编辑器**主通道一致。仅**绑**子树内 **draw** 的列表，或换成 **GPU 节点位图** 方案二选一。
- [ ] **D4** 在**编辑器**主**不透明** / G-buffer 通道**之后**加该**遍**，使**颜色**能进**视口**。**合成**到与 `ImGui::Image` **同一张** 纹理/目标。
- [ ] **D5** 重编着色器；修顶点与 meshlet 路径。有选中时见描边；`uiSelectedGltfNode` 为空时无描边。

**建议的 Git 说明（英文）：** `feat(editor): draw selection subtree outline (inverted hull pass)`

---

## 任务 E：视口 AABB 叠加 + 复选框

**文件：** `src/renderer.cpp`, `src/renderer.h` 或 `src/scene.h`（放 UI 布尔量）

- [ ] **E1** 增加 `uiShowSelectedSubtreeAabb`（可挂在 `Scene` 或 `Renderer` 静态量）。
- [ ] **E2** 在更新编辑器视图时，与 `Globals` / `cullData` 同帧、同坐标系，把 `view`、`proj`、可选的 `viewProj` 缓存在 `Renderer`（如 `lastEditorView`, `lastEditorProjection`）。
- [ ] **E3** 在得到子树并集 AABB 后，用上述矩阵与视口 ImGui 的 `g_editorViewportRectMin/Max`（或等价量）将八角点投到**视口**像素空间。
- [ ] **E4** 在前景 `AddLine` 上画 12 条线（如黄/白、细线）。仅当复选框开启、有合法选中、并集非退化时绘制。
- [ ] **E5** 在 **Assets/Scene** 面板（靠近层级处）加复选框，文案示例：「显示选中子树 AABB（并集）」。

**建议的 Git 说明（英文）：** `feat(editor): show subtree world AABB union in viewport`

---

## 任务 F：验证与回归

- [ ] **F1** 在**关闭**描边与 AABB 时，如仓库有脚本则跑 `kaleido_editor` 冒烟用例 / **视口** **PNG 像素**对比（见本仓库内 **kaleido 视口图像**相关技能脚本）；**黄金基线**图**不得**因默认配置而**无故**改变。
- [ ] **F2** 若自动化对比在「**全部** 叠加/调试关」条件下跑，请新功能在**与 CI 一致** 的**默认**下**处于关闭**。
- [ ] **F3** 仅当项目**已有** `README` 等**测试**说明时，可再在**规约**或 `README` 里**补**一句（**可选项**）。

**建议的 Git 说明（英文）：** `test: document editor overlay defaults for image regression`

---

## 计划自审

| 设计节 | 任务覆盖 |
|--------|----------|
| 规约 §2 子树 **draw 集合** | B3, D1–D5 |
| 规约 §4 **无 Stencil** 的描边路径 | D* |
| 规约 §2 **连续 TRS** | C* |
| 规约 §2 **CPU AABB** 与**并集** | B*, E* |
| 规约 §2 **视口**叠加 | E* |
| 规约 §8 **验证** | F* |

**空栏检查：** 不得留无含义的「待补」；若**改为** 着色器内 **节点**位集而**不**在 CPU 筛 **draw 列表**，须同步**改写** 任务 D 的条目表述。

---

## 执行顺序与交接

1. 先完成 **A → B → C**（**风险小**、**易** 边写边**查**）。  
2. 再 **D**，后 **E**（**画面** 与**界面**）。  
3. 最末 **F**（**回测/回归**）。  

**分工建议：** 每一大任务**可**拆给不同人；**任务 D** 宜**同一人**打通**着色器** 与 `renderer.cpp` 内 **pipeline**/**标志**，**避免** 管线和常量**分裂**成两套语义。

---

## 修订记录

- 2026-04-26：初版（与 `2026-04-26-scene-tree-selection-outline-aabb-design.md` 成对）。  
- 2026-04-26：**正文** 通篇为**简体中文**；「建议的 Git 说明」**保留**英文**示例** 以**符合**本仓库**提交**习惯；**计划自审** 表 与 规约 **节条** 对齐 已 更新。

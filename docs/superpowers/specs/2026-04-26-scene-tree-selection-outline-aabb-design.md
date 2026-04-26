# 编辑器：子树选中描边、TRS 与 AABB（设计）

**状态：** 提议中（2026-04-26）  
**日期：** 2026-04-26  
**实现计划：** `docs/superpowers/plans/2026-04-26-scene-tree-selection-outline-aabb.md`  
**依赖：** `docs/superpowers/specs/2026-04-18-transform-scene-graph-design.md`（变换图、`drawsForNode`、`MeshDraw.world`、`uiSelectedGltfNode`）。

**范围：** 当用户在 glTF 场景树中选中节点时，（1）用围绕受影响几何的**闭合轮廓线**高亮该节点**子树**内所有 mesh 的 draw，（2）在检查器中支持**连续**的局部 **T/R/S（平移/旋转/缩放）** 编辑，并通过既有变换刷新更新渲染，（3）在 **CPU** 上维护每 mesh 的**轴对齐包围盒（对象空间）**，并在需要时显示选中子树在**世界空间**中这些包围盒的**并集**，以**三维线框**形式叠加在编辑器视口上。

**本迭代不纳入：** 基于 Stencil/模板测试的描边（主深度缓冲为**无** Stencil 的 `D32`；为描边而改全局深度格式代价大）。视口内交互式 3D 操控器（gizmo，类似平移/旋转/缩放小工具）。将选中或调试状态写入 `scene.json` 的持久化。在 GBuffer 增加对象 ID 的 MRT 与全屏后处理描边（可作为后续可选项）。超出当前 mesh 顶点可表达范围的蒙皮/形变体积的 AABB。

---

## 1. 背景

引擎已具备：

- `TransformNode` 与 `FlushSceneTransforms` / `MarkTransformSubtreeDirty`（`src/scene_transforms.cpp`）。
- `MeshDraw.gltfNodeIndex` 与 `std::vector<std::vector<uint32_t>> drawsForNode`，将 glTF 节点索引映射到各图元 draw。
- `Scene::uiSelectedGltfNode` 与 `DrawGltfDocumentTree` / 检查器 TRS（`src/renderer.cpp`）。

本设计要补齐的差异：

- **选中的视觉反馈**：仅有树内选中不足；用户需要在**视口**中看到属于所选节点**子树**的所有 mesh 的提示。
- **TRS 使用体验：** 仅依赖 `IsItemDeactivatedAfterEdit` 时，**拖拽**滑条无法途中更新场景；在控件**处于激活**的帧内，编辑应**每帧**生效，或与「值已变」等价的更新路径。
- **包围体：** 剔除使用 mesh 的**球体**（`center` / `radius`）；编辑器需要每 `Mesh` 在**对象空间**的 **AABB**，以便在子树下做世界空间**并集**，且**不**改变计算着色器所用的 `Mesh` 的 std430 布局（见下）。

---

## 2. 目标（规范）

1. **子树 draw 集合：** 对选中的 glTF 节点索引 `N`，沿 `TransformNode::children` 做 BFS/DFS，为每个访问到的节点 `i`（含 `N`）拼上全部 `drawsForNode[i]` 中的 draw 索引，即定义以 `N` 为根的**子树**的 draw 集合。无图元 draw 的节点自然被跳过。
2. **描边（轮廓线）：** 为上述并集体提供可见的**外缘轮廓**。主路径：在主 G-buffer（或等价的首次几何通道）**之后**增加**第二遍光栅化**，采用**反壳（inverted-hull）** 思路：沿**物体/世界**法线外推几何、**只渲染背面**（剔除前向面），将**无照明的纯色高亮**写入**场景颜色 / HDR** 目标，并做深度以正确与场景合成。使用独立图形管线状态（`VK_CULL_MODE_FRONT_BIT`）与 push constant 或 `Globals` 字段标出该**通道**。**传统顶点**与 **meshlet** 两条实现路径须带相同外推门控。选中子集可：（a）在 CPU 上筛出子树内 draw 再仅提交这些 draw，或（b）经 SSBO 下传**节点**位集并在片元/顶点中判断 `gltfNodeIndex` 是否落在子树内（**最小实现**上可用 CPU 过滤的 draw 批次以简化着色器逻辑）。
3. **TRS：** 在检查器中对平移、旋转（由欧拉角界面合成 `local` 用四元数）与缩放的每次修改，在 ImGui 控件**发生变更**时（如 `ImGui::DragFloat3` 本帧返回真、或每帧在控件未失焦时同步数值）**重算 `TransformNode::local`**，并 `MarkTransformSubtreeDirty`、`FlushSceneTransforms`；**不要**仅在**控件失焦**时提交一次。
4. **AABB：** 对 `Geometry::meshes` 中每个 `Mesh`，在 mesh 构建时从**反量化顶点**存 **对象空间** 的 `aabbMin` 与 `aabbMax`；**仅**放在 `scene.h` 的 C++ `struct Mesh` 中，**不要**在 `mesh.h` 的 GPU `struct Mesh` 上扩展（剔除缓冲布局固定）。单个 draw 的世界 AABB：将 mesh 局部 AABB 的**八个角**用 `MeshDraw.world` 变换后再轴对齐取 min/max。**子树并集：** 对当前选中的子树内各 draw 的世界 AABB 求并。可选 UI 标签：「子树 AABB（并集）」。
5. **叠加层：** 若开启「显示选中子树 AABB」且存在选中节点且子树中至少一个 draw，将世界空间并集盒的 **12 条边**用与当帧相同的视图/投影矩阵投到**编辑器视口**矩形中的**屏幕空间**，并用 ImGui 前景点绘制列表或等价的单处辅助逻辑画线。

---

## 3. 非目标（YAGNI：不提前做「将来可能用」的无关功能）

- 在 GPU 上为 AABB 重建 `Mesh`；不修改 `drawcull` / `clustercull` 中的 AABB 表示。
- 除非性能分析需要，**不在** `TransformNode` 上缓存**按节点**的 AABB；子树并集可在**选中或变换变化**时重算。
- 同节点树下的**灯光/相机**：本规约中 TRS 与描边**仅**影响 **mesh 的 draw**；不要求灯光/相机跟随编辑器的 TRS。

---

## 4. 无模板的描边（约束）

默认深度/颜色配置下，主深度是**无** Stencil/模板缓冲的 **D32** 浮点图。**不**把「两遍绘制 + 模板测」类描边作为本迭代必选项；**反壳（inverted hull）** 与**日后可选的**「MRT 输出对象 id + 全屏求边」均可接受。

---

## 5. 数据布局：仅 CPU 的 mesh AABB

**仅**在 C++ 的 `struct Mesh` 中增加（见 `src/scene.h`）：

- `vec3 aabbMin`, `vec3 aabbMax`（或与文件其余部分一致的 `glm::vec3`）。

在 `src/scene.cpp` 的 mesh 构建路径（`appendMesh` 或等价处）中，对 mesh 顶点**反量化位置**扫描后填入。

**不得**在 `src/shaders/mesh.h` 中镜像这些字段，也**不要**在 GLSL 中改变 `sizeof(Mesh)`。

---

## 6. 建议的对外辅助函数（C++）

命名仅为示意：

- `void CollectDrawIndicesInNodeSubtree(const Scene& scene, uint32_t rootNode, std::vector<uint32_t>& outDraws);`
- `void WorldAabbFromMeshAndDraw(const Mesh& mesh, const glm::mat4& world, glm::vec3& outMin, glm::vec3& outMax);`（八角点 → min/max）
- `bool UnionWorldAabbForDraws(const Scene& scene, const std::vector<uint32_t>& drawIndices, glm::vec3& outMin, glm::vec3& outMax);`（无有效 draw 时返回 false）

编辑器状态（如 `Scene::uiShowSelectedSubtreeAabb` 或 `Renderer` 内静态量）及用于叠加的缓存 `lastEditorViewProj` / `lastEditorView` 属实现细节，但应在**实现计划**中写明。

---

## 7. 着色器：选区描边

- **Push constant** 或 `Globals` 增域：`uint selectionOutlinePass`, `float selectionOutlineWidth`（**须**在实现中写明为**世界**还是**观察**空间）。
- **顶点管线路径：** 在 `mesh.vert.glsl` 中，当处于「描边」子通道时，在算 `gl_Position` 前沿**已变换法线**偏移顶点（TBN/世界法线可沿用现实现或现推）。
- **Meshlet 路径：** 在 `meshlet.mesh.glsl` 中作一致处理；若 `meshlet.task` 会改变包围/剔除亦需检视。
- **片元阶段：** 在「描边」子通道中向**与最终编辑器视口**相同的**颜色**目标写入**纯色**（如与 `ImGui::Image` 对应的 HDR/中间缓冲），混合与混色方案避免整屏发糊；可选项为带**夹紧**的加法等。

---

## 8. 验证

- 手测：多层 glTF，选父 → 子树内 mesh 均有描边；选叶 → 单 mesh。
- 可拖拽 TRS：未松开鼠标前模型即连续变化。
- AABB：开关联显线框、关则清除、变换时框应更新。
- **视口与黄金基线图：** 若用自动化 **PNG 像素**对比，测试配置中应**关闭** AABB 与描边叠加；若刻意在开启这些特效下对比，应**重新生成**并提交黄金基线。

---

## 9. 代码参考

- `src/scene.h` — `Scene`, `TransformNode`, `Mesh`, `MeshDraw`, `GltfDocumentOutline`
- `src/scene_transforms.cpp` — `FlushSceneTransforms`, `MarkTransformSubtreeDirty`
- `src/renderer.cpp` — `DrawGltfDocumentTree`, ImGui, 编辑器视口, 渲染图
- `src/shaders/mesh.h`, `mesh.vert.glsl`, `meshlet.mesh.glsl`, `mesh.frag.glsl`

---

## 10. 修订记录

- 2026-04-26：初版（子树选区描边、连续 TRS、CPU AABB 与并集叠加）。  
- 2026-04-26：通篇用语统一为**简体中文**；修正 Stencil/「模板（template）」与「模板测试」的表述；细描 **inverted-hull、pass** 等术语的译法。

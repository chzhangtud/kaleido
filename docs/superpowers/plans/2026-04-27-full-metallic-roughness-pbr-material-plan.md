# 编辑器完整 Metallic-Roughness PBR 材质支持（实施计划）

> 设计文档：`docs/superpowers/specs/2026-04-27-full-metallic-roughness-pbr-material-design.md`

**目标：** 以最小风险完成“工程内完整 MR + transmission/ior/emissiveStrength”链路，覆盖 shader、material dock、序列化兼容与回归验证。

---

## 0. 执行策略

- 分阶段推进，每阶段结束都要求可构建、可运行、可回归。
- 默认采用小步提交；本次按用户要求最终合并为单次文档提交。
- 仅文档阶段不改代码；实施阶段按本计划执行。

质量闸门（所有阶段共享）：

- 每阶段至少一次 Debug 构建通过。
- 任何涉及 shader/排序逻辑的阶段，必须追加一次 editor 实机手测。
- 仅当回归结果可解释时才更新 golden。

完成定义（DoD）：

- 代码通过构建 + 关键手测 + 图像回归；
- 新旧版本场景兼容通过；
- 文档与实现一致（字段名、版本号、流程顺序一致）。

---

## 1. 影响文件清单（预估）

- 材质与场景：`src/material_types.h`, `src/material_types.cpp`, `src/scene.h`, `src/scene.cpp`
- 编辑器 UI：`src/renderer.cpp`
- 场景 IO：`src/editor_scene_io.h`, `src/editor_scene_io.cpp`, `src/editor_scene_state.h`
- 启动/恢复：`src/kaleido_runtime.cpp`
- shader：`src/shaders/mesh.frag.glsl`, `src/shaders/final.comp.glsl`, `src/shaders/transparency_blend.frag.glsl`, `src/shaders/transmission_resolve.comp.glsl`
- 测试资源：`testcases/ABeautifulGame_Material_Edit/*`（含 scene 与 golden）

---

## 2. 阶段任务

## Phase A：参数与结构对齐

- [ ] A1. 盘点 `Material` 字段与 glTF MR+扩展映射关系，补齐缺失元数据注释。
- [ ] A2. 定义“可序列化的完整参数集合”结构（override payload）。
- [ ] A3. 明确 key 相关字段与非 key 字段清单。
- [ ] A4. 增加 `Material` 字段文档注释，标记“可编辑/不可编辑/影响 key”。
- [ ] A5. 为新增序列化字段确定默认值来源（glTF defaults 优先，硬编码仅兜底）。

**完成门槛**

- 形成参数映射表（代码注释或文档）。
- 构建通过：`kaleido_editor`。

## Phase B：Material Dock 完整参数编辑

- [ ] B1. 重构 `DrawMaterialDock` 为分组绘制函数，降低维护复杂度。
- [ ] B2. 新增字段控件：`alphaMode`、`alphaCutoff`、`doubleSided`、`normalScale`、`occlusionStrength`、`emissiveStrength`、`transmissionFactor`、`ior`。
- [ ] B3. 增加 clamp/枚举映射与 tooltip，保证输入合法。
- [ ] B4. 编辑后统一调用 `MarkMaterialEdited(...)`（集中做 data/gpu/key 脏处理）。
- [ ] B5. 对 `workflow != 1` 的材质提供只读提示，避免误编辑 SG 路径。
- [ ] B6. 增加“可能触发重排”的轻提示文案（不阻断编辑）。

**完成门槛**

- 手测可见新增控件，修改后即时生效。
- 无 ImGui 断言或崩溃。

## Phase C：key 重算 + draw 重排

- [ ] C1. 实现 `MaterialKey` 重算入口（基于当前 `PBRMaterial::data`）。
- [ ] C2. 当 key 变化时触发 `SortSceneDrawsByMaterialKey` + `RebuildMaterialDrawBatches`。
- [ ] C3. 对重排后的 `drawsForNode` 映射一致性做回归检查。
- [ ] C4. 减少重排频率：仅提交值变化时重排，不在拖动中每帧重排。
- [ ] C5. 对 `drawsForNode` 结果做节点级回归比对（重排前后 draw 集合一致）。

**完成门槛**

- 修改 `alphaMode/doubleSided/transmission` 不出现错误批次或错误混合。
- 关键路径日志清晰可追踪（仅 `LOGD/LOGW`，不引入 printf）。

## Phase D：序列化 v5 与兼容

- [ ] D1. `EditorMaterialOverride` 扩展完整参数字段。
- [ ] D2. `SaveEditorSceneSnapshot` 写出 v5 + 差分字段（相对 defaults）。
- [ ] D3. `LoadEditorSceneSnapshot` 支持 v3/v4/v5；v5 读取完整字段。
- [ ] D4. `ApplyEditorMaterialOverrides` 应用完整参数并触发统一脏逻辑。
- [ ] D5. 读取 v5 时缺失字段回退到 glTF 默认值；未知字段忽略。
- [ ] D6. 增加解析错误信息标准化前缀，便于日志筛查。

**完成门槛**

- Save/Restore 参数一致。
- v3/v4 老文件成功加载。

## Phase E：shader 行为核对

- [ ] E1. 校核 `mesh.frag.glsl` 对新增可编辑参数的使用路径。
- [ ] E2. 校核 `final/transmission` 通道不会重复计入或丢失 transmission 贡献。
- [ ] E3. 若发现契约偏差，修复并同步注释。

**完成门槛**

- 可观测材质变化符合参数语义。
- 无新增验证层错误。

## Phase F：测试与回归

- [ ] F1. 增加/更新 material-edit testcase（含 v5 覆写样例）。
- [ ] F2. 跑视口 PNG 对比：默认场景不漂移、覆写场景按预期变化。
- [ ] F3. 运行 editor smoke（启动、加载、编辑、保存、恢复）一次完整链路。
- [ ] F4. 增加 JSON 往返测试：v5 -> load -> save -> load 一致性检查。
- [ ] F5. 在 `ABeautifulGame_Material_Edit` 里覆盖至少一组 transmission 与 alphaMode 组合。

**完成门槛**

- 回归全部通过或差异有明确、可接受解释。

---

## 3. 验证命令（Windows）

- 构建：
  - `cmake --build d:\CMakeRepos\kaleido\build --config Debug --target kaleido_editor`
- 运行编辑器（按仓库现有启动方式）。
- 视口图像回归（按仓库既有脚本/流程执行）。

建议验证顺序：

1. `B` 完成后：节点切换 + 参数实时预览。
2. `C` 完成后：反复切换 `alphaMode` / `doubleSided`，观察排序与混合是否稳定。
3. `D` 完成后：同一场景连续两次 Save/Restore，比对参数和截图。

建议附加检查：

4. `F` 完成后：在未编辑材质情况下导出一张基线图，确认与主分支 golden 无意外偏差。

---

## 4. 回滚策略

- 每阶段独立提交，发现问题可按阶段回退。
- 若 C 阶段引入排序副作用，可先临时门控 key 重排，仅保留非 key 参数编辑，待问题定位后再开启。

---

## 5. 交付物

1. 代码：完整 MR 参数编辑 + shader 一致性 + v5 序列化兼容。
2. 文档：spec + plan（本文件）与必要注释更新。
3. 资产：material-edit testcase 与 golden 更新。

---

## 6. 风险跟踪表

| 风险 | 触发点 | 预防措施 | 兜底方案 |
|---|---|---|---|
| key 重排导致 draw 映射异常 | Phase C | 增加映射一致性断言/日志 | 临时门控 key 编辑项 |
| v5 解析破坏旧文件 | Phase D | v3/v4 fixture 回归 | 回退到 v4 并保留兼容代码 |
| transmission 视觉偏差 | Phase E/F | 增加 material-edit golden | 分离 transmission 与非 transmission 回归 |

---

## 7. 修订记录

- R1：建立阶段拆分、文件范围、完成门槛与回滚策略。
- R2：补充质量闸门、阶段细化任务、验证顺序与风险跟踪表。
- R3：补充 DoD、默认值来源策略、阶段附加任务和最终附加检查。

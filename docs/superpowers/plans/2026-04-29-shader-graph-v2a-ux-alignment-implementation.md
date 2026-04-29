# Shader Graph V2A（UX Alignment）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变 V1 运行时语义的前提下，完成 Shader Graph 编辑体验升级（搜索/快捷操作/错误定位/状态机），并将编辑器逻辑从 `renderer.cpp` 拆分为独立模块。  
**Architecture:** 采用“会话态 + UI 层 + 编译报告层 + renderer 薄适配”结构。先实现状态机与报告模型，再挂接 UI，最后做模块拆分与回归。  
**Tech Stack:** C++17, Dear ImGui + imnodes, existing shader graph validate/codegen/runtime pipeline, CMake.

---

## 0. 影响文件

**Create:**

- `src/shader_graph_editor_ui.h`
- `src/shader_graph_editor_ui.cpp`
- `src/shader_graph_editor_session.h`
- `src/shader_graph_editor_session.cpp`
- `src/shader_graph_compile_report.h`
- `src/shader_graph_compile_report.cpp`
- `tests/shader_graph_editor_session_tests.cpp`

**Modify:**

- `src/renderer.cpp`
- `src/editor_scene_state.h`
- `src/shader_graph_io.cpp`（可选 editorMeta 支持）
- `tests/CMakeLists.txt`
- `CMakeLists.txt`

---

## 1. Task 1: 先建会话态与状态机（TDD）

- [ ] **Step 1: 写失败测试**
  - `tests/shader_graph_editor_session_tests.cpp` 覆盖状态迁移：
    - `Clean -> Dirty -> CompiledNotApplied -> Applied`
    - 编译失败进入 `CompileFailed`
    - `Revert` 回到 `Clean`
- [ ] **Step 2: 运行测试确认失败**
  - `ctest -C Debug -R shader_graph_editor_session_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure`
- [ ] **Step 3: 最小实现**
  - 在 `shader_graph_editor_session.*` 中实现状态机与迁移 API。
- [ ] **Step 4: 复跑测试确认通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): add editor session state machine`

---

## 2. Task 2: 编译报告结构与节点定位映射

- [ ] **Step 1: 写失败测试**
  - 报告消息支持 `severity/nodeId/phase/text`；
  - 可按节点查询消息；
  - 支持第一条 error 快速定位。
- [ ] **Step 2: 运行测试确认失败**
- [ ] **Step 3: 最小实现**
  - `shader_graph_compile_report.*` 实现聚合与查询接口。
- [ ] **Step 4: 测试通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): add compile report model with node mapping`

---

## 3. Task 3: UI 节点库搜索与快捷操作

- [ ] **Step 1: 手工失败用例**
  - 不能快速搜索节点；
  - 无复制/粘贴/删除快捷键。
- [ ] **Step 2: 最小实现**
  - `shader_graph_editor_ui.*` 添加节点搜索框；
  - 支持 `Ctrl+C/Ctrl+V/Delete`（按现有输入系统约束）；
  - 最近使用节点列表（会话内）。
- [ ] **Step 3: 连接提示**
  - 创建连线时给出类型兼容提示（不兼容阻止连接）。
- [ ] **Step 4: 手工验证**
  - 节点搜索可定位并创建；
  - 快捷键行为正确；
  - 类型不匹配有反馈。
- [ ] **Step 5: 提交**
  - `feat(editor): improve shader graph node library and shortcuts`

---

## 4. Task 4: 编译日志面板与错误跳转

- [ ] **Step 1: 手工失败用例**
  - 编译失败时无法直接定位问题节点。
- [ ] **Step 2: 最小实现**
  - 日志区显示 Error/Warning/Info；
  - 点击消息时聚焦节点并高亮。
- [ ] **Step 3: 编译按钮流转**
  - `Compile` 写入报告与状态；
  - 失败进入 `CompileFailed`。
- [ ] **Step 4: 手工验证**
  - 构造非法图，验证日志可跳转；
  - 修复后重新编译可恢复。
- [ ] **Step 5: 提交**
  - `feat(editor): add compile log panel and node-focused diagnostics`

---

## 5. Task 5: Apply/Revert 工作流落地

- [ ] **Step 1: 实现按钮状态绑定**
  - `Compile` 仅在 `Dirty/CompileFailed` 可用；
  - `Apply` 仅在 `CompiledNotApplied` 可用；
  - `Revert` 在 `Dirty/CompileFailed/CompiledNotApplied` 可用。
- [ ] **Step 2: 运行时桥接**
  - `Apply` 才触发 runtime graph 生效；
  - `Revert` 回滚到上次已应用版本。
- [ ] **Step 3: 手工验证**
  - 改图但不 apply 不影响运行时；
  - revert 后回到已应用版本。
- [ ] **Step 4: 提交**
  - `feat(shader-graph): add explicit compile apply revert workflow`

---

## 6. Task 6: 从 renderer.cpp 拆分模块

- [ ] **Step 1: 抽取 UI 与会话逻辑**
  - `renderer.cpp` 仅保留 `DrawShaderGraphEditorBridge(...)` 调用。
- [ ] **Step 2: 清理依赖**
  - 避免新模块反向依赖渲染主循环内部细节。
- [ ] **Step 3: 编译验证**
  - `cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor`
- [ ] **Step 4: 提交**
  - `refactor(editor): extract shader graph editor logic from renderer`

---

## 7. Task 7: editorMeta 可选持久化

- [ ] **Step 1: 扩展 IO**
  - 在 graph json 增加可选 `editorMeta` 字段读写；
  - 缺失字段保持兼容。
- [ ] **Step 2: round-trip 验证**
  - 老文件加载不报错；
  - 新文件保存后可恢复视图状态。
- [ ] **Step 3: 提交**
  - `feat(shader-graph): add optional editor metadata persistence`

---

## 8. Task 8: 回归与收尾

- [ ] **Step 1: 构建**
  - `cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor`
- [ ] **Step 2: 关键测试**
  - `ctest -C Debug -R shader_graph_ --test-dir d:/CMakeRepos/kaleido/build --output-on-failure`
- [ ] **Step 3: 手工 smoke**
  - `time_noise.kshadergraph.json` 全流程：打开->编辑->编译->apply->revert。
- [ ] **Step 4: 清理临时输出并检查 git 状态**
- [ ] **Step 5: 最终修复提交（如需要）**
  - `fix(shader-graph): finalize v2a ux alignment regression fixes`

---

## 9. 质量门禁

1. 节点库支持搜索与分类浏览。  
2. 编译错误可点击定位到具体节点。  
3. `Compile/Apply/Revert` 状态机行为一致。  
4. `renderer.cpp` 不再承载主要图编辑细节。  
5. V1 资产兼容通过，既有渲染路径无非预期变化。  
6. 全部相关测试与构建通过。

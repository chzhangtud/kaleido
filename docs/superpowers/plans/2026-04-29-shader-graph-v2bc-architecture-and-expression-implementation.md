# Shader Graph V2B/V2C（Architecture + Expression）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Shader Graph 多阶段能力（Vertex/Fragment）与受控 Expression 节点，完成 v1->v2 迁移链路，并通过单测与视口回归。  
**Architecture:** 在现有 V1 基础上扩展 `Graph IR + Validate + Codegen + Runtime Hook + IO` 五层。先做 IR/校验，再做 codegen/runtime，最后做编辑器与迁移。  
**Tech Stack:** C++17, RapidJSON, Dear ImGui + imnodes, GLSL/SPIR-V pipeline, existing kaleido editor/runtime.

---

## 0. 影响文件

**Create:**

- `src/shader_graph_stage_types.h`
- `src/shader_graph_stage_types.cpp`
- `src/shader_graph_migration.h`
- `src/shader_graph_migration.cpp`
- `tests/shader_graph_stage_validate_tests.cpp`
- `tests/shader_graph_expression_tests.cpp`
- `testcases/ABeautifulGame_ShaderGraph_V2_StageVarying/scene.json`
- `testcases/ABeautifulGame_ShaderGraph_V2_StageVarying/scene.png`
- `testcases/ABeautifulGame_ShaderGraph_V2_Expression/scene.json`
- `testcases/ABeautifulGame_ShaderGraph_V2_Expression/scene.png`

**Modify:**

- `src/shader_graph_types.h/.cpp`
- `src/shader_graph_validate.h/.cpp`
- `src/shader_graph_codegen_glsl.h/.cpp`
- `src/shader_graph_io.h/.cpp`
- `src/shaders/mesh.frag.glsl`
- `src/shaders/mesh.vert.glsl`（若当前无 hook 则新增）
- `src/renderer.cpp`
- `tests/CMakeLists.txt`
- `CMakeLists.txt`
- `README.md`

---

## 1. Task 1: 扩展 Graph IR 到多 stage（TDD）

- [ ] **Step 1: 写失败测试**
  - `ShaderGraphAsset.version == 2` 时可持有 `vertex/fragment` 子图；
  - varying 声明结构可构建。
- [ ] **Step 2: 跑测试确认失败**
  - `ctest -C Debug -R shader_graph_stage_validate_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure`
- [ ] **Step 3: 最小实现**
  - 新增 `SGStage`, `SGSubGraph`, `SGVaryingDecl`；
  - 将 v1 单图模型兼容映射到 fragment 子图。
- [ ] **Step 4: 测试通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): add multi-stage graph ir for v2`

---

## 2. Task 2: stage + varying 校验器扩展

- [ ] **Step 1: 写失败测试**
  - varying 同名不同类型应失败；
  - `GetVarying` 无对应 `SetVarying` 应失败；
  - stage 内环检测仍有效。
- [ ] **Step 2: 跑测试确认失败**
- [ ] **Step 3: 最小实现**
  - `ValidateShaderGraph` 扩展 stage 级与 stage 间检查；
  - 输出可读错误文本（含 stage 与 varying 名）。
- [ ] **Step 4: 测试通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): add stage varying validation rules`

---

## 3. Task 3: Expression 节点模型与白名单校验

- [ ] **Step 1: 写失败测试**
  - 合法表达式可通过；
  - 使用禁用标识符/语句时报错；
  - 端口定义与表达式引用不一致时报错。
- [ ] **Step 2: 跑测试确认失败**
- [ ] **Step 3: 最小实现**
  - `SGNodeOp::Expression` 与 `SGNodeOp::GlobalExpression`；
  - 端口定义结构（输入/输出）；
  - 白名单校验器（词法级 + 规则级）。
- [ ] **Step 4: 测试通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): add controlled expression node model`

---

## 4. Task 4: Codegen 扩展（vertex/fragment + expression）

- [ ] **Step 1: 写失败测试**
  - 生成结果包含 `sg_eval_vertex` 与 `sg_eval_fragment`；
  - varying 声明与读写代码存在；
  - expression 发射变量名稳定。
- [ ] **Step 2: 跑测试确认失败**
  - `ctest -C Debug -R shader_graph_codegen_tests --test-dir d:/CMakeRepos/kaleido/build --output-on-failure`
- [ ] **Step 3: 最小实现**
  - 分 stage 拓扑与代码发射；
  - global expression 仅注入一次；
  - 变量前缀规范化避免冲突。
- [ ] **Step 4: 测试通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): extend glsl codegen for stages and expressions`

---

## 5. Task 5: Runtime hook 接入 vertex/fragment

- [ ] **Step 1: 写失败检查**
  - 在 shader hook 接口中先引用新入口函数，确认编译失败。
- [ ] **Step 2: 最小实现**
  - `mesh.vert.glsl` 增加 vertex graph hook；
  - `mesh.frag.glsl` 接收 varying 并执行 fragment graph；
  - `renderer.cpp` 上传新参数布局。
- [ ] **Step 3: 编译验证**
  - `cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor`
- [ ] **Step 4: 手工 smoke**
  - 运行示例场景，确认无崩溃、画面随 graph 变化。
- [ ] **Step 5: 提交**
  - `feat(shader-graph): wire vertex fragment stage runtime hooks`

---

## 6. Task 6: IO 升级与 v1->v2 迁移器

- [ ] **Step 1: 写失败测试**
  - v1 json 可加载并迁移成 v2；
  - 迁移后节点落在 fragment stage；
  - 迁移失败时给出明确错误。
- [ ] **Step 2: 跑测试确认失败**
- [ ] **Step 3: 最小实现**
  - `DeserializeShaderGraphFromJson` 支持 v1/v2；
  - 独立 `UpgradeShaderGraphV1ToV2(...)`。
- [ ] **Step 4: round-trip 测试通过**
- [ ] **Step 5: 提交**
  - `feat(shader-graph): add v1 to v2 graph migration path`

---

## 7. Task 7: 编辑器 UI 支持 stage 与 expression

- [ ] **Step 1: 手工失败用例**
  - 无 stage 切换页签；
  - 无 varying 管理；
  - 无 expression 端口编辑。
- [ ] **Step 2: 最小实现**
  - Graph Editor 增加 `Vertex/Fragment` 切换；
  - varying 列表编辑（新增/删除/类型）；
  - expression 节点属性面板（端口与正文）。
- [ ] **Step 3: 错误反馈**
  - expression 解析错误定位到节点。
- [ ] **Step 4: 手工验证**
  - 新建 varying 并连通 vertex->fragment；
  - expression 节点可编译并生效。
- [ ] **Step 5: 提交**
  - `feat(editor): add stage tabs varying manager and expression editing`

---

## 8. Task 8: 新 testcase 与图像回归

- [ ] **Step 1: 新增 StageVarying 场景**
  - `ABeautifulGame_ShaderGraph_V2_StageVarying`
- [ ] **Step 2: 新增 Expression 场景**
  - `ABeautifulGame_ShaderGraph_V2_Expression`
- [ ] **Step 3: 导出 golden**
  - 用 `kaleido_editor` 自动导出 PNG 并固化为 `scene.png`。
- [ ] **Step 4: 比对验证**
  - 使用既有 PNG compare 脚本回归验证。
- [ ] **Step 5: 提交**
  - `test(shader-graph): add v2 stage varying and expression regressions`

---

## 9. Task 9: 全量回归与收尾

- [ ] **Step 1: 全量构建**
  - `cmake --build d:/CMakeRepos/kaleido/build --config Debug --target kaleido_editor kaleido_standalone`
- [ ] **Step 2: 关键单测**
  - `ctest -C Debug -R shader_graph_ --test-dir d:/CMakeRepos/kaleido/build --output-on-failure`
- [ ] **Step 3: 关键场景回归**
  - `ABeautifulGame`
  - `ABeautifulGame_Material_Edit`
  - 新增两个 V2 场景
- [ ] **Step 4: 文档更新**
  - `README.md` 增补 V2 用法与迁移说明。
- [ ] **Step 5: 最终修复提交（如需要）**
  - `fix(shader-graph): finalize v2bc regression and migration fixes`

---

## 10. 质量门禁

1. 双 stage 图编译与运行链路可用。  
2. varying 校验与错误提示完整可读。  
3. expression 节点在白名单约束内可稳定生成代码。  
4. v1 资产可迁移读取，不崩溃且行为可解释。  
5. V2 新增 testcase 回归通过。  
6. 既有核心 testcase 无非预期漂移。

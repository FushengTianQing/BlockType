# Phase 2 — Cargo 项目编译

> **目标**：`bt build`（无参数）可编译完整 Cargo 项目
> **估计**：~7 周（36 天）| 3 个 Sprint | 10 个 Task
> **里程碑**：`bt build` 编译 Cargo 项目，含依赖、feature、多 crate

---

## Sprint 2-1: 完整依赖解析（2 周）

### T-02-01: bt-cargo — 完整依赖解析 + 拓扑排序

- **ID**: `T-02-01`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-02-1

**描述**：在 Phase 0 基础上完善 bt-cargo，支持多 crate workspace 的完整依赖解析和拓扑排序。

**产出文件**：
- `crates/bt-cargo/src/workspace.rs` — 完善
- `crates/bt-cargo/src/dependency.rs` — 完善
- `crates/bt-cargo/tests/workspace_test.rs`

**设计规格**（REF）：
- `10-Phased-Roadmap.md §Phase 2`

**前置依赖**（DEP）：
- `T-00-08`：bt-cargo 基础

**验收标准**（TST）：
- [ ] 解析 3 crate workspace 的 Cargo.toml
- [ ] 构建完整 DependencyGraph（含 transitive deps）
- [ ] 拓扑排序正确（被依赖的先编译）
- [ ] 处理循环依赖报错
- [ ] 支持 path/git/registry 三种依赖源
- [ ] MVP：3 crate 工作空间编译成功

---

### T-02-02: bt-cargo — Feature gate + optional 依赖

- **ID**: `T-02-02`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P0
- **Sprint**: S-02-1

**描述**：feature gate 和 optional 依赖的完整支持。

**产出文件**：
- `crates/bt-cargo/src/feature.rs` — 完善

**前置依赖**（DEP）：
- `T-02-01`

**验收标准**（TST）：
- [ ] 解析 `[features]` 节：default/dev/自定义
- [ ] feature 组合：`feature A implies B`
- [ ] optional 依赖激活：`features = ["serde"]` 启用 optional dep
- [ ] BuildPlan 根据 feature 组合决定编译哪些 crate
- [ ] 测试：含 feature 的 crate 正确编译

---

### T-02-03: bt-rustc-bridge — 复杂类型转换

- **ID**: `T-02-03`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-02-1

**描述**：MIR→BTIR 复杂类型转换：trait object / closure / async / generator。

**产出文件**：
- `crates/bt-rustc-bridge/src/type_map.rs` — 补充复杂类型
- `crates/bt-rustc-bridge/src/mir_lower.rs` — 补充复杂 MIR 结构

**设计规格**（REF）：
- `08-Rust-Ecosystem-Integration.md §8.4`

**前置依赖**（DEP）：
- `T-00-11`：基础 MIR→BTIR

**验收标准**（TST）：
- [ ] `Ty::Dynamic` (trait object) → IRType::TraitObject
- [ ] `Ty::Closure` → IRType::Closure + capture 结构
- [ ] `Ty::Generator` / `Ty::GeneratorWitness` → coroutine 状态机
- [ ] `Ty::Param` (泛型参数) → IRType::Generic
- [ ] 含泛型的项目编译通过

---

## Sprint 2-2: 增量编译（2 周）

### T-02-04: 多 crate 增量编译 + 脏标记

- **ID**: `T-02-04`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-02-2

**描述**：基于 Salsa 查询引擎实现多 crate 增量编译。

**产出文件**：
- `crates/bt-cargo/src/incremental.rs`
- `crates/bt-query/src/engine.rs` — 增量支持

**前置依赖**（DEP）：
- `T-01-07`：bt-query
- `T-02-01`：完整依赖解析

**验收标准**（TST）：
- [ ] `dirty_crates(changed_files)` 计算受影响 crate
- [ ] 文件修改后只重编译受影响的 crate
- [ ] 依赖图传播脏标记
- [ ] Salsa 查询引擎自动追踪和失效
- [ ] 测试：改一个文件只重编译受影响 crate

---

### T-02-05: bt-cargo — BuildPlan 执行

- **ID**: `T-02-05`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-02-2

**描述**：BuildPlan 执行引擎，按拓扑顺序编译每个 crate。

**产出文件**：
- `crates/bt-cargo/src/build_executor.rs`

**前置依赖**（DEP）：
- `T-02-01`, `T-02-04`

**设计规格**（REF）：
- `08-Rust-Ecosystem-Integration.md §8.3.1` — bt-cargo BuildPlan

**验收标准**（TST）：
- [ ] `BuildExecutor::execute(plan)` 按序编译
- [ ] 每个 crate 调用 bt-rustc-bridge 编译
- [ ] 编译结果缓存（Salsa）
- [ ] 错误中断后续编译
- [ ] 进度事件推送到 EventStore

---

### T-02-06: bt-backend-llvm — 基础 LLVM 代码生成

- **ID**: `T-02-06`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-02-2

**描述**：实现 LLVM 后端基础代码生成（x86_64 目标）。

**产出文件**：
- `crates/bt-backend-llvm/src/{lib.rs, backend.rs, codegen.rs, type_conv.rs}`

**设计规格**（REF）：
- `02-Core-Types.md §7.7` — Backend trait

**前置依赖**（DEP）：
- `T-00-15`：bt-backend-common
- `T-01-01`：完整 IRType

**验收标准**（TST）：
- [ ] `LlvmBackend` impl `Backend` trait
- [ ] `emit_object()` 输出 .o 文件
- [ ] `targets()` 返回 `["x86_64-unknown-linux-gnu"]`
- [ ] IRType → LLVM 类型映射
- [ ] IRInstruction → LLVM IR 映射（基础指令）
- [ ] 函数代码生成
- [ ] `cargo test -p bt-backend-llvm` 通过

---

## Sprint 2-3: 端到端 Cargo 项目编译（2 周）

### T-02-07: bt-cli — `bt build` Cargo 项目

- **ID**: `T-02-07`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-02-3

**描述**：`bt build`（无参数）自动检测 Cargo.toml 并编译项目。

**产出文件**：
- `crates/bt-cli/src/commands/build.rs` — 更新

**前置依赖**（DEP）：
- `T-02-05`, `T-02-06`

**验收标准**（TST）：
- [ ] `bt build` 检测当前目录 Cargo.toml
- [ ] 解析依赖 → 拓扑排序 → 逐 crate 编译 → 链接
- [ ] 输出可执行文件
- [ ] 错误友好提示

---

### T-02-08: 链接器集成

- **ID**: `T-02-08`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-02-3

**描述**：集成系统链接器（ld/lld），将 .o 文件链接为可执行文件。

**产出文件**：
- `crates/bt-backend-llvm/src/linker.rs`

**前置依赖**（DEP）：
- `T-02-06`

**验收标准**（TST）：
- [ ] 调用 `cc` 或 `lld` 链接 .o 文件
- [ ] 链接标准库（通过 bt-std-bridge）
- [ ] 生成可执行文件

---

### T-02-09: 端到端集成测试 — Cargo 项目编译

- **ID**: `T-02-09`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-02-3

**描述**：端到端测试：`bt build` 编译含依赖、feature、多 crate 的项目。

**产出文件**：
- `tests/integration/test_cargo_build.rs`
- `tests/fixtures/cargo_project/` — 测试项目

**前置依赖**（DEP）：
- `T-02-07`, `T-02-08`

**验收标准**（TST）：
- [ ] 3 crate workspace 编译成功
- [ ] feature gate 编译正确
- [ ] 改一个文件只重编译受影响 crate
- [ ] 含泛型的项目编译通过

---

### T-02-10: Phase 2 回归测试

- **ID**: `T-02-10`
- **状态**: TODO
- **估计**: 1 天
- **优先级**: P1
- **Sprint**: S-02-3

**验收标准**（TST）：
- [ ] `cargo build/test/fmt/clippy --workspace` 通过
- [ ] `bt build` 编译 Cargo 项目全流程通过

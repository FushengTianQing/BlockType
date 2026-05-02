# Phase 0 — 基础设施 + rustc 桥接 + 前端探针 MVP

> **目标**：Cargo workspace + tower::Service + 可观测骨架 + rustc_driver 集成 + `println!("hello")` 编译运行
> **估计**：~9 周（47 天）| 4 个 Sprint | 18 个 Task
> **里程碑**：`bt build hello.rs` 可编译运行使用 `std::println` 的程序

---

## Sprint 0-1: 项目骨架 + bt-core + bt-service（2 周）

### T-00-01: Cargo Workspace 搭建

- **ID**: `T-00-01`
- **状态**: TODO
- **估计**: 1 天
- **优先级**: P0
- **Sprint**: S-00-1

**描述**：创建 blocktype-next 项目根目录，初始化 Cargo workspace，配置 nightly 工具链。

**产出文件**：
- `blocktype-next/Cargo.toml` — workspace 根配置
- `blocktype-next/rust-toolchain.toml` — nightly 日期锁定 + rustc-dev 组件
- `blocktype-next/.cargo/config.toml` — cargo 配置
- `blocktype-next/.gitignore`
- `blocktype-next/crates/` — 空 crate 目录占位

**设计规格**（REF）：
- `04-Project-Structure.md §4.1` — Workspace 布局
- `04-Project-Structure.md §4.2` — Crate 依赖关系

**前置依赖**（DEP）：无

**验收标准**（TST）：
- [ ] `rust-toolchain.toml` 锁定 nightly 版本，含 `[components] rustc-dev`
- [ ] `Cargo.toml` 中 `[workspace]` 含所有 24 个 crate 的 `members`
- [ ] `cargo build` 可通过（所有 crate 为空 lib）
- [ ] 目录结构与 §4.1 完全一致

**实现步骤**：
1. 创建 `blocktype-next/` 根目录
2. 编写 `rust-toolchain.toml`：`[toolchain] channel = "nightly-2026-04-15"`, `components = ["rustc-dev", "llvm-tools"]`
3. 编写 `Cargo.toml`：workspace 配置，列出所有 24 个 member
4. 创建 `crates/` 下每个 crate 的空目录和最小 `Cargo.toml` + `src/lib.rs`
5. 运行 `cargo build` 验证

---

### T-00-02: bt-core — 基础类型与诊断

- **ID**: `T-00-02`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-00-1

**描述**：实现 bt-core crate，包含核心类型、诊断系统、SourceManager、TargetTriple、错误码体系。

**产出文件**：
- `crates/bt-core/src/lib.rs` — 模块导出
- `crates/bt-core/src/diagnostic.rs` — Diagnostic / Severity / SourceSpan
- `crates/bt-core/src/source.rs` — SourceManager / SourceId
- `crates/bt-core/src/target.rs` — TargetTriple 解析
- `crates/bt-core/src/error.rs` — CompilerError + 子错误类型 + 错误码 E0xxx-E4xxx
- `crates/bt-core/src/named.rs` — Named trait（Send + Sync）

**设计规格**（REF）：
- `02-Core-Types.md §7.1` — Named trait
- `03-Communication-Bus.md §3.10` — 错误传播、CompilerError 枚举
- `05-Unified-API.md §2.9` — 错误码体系 E0xxx-E4xxx
- `05-Unified-API.md §2.10.8` — 认证错误码 E3003-E3006
- `04-Project-Structure.md §4.4` — bt-core 职责

**前置依赖**（DEP）：
- `T-00-01`：Cargo workspace

**验收标准**（TST）：
- [ ] `Diagnostic` 含 severity/code/message/location/suggestions 字段
- [ ] `Severity` 枚举含 Error/Warning/Info/Note
- [ ] `SourceSpan` 含 file/line/col/endLine/endCol
- [ ] `SourceManager` 支持 add_source(id, content) / get_source(id)
- [ ] `TargetTriple` 支持 from_str("x86_64-unknown-linux-gnu")
- [ ] `CompilerError` 含 Frontend/IRVerification/Dialect/Backend/Timeout/Pass/AI 变体
- [ ] `Named` trait: `fn name(&self) -> &str` + Send + Sync
- [ ] 错误码映射 `error_code()` / `http_status()` 方法
- [ ] `thiserror` derive 所有错误类型
- [ ] 单元测试覆盖所有公开 API

**实现步骤**：
1. 在 `crates/bt-core/Cargo.toml` 添加依赖：`thiserror`, `serde`, `uuid`, `chrono`
2. 实现 `named.rs`：Named trait
3. 实现 `diagnostic.rs`：Diagnostic, Severity, SourceSpan
4. 实现 `source.rs`：SourceManager（HashMap<SourceId, String>）
5. 实现 `target.rs`：TargetTriple 解析
6. 实现 `error.rs`：CompilerError + 各子错误类型 + error_code() + http_status()
7. 编写 `lib.rs` 统一导出
8. 编写测试

---

### T-00-03: bt-core — Registry 通用注册表

- **ID**: `T-00-03`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-00-1

**描述**：实现通用 Registry<T>，用于 Frontend/Backend/Dialect/Pass 的运行时注册。

**产出文件**：
- `crates/bt-core/src/registry.rs` — Registry<T> + RegistryError
- `crates/bt-core/src/registry_ext.rs` — DST 注册辅助

**设计规格**（REF）：
- `03-Communication-Bus.md §3.8.1` — Registry<T> 定义
- `03-Communication-Bus.md §3.8.2` — DST 注册辅助
- `03-Communication-Bus.md §3.8.3` — 类型别名

**前置依赖**（DEP）：
- `T-00-02`：bt-core 基础（Named trait）

**验收标准**（TST）：
- [ ] `Registry<T: ?Sized + Named>` 含 `register(Arc<T>)` / `unregister(name)` / `get(name)` / `list()`
- [ ] 线程安全：内部使用 `RwLock<HashMap<String, Arc<T>>>`
- [ ] 重复注册返回 `RegistryError::AlreadyExists`
- [ ] 注销不存在项返回 `RegistryError::NotFound`
- [ ] `register_boxed(Box<dyn Trait>)` 可编译
- [ ] `register_concrete<T: Trait + 'static>(T)` 可编译
- [ ] 单元测试覆盖：注册/查找/注销/重复注册/并发访问

---

### T-00-04: bt-service — CompileService + tower::Service

- **ID**: `T-00-04`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-00-1

**描述**：实现 CompileService（tower::Service 抽象）和中间件链。

**产出文件**：
- `crates/bt-service/src/lib.rs`
- `crates/bt-service/src/compile_service.rs` — CompileService + tower::Service impl
- `crates/bt-service/src/request.rs` — CompileRequest / CompileResponse
- `crates/bt-service/src/middleware.rs` — 中间件链

**设计规格**（REF）：
- `03-Communication-Bus.md §3.3.1` — CompileService 定义
- `03-Communication-Bus.md §3.3.2` — tower::Service impl
- `03-Communication-Bus.md §3.4` — 中间件链
- `03-Communication-Bus.md §3.5` — axum handler 集成

**前置依赖**（DEP）：
- `T-00-02`：bt-core
- `T-00-03`：Registry

**验收标准**（TST）：
- [ ] `CompileService` 含所有 6 个 Arc 字段 + `#[derive(Clone)]`
- [ ] `impl Service<CompileRequest> for CompileService`
- [ ] `call(&mut self)` 委托到 `compile(&self)`
- [ ] `build_compile_service()`: rate_limit(100/s) + timeout(5min) + retry
- [ ] `build_ai_service()`: rate_limit(20/s) + timeout(60s)
- [ ] `cargo test -p bt-service` 通过

---

### T-00-05: bt-event-store — Event Sourcing

- **ID**: `T-00-05`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-00-1

**描述**：实现 EventStore，支持事件持久化、回放、导出、实时流订阅。

**产出文件**：
- `crates/bt-event-store/src/lib.rs`
- `crates/bt-event-store/src/event.rs` — CompilerEvent + StoredEvent
- `crates/bt-event-store/src/store.rs` — EventStore
- `crates/bt-event-store/src/filter.rs` — EventFilter

**设计规格**（REF）：
- `03-Communication-Bus.md §3.6` — EventStore 完整设计

**前置依赖**（DEP）：
- `T-00-02`：bt-core

**验收标准**（TST）：
- [ ] `CompilerEvent` 含全部变体（CompilationStarted/Progress/Completed/Failed + 管线节点 + Dialect + AI + 系统）
- [ ] `StoredEvent` 含 sequence/timestamp/event/trace_id
- [ ] `append/replay_task/recent/export/subscribe_stream` 全部实现
- [ ] 内部 broadcaster + RwLock + AtomicU64
- [ ] 线程安全

---

### T-00-06: bt-telemetry — OpenTelemetry 基础

- **ID**: `T-00-06`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P1
- **Sprint**: S-00-1

**描述**：OpenTelemetry 基础 Span 集成。

**产出文件**：
- `crates/bt-telemetry/src/lib.rs`
- `crates/bt-telemetry/src/span.rs` — Span 工具
- `crates/bt-telemetry/src/metrics.rs` — 基础指标

**设计规格**（REF）：
- `06-Observability.md §4.1`

**前置依赖**（DEP）：
- `T-00-02`：bt-core

**验收标准**（TST）：
- [ ] `init_tracer()` + `span_for_phase(phase, task_id)`
- [ ] `PipelinePhase` 枚举
- [ ] compile_duration_ms / phase_duration_ms / active_compilations 指标

---

## Sprint 0-2: bt-api + bt-cargo（2 周）

### T-00-07: bt-api — axum HTTP 服务器 + 编译 API

- **ID**: `T-00-07`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P0
- **Sprint**: S-00-2

**描述**：axum HTTP 服务器，暴露 `/api/v1/compile/*`。

**产出文件**：
- `crates/bt-api/src/lib.rs`
- `crates/bt-api/src/server.rs` — Router + 启动
- `crates/bt-api/src/handlers/compile.rs` — 编译 handler
- `crates/bt-api/src/response.rs` — 统一响应格式
- `crates/bt-api/src/error.rs` — CompilerError → IntoResponse

**设计规格**（REF）：
- `05-Unified-API.md §2.2` — 编译 API 端点
- `05-Unified-API.md §2.8` — 统一响应格式
- `03-Communication-Bus.md §3.5` — axum handler
- `03-Communication-Bus.md §3.10.2` — IntoResponse

**前置依赖**（DEP）：
- `T-00-04`：bt-service
- `T-00-05`：bt-event-store

**验收标准**（TST）：
- [ ] `POST /api/v1/compile` + 分阶段端点 + 任务查询/取消
- [ ] 统一响应格式含 request_id/timestamp/trace_id/status/data/diagnostics/metrics
- [ ] `CompilerError → IntoResponse` 自动 HTTP 映射
- [ ] 集成测试通过

---

### T-00-08: bt-cargo — Cargo.toml 解析 + 依赖图

- **ID**: `T-00-08`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-00-2

**描述**：Cargo 工作空间解析、依赖图、拓扑排序、feature gate。

**产出文件**：
- `crates/bt-cargo/src/lib.rs`
- `crates/bt-cargo/src/workspace.rs`
- `crates/bt-cargo/src/dependency.rs`
- `crates/bt-cargo/src/feature.rs`
- `crates/bt-cargo/src/build_plan.rs`

**设计规格**（REF）：
- `10-Phased-Roadmap.md §Phase 2`
- `04-Project-Structure.md §4.4`

**前置依赖**（DEP）：
- `T-00-01`：Cargo workspace

**验收标准**（TST）：
- [ ] `CargoWorkspace::from_path()` 解析
- [ ] `DependencyGraph` + `topological_sort()`
- [ ] `FeatureGate` 解析 + optional dep
- [ ] `BuildPlan` 生成
- [ ] 使用 `cargo_metadata`
- [ ] 测试：3 crate workspace

---

## Sprint 0-3: bt-rustc-bridge + bt-ir 最小子集（2 周）

### T-00-09: bt-rustc-bridge — sysroot + extern crate

- **ID**: `T-00-09`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-00-3

**描述**：搭建 bt-rustc-bridge 骨架，配置 sysroot。

**产出文件**：
- `crates/bt-rustc-bridge/src/lib.rs` — #![feature(rustc_private)] + extern crate
- `crates/bt-rustc-bridge/src/bridge.rs` — BtRustcBridge 入口
- `crates/bt-rustc-bridge/src/callbacks.rs` — BtCallbacks 空壳

**设计规格**（REF）：
- `08-Rust-Ecosystem-Integration.md §8.4`
- `09-Frontend-Innovation-Extension.md §9.3.1`

**前置依赖**（DEP）：
- `T-00-01`, `T-00-02`

**验收标准**（TST）：
- [ ] `#![feature(rustc_private)]` + 8 个 extern crate 声明
- [ ] `BtRustcBridge::new()` + `compile(source)`
- [ ] `cargo build -p bt-rustc-bridge` 通过

---

### T-00-10: bt-ir — 最小子集（Phase 0 用）

- **ID**: `T-00-10`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-00-3

**描述**：bt-ir 最小子集：基础 IRType、IRInstruction、IRModule、IRBuilder。

**产出文件**：
- `crates/bt-ir/src/{lib.rs, types.rs, instruction.rs, value.rs, module.rs, builder.rs}`

**设计规格**（REF）：
- `02-Core-Types.md §7.2-7.5`

**前置依赖**（DEP）：
- `T-00-02`

**验收标准**（TST）：
- [ ] IRType 基础变体 + IRInstruction 基础指令
- [ ] IRModule/IRFunction/IRBasicBlock + IRBuilder
- [ ] derive Serialize/Deserialize
- [ ] `cargo test -p bt-ir` 通过

---

### T-00-11: bt-rustc-bridge — MIR → BTIR 基础转换

- **ID**: `T-00-11`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-00-3

**描述**：MIR 到 BTIR 的基础类型映射和控制流转换。

**产出文件**：
- `crates/bt-rustc-bridge/src/mir_lower.rs`
- `crates/bt-rustc-bridge/src/type_map.rs`
- `crates/bt-rustc-bridge/src/terminator.rs`

**设计规格**（REF）：
- `08-Rust-Ecosystem-Integration.md §8.4`
- `02-Core-Types.md §7.2-7.3`

**前置依赖**（DEP）：
- `T-00-09`, `T-00-10`

**验收标准**（TST）：
- [ ] rustc Ty → IRType 基础映射（Int/Uint/Float/Bool/Adt/FnDef）
- [ ] MIR terminator → IRInstruction（Goto→Branch, Return→Ret, Call→Call）
- [ ] `cargo build -p bt-rustc-bridge` 通过

---

### T-00-12: bt-std-bridge — sysroot 检测

- **ID**: `T-00-12`
- **状态**: TODO
- **估计**: 1 天
- **优先级**: P1
- **Sprint**: S-00-3

**描述**：rustup sysroot 路径检测和标准库链接。

**产出文件**：
- `crates/bt-std-bridge/src/{lib.rs, sysroot.rs}`

**设计规格**（REF）：
- `04-Project-Structure.md §4.4`

**前置依赖**（DEP）：
- `T-00-02`

**验收标准**（TST）：
- [ ] `detect_sysroot()` + `StdBridge::lib_paths()`

---

## Sprint 0-4: 探针 + 集成测试（2 周）

### T-00-13: BtCallbacks 探针 MVP

- **ID**: `T-00-13`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-00-4

**描述**：BtCallbacks 拦截 after_parsing/after_expansion/after_analysis，记录事件。

**产出文件**：
- `crates/bt-rustc-bridge/src/callbacks.rs` — 完整实现
- `crates/bt-rustc-bridge/src/diagnostic_emitter.rs` — BtDiagnosticEmitter

**设计规格**（REF）：
- `09-Frontend-Innovation-Extension.md §9.3.1-9.3.2`

**前置依赖**（DEP）：
- `T-00-09`, `T-00-05`

**验收标准**（TST）：
- [ ] impl `rustc_driver::Callbacks`：after_parsing/after_expansion/after_analysis
- [ ] 每个回调创建 Span + 记录事件
- [ ] BtDiagnosticEmitter 转换诊断

---

### T-00-14: bt-cli — CLI 入口

- **ID**: `T-00-14`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-00-4

**描述**：bt-cli，支持 `bt build`、`bt serve`、`bt check`、`bt explain`。

**产出文件**：
- `crates/bt-cli/src/{main.rs, commands/build.rs, commands/serve.rs, commands/check.rs, commands/explain.rs}`

**设计规格**（REF）：
- `08-Rust-Ecosystem-Integration.md §8.7` — CLI 设计

**前置依赖**（DEP）：
- `T-00-04`, `T-00-07`

**验收标准**（TST）：
- [ ] `bt build hello.rs` + `bt serve --port 8080` + `bt --help`
- [ ] `bt check src/main.rs` 类型检查（不生成代码）
- [ ] `bt explain E0502` 错误码解释

---

### T-00-15: bt-frontend-common + bt-backend-common

- **ID**: `T-00-15`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-00-4

**描述**：Frontend trait + Backend trait + Registry 类型别名。

**产出文件**：
- `crates/bt-frontend-common/src/{lib.rs, frontend.rs}`
- `crates/bt-backend-common/src/{lib.rs, backend.rs}`

**设计规格**（REF）：
- `02-Core-Types.md §7.6-7.7`

**前置依赖**（DEP）：
- `T-00-02`, `T-00-03`, `T-00-10`

**验收标准**（TST）：
- [ ] Frontend/Backend trait + Registry 类型别名 + DST 辅助

---

### T-00-16: bt-dialect-core — Dialect trait + bt_core Dialect

- **ID**: `T-00-16`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-00-4

**描述**：Dialect trait + bt_core 内建 Dialect。

**产出文件**：
- `crates/bt-dialect-core/src/{lib.rs, dialect.rs, core_dialect.rs}`

**设计规格**（REF）：
- `02-Core-Types.md §7.4`

**前置依赖**（DEP）：
- `T-00-03`, `T-00-10`

**验收标准**（TST）：
- [ ] Dialect trait + DialectRegistry + DialectRegistryExt + BtCoreDialect

---

### T-00-17: 集成测试 — bt build hello.rs

- **ID**: `T-00-17`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-00-4

**描述**：端到端集成测试。

**产出文件**：
- `tests/integration/test_hello_world.rs`
- `tests/integration/test_event_flow.rs`

**前置依赖**（DEP）：
- `T-00-11`, `T-00-13`, `T-00-14`

**验收标准**（TST）：
- [ ] `bt build hello.rs` → 可执行文件 → 输出 "Hello, world!"
- [ ] EventStore 记录全流程事件

---

### T-00-18: Phase 0 回归测试 + 文档

- **ID**: `T-00-18`
- **状态**: TODO
- **估计**: 1 天
- **优先级**: P1
- **Sprint**: S-00-4

**验收标准**（TST）：
- [ ] `cargo build/test/fmt/clippy --workspace` 全部通过
- [ ] README.md 更新

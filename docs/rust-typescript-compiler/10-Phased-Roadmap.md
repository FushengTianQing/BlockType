# 08 — 渐进式开发路线图

## 总览

```
Phase 0 ──── Phase 1 ──── Phase 2 ──── Phase 3 ──── Phase 4 ──── Phase 5
 基础设施      IR+Dialect   Cargo项目    Proc-Macro   AI+可观测    高级特性
 +rustc桥接   +Dialect     编译         +生态兼容    +LSP         +TS+插件
 +探针MVP     +查询引擎                 +Clippy整合
 (8周)        (5周)        (6周)        (8周)        (8周)        (10周)
                                                              总计 ~45周 (~11个月)
```

> **核心变更**：删除自有 Rust 前端（原 Phase 2 的 10 周），由 `bt-rustc-bridge` 复用 rustc 前端替代。
> 详见 [08-Rust-Ecosystem-Integration.md](./08-Rust-Ecosystem-Integration.md)。

## Phase 0: 基础设施 + rustc 桥接 + 前端探针 MVP（8 周）

**目标**：Cargo workspace + tower::Service + 可观测骨架 + rustc_driver 集成 + `println!("hello")` 编译运行 + 前端探针 MVP

| 任务 | 产出 | 周 |
|------|------|---|
| Cargo workspace 搭建 | 项目骨架 + CI + `rust-toolchain.toml` (nightly 日期锁定 + rustc-dev) | 0.5 |
| `bt-core` | 诊断、SourceManager、TargetTriple、错误码体系 (E0xxx-E4xxx) | 1 |
| `bt-service` | CompileService (tower::Service) + 中间件链 (限流/超时/重试) | 1 |
| `bt-api` | axum 服务器 + 编译操作 API (`/api/v1/compile/*`) | 0.5 |
| `bt-telemetry` | OpenTelemetry 基础 Span | 0.5 |
| `bt-event-store` | Event Sourcing 事件持久化 + 订阅 | 0.5 |
| `bt-cargo` | Cargo.toml 解析 + 依赖图 + cargo_metadata 集成 | 1.5 |
| `bt-rustc-bridge` 骨架 | sysroot 配置 + `#![feature(rustc_private)]` + `extern crate` 声明 + MIR 拦截 | 1.5 |
| `bt-rustc-bridge` 转换 | MIR → BTIR 基础转换（类型 + 函数 + 控制流） | (含上一行) |
| `bt-std-bridge` | rustup sysroot 检测 + 标准库链接 | 0.5 |
| `BtCallbacks` 探针 MVP | after_parsing + after_expansion + EventStore 记录 + Dashboard 前端进度 | 1 |
| `BtDiagnosticEmitter` | 诊断拦截 + 收集 + EventStore 记录 | (含上一行) |
| 集成测试 | `bt build hello.rs` 编译运行 `println!("hello")` | 0.5 |

**里程碑**：`bt build hello.rs` 可编译运行使用 `std::println` 的程序，事件可通过 WebSocket 推送，Dashboard 显示 parse/expand 前端阶段

## Phase 1: IR 核心 + Dialect 系统 + 查询引擎（5 周）

**目标**：完整的 BTIR 数据模型 + **运行时 Dialect 注册** + 序列化 + 验证 + 查询引擎

| 任务 | 产出 | 周 |
|------|------|---|
| `bt-ir` 核心类型 | IRType (含泛型/TraitObject/闭包), IRValue, IRInstruction, IRFunction, IRModule, IRBuilder | 2 |
| `bt-dialect-core` | Dialect trait + DialectRegistry (运行时注册) + OperationDef | 1 |
| `bt-dialect-rust` | bt_rust Dialect 定义 + lower_to_core | 0.5 |
| `bt-dialect-ts` | bt_ts Dialect 定义 + lower_to_core | 0.5 |
| `bt-ir-verifier` | IR 完整性验证 + Dialect 验证 | 0.5 |
| `bt-ir` 序列化 | JSON + 二进制序列化 | 0.5 |
| IR 操作 API | `/api/v1/ir/*` 端点 + Dialect 管理 | 0.5 |
| `bt-query` | Salsa 风格查询引擎骨架（从 Phase 0 移入，Phase 1 IR 就绪后才能集成） | 0.5 |

**里程碑**：可通过 API 构造、验证、序列化、查询 IR 模块；Dialect 可运行时注册/注销

## Phase 2: Cargo 项目编译（6 周）

**目标**：`bt build`（无参数）可编译完整 Cargo 项目

| 周 | 任务 | 产出 | MVP |
|---|------|------|-----|
| 1-2 | `bt-cargo`: 完整依赖解析 + 拓扑排序 | DependencyGraph | 3 crate 工作空间编译成功 |
| 3-4 | `bt-cargo`: Feature gate + optional 依赖 | BuildPlan | 有 feature 的 crate 正确编译 |
| 5 | 多 crate 增量编译 + 脏标记 | dirty_crates() | 改一个文件只重编译受影响 crate |
| 6 | `bt-rustc-bridge` 完善: 复杂类型转换 | trait object / closure / async | 含泛型的项目编译通过 |

**里程碑**：`bt build` 编译 Cargo 项目，含依赖、feature、多 crate

## Phase 3: Proc-Macro + 生态兼容 + Clippy 整合（8 周）

**目标**：使用 serde/tokio/axum 的主流 Rust 项目编译通过 + Clippy 700+ 规则整合 + AI 增强层

| 周 | 任务 | 产出 | MVP |
|---|------|------|-----|
| 1-2 | `bt-proc-macro`: .so 加载 + dlopen + 执行 | ProcMacroHost | 自定义 derive macro 编译运行 |
| 3-4 | 常用 proc-macro 透传 | serde_derive 等 | `#[derive(Serialize)]` 编译通过 |
| 5 | 沙箱化（子进程执行） | proc-macro 隔离 | proc-macro panic 不崩溃编译器 |
| 6 | top-100 crate 兼容性测试 | 测试报告 | serde/tokio/axum/clap 项目编译通过 |
| 7 | `bt-clippy-integration` + Clippy lint 注册 | BtClippyCallbacks + Clippy 700+ lint | `bt clippy` 输出与 `cargo clippy` 一致 |
| 8 | `BtClippyEnhancer` AI 增强层 + 补充规则引擎（~20 条） | AI 增强 + 跨文件 lint | Clippy 结果 + AI 解释叠加输出 |

**里程碑**：主流 Rust 生态项目编译通过；`bt clippy` 可运行 Clippy 700+ 规则并叠加 AI 增强建议

## Phase 4: AI 增强 + 可观测性 + LSP（8 周）

**目标**：AI 辅助优化 + IDE 集成 + 全链路可观测

| 任务 | 产出 | 周 |
|------|------|---|
| AI Pass 集成到 Rust 编译管线 | `bt build --ai=auto` 优化建议 | 2 |
| `bt-backend-llvm` 完善 | AArch64 目标 + 调试信息 | 1.5 |
| `bt-backend-cranelift` | Cranelift 封装 + WASM 目标 | 2 |
| OpenTelemetry 全链路追踪 | 每个编译阶段有 Span | 1 |
| LSP 适配器 | VS Code 基本代码补全/诊断 | 1 |
| Dashboard + WebSocket | Web 监控仪表盘 | 0.5 |

**里程碑**：AI 辅助优化 + 双后端 + IDE 集成 + 实时监控全部可用

## Phase 5: 高级特性（10 周）

| 任务 | 产出 | 周 |
|------|------|---|
| `bt-passes` 优化 Pass | DCE/常量折叠/内联/CFG 简化 | 1 |
| Dialect 降级完善 | bt_rust/bt_ts → bt_core 完整降级 | 0.5 |
| `bt-plugin-host` | WASM 插件宿主 (WIT 接口 + 沙箱) | 1.5 |
| Salsa 增量编译完善 | 文件修改后秒级重编译 | 1 |
| AI 流式分析 + 自动变换 | SSE + AIAction::AutoApply | 1 |
| TypeScript 前端 | TS Lexer/Parser/Deno 桥接/bt_ts Dialect | 4 |
| ts-runtime 最小子集 | string/array 基础操作 | 1 |
| 端到端集成测试 | Rust + TS 双前端测试 | 1 |

**里程碑**：BlockType Rust 全功能可用

> **注**：`bt-clippy-integration` + AI 增强已在 Phase 3 完成（提前 2 个 Phase，快速胜利）。

## 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| rustc_driver 是 nightly-only API | 高 | 高 | `rust-toolchain.toml` 锁定 nightly 日期版本；CI 每日 latest nightly 预警；最小化 API 接触面 |
| MIR → BTIR 复杂类型信息丢失 | 中 | 中 | 保留 bt_rust Dialect 标记，不强制完全转平 |
| proc-macro .so 跨平台兼容 | 中 | 中 | 使用 rustc 自己的 proc_macro 加载器 |
| crates.io crate 编译兼容性 | 中 | 中 | Phase 3 从 top-100 crate 逐步覆盖 |
| inkwell 跟不上 LLVM 版本 | 中 | 中 | Cranelift 作为备选 |
| TS JSON-RPC 桥接序列化开销 | 低 | 中 | 可选 bincode 二进制协议 |
| WASM 插件性能沙箱开销 | 低 | 中 | 热路径用 native Pass，仅扩展用 WASM |
| AI Provider 可用性和延迟 | 高 | 中 | 规则引擎 fallback 保证零延迟兜底 |
| Deno 进程池管理复杂度 | 中 | 中 | 健康检查 + 自动重启 + 进程池预热 |

## 关键技术选型

| 选型 | 决策 | 理由 |
|------|------|------|
| Rust 前端 | **复用 rustc** (bt-rustc-bridge) | sysroot + extern crate 方式，与 clippy/miri 同方案 |
| rustc 依赖 | **sysroot + extern crate** | 唯一可靠路径，版本与本地 nightly 完全一致 |
| Cargo 集成 | bt-cargo + cargo_metadata | 与 Cargo 100% 兼容 |
| HTTP 框架 | axum | Tokio 官方生态，tower 中间件 |
| 服务抽象 | tower::Service | 统一中间件链，限流/超时/重试 |
| 序列化 | serde + serde_json + bincode | JSON(外部) + 二进制(内部高性能) |
| 可观测性 | tracing + opentelemetry | Rust 生态标准 |
| Dialect | 运行时 trait + Registry | MLIR 风格，真正可插拔 |
| LLVM 绑定 | inkwell | 最成熟的 Rust LLVM 绑定 |
| 备选后端 | cranelift | Rust 原生，WASM 支持好 |
| 插件系统 | WASM Component Model (WIT) | 沙箱隔离 + 多语言 + 正式接口 |
| 增量编译 | Salsa 风格查询引擎 | 依赖追踪 + 自动失效 |
| 事件系统 | Event Sourcing | 可回放 + AI 训练 + Dashboard |
| TS 运行时 | Deno | 安全、快速、原生 TypeScript |
| CLI | clap | Rust 标准选择 |
| 异步运行时 | tokio | 生态标准 |
| AI 编排 | 自研 AIOrchestrator | 多 Provider + 预算 + 缓存 + fallback |
| Lint 整合 | **整合 Clippy** (bt-clippy-integration) | Day 1 即有 700+ 规则，不与 Clippy 竞争，AI 增强 |

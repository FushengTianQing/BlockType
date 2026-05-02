# 04 — Rust Cargo Workspace 项目结构

## 4.1 Workspace 布局

```
blocktype-next/
├── Cargo.toml                         # workspace 根
├── rust-toolchain.toml                # 锁定 nightly 日期版本 + rustc-dev 组件（sysroot 依赖必需）
│
├── crates/
│   │
│   │  ─── Layer 1: 基础设施 ───
│   ├── bt-core/                       # 核心类型、诊断、SourceManager、错误码
│   ├── bt-service/                    # tower::Service 编排、CompileService
│   ├── bt-api/                        # RESTful API 层 (axum) + WebSocket + LSP 适配器
│   ├── bt-telemetry/                  # 可观测性 (OpenTelemetry)
│   ├── bt-event-store/                # Event Sourcing (事件持久化/回放)
│   ├── bt-query/                      # Salsa 风格增量查询引擎
│   │
│   │  ─── Layer 2: IR 层 ───
│   ├── bt-ir/                         # BTIR 核心 (类型/值/指令/模块/Builder)
│   ├── bt-ir-verifier/                # IR 验证
│   ├── bt-passes/                     # IR 优化 Pass + PassManager
│   │
│   │  ─── Layer 2: Dialect 层 ───
│   ├── bt-dialect-core/               # bt_core Dialect (内建)
│   ├── bt-dialect-rust/               # bt_rust Dialect (运行时注册)
│   ├── bt-dialect-ts/                 # bt_ts Dialect (运行时注册)
│   │
│   │  ─── Layer 3: Rust 前端对接（复用 rustc） ───
│   ├── bt-cargo/                      # Cargo 工作空间 + 依赖管理 + feature gate
│   ├── bt-rustc-bridge/               # rustc MIR → BTIR 桥接（核心适配层）
│   ├── bt-clippy-integration/         # Clippy 整合 + AI 增强（bt clippy）
│   ├── bt-proc-macro/                 # 过程宏加载/执行
│   ├── bt-std-bridge/                 # 标准库链接（rustup sysroot）
│   │
│   │  ─── Layer 2: TypeScript 前端 ───
│   ├── bt-frontend-common/            # Frontend trait + Registry
│   ├── bt-frontend-ts/                # TS 前端桥接 (Deno 进程池 + 健康检查)
│   │
│   │  ─── Layer 2: 后端层 ───
│   ├── bt-backend-common/             # Backend trait + Registry
│   ├── bt-backend-llvm/               # LLVM 后端 (inkwell)
│   ├── bt-backend-cranelift/          # Cranelift 后端
│   │
│   │  ─── Layer 2: AI 服务 ───
│   ├── bt-ai/                         # AI 编排器 + Provider + 预算 + 缓存
│   │
│   │  ─── Layer 2: 插件系统 ───
│   ├── bt-plugin-host/                # WASM 插件宿主 (沙箱隔离)
│   │
│   │  ─── CLI 入口 ───
│   └── bt-cli/                        # CLI (clap) + 服务器启动
│
├── ts-frontend/                       # TypeScript 前端 (Deno)
│   ├── deno.json
│   ├── src/
│   │   ├── main.ts                    # JSON-RPC 入口
│   │   ├── lexer.ts
│   │   ├── parser.ts
│   │   ├── type_checker.ts
│   │   ├── type_narrower.ts
│   │   └── ir_emitter.ts              # 输出 JSON IR
│   └── tests/
│
├── plugin-sdk/                        # 插件开发 SDK (WIT 定义)
│   ├── wit/
│   │   ├── compiler-pass.wit          # Pass 插件接口
│   │   └── ai-provider.wit            # AI Provider 插件接口
│   └── examples/                      # Rust/Go/C 插件示例
│
├── runtime/
│   ├── ts-runtime/                    # TypeScript 运行时
│   └── rust-runtime/                  # Rust 运行时 stub
│
├── dashboard/                         # Web 监控仪表盘 (可选)
│   ├── package.json
│   └── src/
│
├── tests/
│   ├── integration/
│   └── snapshots/
│
└── docs/
    └── plan/
```

## 4.2 Crate 依赖关系

```
bt-cli
 ├── bt-cargo                         # Cargo 集成
 │    └── cargo_metadata + toml
 ├── bt-rustc-bridge                  # rustc 前端对接（MIR → BTIR，bt build 模式）
 │    ├── [sysroot] rustc_driver      # ← sysroot + extern crate（详见 08 §8.4）
 │    ├── [sysroot] rustc_interface
 │    ├── [sysroot] rustc_middle
 │    ├── [sysroot] rustc_mir
 │    ├── [sysroot] rustc_hir
 │    ├── [sysroot] rustc_session
 │    ├── [sysroot] rustc_span
 │    ├── [sysroot] rustc_errors
 │    ├── bt-ir
 │    ├── bt-dialect-rust
 │    └── bt-core
 ├── bt-clippy-integration            # Clippy 整合 + AI 增强（bt clippy 模式）★ 新增
 │    ├── clippy_lints                # Clippy 700+ lint（crates.io 正常依赖）
 │    ├── [sysroot] rustc_driver      # sysroot + extern crate
 │    ├── [sysroot] rustc_lint        # Lint 框架
 │    ├── [sysroot] rustc_interface
 │    ├── [sysroot] rustc_middle
 │    ├── bt-ai                       # AI 增强层
 │    ├── bt-event-store              # 事件记录
 │    └── bt-core
 ├── bt-proc-macro                    # 过程宏
 │    └── bt-core
 ├── bt-std-bridge                    # 标准库链接
 │    └── bt-core
 ├── bt-api
 │    ├── bt-service
 │    │    ├── bt-ir
 │    │    ├── bt-frontend-common
 │    │    ├── bt-backend-common
 │    │    ├── bt-dialect-core
 │    │    ├── bt-event-store
 │    │    ├── bt-query
 │    │    └── bt-core
 │    ├── bt-telemetry
 │    │    └── bt-core
 │    └── axum + tower + tokio (LSP 适配器内置于此 crate)
 ├── bt-frontend-ts                   # TypeScript 前端
 │    ├── bt-frontend-common
 │    ├── bt-ir
 │    └── bt-dialect-ts
 ├── bt-backend-llvm
 │    ├── bt-backend-common
 │    │    └── bt-core
 │    ├── bt-ir
 │    └── inkwell
 ├── bt-backend-cranelift
 │    ├── bt-backend-common
 │    ├── bt-ir
 │    └── cranelift
 ├── bt-passes
 │    ├── bt-ir
 │    ├── bt-ir-verifier
 │    └── bt-dialect-core
 ├── bt-dialect-rust
 │    ├── bt-ir
 │    └── bt-dialect-core
 ├── bt-dialect-ts
 │    ├── bt-ir
 │    └── bt-dialect-core
 ├── bt-ai
 │    └── bt-core
 ├── bt-plugin-host
 │    ├── bt-ir
 │    └── wasmtime + wit-bindgen
 ├── bt-event-store
 │    └── bt-core
 ├── bt-query
 │    ├── bt-ir
 │    └── bt-core
 └── clap
```

> **注意**：`bt-rustc-bridge` 是 BlockType 与 rustc 的唯一接口（bt build 模式），负责 MIR → BTIR 转换。
> `bt-clippy-integration` 是 Clippy 整合的唯一入口（bt clippy 模式），复用 Clippy 700+ 规则并叠加 AI 增强。
> Rust 前端全部复用 rustc，不再有自有 Rust 前端 crate。
>
> **Sysroot 依赖说明**：`[sysroot]` 标记的 rustc crate 通过 `#![feature(rustc_private)]` + `extern crate`
> 从 sysroot 引入，**不在 Cargo.toml 中声明**。`rust-toolchain.toml` 锁定 nightly 版本。
> Clippy lint 规则通过 `clippy_lints` crate（crates.io）正常依赖。
> 详见 [08-Rust-Ecosystem-Integration.md §8.4](./08-Rust-Ecosystem-Integration.md) 和 [§8.12](./08-Rust-Ecosystem-Integration.md)。

## 4.3 TypeScript 前端桥接（架构概览）

> **📝 结构优化备注**：§4.3.1 JSON-RPC 协议规范已迁移到 **[05-Unified-API.md §2.11](./05-Unified-API.md)** 统一管理。
> 本节保留桥接架构概览图。

```
┌──────────────────────┐     JSON-RPC      ┌──────────────────────┐
│  Rust 进程 (bt-cli)   │ ◀──────────────▶ │  Deno 进程池 (TS前端) │
│                      │   stdin/stdout    │                      │
│  bt-frontend-ts:     │                   │  ts-frontend/:       │
│  - Worker 进程池     │  ──{method:"compile"}──────────────────▶ │
│  - 健康检查/心跳     │                   │  Lexer→Parser→      │
│  - 超时/重启策略     │  ◀──{result:{ir_module}}─────────────── │
│  - 负载均衡          │                   │  TypeCheck→Narrower │
│  - 协议版本协商      │                   │  → IR JSON 输出      │
└──────────────────────┘                   └──────────────────────┘
```

> JSON-RPC 协议详细规范（Method 列表、请求/响应格式、错误码、进程池配置）见
> **[05-Unified-API.md §2.11](./05-Unified-API.md)**。

## 4.4 各 Crate 职责

| Crate | 职责 | 行数估算 |
|-------|------|---------|
| `bt-core` | 诊断、SourceManager、TargetTriple、错误码体系 | ~1,800 |
| `bt-service` | tower::Service 编排、CompileService、中间件链 | ~1,500 |
| `bt-api` | axum HTTP 服务器、路由、handler、WebSocket、LSP 适配器 | ~3,500 |
| `bt-telemetry` | OpenTelemetry 集成、PipelineNode、指标 | ~1,500 |
| `bt-event-store` | Event Sourcing 事件持久化、回放、导出、订阅 | ~1,500 |
| `bt-query` | Salsa 风格增量查询引擎、依赖追踪、缓存 | ~2,500 |
| `bt-ir` | IR 类型/值/指令/模块/Builder/DialectInstruction | ~4,500 |
| `bt-ir-verifier` | IR 验证、类型检查、等价性 | ~1,000 |
| `bt-passes` | Pass trait (含依赖声明)、PassManager、优化 Pass | ~3,500 |
| `bt-dialect-core` | bt_core Dialect 定义 + Dialect trait | ~800 |
| `bt-dialect-rust` | bt_rust Dialect (运行时注册) + 降级规则 | ~1,500 |
| `bt-dialect-ts` | bt_ts Dialect (运行时注册) + 降级规则 | ~1,200 |
| `bt-cargo` | Cargo 工作空间解析、依赖图、feature gate、构建计划 | ~2,000 |
| `bt-rustc-bridge` | rustc_driver 集成、MIR→BTIR 转换、类型映射 | ~4,000 |
| `bt-proc-macro` | 过程宏 .so 加载/执行、沙箱化 | ~1,500 |
| `bt-std-bridge` | rustup sysroot 检测、标准库链接 | ~500 |
| `bt-frontend-common` | Frontend trait + Registry | ~500 |
| `bt-frontend-ts` | Deno 进程池 + 健康检查 + JSON-RPC 桥接 | ~1,500 |
| `bt-backend-common` | Backend trait + Registry | ~500 |
| `bt-backend-llvm` | inkwell 封装，x86_64/AArch64 | ~3,000 |
| `bt-backend-cranelift` | Cranelift 封装，WASM 目标 | ~2,000 |
| `bt-ai` | AI 编排器 + 多 Provider + 预算 + 缓存 + 规则引擎 | ~3,000 |
| `bt-plugin-host` | WASM 插件宿主、沙箱隔离、WIT 接口 | ~1,500 |
| `bt-cli` | CLI 入口 (clap) | ~500 |
| **Rust 总计** | | **~44,300** |
| `ts-frontend/` | TypeScript 前端 (Deno) | ~5,000 |
| `plugin-sdk/` | WIT 定义 + 插件示例 | ~500 |
| **项目总计** | | **~49,800** |

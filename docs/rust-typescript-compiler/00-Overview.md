# BlockType Next — 统一通信架构的 AI 编译器设计方案

> **文档版本**：v3.0 | **日期**：2026-05-02 | **状态**：总体方案（已合并架构优化建议 + Rust 生态集成方案）
> **前置**：现有 BlockType C++ 代码库作为参考，本项目从零用 Rust 重写

---

## 文档索引

### Part I: 概览与原则

| 文件 | 内容 |
|------|------|
| [01-Design-Philosophy.md](./01-Design-Philosophy.md) | 设计理念、六大原则、架构层次图 |

### Part II: 核心架构（类型 → 通信 → 结构 → API）

| 文件 | 内容 |
|------|------|
| [02-Core-Types.md](./02-Core-Types.md) | 核心类型定义（IR / Dialect 运行时注册 / Frontend / Backend trait） |
| [03-Communication-Bus.md](./03-Communication-Bus.md) | tower::Service 统一抽象、四子系统、Event Sourcing、错误传播 |
| [04-Project-Structure.md](./04-Project-Structure.md) | Rust Cargo Workspace 结构、crate 依赖关系、JSON-RPC 协议 |
| [05-Unified-API.md](./05-Unified-API.md) | 分层 RESTful API 端点设计、错误码、分页 |

### Part III: 横切关注点

| 文件 | 内容 |
|------|------|
| [06-Observability.md](./06-Observability.md) | 全链路可观测性、OpenTelemetry、监控节点、实时推送、EventStore |
| [07-AI-Native.md](./07-AI-Native.md) | AI 原生架构、AI 编排器、流式分析、预算管理 |

### Part IV: Rust 前端深度方案

| 文件 | 内容 |
|------|------|
| [08-Rust-Ecosystem-Integration.md](./08-Rust-Ecosystem-Integration.md) | **Rust 生态集成方案**：sysroot + extern crate 依赖方式、bt-rustc-bridge、**Clippy 整合**（700+ 规则 + AI 增强）、Cargo/proc-macro/std 兼容 |
| [09-Frontend-Innovation-Extension.md](./09-Frontend-Innovation-Extension.md) | **前端创新扩展方案**：rustc Callbacks 探针、诊断增强 Emitter、AI Lint Pass、全阶段可观测性 |

### Part V: 路线图

| 文件 | 内容 |
|------|------|
| [10-Phased-Roadmap.md](./10-Phased-Roadmap.md) | 渐进式开发路线图、周级 MVP、里程碑 |

### Appendix（附录）

| 文件 | 内容 |
|------|------|
| [A1-Architecture-Review.md](./appendix/A1-Architecture-Review.md) | 功能架构优化建议（评审记录，已合并） |
| [A2-Final-Review.md](./appendix/A2-Final-Review.md) | 最终审查报告（v2.1 定稿，全五星） |
| [A3-BlockType-vs-Clippy-Analysis.md](./appendix/A3-BlockType-vs-Clippy-Analysis.md) | **BlockType vs Clippy 对比分析**：架构层级、功能覆盖、优劣势、整合策略 |

---

## 一句话概括

**BlockType Rust 是 Rust 编译器的下一代架构演进——复用 rustc 前端，注入 tower::Service 服务抽象、AI 原生分析、全链路可观测性，构建可插拔的、API 驱动的编译器平台。**

```
rustc 现有：源码 → [parse → typeck → borrowck → MIR → LLVM] → 目标文件
BlockType：  源码 → [rustc前端] → MIR → [BTIR桥接] → [AI+优化+可观测] → [LLVM/Cranelift] → 目标文件 + 洞察 + AI 建议
```

## 技术栈

| 层 | 技术 |
|----|------|
| Rust 前端 | **rustc (复用)** — sysroot + `extern crate` 方式，通过 bt-rustc-bridge 桥接 MIR → BTIR |
| Cargo 集成 | bt-cargo — cargo_metadata 解析 + 依赖图 + feature gate |
| 标准库 | **直接链接 rustup sysroot** — 零额外工作，100% 兼容 |
| 过程宏 | bt-proc-macro — .so 加载执行，与 rustc 方式一致 |
| HTTP 框架 | axum (Tokio 官方生态) |
| 服务抽象 | tower::Service (统一的中间件链：限流/超时/重试/日志) |
| 通信协议 | HTTP/REST (外部) + 内存直接调用 (内部) + LSP (IDE 集成) |
| 可观测性 | OpenTelemetry (tracing + metrics) + Event Sourcing |
| 插件系统 | WASM Component Model (WIT 接口定义，沙箱隔离，多语言插件) |
| 序列化 | serde + serde_json (JSON) + bincode (二进制) |
| 构建 | Cargo workspace (nightly toolchain, `rust-toolchain.toml` 日期锁定 + `rustc-dev` 组件) |
| Lint | **整合 Clippy** (bt-clippy-integration) — 700+ 规则 + AI 增强，不自建 Lint 规则集 |
| TS 前端 | Deno (TypeScript 前端可选支持) |
| 增量编译 | Salsa 风格查询引擎 (依赖追踪 + 自动失效) |

## v2.0 关键架构变更（相比 v1.0）

| 变更点 | v1.0 | v2.0 |
|--------|------|------|
| Dialect 扩展 | 编译时 `#[cfg(feature)]` | 运行时 `Dialect trait` + `DialectRegistry` |
| 通信抽象 | 自定义 `BusEndpoint` | `tower::Service` + 中间件链 |
| 查询系统 | 混杂的 Query Bus | StatusStore / ArtifactStore / Registry / QueryEngine 四子系统 |
| 插件系统 | `dyn trait`（无隔离） | WASM Component Model（沙箱隔离，多语言） |
| AI 服务 | 3 插槽 + 单 provider | AI 编排器 + 流式 + fallback + 预算 + 缓存 |
| 事件系统 | 简单 pub/sub | Event Sourcing（可回放、可导出） |
| IDE 集成 | 无 | LSP 适配器 |
| IR 类型 | 基本类型 | 支持泛型 / Trait对象 / 闭包 / 函数指针 |
| API 设计 | 扁平 `/api/v1/*` | 按关注点分层 `/compile/*`, `/ir/*`, `/admin/*`, `/ai/*` |

## v3.0 文档结构优化（相比 v2.1）

| 变更 | 说明 |
|------|------|
| 文档重编号 | 按自底向上逻辑重排：核心类型 → 通信架构 → 项目结构 → API → 横切关注点 → 深度方案 → 路线图 |
| 审查文档归入附录 | A1/A2/A3 不再打断主阅读流 |
| Clippy 整合提前 | 从 Phase 5 移到 Phase 3（路线图 §10） |
| 13 → A3 内容修正 | "自建 AI Lint" 标注为已否决，统一为 Clippy 整合方案 |
| 12 → 09 去重 | §9.3.5 BtClippyEnhancer 重复代码简化为引用 |

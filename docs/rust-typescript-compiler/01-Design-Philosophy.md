# 01 — 设计理念与架构原则

---

## 1.1 六大设计原则

| # | 原则 | 说明 | 技术手段 |
|---|------|------|---------|
| P1 | **一切皆 API** | 编译器每个阶段、每个节点都通过统一 RESTful API 暴露 | axum HTTP 服务器 + tower::Service 中间件链 |
| P2 | **通信驱动** | 组件间通过统一的 Service 抽象互联，内部/外部调用接口一致 | tower::Service + axum handler |
| P3 | **架构可插拔** | 前端、后端、Dialect、Pass、AI 分析器、插件全部可运行时热插拔 | Rust trait + Registry + WASM Component Model |
| P4 | **全链路可观测** | 每个编译节点的输入/输出/耗时/状态全程可追踪，事件可回放 | OpenTelemetry Span + WebSocket + Event Sourcing |
| P5 | **AI 原生** | AI 是管线内一等公民，支持流式分析、预算管理、自动变换 | AI 编排器 + fallback 链 + token 预算 |
| P6 | **渐进式交付** | 每阶段可编译、可测试、可独立运行 | Cargo workspace 模块化 |

## 1.2 架构层次图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Layer 5: Client Layer                           │
│   CLI (bt) │ Web Dashboard │ VS Code (LSP) │ AI Agent │ REST Client    │
├─────────────────────────────────────────────────────────────────────────┤
│                     Layer 4: API Gateway (axum)                        │
│  /api/v1/compile/* │ /api/v1/ir/* │ /api/v1/admin/* │ /api/v1/ai/*    │
│  /ws/v1/* (WebSocket) │ LSP Adapter (stdin/stdout)                     │
│  认证 │ 限流 │ 路由 (全部基于 tower::Service 中间件链)                   │
├─────────────────────────────────────────────────────────────────────────┤
│                     Layer 3: Service Layer (tower::Service)            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                 │
│  │ CompileSvc   │  │ AIOrchestr.  │  │ PluginHost   │                 │
│  │ (管线编排)    │  │ (AI 编排)    │  │ (WASM 沙箱)  │                 │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                 │
├─────────┼─────────────────┼─────────────────┼─────────────────────────┤
│                     Layer 2: Compiler Core                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ Frontend │ │ IR/Pass  │ │ Backend  │ │ Query    │ │ Event    │   │
│  │ Registry │ │ Manager  │ │ Registry │ │ Engine   │ │ Store    │   │
│  │  ┌────┐  │ │ ┌──────┐ │ │  ┌────┐  │ │(Salsa式) │ │(EventSrc)│   │
│  │  │Rust│  │ │ │Dialect│ │ │  │LLVM│  │ │          │ │          │   │
│  │  │ TS │  │ │ │PassMgr│ │ │  │Cran│  │ │          │ │          │   │
│  │  │... │  │ │ │Verify │ │ │  │... │  │ │          │ │          │   │
│  │  └────┘  │ │ └──────┘ │ │  └────┘  │ │          │ │          │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
├─────────────────────────────────────────────────────────────────────────┤
│                     Layer 1: Infrastructure                             │
│  bt-core (诊断/SourceManager/TargetTriple)                             │
│  bt-telemetry (OpenTelemetry) │ bt-cache (查询缓存)                    │
│  DialectRegistry │ StatusStore │ ArtifactStore                         │
└─────────────────────────────────────────────────────────────────────────┘
```

## 1.3 编译管线流程

```
源码 (.rs / .ts)
    │
    ▼
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│  Lex     │───▶│  Parse   │───▶│ Analyze  │───▶│  Lower   │───▶│ Optimize │───▶│ Codegen  │
│  词法分析 │    │  语法分析 │    │ 语义分析  │    │ IR 降低  │    │ IR 优化  │    │ 代码生成  │
└──────────┘    └──────────┘    └──────────┘    └──────────┘    └──────────┘    └──────────┘
    │               │               │               │               │               │
    ▼               ▼               ▼               ▼               ▼               ▼
 [Tokens]       [HIR/AST]    [Typed HIR]      [IRModule]   [Optimized IR]    [Object/ASM]
    │               │               │               │               │               │
    └───────────────┴───────┬───────┴───────┬───────┴───────────────┘               │
                            │               │                                       │
                     ┌──────▼──────┐  ┌──────▼──────┐                         ┌──────▼──────┐
                     │ AI Analysis │  │ AI Optimize │                         │  AI Review  │
                     │  (Pass S1)  │  │  (Pass S2)  │                         │  (Pass S3)  │
                     └─────────────┘  └─────────────┘                         └─────────────┘
```

**每个节点都是可观测的、可通过 API 单独调用的、可被 AI 分析的。**
**Dialect 通过 DialectRegistry 在运行时注册，前端/后端/Pass 通过各自 Registry 动态管理。**

## 1.4 与传统编译器的本质区别

| 维度 | 传统编译器 (gcc/clang/rustc) | BlockType Next |
|------|---------------------------|----------------|
| 调用方式 | CLI 命令行 | RESTful API + CLI + LSP |
| 组件耦合 | 函数调用，紧耦合 | tower::Service，松耦合，统一中间件链 |
| 可观测性 | printf/gdb 调试 | OpenTelemetry 全链路追踪 + Event Sourcing + 实时仪表盘 |
| AI 集成 | 无或外挂 | 管线内一等公民，AI 编排器 + 流式 + 预算管理 |
| 扩展方式 | 修改源码或插件 | REST API + 运行时 Dialect 注册 + WASM 插件热加载 |
| 增量编译 | 基于文件时间戳 | Salsa 风格查询系统 + 依赖追踪 + 自动失效 |
| 跨语言 | 仅限 C/C++ | Rust + TypeScript + WASM 插件（任意语言） |
| IDE 集成 | 外部 LSP 封装 | 内置 LSP 适配器 |
| 插件隔离 | 无（共享进程内存） | WASM 沙箱隔离 |

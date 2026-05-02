# BlockType Next — AI Coder 开发总纲

> **文档版本**：v1.0 | **日期**：2026-05-02 | **用途**：AI Coder 任务领取与执行指南
> **设计文档根目录**：`docs/plan/rust-typescript-compiler/`
> **开发方案目录**：`docs/plan/rust-typescript-compiler/dev-plan/`

---

## 0. 文档体系结构

```
docs/plan/rust-typescript-compiler/
├── 00-Overview.md                 ← 项目总览
├── 01-Design-Philosophy.md       ← 设计原则
├── 02-Core-Types.md              ← 核心类型定义（Rust 代码级详细）
├── 03-Communication-Bus.md       ← 通信架构（tower::Service 详细设计）
├── 04-Project-Structure.md       ← Cargo Workspace 结构 + 依赖关系
├── 05-Unified-API.md             ← RESTful API 设计 + 认证 + JSON-RPC
├── 06-Observability.md           ← 可观测性设计
├── 07-AI-Native.md               ← AI 原生架构
├── 08-Rust-Ecosystem-Integration.md  ← Rust 生态集成（rustc 桥接 + Clippy）
├── 09-Frontend-Innovation-Extension.md ← 前端探针扩展
├── 10-Phased-Roadmap.md          ← 渐进式路线图
├── appendix/                     ← 审查记录（仅供参考）
│
└── dev-plan/                     ← ★ 开发方案（AI Coder 必读）
    ├── DEV-00-AI-Coder-Guide.md  ← 本文件：总纲 + 行为准则
    ├── DEV-01-Phase0-Tasks.md    ← Phase 0 详细任务清单
    ├── DEV-02-Phase1-Tasks.md    ← Phase 1 详细任务清单
    ├── DEV-03-Phase2-Tasks.md    ← Phase 2 详细任务清单
    ├── DEV-04-Phase3-Tasks.md    ← Phase 3 详细任务清单
    ├── DEV-05-Phase4-Tasks.md    ← Phase 4 详细任务清单
    └── DEV-06-Phase5-Tasks.md    ← Phase 5 详细任务清单
```

**文档权威等级**（从高到低）：

| 等级 | 文档 | 说明 |
|------|------|------|
| R1 | `02-Core-Types.md` | 所有类型/接口的精确 Rust 定义 |
| R2 | `03-Communication-Bus.md` | 通信架构、子系统、错误传播的精确设计 |
| R3 | `04-Project-Structure.md` | crate 结构、文件路径、依赖关系 |
| R4 | `05-Unified-API.md` | API 端点、请求/响应 schema、认证 |
| R5 | `06-Observability.md` / `07-AI-Native.md` | 横切关注点设计 |
| R6 | `08-*.md` / `09-*.md` | Rust 生态集成深度方案 |
| R7 | `10-Phased-Roadmap.md` | 路线图总览 |
| R8 | `01-Design-Philosophy.md` | 设计原则（指导性，非具体规格） |
| **DEV** | **`dev-plan/DEV-*.md`** | **任务级规格（AI Coder 执行依据）** |

**冲突解决规则**：DEV 任务文件中的规格优先于路线图中的粗略描述。DEV 与 R1-R6 冲突时，以 R1-R6 为准，但需标注差异。

---

## 1. AI Coder 行为准则

### 1.1 绝对红线

| # | 红线 | 说明 |
|---|------|------|
| 1 | **禁止虚构数据和接口** | 实现前必须到 R1-R6 核实接口是否存在、签名是否正确 |
| 2 | **禁止省略实现** | 不允许 TODO、空函数体、跳过 EDGE/ERR 路径 |
| 3 | **禁止偏离设计文档** | 接口签名不可改，数据流不可绕过 DataStore，文件路径不可偏移 |
| 4 | **测试必须完整** | 覆盖所有 TST 断言和 EDGE 边界，禁止空测试和弱断言 |
| 5 | **禁止并行开发** | 同一时间只有一个 AI Coder 在开发，所有任务严格串行 |

### 1.2 开发流程（每个 Task）

```
1. 读取任务文件（DEV-XX-*.md 中的具体 Task）
2. 读取该 Task 引用的设计文档（REF 字段指定的 R1-R6 文件）
3. 读取前置 Task 的产出文件（DEP 字段指定的已实现文件）
4. 实现代码（按 REF 中的接口签名、文件路径精确实现）
5. 编写测试（覆盖 TST 字段中的所有断言）
6. 运行测试确认通过
7. 输出交付报告
```

### 1.3 Task 状态

| 状态 | 含义 |
|------|------|
| `TODO` | 未开始 |
| `IN_PROGRESS` | 正在开发 |
| `DONE` | 已完成并通过测试 |
| `BLOCKED` | 被前置依赖阻塞 |
| `SKIP` | 跳过（需注明原因） |

---

## 2. 项目总览

### 2.1 一句话概括

**BlockType Next 是 Rust 编译器的下一代架构演进——复用 rustc 前端，注入 tower::Service 服务抽象、AI 原生分析、全链路可观测性，构建可插拔的、API 驱动的编译器平台。**

### 2.2 技术栈

| 层 | 技术 |
|----|------|
| Rust 前端 | rustc (复用) — sysroot + extern crate |
| HTTP | axum (tower 生态) |
| 服务抽象 | tower::Service |
| 可观测性 | OpenTelemetry (tracing + metrics) |
| 插件 | WASM Component Model (WIT) |
| 序列化 | serde + serde_json + bincode |
| 构建 | Cargo workspace (nightly) |
| Lint | 整合 Clippy (700+ 规则 + AI 增强) |
| TS 前端 | Deno (可选) |
| 增量编译 | Salsa 风格查询引擎 |

### 2.3 架构层次

```
Layer 5: Client Layer   — CLI / Dashboard / VS Code / AI Agent / REST Client
Layer 4: API Gateway    — axum 路由 / 认证 / WebSocket / LSP
Layer 3: Service Layer  — CompileService / AIOrchestrator / PluginHost
Layer 2: Compiler Core  — Frontend / IR+Pass / Backend / Query / EventStore
Layer 1: Infrastructure — bt-core / bt-telemetry / bt-cache / Registry
```

### 2.4 Cargo Workspace 结构（24 个 crate）

```
blocktype-next/
├── crates/
│   ├── bt-core/               # 核心类型、诊断、错误码
│   ├── bt-service/            # tower::Service 编排
│   ├── bt-api/                # RESTful API (axum) + WebSocket + LSP
│   ├── bt-telemetry/          # OpenTelemetry
│   ├── bt-event-store/        # Event Sourcing
│   ├── bt-query/              # Salsa 增量查询
│   ├── bt-ir/                 # BTIR 核心
│   ├── bt-ir-verifier/        # IR 验证
│   ├── bt-passes/             # 优化 Pass + PassManager
│   ├── bt-dialect-core/       # bt_core Dialect
│   ├── bt-dialect-rust/       # bt_rust Dialect
│   ├── bt-dialect-ts/         # bt_ts Dialect
│   ├── bt-cargo/              # Cargo 集成
│   ├── bt-rustc-bridge/       # rustc MIR → BTIR
│   ├── bt-clippy-integration/ # Clippy 整合
│   ├── bt-proc-macro/         # 过程宏加载
│   ├── bt-std-bridge/         # 标准库链接
│   ├── bt-frontend-common/    # Frontend trait + Registry
│   ├── bt-frontend-ts/        # TS 前端桥接
│   ├── bt-backend-common/     # Backend trait + Registry
│   ├── bt-backend-llvm/       # LLVM 后端
│   ├── bt-backend-cranelift/  # Cranelift 后端
│   ├── bt-ai/                 # AI 编排器
│   ├── bt-plugin-host/        # WASM 插件
│   └── bt-cli/                # CLI 入口
├── ts-frontend/               # TypeScript 前端 (Deno)
├── plugin-sdk/                # WIT 定义 + 插件示例
├── runtime/                   # TS/Rust 运行时
├── dashboard/                 # Web 监控仪表盘
└── tests/                     # 集成测试
```

---

## 3. Phase 总览与依赖关系

```
Phase 0 ──── Phase 1 ──── Phase 2 ──── Phase 3 ──── Phase 4 ──── Phase 5
 基础设施      IR+Dialect   Cargo项目    Proc-Macro   AI+可观测    高级特性
 +rustc桥接   +Dialect     编译         +生态兼容    +LSP         +TS+插件
 +探针MVP     +查询引擎                 +Clippy整合
 (~9周)       (~6周)       (~7周)       (~9周)       (~10周)      (~12周)
                                                          总计 ~53周 (~13个月)
```

| Phase | 名称 | 任务文件 | Sprint 数 | Task 数 | 估计天 | 估计周 |
|-------|------|---------|----------|---------|--------|--------|
| 0 | 基础设施 + rustc 桥接 | `DEV-01-Phase0-Tasks.md` | 4 | 18 | 47 | ~9 |
| 1 | IR 核心 + Dialect + 查询引擎 | `DEV-02-Phase1-Tasks.md` | 3 | 12 | 29 | ~6 |
| 2 | Cargo 项目编译 | `DEV-03-Phase2-Tasks.md` | 3 | 10 | 36 | ~7 |
| 3 | Proc-Macro + Clippy 整合 | `DEV-04-Phase3-Tasks.md` | 4 | 14 | 46 | ~9 |
| 4 | AI + 可观测性 + LSP | `DEV-05-Phase4-Tasks.md` | 4 | 14 | 50 | ~10 |
| 5 | 高级特性 + TS 前端 | `DEV-06-Phase5-Tasks.md` | 5 | 16 | 60 | ~12 |
| **合计** | | | **23** | **84** | **268** | **~53** |

### Phase 间依赖

```
Phase 0 ──→ Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4 ──→ Phase 5
                     ╲                  ╱
                      ╲────────────────╱
                   （Phase 3 部分依赖 Phase 1 的 IR）
```

---

## 4. Task 格式规范

每个 Task 遵循以下标准格式：

```markdown
### T-XX-YY: 任务标题

- **ID**: `T-XX-YY`（XX=Phase编号, YY=Task编号）
- **状态**: TODO | IN_PROGRESS | DONE
- **估计**: X 天
- **优先级**: P0(阻塞) | P1(关键) | P2(重要) | P3(常规)
- **Sprint**: S-XX-Z（Z=Sprint编号）

**描述**：一段话说明任务目标。

**产出文件**：
- `crates/xxx/src/lib.rs` — 模块入口
- `crates/xxx/src/types.rs` — 类型定义

**设计规格**（REF）：
- `02-Core-Types.md §7.2` — IRType 定义
- `04-Project-Structure.md §4.1` — 文件路径

**前置依赖**（DEP）：
- `T-XX-YY`：已完成的前置任务

**验收标准**（TST）：
- [ ] `IRType` 枚举包含所有 13 种变体
- [ ] `#[derive(Serialize, Deserialize)]` 已添加
- [ ] 单元测试覆盖每个变体的序列化/反序列化
- [ ] `cargo test -p bt-ir` 全部通过

**实现步骤**：
1. 在 `crates/bt-ir/` 创建 Cargo.toml
2. 创建 `src/types.rs`，按 REF 中的 Rust 定义实现
3. 创建 `src/lib.rs`，导出公共 API
4. 创建 `tests/types_test.rs`，覆盖 TST 断言
```

---

## 5. 质量门禁

每个 Task 完成时必须满足：

| 检查项 | 标准 |
|--------|------|
| 编译 | `cargo build` 无 warning |
| 测试 | `cargo test -p <crate>` 全部通过 |
| 格式 | `cargo fmt --check` 通过 |
| Clippy | `cargo clippy -p <crate>` 无 warning |
| 文档注释 | 所有 pub 类型/函数有 `///` 文档注释 |
| 错误处理 | 无 `unwrap()` 在非测试代码中，使用 `?` 或显式处理 |
| 依赖方向 | 只依赖 DEP 中列出的 crate，无反向依赖 |

---

## 6. 快速开始

AI Coder 按以下步骤开始工作：

1. **阅读本文件**（DEV-00）了解总纲
2. **找到当前 Phase**：查看 Phase 总览确认当前阶段
3. **打开对应 Task 文件**：如 `DEV-01-Phase0-Tasks.md`
4. **领取第一个 `TODO` 状态的 Task**：按文件中的顺序
5. **读取 REF 设计文档**：理解接口规格
6. **读取 DEP 前置产出**：了解已实现的代码
7. **实现 + 测试**：按 Task 的实现步骤执行
8. **通过质量门禁**：确认所有检查项通过
9. **标记 Task 为 DONE**
10. **领取下一个 Task**

**Task 严格按编号顺序执行**，不可跳过或并行。

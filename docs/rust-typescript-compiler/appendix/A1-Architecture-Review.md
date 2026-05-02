# 09 — 功能架构优化建议（Review & Optimization）

> **版本**：v1.0 | **日期**：2026-05-02 | **基于**：00-08 设计文档 (v1.0) 全面审查
> **状态**：已合并 — 所有优化建议已合并回 00-08 设计文档 (v2.0)

---

## 总体评价

当前方案是一个**有雄心的、方向正确的** AI 编译器架构设计。核心亮点：

| 维度 | 评分 | 说明 |
|------|------|------|
| 前瞻性 | ★★★★★ | 通信驱动 + AI 原生 + 全链路可观测，领先传统编译器 5 年 |
| 模块化 | ★★★★☆ | crate 拆分合理，但 Dialect 扩展机制需改进 |
| API 设计 | ★★★☆☆ | 端点覆盖全但缺乏分层，部分设计过于粗粒度 |
| 可行性 | ★★★☆☆ | 存在若干架构硬伤，不修复将在 Phase 1 后严重阻塞 |
| 一致性 | ★★★☆☆ | 核心类型定义和总线设计存在自相矛盾之处 |

---

## 🔴 严重问题（必须修复，否则阻塞开发）

### R1. Dialect 扩展是编译时的，不是运行时的

**现状**：`02-Core-Types.md` 中 IRInstruction 使用 `#[cfg(feature = "dialect-rust")]` 编译时 feature flag 控制 Dialect。

```rust
// 当前设计 — 编译时 feature
#[cfg(feature = "dialect-rust")]
OwnershipTransfer { value: IRValueId, kind: OwnershipKind },
```

**问题**：
1. **违反 P3（架构可插拔）**：加载/卸载 Dialect 需要重新编译
2. **违反"一切皆 API"**：运行时 `GET /api/v1/ir/dialects` 列出的 Dialect 无法动态变化
3. **与 MLIR 的核心价值背道而驰**：MLIR 的 Dialect 就是运行时可注册的
4. **前端后端无法独立组合**：如果用户只想用 Rust 前端 + Cranelift 后端，但仍需编译 bt_ts Dialect 代码

**建议**：采用 MLIR 风格的运行时 Dialect 注册机制：

```rust
/// Dialect trait — 运行时可注册
pub trait Dialect: Send + Sync {
    fn name(&self) -> &str;                    // "bt_rust", "bt_ts"
    fn opcode_range(&self) -> (u16, u16);      // (224, 239) for bt_rust
    fn operations(&self) -> Vec<OperationDef>;  // 操作定义表
    fn lower_to_core(&self, inst: &DialectInst) -> Vec<CoreInstruction>;
    fn verify(&self, inst: &DialectInst, module: &IRModule) -> Result<(), VerifyError>;
}

/// IRInstruction 改为统一结构
pub enum IRInstruction {
    // bt_core 内建指令 (0-63) — 编译时固定
    Add { lhs: IRValueId, rhs: IRValueId },
    // ...其他 core 指令

    // Dialect 扩展指令 — 运行时解析
    Dialect(DialectInstruction),
}

pub struct DialectInstruction {
    pub dialect: String,           // "bt_rust"
    pub opcode: u16,               // 224-239
    pub operands: Vec<IRValueId>,
    pub attributes: HashMap<String, IRAttribute>,
}

/// Dialect 注册表
pub struct DialectRegistry {
    dialects: HashMap<String, Box<dyn Dialect>>,
}
```

**影响范围**：`bt-ir`、`bt-passes`、`bt-frontend-*`、`bt-backend-*`

---

### R2. 三总线设计缺乏统一的 Service 抽象层

**现状**：`03-Communication-Bus.md` 定义了 `BusEndpoint<Request, Response>` trait，但没有与 Rust 生态对齐。

**问题**：
1. **重复发明轮子**：axum 生态已有 `tower::Service` trait，这正是 Rust 中处理请求/响应的标准抽象
2. **缺少中间件组合能力**：自造的 `BusEndpoint` 无法使用 tower 的 `ServiceBuilder` 中间件链（限流、超时、重试、日志等）
3. **双模式传输设计过于复杂**：`InProcessBus` vs `HttpBus` 的区分增加了不必要的类型复杂度

**建议**：统一到 `tower::Service` 抽象，双模式传输用 Layer 切换：

```rust
use tower::Service;

/// 所有编译器服务统一为 tower::Service
/// FrontendService, PassService, BackendService 都实现 Service trait
///
/// 内部模式：直接调用 Service::call()
/// HTTP 模式：axum handler 内调用同一个 Service::call()
///
/// 中间件通过 Layer 组合：
/// let service = ServiceBuilder::new()
///     .rate_limit(100, Duration::from_secs(1))  // 限流
///     .timeout(Duration::from_secs(30))          // 超时
///     .retry(RetryPolicy::exponential_backoff()) // 重试
///     .service(CompileService::new(...));
```

```rust
/// 不再需要 InProcessBus / HttpBus 两个类型
/// 只有一个 CompileService，内部/外部调用方式一致：
pub struct CompileService {
    frontend_registry: Arc<FrontendRegistry>,
    backend_registry: Arc<BackendRegistry>,
    pass_manager: Arc<PassManager>,
}

impl Service<CompileRequest> for CompileService {
    type Response = CompileResponse;
    type Error = CompilerError;
    type Future = Pin<Box<dyn Future<Output = Result<Self::Response, Self::Error>>>>;

    fn poll_ready(&mut self, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn call(&mut self, req: CompileRequest) -> Self::Future {
        // 编译管线的实际逻辑
        // axum handler 和内部调用都走这里
        Box::pin(self.compile(req))
    }
}
```

**收益**：删除 `bt-bus` crate 中约 60% 的自定义代码，免费获得限流、超时、重试、并发控制等中间件。

---

### R3. Query Bus 混杂了多种职责，且增量编译机制缺失

**现状**：`03-Communication-Bus.md` 的 Query Bus 把 5 种不同性质的操作混在一起：

```rust
pub enum CompilerQuery {
    TaskStatus { task_id: Uuid },       // 任务状态查询
    Tokens { task_id: Uuid },           // 管线中间产物查询
    FunctionByIndex { ... },            // IR 数据查询
    RegisteredFrontends,                // 注册表查询
    CachedLex { source_hash: ... },     // 增量编译缓存
}
```

**问题**：
1. 任务状态查询是**运营数据**，应走 CQRS 的 Read Model
2. IR 数据查询是**编译器内部 API**，不应暴露为 Bus
3. 注册表查询是**系统元数据**，应有独立的 Registry Service
4. 缓存查询是**增量编译引擎**的核心，需要依赖追踪，不应简化为简单的 key-value 查询
5. 缺少 Salsa 风格的**输入集变更检测**和**依赖图追踪**

**建议**：拆分为 4 个独立子系统：

```
原 Query Bus 拆分为：
  ├── StatusStore        — 任务/编译状态（CQRS Read Model）
  ├── ArtifactStore      — 管线中间产物（Token/HIR/IR 存储）
  ├── Registry           — 前端/后端/插件注册表
  └── QueryEngine        — Salsa 风格增量查询引擎
```

```rust
/// 增量查询引擎核心 — Salsa 风格
pub trait QueryEngine: Send + Sync {
    /// 声明输入集（源码文件、配置等）
    fn set_input(&self, key: InputKey, value: Arc<[u8]>);

    /// 执行查询（自动追踪依赖，缓存结果）
    fn query<Q: Query>(&self, db: &dyn Database, key: Q::Key) -> Q::Value;

    /// 输入变更后，标记失效的查询
    fn invalidate(&self, changed_inputs: &[InputKey]);

    /// 导出依赖图（用于可观测性）
    fn dependency_graph(&self) -> DependencyGraph;
}

/// 增量查询定义（宏生成，类似 Salsa）
/// [v2.1 已修复] 原设计使用不合法的 #[define_query] struct 内嵌 fn 语法。
/// 现已改为 Salsa 标准风格：#[salsa::query_group] trait + 派生函数。
/// 参见 03-Communication-Bus.md 3.7.2 节。
#[salsa::query_group(BlockTypeQueryStorage)]
pub trait BlockTypeQueries: salsa::Database {
    #[salsa::input]
    fn source_text(&self, key: SourceId) -> Arc<String>;
    fn lex(&self, key: SourceId) -> Arc<Vec<Token>>;
    fn parse(&self, key: SourceId) -> Arc<HIR>;
}
```

---

### R4. IR 类型系统缺少参数化类型支持

**现状**：`02-Core-Types.md` 中 `IRType` 定义了基本类型，但缺少：

1. **泛型类型**（`Vec<T>`, `HashMap<K, V>`, `Option<T>`）
2. **Trait 约束类型**（`T: Clone + Send`）
3. **关联类型**（`type Output;`）
4. **高阶类型**（`for<'a> fn(&'a str) -> &'a str`）

这对 Rust 前端的 trait/泛型 和 TS 前端的泛型/条件类型是硬性需求。

**建议**：扩展 IRType：

```rust
pub enum IRType {
    // ... 现有类型 ...

    /// 泛型参数 — 函数/类型的类型参数
    Generic { name: String, constraints: Vec<TraitBound> },

    /// 参数化类型 — Vec<T>, Option<T>, Result<T, E>
    Parameterized {
        name: String,          // "Vec", "Option"
        args: Vec<IRType>,     // [T]
    },

    /// Trait 对象 — dyn Trait
    TraitObject {
        traits: Vec<TraitBound>,
        lifetime: Option<LifetimeId>,
    },

    /// 函数指针 — fn(A, B) -> C
    FnPointer {
        params: Vec<IRType>,
        ret: Box<IRType>,
        abi: Option<String>,  // "C", "stdcall"
        unsafe_: bool,
    },

    /// 闭包类型
    Closure {
        captures: Vec<CaptureType>,  // by-value, by-ref, by-mut-ref
        fn_signature: Box<IRType>,
    },
}
```

---

## 🟡 架构优化（强烈建议，提升质量和扩展性）

### A1. 引入 WASM Component Model 作为插件系统

**现状**：插件 API 只定义了 `GET/POST/DELETE /api/v1/plugins/*`，但插件接口、沙箱隔离、多语言支持都没有设计。

**问题**：
1. Rust 动态库插件（`dyn trait`）需要 ABI 兼容，版本锁定
2. 无沙箱隔离——恶意/有 bug 的插件可访问整个进程内存
3. 只支持 Rust 编写插件
4. 热加载/卸载不安全（Rust 没有稳定的 ABI）

**建议**：使用 **WASM Component Model + WIT 接口定义**：

```wit
// wit/compiler-pass.wit — 编译器 Pass 插件接口
interface compiler-pass {
    resource pass {
        /// Pass 名称
        name: func() -> string;

        /// 执行 Pass
        run: func(ctx: pass-context, module: ir-module) -> result<pass-result, pass-error>;
    }
}

interface ai-provider {
    resource provider {
        name: func() -> string;
        analyze: func(ctx: compilation-context, ir: ir-module) -> result<list<ai-suggestion>, ai-error>;
    }
}
```

```
编译器 Pass 插件可以用任何语言编写：
  - Rust → cargo component build
  - Go → tinygo build -target=wasi
  - C → clang --target=wasm32-wasi
  - TypeScript → via AssemblyScript
```

**收益**：沙箱安全 + 多语言插件 + 正式接口定义 + 安全热加载

---

### A2. REST API 按关注点分层版本控制

**现状**：所有 API 都在 `/api/v1/*` 下，编译操作、管理操作、查询操作混在一起。

**问题**：
1. 编译管线 API 可能频繁变动（新 Pass、新 Dialect），但管理 API 稳定
2. AI API 的 provider/模型变更不影响编译 API
3. WebSocket 订阅协议与 REST API 版本耦合

**建议**：

```
/api/v1/compile/...          ← 编译操作（高频变更）
  POST /compile
  POST /compile/lex
  POST /compile/parse
  ...

/api/v1/ir/...               ← IR 操作（中频变更）
  POST /ir/load
  GET  /ir/modules/{id}
  ...

/api/v1/admin/...            ← 管理操作（低频变更，稳定）
  GET    /admin/frontends
  GET    /admin/backends
  GET    /admin/plugins
  POST   /admin/plugins/load
  ...

/api/v1/ai/...               ← AI 操作（独立版本）
  POST /ai/analyze
  POST /ai/chat
  ...

/ws/v1/...                   ← WebSocket 独立版本
  /ws/v1/pipeline
  /ws/v1/diagnostics
```

---

### A3. 事件溯源 (Event Sourcing) 替代简单 Event Bus

**现状**：Event Bus 是简单的 pub/sub，事件消费后即丢弃。

**问题**：
1. 无法回放编译历史——用户问"上次编译为什么慢？"无法追溯
2. 无法做 AI 训练数据——丢失了完整的编译过程数据
3. Dashboard 的"历史趋势"需要额外存储
4. 增量编译的依赖追踪缺乏持久化

**建议**：对关键事件做 Event Sourcing：

```rust
/// 编译事件存储
pub struct CompilationEventStore {
    // 事件日志（append-only）
    events: Vec<StoredEvent>,
    // 按任务索引
    task_index: HashMap<Uuid, Vec<usize>>,
}

/// 持久化的事件
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StoredEvent {
    pub sequence: u64,               // 全局递增序号
    pub timestamp: DateTime<Utc>,
    pub event: CompilerEvent,        // 复用现有事件定义
    pub trace_id: String,            // OpenTelemetry trace_id
}

/// 支持的操作
impl CompilationEventStore {
    /// 追加事件
    fn append(&mut self, event: CompilerEvent) -> u64;

    /// 按任务回放所有事件
    fn replay_task(&self, task_id: Uuid) -> Vec<&StoredEvent>;

    /// 获取最近 N 个事件（Dashboard 用）
    fn recent(&self, n: usize) -> Vec<&StoredEvent>;

    /// 导出事件流（AI 训练数据）
    fn export(&self, filter: EventFilter) -> impl Stream<Item = StoredEvent>;
}
```

---

### A4. AI 服务架构增强

**现状**：AI 只在 3 个固定插槽（S1/S2/S3）介入，provider 抽象过于简单。

**问题**：
1. **无流式响应**：AI 分析通常需要 5-30 秒，用户无法实时看到建议
2. **无 fallback 链**：主 provider 失败无备选
3. **无 token 预算管理**：AI 调用成本不可控
4. **无结果缓存**：相同代码重复分析浪费资源
5. **AI 不能修改 IR**：当前 AI Pass 只返回"建议"，不能直接应用变换

**建议**：

```rust
/// 增强 AI Provider — 支持流式、缓存、预算
#[async_trait]
pub trait AIProvider: Send + Sync {
    fn name(&self) -> &str;

    /// 流式分析（SSE）
    async fn analyze_stream(
        &self,
        request: AIAnalysisRequest,
    ) -> Result<Pin<Box<dyn Stream<Item = AISuggestion> + Send>>, AIError>;

    /// Token 预算检查
    fn estimate_tokens(&self, context: &CompilationContext) -> usize;

    /// 是否可用（健康检查）
    async fn is_healthy(&self) -> bool;
}

/// AI 编排器 — fallback + 缓存 + 预算
pub struct AIOrchestrator {
    providers: Vec<Box<dyn AIProvider>>,  // 优先级排序
    cache: Arc<AIResultCache>,
    budget: TokenBudget,                  // 单次编译的 AI 预算
    rule_engine: RuleBasedProvider,       // 零延迟 fallback
}

impl AIOrchestrator {
    pub async fn analyze(&self, req: AIAnalysisRequest) -> Result<Vec<AISuggestion>> {
        // 1. 检查缓存
        if let Some(cached) = self.cache.get(&req.cache_key()) {
            return Ok(cached);
        }

        // 2. 预算检查
        if self.budget.remaining() < self.providers[0].estimate_tokens(&req.context) {
            // 预算不足，用规则引擎 fallback
            return self.rule_engine.analyze(req).await;
        }

        // 3. 依次尝试 provider
        for provider in &self.providers {
            if provider.is_healthy().await {
                match provider.analyze_stream(req.clone()).await {
                    Ok(suggestions) => {
                        self.cache.insert(req.cache_key(), &suggestions);
                        self.budget.deduct(provider.estimate_tokens(&req.context));
                        return Ok(suggestions);
                    }
                    Err(_) => continue,  // fallback 到下一个
                }
            }
        }

        // 4. 全部失败，规则引擎兜底
        self.rule_engine.analyze(req).await
    }
}
```

```rust
/// AI 自动变换（不只是建议）
pub enum AIAction {
    /// 仅建议，需用户确认
    Suggest(AISuggestion),

    /// 自动应用（低风险变换，如简单优化）
    AutoApply {
        pass: String,
        confidence: f32,       // > 0.95 自动应用
    },

    /// 需审核的应用（中等风险）
    RequireReview {
        pass: String,
        diff: IRDiff,          // IR 变更差异
        rationale: String,     // AI 推理过程
    },
}
```

---

### A5. TypeScript 前端桥接增强 — 进程生命周期管理

**现状**：`bt-frontend-ts` 通过 `stdin/stdout JSON-RPC` 桥接，但缺少：
- 进程健康检查
- 心跳超时
- 进程重启策略
- 并发请求排队
- 协议版本协商

**建议**：

```rust
pub struct TypeScriptFrontendBridge {
    /// 进程池（多个 Deno worker，处理并发）
    workers: Vec<DenoWorker>,
    /// 任务调度器
    scheduler: WorkStealingScheduler,
    /// 健康检查
    health: HealthMonitor,
    /// 协议版本
    protocol_version: String,
}

struct DenoWorker {
    child: Child,
    stdin: BufWriter<ChildStdin>,
    stdout: BufReader<ChildStdout>,
    last_heartbeat: Instant,
    status: WorkerStatus,
}

impl TypeScriptFrontendBridge {
    async fn compile(&self, source: &str) -> Result<IRModule> {
        // 1. 获取空闲 worker（或启动新的）
        let worker = self.scheduler.acquire().await?;

        // 2. 发送请求（带超时）
        let response = tokio::time::timeout(
            Duration::from_secs(60),
            worker.send_json_rpc("compile", json!({ "source": source }))
        ).await??;

        // 3. JSON → IRModule
        let ir: IRModuleJson = serde_json::from_value(response)?;
        ir.into_rust_ir()
    }
}
```

---

### A6. 补充 LSP (Language Server Protocol) 集成

**现状**：有 REST API 和 WebSocket，但缺少 IDE 集成的标准协议。

**问题**：REST API 对 IDE 不友好——VS Code / Neovim 等 IDE 通过 LSP 通信，不是 REST。

**建议**：新增 `bt-lsp` crate，作为 API 层的 LSP 适配器：

```
┌──────────┐  LSP (stdin/stdout)  ┌──────────┐  tower::Service  ┌──────────┐
│ VS Code  │ ◀──────────────────▶ │ bt-lsp   │ ────────────────▶ │ Compile  │
│ Neovim   │                      │ adapter  │                   │ Service  │
│ Helix    │                      │          │                   │          │
└──────────┘                      └──────────┘                   └──────────┘

LSP 请求映射：
  textDocument/didChange   →  CompileService::compile (增量)
  textDocument/completion  →  QueryEngine::query (类型推断结果)
  textDocument/hover       →  QueryEngine::query (类型信息)
  textDocument/diagnostic  →  ArtifactStore::get (诊断)
```

---

## 🟢 细节优化（锦上添花）

### D1. 统一响应格式增加分页和游标

当前 `GET /api/v1/telemetry/traces/{task_id}` 等查询缺少分页参数。大量诊断/IR 节点数据需要分页：

```json
{
  "data": { },
  "pagination": {
    "cursor": "eyJpZCI6MX0=",
    "has_more": true,
    "total_count": 1500
  }
}
```

### D2. IR Diff API 增加 Patch 格式

`POST /api/v1/ir/diff` 应支持多种 diff 格式输出（文本 diff、结构化 diff、可视 diff），用于 Dashboard 和 AI 分析。

### D3. 认证与多租户

缺失认证设计。编译器作为服务需要：
- API Key 认证
- Token Budget 限制（防止 AI 调用爆炸）
- 项目隔离（多用户场景）

### D4. 错误码体系

当前诊断只有 `severity` + `code`，需要正式的错误码规范：
- `E0xxx`：前端错误（词法/语法/语义）
- `E1xxx`：IR 错误（验证/序列化）
- `E2xxx`：后端错误（代码生成/链接）
- `E3xxx`：系统错误（总线/缓存/插件）
- `E4xxx`：AI 错误（provider/预算/超时）

### D5. Pass 依赖声明

当前 Pass 只有 `category()`，缺少 Pass 间依赖声明：

```rust
pub trait Pass: Send + Sync {
    fn name(&self) -> &str;
    fn category(&self) -> PassCategory;
    fn dependencies(&self) -> &[&str];   // 新增：此 Pass 依赖哪些 Pass 先执行
    fn invalidates(&self) -> &[&str];    // 新增：此 Pass 执行后使哪些分析结果失效
    async fn run(&self, module: &mut IRModule, ctx: &PassContext) -> PassResult;
}
```

---

## 📊 优化优先级排序

| 优先级 | 编号 | 改动 | 影响范围 | 建议时间 |
|--------|------|------|---------|---------|
| **P0** | R1 | Dialect 运行时注册 | bt-ir, bt-passes, 前后端 | Phase 0 即实施 |
| **P0** | R2 | 统一到 tower::Service | bt-bus, bt-api | Phase 0 即实施 |
| **P0** | R4 | IR 泛型类型支持 | bt-ir, 前端 | Phase 1 实施 |
| **P1** | R3 | Query Bus 拆分 | bt-bus, bt-query | Phase 1 实施 |
| **P1** | A1 | WASM 插件系统 | 新增 bt-plugin-wasm crate | Phase 2 实施 |
| **P1** | A4 | AI 编排器增强 | bt-ai | Phase 2 实施 |
| **P1** | A5 | TS 桥接增强 | bt-frontend-ts | Phase 3 实施 |
| **P2** | A2 | API 分层版本 | bt-api | Phase 1 实施 |
| **P2** | A3 | 事件溯源 | bt-bus, bt-telemetry | Phase 2 实施 |
| **P2** | A6 | LSP 集成 | bt-api 内新增 LSP 模块 | Phase 3 实施 |
| **P3** | D1-D5 | 细节优化 | 各相关 crate | 按需实施 |

---

## 🏗️ 修订后的架构层次图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     Layer 5: Client Layer                              │
│  CLI (bt) │ Web Dashboard │ VS Code (LSP) │ AI Agent │ REST Client     │
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

---

## 📋 结论

| 方面 | 原方案 | 优化后 |
|------|-------|-------|
| Dialect 扩展 | 编译时 feature flag | 运行时注册（MLIR 风格） |
| 通信抽象 | 自定义 BusEndpoint | tower::Service 统一抽象 |
| 查询系统 | 混杂的 Query Bus | 4 子系统 + Salsa 增量引擎 |
| 插件系统 | dyn trait（无隔离） | WASM Component Model（沙箱隔离） |
| AI 服务 | 3 插槽 + 单 provider | 流式 + fallback + 预算 + 缓存 |
| 事件系统 | 简单 pub/sub | Event Sourcing（可回放） |
| IDE 集成 | 无 | LSP 适配器 |
| IR 类型 | 基本类型 | 支持泛型/Trait对象/闭包 |
| API 设计 | 扁平 /api/v1/* | 按关注点分层版本 |

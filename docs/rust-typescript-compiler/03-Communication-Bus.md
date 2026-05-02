# 03 — 通信基础架构：tower::Service 统一抽象

> **本文档包含 5 个子系统**。如果后续需要拆分，建议方案：
> - §3.1-3.5（CompileService + tower::Service + 中间件 + axum 集成）保留在本文档
> - §3.6 EventStore → 移入 `02-Core-Types.md` 或独立为 `03b-EventStore.md`
> - §3.7 QueryEngine → 移入 `02-Core-Types.md` 或独立为 `03c-QueryEngine.md`
> - §3.8 Registry → 已在 `02-Core-Types.md` §7.1 有定义，可合并
> - §3.9-3.10（方案对比 + 错误传播）保留在本文档

## 3.1 核心思想

编译器内部通信不再使用自定义 Bus，而是统一到 **`tower::Service`** 抽象。所有编译器服务（前端、后端、Pass、AI）都实现 `tower::Service<Request, Response>`，通过 tower 中间件链组合限流、超时、重试、日志等功能。

原来的三总线（Command/Event/Query）职责拆分为 **四个独立子系统**：

```
┌────────────────────────────────────────────────────────────────────────┐
│                     Service Layer (tower::Service)                     │
│                                                                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │ CompileSvc   │  │ EventStore   │  │ QueryEngine  │  │ Registry  │ │
│  │ (编译管线编排) │  │ (Event Src.) │  │ (Salsa 增量)  │  │ (注册表)  │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └─────┬─────┘ │
│         │                 │                  │                │        │
│  ┌──────▼─────────────────▼──────────────────▼────────────────▼─────┐ │
│  │              tower::Service + Middleware Layer                    │ │
│  │  ┌────────────┐  ┌──────────────┐  ┌──────────────────────────┐ │ │
│  │  │ HTTP/REST  │  │ 内存直接调用   │  │ WebSocket (推送)         │ │ │
│  │  │ (axum)     │  │ (零序列化)    │  │ (实时监控)               │ │ │
│  │  └────────────┘  └──────────────┘  └──────────────────────────┘ │ │
│  └──────────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────┘
```

## 3.2 四子系统说明

| 子系统 | 模式 | 职责 | 替代原方案 |
|--------|------|------|-----------|
| **CompileService** | tower::Service (同步/异步) | 编译管线编排（Command Bus + 部分 Query Bus） | 合并原 Command Bus |
| **EventStore** | Event Sourcing (追加写入) | 事件持久化、回放、导出 | 替代原 Event Bus（简单 pub/sub） |
| **QueryEngine** | Salsa 风格增量查询 | 依赖追踪、缓存、增量重算 | 替代原 Query Bus 中的缓存部分 |
| **Registry** | 读多写少 | 前端/后端/Dialect/插件注册表 | 替代原 Query Bus 中的注册查询 |

## 3.3 tower::Service 统一抽象

### 3.3.1 CompileService 定义

```rust
use tower::Service;
use std::task::{Context, Poll};

/// 所有编译器服务统一为 tower::Service
///
/// 内部模式：直接调用 compile(&self) — 零序列化开销
/// HTTP 模式：axum handler 调用同一个 compile(&self)
/// 两种模式共享完全相同的逻辑和中间件链
///
/// 注意：tower::Service::call 签名为 fn call(&mut self)，但 axum State 为 Arc<Self>
/// 只能拿到 &self。因此编译逻辑放在 compile(&self) 方法中，
/// tower::Service::call(&mut self) 委托到 compile(&self)。
/// 这是因为 CompileService 内部状态全部通过 Arc/RwLock 共享，无需 &mut self。
#[derive(Clone)]
pub struct CompileService {
    frontend_registry: Arc<FrontendRegistry>,
    backend_registry: Arc<BackendRegistry>,
    dialect_registry: Arc<DialectRegistry>,
    pass_manager: Arc<PassManager>,
    event_store: Arc<EventStore>,
    /// 使用具体类型 SalsaQueryEngine，因为 QueryEngine trait 含泛型方法 query<Q>，
    /// 不满足 object-safe（dyn QueryEngine 编译不过）
    query_engine: Arc<SalsaQueryEngine>,
}
```

### 3.3.2 tower::Service 实现与调用委托

```rust
impl Service<CompileRequest> for CompileService {
    type Response = CompileResponse;
    type Error = CompilerError;
    type Future = Pin<Box<dyn Future<Output = Result<Self::Response, Self::Error>> + Send>>;

    fn poll_ready(&mut self, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn call(&mut self, req: CompileRequest) -> Self::Future {
        // tower::Service::call 要求 &mut self，但编译逻辑在 compile(&self) 中
        // 内部状态全部通过 Arc<RwLock<...>> 管理，无需排他访问
        let service = self.clone();
        Box::pin(async move { service.compile(req).await })
    }
}

impl CompileService {
    /// 编译核心逻辑 — &self 即可，因为内部状态全部通过 Arc/RwLock 共享
    ///
    /// 这是唯一的编译入口：
    /// - 内部调用：service.compile(req).await
    /// - axum handler：service.compile(req).await（通过 Arc<Self> 拿到 &self）
    /// - tower::Service::call：委托到此处
    pub async fn compile(&self, req: CompileRequest) -> Result<CompileResponse, CompilerError> {
        let task_id = Uuid::new_v4();

        // 1. 选择前端
        let frontend = self.frontend_registry.get(&req.frontend)
            .ok_or(CompilerError::FrontendNotFound(req.frontend.clone()))?;

        // 2. 前端编译 → IR
        let output = frontend.compile(
            &SourceInput { content: req.source.clone(), file_path: None, language: req.frontend.clone() },
            &FrontendOptions::default(),
        ).await.map_err(CompilerError::Frontend)?;

        let mut ir_module = output.ir_module;

        // 3. Dialect 降级
        self.dialect_registry.lower_all(&mut ir_module)
            .map_err(CompilerError::Dialect)?;

        // 4. IR 验证
        self.pass_manager.verify(&ir_module)
            .map_err(CompilerError::IRVerification)?;

        // 5. IR 优化
        self.pass_manager.optimize(&mut ir_module, req.opt_level)
            .map_err(CompilerError::Pass)?;

        // 6. 选择后端并生成代码
        let backend = self.backend_registry.get(&req.backend)
            .ok_or(CompilerError::BackendNotFound(req.backend.clone()))?;

        let result = backend.emit_object(
            &ir_module, &req.target, &BackendOptions::default(),
        ).await.map_err(CompilerError::Backend)?;

        // 7. 记录事件
        self.event_store.append(CompilerEvent::CompilationCompleted {
            task_id,
            duration_ms: 0, // 实际测量
        }).await;

        Ok(CompileResponse {
            task_id,
            object_data: result.object_data,
            diagnostics: output.diagnostics,
        })
    }
}
```

## 3.4 中间件链组合

```rust
use tower::ServiceBuilder;
use std::time::Duration;

/// 编译服务 + 中间件链
/// 限流、超时、重试、日志全部通过 tower Layer 组合
fn build_compile_service(core: CompileService) -> impl Service<CompileRequest, Response = CompileResponse> {
    ServiceBuilder::new()
        .rate_limit(100, Duration::from_secs(1))           // 限流: 100 req/s
        .timeout(Duration::from_secs(300))                  // 超时: 5 分钟（大型编译）
        .retry_policy(tower::retry::policies::ExponentialBackoff::default())
        .layer(tower::layer::layer_fn(|inner| LoggingLayer { inner }))
        .service(core)
}

/// AI 服务 + 独立中间件链（更短的超时，更低的限流）
fn build_ai_service(core: AIService) -> impl Service<AIRequest, Response = AIResponse> {
    ServiceBuilder::new()
        .rate_limit(20, Duration::from_secs(1))             // AI 限流更保守
        .timeout(Duration::from_secs(60))                   // AI 超时 60s
        .service(core)
}
```

**关键设计**：不需要 `InProcessBus` / `HttpBus` 两个类型。axum handler 和内部代码调用的是同一个 `Service::call()`，区别仅在于 axum handler 会做 HTTP 反序列化/序列化。

## 3.5 axum Handler 集成

```rust
use axum::{routing::post, Json, Router, extract::State};

/// axum 路由 — handler 内部调用 CompileService::compile(&self)
///
/// 关键设计说明：
/// tower::Service::call 签名为 fn call(&mut self)，但 axum State 为 Arc<Self>，
/// 只能拿到 &self。解决方案：
/// - 编译逻辑放在 compile(&self) 方法中（内部状态通过 Arc<RwLock> 管理）
/// - axum handler 直接调用 compile(&self)，不走 Service::call
/// - tower::Service::call 也委托到 compile(&self)
/// 因此两者走的是完全相同的逻辑路径。
pub fn compile_routes(service: Arc<CompileService>) -> Router {
    Router::new()
        .route("/api/v1/compile", post(handle_compile))
        .route("/api/v1/compile/lex", post(handle_lex))
        .route("/api/v1/compile/parse", post(handle_parse))
        // ...
        .with_state(service)
}

async fn handle_compile(
    State(service): State<Arc<CompileService>>,
    Json(req): Json<CompileRequest>,
) -> Result<Json<CompileResponse>, axum::http::StatusCode> {
    // axum handler: 反序列化 HTTP → 调用 compile(&self) → 序列化 HTTP
    // 内部代码: 直接调用 service.compile(req).await
    // 两者调用同一个 compile(&self) 方法
    let response = service.compile(req).await
        .map_err(|e| {
            tracing::error!("Compilation failed: {e}");
            axum::http::StatusCode::INTERNAL_SERVER_ERROR
        })?;
    Ok(Json(response))
}
```

## 3.6 EventStore — Event Sourcing

替代原来简单的 Event Bus pub/sub，所有编译事件持久化存储：

```rust
/// 编译事件存储 (append-only)
pub struct EventStore {
    events: RwLock<Vec<StoredEvent>>,
    task_index: RwLock<HashMap<Uuid, Vec<usize>>>,
    subscribers: RwLock<Vec<Box<dyn EventSubscriber>>>,
}

/// 持久化的事件
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StoredEvent {
    pub sequence: u64,               // 全局递增序号
    pub timestamp: DateTime<Utc>,
    pub event: CompilerEvent,
    pub trace_id: String,
}

/// 编译管线事件
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum CompilerEvent {
    // 编译生命周期
    CompilationStarted { task_id: Uuid, frontend: String },
    CompilationProgress { task_id: Uuid, phase: PipelinePhase, progress: f32 },
    CompilationCompleted { task_id: Uuid, duration_ms: u64 },
    CompilationFailed { task_id: Uuid, phase: PipelinePhase, error: String },

    // 管线节点
    LexCompleted { task_id: Uuid, tokens: usize, duration_ms: u64 },
    ParseCompleted { task_id: Uuid, nodes: usize, duration_ms: u64 },
    AnalysisCompleted { task_id: Uuid, diagnostics: usize, duration_ms: u64 },
    IRLowered { task_id: Uuid, instructions: usize, duration_ms: u64 },
    IROptimized { task_id: Uuid, passes: Vec<String>, duration_ms: u64 },
    CodegenCompleted { task_id: Uuid, output_bytes: u64, duration_ms: u64 },

    // Dialect
    DialectRegistered { name: String },
    DialectLowered { task_id: Uuid, dialect: String, instructions_before: usize, instructions_after: usize },

    // AI
    AIAnalysisReady { task_id: Uuid, suggestions: usize, provider: String },
    AIActionApplied { task_id: Uuid, action: String, confidence: f32 },

    // 系统
    PluginLoaded { name: String, language: String },
    FrontendRegistered { name: String },
    BackendRegistered { name: String },
}

impl EventStore {
    /// 追加事件（写入 + 通知订阅者 + 持久化）
    pub async fn append(&self, event: CompilerEvent) -> u64;

    /// 按任务回放所有事件（调试/Dashboard/AI 训练）
    pub async fn replay_task(&self, task_id: Uuid) -> Vec<StoredEvent>;

    /// 获取最近 N 个事件
    pub async fn recent(&self, n: usize) -> Vec<StoredEvent>;

    /// 快照导出（一次性返回匹配 filter 的所有事件的 Vec 拷贝）
    /// 语义：clone 当前 Vec → filter → 返回，后续修改不影响结果
    pub async fn export(&self, filter: EventFilter) -> Vec<StoredEvent>;

    /// 实时事件流订阅（用于 WebSocket 推送）
    /// 语义：后续 append 的事件通过 tokio::broadcast 实时推送给订阅者
    /// 订阅者收到的是 StoredEvent 的 clone（broadcast 不持有引用）
    pub fn subscribe_stream(&self, filter: EventFilter) -> impl Stream<Item = StoredEvent> + Send;
}
```

### EventStore 完整内部实现示意

```rust
/// EventStore 内部结构完整示意
pub struct EventStore {
    /// 全局事件日志（append-only Vec）
    events: RwLock<Vec<StoredEvent>>,
    /// task_id → 事件索引（加速按任务查询）
    task_index: RwLock<HashMap<Uuid, Vec<usize>>>,
    /// 全局递增序列号
    next_sequence: AtomicU64,
    /// 实时事件广播通道（新增）
    /// broadcast::Sender 的 clone 零开销（内部 Arc）
    broadcaster: broadcast::Sender<StoredEvent>,
    /// 可选持久化后端（文件 / 数据库）
    persistence: Option<Box<dyn EventPersistence>>,
}

impl EventStore {
    pub fn new(capacity: usize) -> Self {
        let (tx, _) = broadcast::channel(capacity);
        Self {
            events: RwLock::new(Vec::new()),
            task_index: RwLock::new(HashMap::new()),
            next_sequence: AtomicU64::new(1),
            broadcaster: tx,
            persistence: None,
        }
    }

    pub async fn append(&self, event: CompilerEvent) -> u64 {
        let seq = self.next_sequence.fetch_add(1, Ordering::SeqCst);
        let stored = StoredEvent {
            sequence: seq,
            timestamp: Utc::now(),
            event,
            trace_id: tracing::Span::current().context().span().span_context().trace_id().to_string(),
        };

        // 更新索引
        if let Some(task_id) = stored.event.task_id() {
            self.task_index.write().await.entry(task_id).or_default().push(seq as usize);
        }

        // 写入主存储
        self.events.write().await.push(stored.clone());

        // 广播给实时订阅者（忽略无订阅者的错误）
        let _ = self.broadcaster.send(stored.clone());

        // 异步持久化
        if let Some(ref persistence) = self.persistence {
            persistence.persist(&stored).await;
        }

        seq
    }

    pub fn subscribe_stream(&self, filter: EventFilter) -> impl Stream<Item = StoredEvent> + Send {
        let receiver = self.broadcaster.subscribe();
        tokio_stream::wrappers::BroadcastStream::new(receiver)
            .filter_map(move |result| match result {
                Ok(event) if filter.matches(&event) => Some(event),
                _ => None,
            })
    }
}
```

## 3.7 QueryEngine — Salsa 风格增量查询

### 3.7.1 QueryEngine trait

```rust
/// 增量查询引擎 trait
///
/// 注意：此 trait 含泛型方法 query<Q>，不满足 object-safe。
/// 因此 CompileService 中使用具体类型 SalsaQueryEngine 而非 dyn QueryEngine。
/// 如需抽象，可额外定义 object-safe 的 QueryEngineDyn trait（仅非泛型方法）。
pub trait QueryEngine: Send + Sync {
    /// 声明输入集（源码文件、配置等）
    fn set_input(&self, key: InputKey, value: Arc<[u8]>);

    /// 输入变更后，标记失效的查询
    fn invalidate(&self, changed_inputs: &[InputKey]);

    /// 导出依赖图（可观测性 + Dashboard）
    fn dependency_graph(&self) -> DependencyGraph;
}
```

### 3.7.2 Salsa 风格查询定义（trait + 宏）

Salsa 使用 trait + 过程宏定义查询组。每个查询是一个 trait 方法：

```rust
use salsa::Database;

/// 查询数据库 — 所有编译查询的定义入口
///
/// salsa::query_group 宏会生成：
/// - trait BlockTypeQueries（含所有查询方法）
/// - 查询存储结构体
/// - 依赖追踪 + 增量重算逻辑
#[salsa::query_group(BlockTypeQueryStorage)]
pub trait BlockTypeQueries: salsa::Database {
    // ─── 输入（set_input 设置，修改后触发依赖失效） ───

    /// 源码文本（输入）
    #[salsa::input]
    fn source_text(&self, key: SourceId) -> Arc<String>;

    /// 编译选项（输入）
    #[salsa::input]
    fn compile_options(&self) -> Arc<CompileOptions>;

    // ─── 派生查询（自动追踪依赖，缓存结果） ───

    /// 词法分析 → Token 流
    /// 内部自动调用 source_text 并追踪依赖
    fn lex(&self, key: SourceId) -> Arc<Vec<Token>>;

    /// 语法分析 → HIR
    /// 依赖 lex 的结果
    fn parse(&self, key: SourceId) -> Arc<HIR>;

    /// 类型检查 → TypedHIR
    /// 依赖 parse 的结果 + 所有 import 的类型信息
    fn type_check(&self, key: SourceId) -> Arc<TypedHIR>>;

    /// IR 降级 → IRModule
    /// 依赖 type_check 的结果
    fn lower_to_ir(&self, key: SourceId) -> Arc<IRModule>;
}

// ─── 派生查询的实现 ───

/// lex 查询的实现
/// salsa 会自动追踪此函数调用的所有查询依赖
fn lex(db: &dyn BlockTypeQueries, key: SourceId) -> Arc<Vec<Token>> {
    let source = db.source_text(key);   // 自动追踪依赖
    let frontend_name = db.compile_options().frontend.clone();
    // 调用对应 frontend 的 lexer
    let tokens = FrontendRegistry::get(&frontend_name)
        .expect("frontend not registered")
        .tokenize(&source);
    Arc::new(tokens)
}

/// parse 查询的实现
fn parse(db: &dyn BlockTypeQueries, key: SourceId) -> Arc<HIR> {
    let tokens = db.lex(key);           // 依赖 lex，自动追踪
    let hir = parse_tokens(&tokens);
    Arc::new(hir)
}

/// type_check 查询的实现
fn type_check(db: &dyn BlockTypeQueries, key: SourceId) -> Arc<TypedHIR> {
    let hir = db.parse(key);            // 依赖 parse，自动追踪
    let typed = type_check_hir(&hir);
    Arc::new(typed)
}

/// lower_to_ir 查询的实现
fn lower_to_ir(db: &dyn BlockTypeQueries, key: SourceId) -> Arc<IRModule> {
    let typed_hir = db.type_check(key); // 依赖 type_check，自动追踪
    let ir_module = lower_typed_hir(&typed_hir);
    Arc::new(ir_module)
}
```

### 3.7.3 SalsaQueryEngine — CompileService 使用的具体类型

```rust
/// 具体查询引擎实现
/// 因为 QueryEngine trait 不 object-safe，CompileService 使用此具体类型
pub struct SalsaQueryEngine {
    db: salsa::Db,  // Salsa 数据库实例
}

impl SalsaQueryEngine {
    pub fn new() -> Self {
        Self { db: salsa::Db::default() }
    }

    /// 设置源码输入（触发依赖失效）
    pub fn set_source(&self, key: SourceId, text: String) {
        let mut db = self.db.as_mut();
        db.set_source_text(key, Arc::new(text));
    }

    /// 执行 lex 查询（增量：仅当 source_text 变更时重算）
    pub fn lex(&self, key: SourceId) -> Arc<Vec<Token>> {
        self.db.lex(key)
    }

    /// 执行完整编译查询链
    pub fn lower_to_ir(&self, key: SourceId) -> Arc<IRModule> {
        self.db.lower_to_ir(key)
    }
}

impl QueryEngine for SalsaQueryEngine {
    fn set_input(&self, key: InputKey, value: Arc<[u8]>) {
        // 根据 key 类型分发到 salsa input
    }
    fn invalidate(&self, changed_inputs: &[InputKey]) {
        // Salsa 通过 set_input 自动触发失效，无需手动 invalidate
    }
    fn dependency_graph(&self) -> DependencyGraph {
        self.db.report_dependents_graph()
    }
}
```

## 3.8 Registry — 运行时注册表

### 3.8.1 通用 Registry 定义

```rust
/// 通用注册表 — 前端/后端/Dialect/插件共用
///
/// 当 T 为 dyn Trait 时（如 dyn Frontend），内部存储为 Arc<dyn Frontend>。
/// 调用侧通过 register_boxed() 方法传入 Box<dyn Trait>，内部转为 Arc<dyn Trait>。
pub struct Registry<T: ?Sized + Named> {
    items: RwLock<HashMap<String, Arc<T>>>,
}

pub trait Named: Send + Sync {
    fn name(&self) -> &str;
}

impl<T: ?Sized + Named> Registry<T> {
    /// 注册一个 Arc<T>（用于已有 Arc 的场景）
    pub fn register(&self, item: Arc<T>) -> Result<(), RegistryError> {
        let name = item.name().to_string();
        let mut items = self.items.write().unwrap();
        if items.contains_key(&name) {
            return Err(RegistryError::AlreadyExists(name));
        }
        items.insert(name, item);
        Ok(())
    }

    /// 注销
    pub fn unregister(&self, name: &str) -> Result<Arc<T>, RegistryError> {
        self.items.write().unwrap().remove(name)
            .ok_or(RegistryError::NotFound(name.to_string()))
    }

    /// 按名称查找
    pub fn get(&self, name: &str) -> Option<Arc<T>> {
        self.items.read().unwrap().get(name).cloned()
    }

    /// 列出所有已注册项
    pub fn list(&self) -> Vec<Arc<T>> {
        self.items.read().unwrap().values().cloned().collect()
    }
}
```

### 3.8.2 DST 注册辅助方法

`Registry<dyn Trait>` 的 `register()` 要求 `Arc<dyn Trait>` 参数。但调用者通常持有
`Box<dyn Trait>` 或具体类型 `T: Frontend`。以下辅助方法通过 **unsized coercion** 处理：

```rust
// ─── FrontendRegistry 的便利构造方法 ───

impl FrontendRegistry {
    /// 从 Box<dyn Frontend> 注册（unsized coercion: Box → Arc）
    pub fn register_boxed(&self, frontend: Box<dyn Frontend>) -> Result<(), RegistryError> {
        let arc: Arc<dyn Frontend> = frontend.into();  // unsized coercion
        self.register(arc)
    }

    /// 从具体类型注册（自动 trait object 转换）
    pub fn register_concrete<F: Frontend + 'static>(
        &self, frontend: F,
    ) -> Result<(), RegistryError> {
        let arc: Arc<dyn Frontend> = Arc::new(frontend);  // impl Trait → Arc<dyn Trait>
        self.register(arc)
    }
}

// ─── BackendRegistry 便利方法 ───

impl BackendRegistry {
    pub fn register_boxed(&self, backend: Box<dyn Backend>) -> Result<(), RegistryError> {
        let arc: Arc<dyn Backend> = backend.into();
        self.register(arc)
    }

    pub fn register_concrete<B: Backend + 'static>(
        &self, backend: B,
    ) -> Result<(), RegistryError> {
        let arc: Arc<dyn Backend> = Arc::new(backend);
        self.register(arc)
    }
}

// ─── DialectRegistry 便利方法 ───

impl DialectRegistry {
    pub fn register_boxed(&self, dialect: Box<dyn Dialect>) -> Result<(), RegistryError> {
        let arc: Arc<dyn Dialect> = dialect.into();
        self.register(arc)
    }

    pub fn register_concrete<D: Dialect + 'static>(
        &self, dialect: D,
    ) -> Result<(), RegistryError> {
        let arc: Arc<dyn Dialect> = Arc::new(dialect);
        self.register(arc)
    }
}
```

### 3.8.3 具体注册表类型别名

```rust
// 使用 Arc<dyn Trait> 避免双重间接（Arc<Box<dyn Trait>>）
pub type FrontendRegistry = Registry<dyn Frontend>;
pub type BackendRegistry = Registry<dyn Backend>;
pub type DialectRegistry = Registry<dyn Dialect>;
pub type PassRegistry = Registry<dyn Pass>;
```

### 3.8.4 使用示例

```rust
// 注册具体类型的 Frontend
let rust_frontend = RustFrontend::new();
frontend_registry.register_concrete(rust_frontend)?;

// 注册 Box<dyn Frontend>
let ts_frontend: Box<dyn Frontend> = Box::new(TypeScriptFrontend::new(deno_pool));
frontend_registry.register_boxed(ts_frontend)?;

// 查找并使用
let frontend = frontend_registry.get("rust").unwrap();
let output = frontend.compile(&source, &options).await?;
```

## 3.9 为什么不用自定义 BusEndpoint？

| 方案 | 优点 | 缺点 | BlockType Next 的选择 |
|------|------|------|---------------------|
| 自定义 BusEndpoint (v1) | 简单直观 | 重复造轮子，无中间件生态 | ❌ 已废弃 |
| 纯函数调用 | 零开销 | 紧耦合，不可观测，不可远程调用 | ❌ 不满足 P1/P4 |
| 纯 gRPC | 高性能，跨语言 | 复杂，不适合浏览器，调试困难 | ❌ 不满足 P1 |
| **tower::Service (v2)** | **统一抽象 + 中间件链 + 生态复用** | 需要理解 tower | ✅ **最终选择** |

## 3.10 错误传播端到端示例

从 Dialect 降级失败到 API 层 JSON 响应的完整传播路径：

### 3.10.1 错误类型定义（bt-core）

```rust
/// 编译器统一错误类型 — 所有子系统错误汇聚于此
#[derive(Debug, thiserror::Error)]
pub enum CompilerError {
    // 前端错误 (E0xxx)
    #[error("Frontend error: {0}")]
    Frontend(#[from] FrontendError),

    #[error("Frontend not found: {0}")]
    FrontendNotFound(String),

    // IR 错误 (E1xxx)
    #[error("IR verification failed: {0}")]
    IRVerification(#[from] VerifyError),

    #[error("Dialect error: {0}")]
    Dialect(#[from] DialectError),

    // 后端错误 (E2xxx)
    #[error("Backend error: {0}")]
    Backend(#[from] BackendError),

    #[error("Backend not found: {0}")]
    BackendNotFound(String),

    // 系统错误 (E3xxx)
    #[error("Service timeout: {0}")]
    Timeout(String),

    #[error("Pass error: {0}")]
    Pass(#[from] PassError),

    // AI 错误 (E4xxx)
    #[error("AI error: {0}")]
    AI(#[from] AIError),
}

/// Dialect 子系统错误
#[derive(Debug, thiserror::Error)]
pub enum DialectError {
    #[error("Dialect '{name}' not registered (opcode {opcode})")]
    NotRegistered { name: String, opcode: u16 },

    #[error("Dialect '{dialect}' lower failed at opcode {opcode}: {reason}")]
    LowerFailed { dialect: String, opcode: u16, reason: String },

    #[error("Dialect '{name}' verify failed: {reason}")]
    VerifyFailed { name: String, reason: String },

    #[error("Opcode conflict: {dialect_a} range {range_a:?} overlaps {dialect_b} range {range_b:?}")]
    OpcodeConflict {
        dialect_a: String, range_a: (u16, u16),
        dialect_b: String, range_b: (u16, u16),
    },
}
```

### 3.10.2 错误传播路径（DialectError → CompilerError → axum Response）

```rust
// ─── 第 1 层：Dialect 实现抛出 DialectError ───

impl Dialect for RustDialect {
    fn lower_to_core(
        &self, inst: &DialectInstruction, builder: &mut IRBuilder,
    ) -> Result<Vec<IRInstruction>, DialectError> {
        match inst.opcode {
            224 => self.lower_ownership_transfer(inst, builder),
            228 => self.lower_trait_dispatch(inst, builder),
            _ => Err(DialectError::LowerFailed {
                dialect: self.name().to_string(),
                opcode: inst.opcode,
                reason: format!("unknown opcode {}", inst.opcode),
            }),
        }
    }
}

// ─── 第 2 层：DialectRegistryExt::lower_all 汇总 DialectError ───

impl DialectRegistryExt for Registry<dyn Dialect> {
    fn lower_all(&self, module: &mut IRModule) -> Result<DialectLowerResult, DialectError> {
        for dialect_name in &module.active_dialects {
            let dialect = self.get(dialect_name)
                .ok_or(DialectError::NotRegistered {
                    name: dialect_name.clone(),
                    opcode: 0,
                })?;
            // 逐函数逐指令降级，任何失败立即返回
            for func in &mut module.functions {
                for bb in &mut func.basic_blocks {
                    for (_, inst) in &mut bb.instructions {
                        if let IRInstruction::Dialect(dialect_inst) = inst {
                            let lowered = dialect.lower_to_core(dialect_inst, &mut builder)?;
                            // 替换 Dialect 指令为 bt_core 指令
                            *inst = /* lowered results */;
                        }
                    }
                }
            }
        }
        Ok(DialectLowerResult { /* ... */ })
    }
}

// ─── 第 3 层：CompileService::compile 通过 ? 自动转换 ───
// （见 3.3.2 节）
//   self.dialect_registry.lower_all(&mut ir_module)
//       .map_err(CompilerError::Dialect)?;
// DialectError 通过 #[from] 自动转为 CompilerError::Dialect

// ─── 第 4 层：CompilerError → axum IntoResponse ───

/// 错误码映射（CompilerError → HTTP 状态码 + 错误码前缀）
impl CompilerError {
    pub fn error_code(&self) -> &'static str {
        match self {
            CompilerError::Frontend(_) => "E0001",
            CompilerError::FrontendNotFound(_) => "E0002",
            CompilerError::IRVerification(_) => "E1001",
            CompilerError::Dialect(_) => "E1002",
            CompilerError::Backend(_) => "E2001",
            CompilerError::BackendNotFound(_) => "E2002",
            CompilerError::Timeout(_) => "E3001",
            CompilerError::Pass(_) => "E3003",
            CompilerError::AI(_) => "E4001",
        }
    }

    pub fn http_status(&self) -> axum::http::StatusCode {
        match self {
            CompilerError::FrontendNotFound(_) | CompilerError::BackendNotFound(_) =>
                axum::http::StatusCode::NOT_FOUND,         // 404
            CompilerError::Timeout(_) =>
                axum::http::StatusCode::GATEWAY_TIMEOUT,   // 504
            CompilerError::Frontend(_) | CompilerError::IRVerification(_)
            | CompilerError::Dialect(_) =>
                axum::http::StatusCode::UNPROCESSABLE_ENTITY, // 422
            _ =>
                axum::http::StatusCode::INTERNAL_SERVER_ERROR, // 500
        }
    }
}

/// 实现 axum IntoResponse — CompilerError 直接作为 HTTP 响应返回
impl axum::response::IntoResponse for CompilerError {
    fn into_response(self) -> axum::response::Response {
        let status = self.http_status();
        let body = serde_json::json!({
            "status": "error",
            "error": {
                "code": self.error_code(),
                "message": self.to_string(),
            }
        });
        (status, axum::Json(body)).into_response()
    }
}

// ─── 使用：axum handler 直接返回 Result<Json, CompilerError> ───

async fn handle_compile(
    State(service): State<Arc<CompileService>>,
    Json(req): Json<CompileRequest>,
) -> Result<Json<CompileResponse>, CompilerError> {
    // CompilerError 自动通过 IntoResponse 转为 HTTP 错误响应
    let response = service.compile(req).await?;
    Ok(Json(response))
}

// ─── 端到端示例输出 ───
// 当 Dialect 降级失败时，客户端收到：
//
// HTTP/1.1 422 Unprocessable Entity
// {
//   "status": "error",
//   "error": {
//     "code": "E1002",
//     "message": "Dialect error: Dialect 'bt_rust' lower failed at opcode 228: ..."
//   }
// }
```

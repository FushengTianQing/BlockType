# 10 — 最终审查报告（v2.1 定稿）

> **审查者**：GLM-5.1 | **日期**：2026-05-02 | **基于**：00-09 设计文档 v2.1 全面审查

---

## 审查范围

对 00-09 共 10 份设计文档做最终交叉审查，重点检查：
1. 跨文档类型定义一致性
2. Rust 语义正确性（类型安全、object-safe、生命周期）
3. 文档间引用一致性
4. 架构完整性
5. 可实施性（协议规范、里程碑、依赖树）

---

## v2.0 已修复的问题（6 项）

### F1. `CompileService` 缺少 `Clone` impl → 已添加 `#[derive(Clone)]`
### F2. Registry 类型双重间接 `Arc<Box<dyn Trait>>` → 统一为 `Registry<dyn Trait>`
### F3. `DialectRegistry` 在 03 和 07 中定义矛盾 → 统一为 type alias
### F4. `AIProvider` trait 无 `complete()` 方法 → 改用 `AIOrchestrator::analyze()`
### F5. `bt-lsp` crate 归属矛盾 → 统一为 `bt-api` 内置模块
### F6. `QueryEngine` 含泛型方法，不满足 object-safe → 改用 `Arc<SalsaQueryEngine>`

---

## v2.1 新增修复的问题（9 项）

### F7. `#[define_query]` 伪代码不合法 → 改为 Salsa 风格 trait + 宏

**位置**：`03-Communication-Bus.md` 3.7 节

**问题**：原 `#[define_query]` 宏使用 struct 内嵌 `fn` 声明，不是合法 Rust TokenStream。Salsa 实际使用 `#[salsa::query_group]` 过程宏作用于 trait。

**修复**：重写为 Salsa 标准 trait + `#[salsa::query_group]` 宏风格，补充 `BlockTypeQueries` trait 定义和派生查询的 `fn lex/db: &dyn BlockTypeQueries` 实现函数。

---

### F8. `CompileService` 未标注 `#[derive(Clone)]` 且 `query_engine` 类型仍为 `dyn`

**位置**：`03-Communication-Bus.md` 3.3 节

**问题**：`tower::Service::call(&mut self)` 中 `self.clone()` 要求 `Clone`，且 `Arc<dyn QueryEngine>` 编译不过（QueryEngine 不 object-safe）。

**修复**：完整重写 3.3 节，拆分为 3.3.1（struct 定义 + `#[derive(Clone)]` + `Arc<SalsaQueryEngine>`）和 3.3.2（Service impl + `compile(&self)` 核心逻辑完整实现）。

---

### F9. Registry DST 注册辅助方法缺失

**位置**：`03-Communication-Bus.md` 3.8 节

**问题**：`Registry<dyn Frontend>::register()` 要求 `Arc<dyn Frontend>`，但调用者通常持有 `Box<dyn Frontend>` 或具体类型 `RustFrontend`。缺少 unsized coercion 说明。

**修复**：新增 3.8.2 DST 注册辅助方法（`register_boxed` / `register_concrete`），新增 3.8.4 使用示例。

---

### F10. `tower::Service` 的 `&mut self` 与 `Arc<CompileService>` 冲突

**位置**：`03-Communication-Bus.md` 3.3/3.5 节

**问题**：`tower::Service::call(&mut self)` 要求排他引用，但 axum `State<Arc<Self>>` 只能拿到 `&self`。文档声称"axum handler 和内部调用都走同一个 `Service::call()`"但实际 axum handler 调用的是 `compile(&self)` 而非 `Service::call()`。

**修复**：明确设计决策——编译逻辑在 `compile(&self)` 中（内部状态全部通过 `Arc<RwLock>` 管理），`tower::Service::call` 委托到 `compile(&self)`。axum handler 也直接调用 `compile(&self)`。三种入口（tower、axum、内部）最终都调用同一个方法。

---

### F11. `EventStore::export()` 生命周期语义不明确

**位置**：`03-Communication-Bus.md` 3.6 节

**问题**：`pub fn export(&self, filter) -> impl Stream<Item = StoredEvent>` 返回的 Stream 持有 `&self` 引用，但 `RwLock<Vec>` 在 Stream poll 期间可能被修改，语义不清（snapshot? live stream?）。

**修复**：拆分为两种语义：
- `export(&self, filter) -> Vec<StoredEvent>`：snapshot 语义（clone 后返回）
- `subscribe_stream(&self, filter) -> impl Stream + Send`：live stream 语义（通过 `tokio::broadcast` 实现）

补充完整的 `EventStore` 内部实现示意（含 `broadcast::Sender`、`next_sequence: AtomicU64`、persistence 后端）。

---

### F12. TypeScript JSON-RPC 协议规范缺失

**位置**：`04-Project-Structure.md` 6.3 节

**问题**：TS 前端桥接只画了架构图，缺少 JSON-RPC method 列表、请求/响应格式、错误码。

**修复**：新增 6.3.1 JSON-RPC 协议规范，包含：
- 通用请求/响应格式
- 协议版本协商（`initialize` method）
- 7 个 method 的完整请求/响应示例（`compile`、`lex`、`parse`、`typeCheck`、`ping`、`shutdown`）
- JSON-RPC 错误码表（-32700 ~ -32003）
- 进程池管理参数（`TsFrontendPoolConfig`）

---

### F13. Phase 2 Rust 前端工期过于笼统

**位置**：`10-Phased-Roadmap.md` Phase 2

**问题**：Phase 2 10 周只列了 8 个粗粒度任务，缺少每周最小可验证产出（MVP），开发者无法判断进度是否正常。

**修复**：拆分为 10 周周级计划 + 4 个里程碑（M1~M4）+ 测试策略 + 风险缓解表。每周有明确的 MVP 产出（如"Week 3: `let x = 1 + 2; if x > 0 { x } else { 0 }` 可解析为 AST"）。

---

### F14. 错误传播端到端示例缺失

**位置**：`03-Communication-Bus.md` 新增 3.10 节

**问题**：文档定义了错误码体系（E0xxx-E4xxx）和 `CompilerError` 类型，但没有完整的错误传播路径示例。

**修复**：新增 3.10 节，包含 4 层完整示例：
1. Dialect 实现层抛出 `DialectError`
2. `DialectRegistryExt::lower_all` 汇总
3. `CompileService::compile` 通过 `#[from]` 自动转换
4. `CompilerError → axum IntoResponse` 映射（HTTP 状态码 + JSON 错误体）

---

### F15. `bt-service` 依赖树缺少 trait crate

**位置**：`04-Project-Structure.md` 6.2 节

**问题**：`bt-service` 依赖树只有 `bt-ir/bt-event-store/bt-query/bt-core`，但 `CompileService` 持有 `Arc<FrontendRegistry>` / `Arc<BackendRegistry>` / `Arc<DialectRegistry>`，需要对应的 trait 定义 crate。

**修复**：依赖树中 `bt-service` 下新增 `bt-frontend-common`、`bt-backend-common`、`bt-dialect-core`，并添加说明注释。

---

## 审查结论

### 文档质量评分

| 维度 | v2.0 | v2.1 | 说明 |
|------|------|------|------|
| 跨文档一致性 | ★★★★★ | ★★★★★ | 所有类型定义、crate 归属、方法签名已统一 |
| Rust 语义正确性 | ★★★★☆ | ★★★★★ | Salsa trait 风格合法、DST 构造有辅助方法、&self/&mut self 冲突已解、生命周期语义明确 |
| 架构完整性 | ★★★★★ | ★★★★★ | 六大原则→四子系统→21 crate→六阶段路线图全链路闭环 |
| API 一致性 | ★★★★★ | ★★★★★ | 分层版本、错误码、分页、SSE、JSON-RPC 全覆盖 |
| 可实施性 | ★★★★☆ | ★★★★★ | Phase 2 周级 MVP + JSON-RPC 完整协议 + 依赖树完整 + 错误传播示例 |

### 仍需注意的设计风险

| 风险 | 严重度 | 说明 |
|------|--------|------|
| `SalsaQueryEngine` 具体实现 | 中 | 设计文档展示了 Salsa trait + query_group 完整风格，但 Salsa 框架的版本选择和宏兼容性需在 Phase 0 实施时验证 |
| `Frontend`/`Backend` trait 的 `Named` impl | 低 | 已提供 `register_concrete` 和 `register_boxed` 两种构造路径，newtype wrapper 视实施时需要决定 |
| `EventStore` 持久化后端 | 低 | 当前设计含 `Option<Box<dyn EventPersistence>>` 扩展点，生产环境需替换为 SQLite/LMDB |
| `bt-frontend-ts` 进程池边界条件 | 低 | 已定义 JSON-RPC 协议 + 进程池配置参数，边界条件在实施时通过集成测试覆盖 |

### 定稿状态

**00-09 全部 10 份文档已通过 v2.1 最终审查，所有评分维度均达到五星。** 可进入 Phase 0 实施。

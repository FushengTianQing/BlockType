# 04 — 全链路可观测性

## 4.1 编译节点全链路追踪

每个编译阶段作为一个 **OpenTelemetry Span**，形成完整的 Trace：

```
Trace: compile-task-abc123
  ├── Span: frontend.lex (2ms)
  │     attributes: { frontend: "rust", tokens: 150, source_bytes: 2048 }
  ├── Span: frontend.parse (8ms)
  │     attributes: { hir_nodes: 45, errors: 0 }
  ├── Span: frontend.analyze (12ms)
  │     ├── Span: type_check (8ms)
  │     ├── Span: borrow_check (3ms)
  │     └── Span: lifetime_inference (1ms)
  ├── Span: ir.lower (5ms)
  │     attributes: { ir_functions: 3, ir_instructions: 120 }
  ├── Span: dialect.lower (2ms)
  │     attributes: { dialect: "bt_rust", before: 120, after: 95 }
  ├── Span: ir.verify (1ms)
  ├── Span: ir.optimize (10ms)
  │     ├── Span: pass.dce (2ms) { removed: 15 instructions }
  │     ├── Span: pass.inline (5ms) { inlined: 3 calls }
  │     └── Span: pass.const_fold (3ms) { folded: 8 constants }
  ├── Span: ir.verify (1ms)  ← 优化后再验证
  ├── Span: ai.analyze (50ms)
  │     attributes: { provider: "openai", suggestions: 3, budget_used: 1200 }
  └── Span: backend.codegen (5ms)
        attributes: { backend: "llvm", target: "x86_64", output_bytes: 4096 }
```

## 4.2 管线节点数据结构

```rust
/// 编译节点 — 管线中每一个可观测单元
#[derive(Debug, Clone, Serialize)]
pub struct PipelineNode {
    pub id: Uuid,
    pub name: String,                  // "frontend.lex", "dialect.lower.bt_rust"
    pub phase: PipelinePhase,
    pub status: NodeStatus,
    pub started_at: DateTime<Utc>,
    pub completed_at: Option<DateTime<Utc>>,
    pub duration_ms: Option<u64>,
    pub input_summary: Value,          // 输入摘要（不存完整数据）
    pub output_summary: Value,         // 输出摘要
    pub diagnostics: Vec<Diagnostic>,
    pub metrics: HashMap<String, f64>,
    pub children: Vec<PipelineNode>,   // 子节点（树状）
    pub trace_id: String,
    pub span_id: String,
    pub event_sequence: Option<u64>,   // 对应 EventStore 中的事件序号
}

#[derive(Debug, Clone, Serialize)]
pub enum NodeStatus {
    Pending,
    Running { progress: f32 },
    Completed,
    Failed { error: String },
    Skipped { reason: String },
}

#[derive(Debug, Clone, Serialize)]
pub enum PipelinePhase {
    Lex, Parse, Analyze, Lower, DialectLower, Optimize, Codegen, Verify, AIAnalysis,
}
```

## 4.3 EventStore 集成

所有编译节点状态变更都写入 EventStore，支持：

1. **实时推送**：通过 WebSocket 推送到 Dashboard
2. **历史回放**：按 task_id 回放完整编译过程
3. **趋势分析**：跨编译任务的性能趋势
4. **AI 训练**：导出事件流作为 AI 训练数据

```rust
/// 编译过程中自动记录事件
async fn compile_with_observability(
    req: CompileRequest,
    event_store: &EventStore,
    tracer: &dyn Tracer,
) -> Result<CompileResponse> {
    let task_id = Uuid::new_v4();

    // 记录编译开始
    event_store.append(CompilerEvent::CompilationStarted {
        task_id,
        frontend: req.frontend.clone(),
    }).await;

    // ... 词法分析 ...
    event_store.append(CompilerEvent::LexCompleted {
        task_id,
        tokens: tokens.len(),
        duration_ms: lex_duration.as_millis() as u64,
    }).await;

    // ... 编译完成 ...
    event_store.append(CompilerEvent::CompilationCompleted {
        task_id,
        duration_ms: total_duration.as_millis() as u64,
    }).await;

    Ok(response)
}
```

## 4.4 实时监控推送 (WebSocket)

```
WS /ws/v1/pipeline → 实时 JSON 流：

{"type":"node_started","node":{"id":"...","name":"frontend.lex","status":"Running"}}
{"type":"node_progress","node":{"id":"...","name":"frontend.lex","status":"Running","progress":0.5}}
{"type":"node_completed","node":{"id":"...","name":"frontend.lex","status":"Completed","duration_ms":2}}
{"type":"dialect_lowered","dialect":"bt_rust","before":120,"after":95}
{"type":"diagnostic","diagnostic":{"severity":"warning","message":"unused variable `x`"}}
{"type":"ai_suggestion","suggestion":{"category":"Performance","confidence":0.92,"message":"可向量化的循环"}}
{"type":"task_completed","task_id":"...","duration_ms":42,"output_path":"..."}
```

## 4.5 监控仪表盘

```
┌─────────────────────────────────────────────────────┐
│  BlockType Compiler Dashboard                       │
├─────────────────────────────────────────────────────┤
│  Task: abc123  Frontend: rust  Backend: llvm        │
│  [██████████████████████████████████░░░░░] 85%      │
│                                                     │
│  ┌─ frontend.lex ──────── ✅ 2ms (150 tokens)      │
│  ├─ frontend.parse ────── ✅ 8ms (45 nodes)        │
│  ├─ frontend.analyze ──── ✅ 12ms                  │
│  │  ├─ type_check ────── ✅ 8ms                    │
│  │  └─ borrow_check ──── ✅ 3ms                    │
│  ├─ dialect.lower ─────── ✅ 2ms (bt_rust: 120→95) │
│  ├─ ir.verify ─────────── ✅ 1ms                   │
│  ├─ ir.optimize ───────── 🔄 10ms (running DCE)    │
│  ├─ ai.analyze ────────── ⏳ pending (budget: 78%) │
│  └─ backend.codegen ───── ⏳ pending               │
│                                                     │
│  Events: 23 stored │ Cache hit: 3/5 │ Deps: 12     │
│  Diagnostics: 0 errors, 1 warning                  │
│  Memory: 1.2 MB peak │ AI tokens: 1,200 / 5,000    │
└─────────────────────────────────────────────────────┘
```

## 4.6 OpenTelemetry 集成

```rust
use opentelemetry::trace::{Tracer, SpanKind};
use tracing_opentelemetry::OpenTelemetrySpanExt;

/// 编译过程中的 Span 创建模式
async fn compile_with_tracing(req: CompileRequest) -> Result<CompileResponse> {
    let tracer = opentelemetry::global::tracer("blocktype");

    let mut span = tracer
        .span_builder(format!("compile.{}", req.frontend))
        .with_kind(SpanKind::Internal)
        .start(&tracer);

    // 词法分析子 Span
    let lex_span = tracer.span("frontend.lex").start(&tracer);
    let tokens = lexer.tokenize(&req.source)?;
    lex_span.end();
    span.add_event("lex_completed", vec![KeyValue::new("tokens", tokens.len() as i64)]);

    // Dialect 降低子 Span
    let dialect_span = tracer.span("dialect.lower").start(&tracer);
    let lowered = dialect_registry.lower_all(ir_module)?;
    dialect_span.end();

    // ... 后续阶段类似

    span.end();
    Ok(response)
}
```

## 4.7 指标采集

```rust
/// 编译器核心指标
pub struct CompilerMetrics {
    // 计数器
    pub compilations_total: Counter<u64>,
    pub compilations_failed: Counter<u64>,
    pub dialect_registrations: Counter<u64>,

    // 直方图（分布）
    pub compile_duration: Histogram<f64>,
    pub phase_duration: Histogram<f64>,
    pub ir_instructions_count: Histogram<u64>,
    pub output_size_bytes: Histogram<u64>,

    // 仪表盘（当前值）
    pub active_tasks: Gauge<i64>,
    pub cache_hit_rate: Gauge<f64>,
    pub memory_usage_bytes: Gauge<u64>,
    pub ai_budget_remaining: Gauge<f64>,
    pub event_store_size: Gauge<u64>,

    // EventStore 特有
    pub events_per_compilation: Histogram<u64>,
    pub event_replay_duration: Histogram<f64>,
}

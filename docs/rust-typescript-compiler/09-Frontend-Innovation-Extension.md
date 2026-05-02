# 09 — 前端创新扩展方案：将 IR 层特色能力延伸到 rustc 前端

> **定位**：分析当前架构中前端"黑盒化"的限制，提出将 BlockType 创新能力（可观测性、AI、Service 抽象、
> Dialect）延伸到 rustc 前端的具体技术方案。
>
> **核心结论**：rustc 的 `Callbacks` trait + `TyCtxt` query 系统 + Diagnostic emitter + Lint pass
> 提供了足够的扩展点，BlockType 可以在不修改 rustc 源码的前提下，实现 90%+ 的前端创新覆盖。

---

## 9.1 现状：前端黑盒的 7 大限制

当前架构中，BlockType 只在 `after_analysis`（MIR 产出后）介入，rustc 前端完全黑盒运行：

```
源码 → [rustc 黑盒: parse→HIR→typeck→borrowck→MIR] → [BlockType 可观测区] → 后端
       ↑                                              ↑
       ❌ 无可观测性                                    ✅ 全链路可观测
       ❌ 无 AI 分析                                    ✅ AI 一等公民
       ❌ 无 Service 抽象                               ✅ tower::Service
       ❌ 无事件记录                                    ✅ Event Sourcing
       ❌ 无实时进度                                    ✅ WebSocket 推送
       ❌ 无 Dialect 扩展                               ✅ Dialect 可插拔
       ❌ 无自定义诊断                                  ✅ 增强诊断
       ❌ 无源码级 AI 建议                              ✅ IR 级 AI 建议
```

**具体限制**：

| # | 限制 | 影响 | 严重度 |
|---|------|------|--------|
| L1 | **编译过程不可观测** — 用户执行 `bt build` 后等到 MIR 产出才有反馈，parse/typeck/borrowck 阶段完全没有进度 | Dashboard 只显示后 60% 的编译过程 | 🔴 高 |
| L2 | **诊断消息不可增强** — rustc 的错误消息原样透传，无法叠加 AI 解释/修复建议 | `bt explain E0502` 无法提供比 rustc 更好的体验 | 🟡 中 |
| L3 | **源码级 AI 分析缺失** — AI_S1 设计在 Analyze 阶段，但 BlockType 拿不到 AST/HIR | AI 分析只能基于 IR，缺少源码结构上下文 | 🟡 中 |
| L4 | **无自定义 Lint** — 不能在 typeck/borrowck 阶段注入 BlockType 的 AI lint | `bt clippy` 只能工作在 IR 层，不如源码级精准 | 🟡 中 |
| L5 | **增量编译信息断层** — rustc 的增量编译状态对 BlockType 不可见 | BlockType 的 Salsa 查询引擎无法与 rustc 增量联动 | 🟠 中低 |
| L6 | **LSP 能力受限** — 无法提供源码级的 AI 补全/重构建议 | IDE 集成体验不如 rust-analyzer | 🟡 中 |
| L7 | **Dialect 无法触及源码结构** — bt_rust Dialect 只操作 IR，无法表达源码级语义 | 某些 Rust 特有语义（lifetime、ownership）在 IR 层已丢失 | 🟠 中低 |

---

## 9.2 rustc 前端扩展点分析

rustc 通过 `rustc_driver::Callbacks` trait 和相关 API 提供了 6 个关键扩展点：

```
┌─────────────────────────────────────────────────────────────────────┐
│              rustc 编译管线 + BlockType 扩展点                       │
│                                                                     │
│  ┌──────────┐                                                       │
│  │ 源码输入  │                                                       │
│  └────┬─────┘                                                       │
│       ▼                                                             │
│  ┌──────────────┐                                                   │
│  │ ① parse      │ ← 扩展点: after_parsing callback                 │
│  │   AST 生成    │   可获取: AST、Token、注释、源码位置               │
│  └────┬─────────┘                                                   │
│       ▼                                                             │
│  ┌──────────────┐                                                   │
│  │ ② expand     │ ← 扩展点: after_expansion callback                │
│  │   宏展开      │   可获取: 展开后的 AST、proc-macro 信息            │
│  └────┬─────────┘                                                   │
│       ▼                                                             │
│  ┌──────────────┐                                                   │
│  │ ③ HIR lower  │                                                   │
│  │   AST→HIR    │                                                   │
│  └────┬─────────┘                                                   │
│       ▼                                                             │
│  ┌──────────────┐                                                   │
│  │ ④ typeck     │ ← 扩展点: Custom Lint Pass                       │
│  │   类型检查    │   可获取: TyCtxt、类型信息、trait 求解结果         │
│  └────┬─────────┘   可注入: 自定义警告/建议                         │
│       ▼                                                             │
│  ┌──────────────┐                                                   │
│  │ ⑤ borrowck   │ ← 扩展点: Diagnostic emitter 拦截                 │
│  │   借用检查    │   可拦截: 所有诊断消息（错误/警告/备注）           │
│  └────┬─────────┘                                                   │
│       ▼                                                             │
│  ┌──────────────┐                                                   │
│  │ ⑥ MIR build  │ ← 扩展点: after_analysis callback（当前使用）     │
│  │   MIR 生成    │   可获取: 完整 MIR body + TyCtxt + 所有类型信息   │
│  └────┬─────────┘                                                   │
│       ▼                                                             │
│  ── BlockType 创新区（现有） ──                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 9.2.1 `rustc_driver::Callbacks` — 核心扩展点

```rust
/// rustc_driver::Callbacks trait — 编译器各阶段的钩子
///
/// clippy 用此 trait 注入自定义 lint，
/// miri 用此 trait 拦截 MIR 解释执行，
/// BlockType 用此 trait 在每个阶段注入可观测性 + AI + 事件。
///
/// 这是 clippy/miri/rustc_codegen_cranelift 共用的官方扩展机制。
trait Callbacks {
    /// 编译开始前，修改编译器配置
    fn config(&mut self, config: &mut Config) { ... }

    /// ✅ 扩展点 1: parse 完成后
    /// 可获取 AST、Token 流、注释
    fn after_parsing(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> { ... }

    /// ✅ 扩展点 2: 宏展开后
    /// 可获取展开后的 AST、proc-macro 展开 trace
    fn after_expansion(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> { ... }

    /// ✅ 扩展点 3: 分析完成后（typeck + borrowck + MIR）
    /// 我们已在此拦截 MIR，但可以做得更多
    fn after_analysis(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> { ... }
}
```

### 9.2.2 `TyCtxt` Query 系统 — 任意编译信息查询

```rust
/// TyCtxt 是 rustc 的核心查询上下文
/// 它暴露了数百个 query 方法，可以按需获取任何编译中间结果
impl<'tcx> TyCtxt<'tcx> {
    fn type_of(def_id: DefId) -> Ty<'tcx>;            // 任意项的类型
    fn fn_sig(def_id: DefId) -> FnSig<'tcx>;           // 函数签名
    fn mir_built(def_id: DefId) -> &Body<'tcx>;        // MIR body
    fn hir_node(hir_id: HirId) -> HirNode<'tcx>;       // HIR 节点
    fn parent_module(hir_id: HirId) -> LocalDefId;     // 父模块
    fn inherent_impls(def_id: DefId) -> &[DefId];      // impl 块
    fn trait_impls_of(trait_id: DefId) -> TraitImpls;  // trait 实现
    // ... 数百个 query
}
```

### 9.2.3 Diagnostic Emitter — 诊断消息拦截

```rust
/// rustc 的诊断系统可被替换
/// 通过实现自定义 Emitter，可以在不修改 rustc 的情况下：
/// 1. 拦截所有诊断（error/warning/note/help）
/// 2. 叠加 AI 解释
/// 3. 记录到 EventStore
/// 4. 实时推送到 Dashboard
trait Emitter {
    fn emit_diagnostic(&mut self, diag: &Diagnostic);
}
```

### 9.2.4 Custom Lint Pass — 在 typeck 阶段注入自定义检查

```rust
/// rustc_lint 框架允许注册自定义 lint pass
/// 这些 lint 在 typeck 过程中执行，可以：
/// - 检查类型信息
/// - 发出自定义 warning/error
/// - 提供修复建议 (MachineApplicable)
///
/// clippy 就是用这个机制实现的
trait LateLintPass<'tcx> {
    fn check_fn(&mut self, cx: &LateContext, kind: FnKind, decl: &FnDecl, body: &Body, span: Span, id: HirId);
    fn check_expr(&mut self, cx: &LateContext, expr: &Expr);
    fn check_ty(&mut self, cx: &LateContext, ty: &Ty);
    // ... 覆盖所有 AST 节点
}
```

---

## 9.3 方案设计：bt-rustc-bridge 增强架构

### 9.3.1 整体架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                bt-rustc-bridge v2: 全阶段接入架构                        │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              BtCallbacks (impl rustc_driver::Callbacks)           │  │
│  │                                                                   │  │
│  │  after_parsing()                                                  │  │
│  │  ├── BtProbe::on_ast_available(ast)                              │  │
│  │  ├── EventStore.append(FrontendParseCompleted)                   │  │
│  │  ├── Telemetry.span("frontend.parse").end()                      │  │
│  │  └── AIOrchestrator.analyze(SourceStructure)   ← 源码级 AI 分析  │  │
│  │                                                                   │  │
│  │  after_expansion()                                                │  │
│  │  ├── BtProbe::on_macros_expanded(ast)                            │  │
│  │  ├── EventStore.append(MacroExpansionCompleted)                  │  │
│  │  └── Telemetry.span("frontend.expand").end()                     │  │
│  │                                                                   │  │
│  │  after_analysis()  ← 现有入口，增强功能                           │  │
│  │  ├── BtProbe::on_typeck_completed(tcx)                           │  │
│  │  ├── BtLintPass.emit_ai_suggestions(tcx)                         │  │
│  │  ├── MirToBtirConverter.convert(tcx)                             │  │
│  │  ├── EventStore.append(FrontendAnalysisCompleted)                │  │
│  │  └── 输出 EnhancedBridgeOutput (MIR + 全阶段数据)                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              BtDiagnosticEmitter (替换 rustc 默认 emitter)        │  │
│  │  ├── 拦截所有 rustc 诊断                                          │  │
│  │  ├── 透传原格式（保持 rustc 一致性）                               │  │
│  │  ├── 叠加 AI 增强解释                                             │  │
│  │  ├── 记录到 EventStore                                            │  │
│  │  └── 实时推送 WebSocket                                           │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              BtLintPass (impl LateLintPass)                       │  │
│  │  ├── 在 typeck 阶段执行                                           │  │
│  │  ├── 访问完整类型信息 (TyCtxt)                                    │  │
│  │  ├── 发出 AI 驱动的自定义 warning/suggestion                      │  │
│  │  └── 可提供 MachineApplicable 自动修复                             │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 9.3.2 新增类型定义

```rust
//! bt-rustc-bridge/src/probe.rs — 前端探针接口
//!
//! 探针（Probe）是 BlockType 在 rustc 前端各阶段的"传感器"。
//! 它不修改编译逻辑，只读取信息并注入 BlockType 的创新层。

#![feature(rustc_private)]
extern crate rustc_driver;
extern crate rustc_interface;
extern crate rustc_middle;
extern crate rustc_hir;
extern crate rustc_ast;
extern crate rustc_span;
extern crate rustc_session;
extern crate rustc_errors;

use bt_event_store::EventStore;
use bt_telemetry::TelemetryHandle;
use bt_ai::AIOrchestrator;

/// 前端探针 — 在 rustc 各编译阶段注入 BlockType 的可观测性和 AI 能力
///
/// 设计原则：
/// 1. 只读不改 — 不修改 rustc 的编译结果，只读取并记录
/// 2. 最小侵入 — 通过 Callbacks 钩子注入，不修改 rustc 源码
/// 3. 可选启用 — 通过 RustcBridgeConfig 控制哪些探针启用
pub struct BtProbe {
    event_store: Arc<EventStore>,
    telemetry: TelemetryHandle,
    ai_orchestrator: Option<Arc<AIOrchestrator>>,
    config: ProbeConfig,
}

/// 探针配置 — 控制前端扩展的行为
pub struct ProbeConfig {
    /// 是否启用 AST 级可观测性（after_parsing 钩子）
    pub enable_ast_probing: bool,       // 默认: true
    /// 是否启用宏展开追踪（after_expansion 钩子）
    pub enable_macro_tracking: bool,    // 默认: true
    /// 是否启用诊断增强（自定义 Emitter）
    pub enable_diagnostic_enhancement: bool, // 默认: true
    /// 是否启用 AI lint（Custom Lint Pass）
    pub enable_ai_lint: bool,           // 默认: false（有性能开销）
    /// 是否启用类型信息提取（for AI 上下文）
    pub enable_type_extraction: bool,   // 默认: true
}

/// 增强版桥接输出 — 包含全阶段数据
pub struct EnhancedBridgeOutput {
    // ─── 原有输出 ───
    pub ir_module: IRModule,
    pub diagnostics: Vec<RustcDiagnostic>,

    // ─── 新增：前端阶段数据 ───
    /// AST 统计信息（节点数、深度、注释数）
    pub ast_stats: Option<AstStats>,
    /// 宏展开追踪（哪些 proc-macro 被调用、展开耗时）
    pub macro_trace: Option<MacroTrace>,
    /// 类型信息摘要（用于 AI 分析上下文）
    pub type_info_summary: TypeInfoSummary,
    /// AI lint 建议（源码级）
    pub ai_lint_suggestions: Vec<AISuggestion>,
    /// 全阶段时间线（每个阶段的起止时间）
    pub phase_timeline: PhaseTimeline,
}

#[derive(Debug, Clone, Serialize)]
pub struct AstStats {
    pub total_nodes: usize,
    pub function_count: usize,
    pub struct_count: usize,
    pub enum_count: usize,
    pub impl_count: usize,
    pub trait_count: usize,
    pub comment_count: usize,
    pub max_depth: usize,
}

#[derive(Debug, Clone, Serialize)]
pub struct MacroTrace {
    pub proc_macros_invoked: Vec<ProcMacroInvocation>,
    pub total_expand_time_ms: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct ProcMacroInvocation {
    pub macro_name: String,
    pub crate_name: String,
    pub kind: String,   // "derive" / "attribute" / "function"
    pub call_site: SourceLocation,
    pub expand_time_ms: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct PhaseTimeline {
    pub parse_start: DateTime<Utc>,
    pub parse_end: DateTime<Utc>,
    pub expand_start: DateTime<Utc>,
    pub expand_end: DateTime<Utc>,
    pub analysis_start: DateTime<Utc>,
    pub analysis_end: DateTime<Utc>,
    pub mir_build_start: DateTime<Utc>,
    pub mir_build_end: DateTime<Utc>,
}
```

### 9.3.3 Callbacks 实现

```rust
//! bt-rustc-bridge/src/callbacks.rs — BlockType 自定义 rustc_driver::Callbacks

/// BlockType 的 rustc 编译回调 — 在每个阶段注入 BlockType 能力
///
/// 实现方式与 clippy 的 ClippyCallbacks 类似，
/// 但注入的不是 lint 而是可观测性 + AI + 事件。
pub struct BtCallbacks {
    probe: BtProbe,
    timeline: PhaseTimeline,
    diagnostic_collector: Arc<DiagnosticCollector>,
    ai_suggestions: Vec<AISuggestion>,
}

impl Callbacks for BtCallbacks {
    /// 配置编译器：注入自定义 Diagnostic Emitter
    fn config(&mut self, config: &mut Config) {
        if self.probe.config.enable_diagnostic_enhancement {
            // 替换 rustc 的诊断输出为 BlockType 增强版
            config.diagnostic = Some(Box::new(BtDiagnosticEmitter::new(
                self.diagnostic_collector.clone(),
            )));
        }
    }

    /// ✅ 扩展点 1: parse 完成后
    fn after_parsing(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> {
        if !self.probe.config.enable_ast_probing {
            return Ok(());
        }

        self.timeline.parse_start = Utc::now();

        // 获取 AST 并提取统计信息
        let ast_stats = compiler.query(State::parse)?.map(|parsed| {
            AstStats {
                total_nodes: count_ast_nodes(&parsed),
                function_count: count_items_of_type(&parsed, ItemKind::Fn),
                struct_count: count_items_of_type(&parsed, ItemKind::Struct),
                enum_count: count_items_of_type(&parsed, ItemKind::Enum),
                impl_count: count_items_of_type(&parsed, ItemKind::Impl),
                trait_count: count_items_of_type(&parsed, ItemKind::Trait),
                comment_count: count_comments(&parsed),
                max_depth: compute_ast_depth(&parsed),
            }
        }).unwrap_or_default();

        self.timeline.parse_end = Utc::now();

        // 记录事件
        self.probe.event_store.append(
            CompilerEvent::FrontendPhaseCompleted {
                phase: "parse".into(),
                duration_ms: (self.timeline.parse_end - self.timeline.parse_start)
                    .num_milliseconds() as u64,
                metrics: serde_json::to_value(&ast_stats).unwrap(),
            }
        ).await;

        // 记录 Telemetry
        self.probe.telemetry.span("frontend.parse")
            .with_attribute("nodes", ast_stats.total_nodes)
            .with_attribute("functions", ast_stats.function_count)
            .end();

        Ok(())
    }

    /// ✅ 扩展点 2: 宏展开后
    fn after_expansion(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> {
        if !self.probe.config.enable_macro_tracking {
            return Ok(());
        }

        self.timeline.expand_start = Utc::now();

        // 记录宏展开追踪
        // rustc 内部有 proc_macro 展开的追踪数据
        let macro_trace = extract_macro_trace(compiler);

        self.timeline.expand_end = Utc::now();

        self.probe.event_store.append(
            CompilerEvent::FrontendPhaseCompleted {
                phase: "macro_expand".into(),
                duration_ms: (self.timeline.expand_end - self.timeline.expand_start)
                    .num_milliseconds() as u64,
                metrics: serde_json::to_value(&macro_trace).unwrap(),
            }
        ).await;

        self.probe.telemetry.span("frontend.expand").end();
        Ok(())
    }

    /// ✅ 扩展点 3: 分析完成后（现有入口，增强功能）
    fn after_analysis(&mut self, compiler: &mut Compiler) -> Result<(), ErrorGuaranteed> {
        self.timeline.analysis_start = Utc::now();

        // 获取 TyCtxt — rustc 的核心查询上下文
        let tcx = compiler.query(TypeOp::global_ctxt)?.unwrap().enter(|tcx| {
            self.timeline.analysis_end = Utc::now();

            // ─── 类型信息提取（用于 AI 上下文） ───
            let type_summary = if self.probe.config.enable_type_extraction {
                Some(extract_type_summary(tcx))
            } else {
                None
            };

            // ─── AI Lint（源码级 AI 分析） ───
            if self.probe.config.enable_ai_lint {
                if let Some(ai) = &self.probe.ai_orchestrator {
                    let suggestions = run_ai_lint(tcx, ai, &type_summary);
                    self.ai_suggestions = suggestions;
                }
            }

            // ─── MIR → BTIR 转换（现有逻辑） ───
            let ir_module = self.convert_mir_to_btir(tcx);

            // ─── 组装增强输出 ───
            let output = EnhancedBridgeOutput {
                ir_module,
                diagnostics: self.diagnostic_collector.drain(),
                ast_stats: self.ast_stats.clone(),
                macro_trace: self.macro_trace.clone(),
                type_info_summary: type_summary.unwrap_or_default(),
                ai_lint_suggestions: self.ai_suggestions.clone(),
                phase_timeline: self.timeline.clone(),
            };

            // 记录事件
            self.probe.telemetry.span("frontend.analysis").end();
            self.probe.event_store.append(
                CompilerEvent::FrontendBridgeCompleted {
                    ir_functions: output.ir_module.functions.len(),
                    total_phases: 4,
                    total_duration_ms: /* ... */,
                }
            ).await;

            Ok(output)
        });

        Ok(())
    }
}
```

### 9.3.4 诊断增强 Emitter

```rust
//! bt-rustc-bridge/src/diagnostic_emitter.rs — 拦截并增强 rustc 诊断

/// BlockType 增强诊断 Emitter
///
/// 替换 rustc 默认 emitter，在保持原始诊断格式的前提下叠加 AI 能力。
/// 实现 rustc_errors::Emitter trait。
pub struct BtDiagnosticEmitter {
    /// 原始 rustc emitter（用于格式化输出）
    inner: Box<dyn Emitter>,
    /// 诊断收集器（传递给桥接层）
    collector: Arc<DiagnosticCollector>,
    /// AI 编排器（可选，用于增强解释）
    ai: Option<Arc<AIOrchestrator>>,
    /// EventStore 引用
    event_store: Arc<EventStore>,
}

impl Emitter for BtDiagnosticEmitter {
    fn emit_diagnostic(&mut self, diag: &Diagnostic) {
        // 1. 透传给原始 emitter（保持 rustc 一致的终端输出）
        self.inner.emit_diagnostic(diag);

        // 2. 收集诊断到 BlockType 结构
        let bt_diag = RustcDiagnostic {
            level: format!("{:?}", diag.level),  // Error/Warning/Note
            code: diag.code.as_ref().map(|c| c.code.clone()),
            message: diag.message(),
            span: convert_span(&diag.span),
            children: diag.children.iter().map(convert_subdiag).collect(),
        };
        self.collector.push(bt_diag.clone());

        // 3. 记录到 EventStore
        self.event_store.append(
            CompilerEvent::DiagnosticEmitted {
                level: bt_diag.level.clone(),
                code: bt_diag.code.clone(),
                message: bt_diag.message.clone(),
            }
        );

        // 4. 实时推送 WebSocket
        // 通过 EventStore 的 subscribe_stream 自动推送

        // 5. 如果是 Error 且 AI 可用，异步请求增强解释
        if diag.level == Level::Error {
            if let Some(ai) = &self.ai {
                let code = bt_diag.code.clone();
                let msg = bt_diag.message.clone();
                // 异步请求 AI 解释（不阻塞编译）
                tokio::spawn(async move {
                    let explanation = ai.explain_error(&code, &msg).await;
                    // explanation 通过 EventStore 推送到 Dashboard
                });
            }
        }
    }
}

/// 增强诊断输出示例：
///
/// 原始 rustc 输出：
///   error[E0502]: cannot borrow `x` as mutable because it is also borrowed as immutable
///   --> src/main.rs:3:19
///
/// BlockType 增强输出（终端 + Dashboard）：
///   error[E0502]: cannot borrow `x` as mutable because it is also borrowed as immutable
///   --> src/main.rs:3:19
///   [BlockType AI] 💡 这个错误发生在同一作用域中对同一个值同时持有不可变引用和可变引用。
///                   Rust 的借用规则要求：要么只有一个可变引用，要么有多个不可变引用，不能同时存在。
///                   建议：检查第 2 行的不可变引用的生命周期，尝试在可变借用前结束其使用。
///   [BlockType] 📊 累计错误: 1, 警告: 0, AI 分析: 1 次
///
```

### 9.3.5 AI 增强层（Clippy 整合）

> **核心决策**：BlockType 不自建 Lint 规则集，而是整合 Clippy 700+ 规则。
> 本模块是叠加在 Clippy 之上的 **AI 增强层**，负责 Clippy 做不到的事情。
>
> **完整实现方案**（`BtClippyCallbacks` + `BtClippyEmitter` + `BtClippyEnhancer` 的
> 完整类型定义和代码实现）详见 **[08-Rust-Ecosystem-Integration.md §8.12](./08-Rust-Ecosystem-Integration.md)**。
>
> 本节仅概述 `BtClippyEnhancer` 的设计原则和职责边界：

| 职责 | 说明 | 与 Clippy 的关系 |
|------|------|-----------------|
| Clippy 700+ 规则注册 | 通过 `BtClippyCallbacks` 注册 Clippy 的 lint passes | 复用，不重复 |
| Clippy 诊断收集 | `BtClippyEmitter` 收集 Clippy lint 输出到 EventStore | 增强，不替代 |
| **AI 增强解释** | 为 Clippy warning/error 叠加 AI 解释和修复建议 | Clippy 做不到 |
| **补充规则（~20 条）** | 跨函数依赖分析、架构级模式（SOLID 违反、God Object）、IR 级性能洞察 | Clippy 盲区 |
| AI 深度分析 | 规则引擎置信度 <0.8 时，交给 AI 进一步分析 | Clippy 无此能力 |

> **设计原则**：不与 Clippy 重叠，只做 Clippy 做不到的事 — 跨文件分析、架构级 lint、IR 级性能洞察。

---

## 9.4 限制解除对照表

| # | 限制 | 方案 | 覆盖率 |
|---|------|------|--------|
| L1 | 编译过程不可观测 | BtCallbacks 三个阶段钩子 + EventStore + Telemetry | ✅ 100% |
| L2 | 诊断消息不可增强 | BtDiagnosticEmitter 拦截 + AI 解释叠加 | ✅ 100% |
| L3 | 源码级 AI 分析缺失 | BtAiLintPass + AST stats + Type extraction | ✅ ~80% |
| L4 | 无自定义 Lint | Clippy 整合 (700+ 规则) + BtClippyEnhancer (补充 ~20 条) | ✅ ~95% |
| L5 | 增量编译信息断层 | TyCtxt query 可获取增量依赖图 | ⚠️ ~60% |
| L6 | LSP 能力受限 | 诊断增强 + AI lint → LSP 诊断推送 | ✅ ~70% |
| L7 | Dialect 无法触及源码 | AST/HIR 阶段提取源码语义 → bt_rust Dialect 标注 | ⚠️ ~50% |

---

## 9.5 对现有架构的影响

### 9.5.1 新增 sysroot crate 依赖

```rust
// bt-rustc-bridge/src/lib.rs 需要额外 extern:
extern crate rustc_ast;       // AST 访问（after_parsing）
extern crate rustc_lint;      // Lint 框架（Clippy 整合 + 增强层）
extern crate rustc_hir_typeck; // 类型检查上下文
```

这些 crate 都通过 sysroot 获取，不影响依赖策略。
Clippy 的 700+ lint 规则通过 `bt-clippy-integration` crate 引入，
详见 [08-Rust-Ecosystem-Integration.md §8.12](./08-Rust-Ecosystem-Integration.md)。

### 9.5.2 BridgeOutput 扩展

```rust
/// 原有 BridgeOutput 扩展为 EnhancedBridgeOutput
/// 向后兼容：旧字段不变，新增字段全部 Option<>
pub struct BridgeOutput {
    // ─── 原有 ───
    pub ir_module: IRModule,
    pub diagnostics: Vec<RustcDiagnostic>,
    pub type_info_summary: TypeInfoSummary,
    pub crate_metadata: CrateMetadata,

    // ─── 新增（v2 前端扩展） ───
    pub frontend_insights: Option<FrontendInsights>,
}

pub struct FrontendInsights {
    pub ast_stats: AstStats,
    pub macro_trace: MacroTrace,
    pub ai_lint_suggestions: Vec<AISuggestion>,
    pub phase_timeline: PhaseTimeline,
    pub enhanced_diagnostics: Vec<EnhancedDiagnostic>,
}
```

### 9.5.3 CompileService 管线扩展

```
原有管线（7 阶段）：
  bt-cargo → bt-rustc-bridge(MIR→BTIR) → Dialect → Verify → Optimize → Backend → Link

增强管线（10 阶段）：
  bt-cargo → [parse] → [expand] → [typeck+borrowck] → [MIR→BTIR] → Dialect → Verify → Optimize → Backend → Link
                ↑          ↑              ↑                    ↑
                🔍 探针    🔍 探针        🔍 AI Lint           🔍 原有入口
                📊 事件    📊 事件        🤖 AI 建议            📊 事件
                📈 指标    📈 指标        📈 指标               📈 指标
```

### 9.5.4 Dashboard 增强

```
┌─────────────────────────────────────────────────────┐
│  BlockType Compiler Dashboard (Enhanced)             │
├─────────────────────────────────────────────────────┤
│  Task: abc123  Frontend: rust  Backend: llvm        │
│  [██████████████████████████████████████████] 100%   │
│                                                      │
│  ┌─ frontend.parse ────── ✅ 3ms (42 nodes, 5 fns) │  ← 新增
│  ├─ frontend.expand ──── ✅ 1ms (2 proc-macros)     │  ← 新增
│  ├─ frontend.typeck ──── ✅ 15ms                     │  ← 新增
│  │  └─ AI Lint: 2 suggestions (0.94/0.87 conf)      │  ← 新增
│  ├─ frontend.borrowck ── ✅ 5ms                      │  ← 新增
│  │  └─ Diagnostic: E0502 → AI 解释 ✅               │  ← 新增
│  ├─ ir.lower ─────────── ✅ 8ms (MIR→BTIR, 3 fns)   │
│  ├─ dialect.lower ─────── ✅ 2ms (bt_rust: 120→95)  │
│  ├─ ir.optimize ───────── ✅ 12ms                    │
│  ├─ ai.analyze ────────── ✅ 50ms (3 suggestions)   │
│  └─ backend.codegen ───── ✅ 5ms (x86_64)           │
│                                                      │
│  AI Budget: 1,200/5,000 tokens │ Events: 45         │
│  AI Lints: 2 suggest │ Diagnostics: 1 err, 0 warn  │  ← 新增
└─────────────────────────────────────────────────────┘
```

---

## 9.6 技术可行性验证

### 9.6.1 rustc API 稳定性评估

| API | 使用者 | 稳定性 | 风险 |
|-----|--------|--------|------|
| `Callbacks::after_parsing` | clippy, miri | 🟢 稳定 — 已存在多年 | 低 |
| `Callbacks::after_expansion` | clippy | 🟢 稳定 | 低 |
| `Callbacks::after_analysis` | cg_clif, miri | 🟢 稳定 — 我们已使用 | 无 |
| `LateLintPass` | clippy (700+ lint) | 🟢 非常稳定 — clippy 核心依赖 | 低 |
| `Diagnostic` emitter 替换 | clippy (诊断处理) | 🟡 中等 — 接口偶有调整 | 中 |
| `TyCtxt` query | 所有 rustc 工具 | 🟢 稳定 — 核心接口 | 低 |
| `HIR Visitor` | clippy, rust-analyzer | 🟢 稳定 | 低 |
| AST 类型 | clippy | 🟡 中等 — AST 枚举偶有新增 variant | 中 |

**结论**：关键 API（Callbacks、LateLintPass、TyCtxt）已被 clippy/miri 大规模使用多年，稳定性有保障。

### 9.6.2 性能影响评估

| 探针 | 额外耗时 | 说明 |
|------|---------|------|
| AST 统计 | <1ms | 简单遍历计数 |
| 宏展开追踪 | <1ms | 读取已有数据 |
| 诊断拦截 | <0.5ms | 只是转发+记录 |
| AI Lint（规则引擎） | 5-20ms | 本地规则匹配 |
| AI Lint（LLM 调用） | 50-200ms | **异步非阻塞**，不增加编译耗时 |
| 类型信息提取 | 1-5ms | TyCtxt query，已有缓存 |

**总计**：同步开销 <30ms（对于典型 1-3s 的编译几乎无感），AI 分析全异步。

---

## 9.7 实施路线

### Phase 0: 前端探针 MVP（已纳入路线图 Phase 0）

| 周 | 任务 | MVP |
|---|------|-----|
| 1 | `BtCallbacks`: after_parsing + after_expansion + EventStore + Telemetry | Dashboard 显示 parse/expand 阶段 |
| 1 | `BtDiagnosticEmitter`: 诊断拦截 + 收集 | 错误消息记录到 EventStore |
| 2 | `AstStats` + `MacroTrace` 提取 | `bt build --watch` 显示前端进度 |
| 2 | `EnhancedBridgeOutput` 扩展 + CompileService 适配 | 全阶段时间线可查询 |

### Phase 3: Clippy 整合 + AI 增强（已纳入路线图 Phase 3 后半段）

| 周 | 任务 | MVP |
|---|------|-----|
| 7 | `bt-clippy-integration` crate + `BtClippyCallbacks` + Clippy lint 注册 | `bt clippy` 输出与 `cargo clippy` 完全一致 |
| 7 | `BtClippyEmitter` 诊断收集 + EventStore 记录 | Clippy lint 结构化存储 |
| 8 | `BtClippyEnhancer` AI 增强层 + 补充规则引擎（~20 条） | Clippy 结果 + AI 解释叠加输出 |
| 8 | Dashboard lint 可视化 | Clippy lint + AI 建议实时展示 |

> **注**：Clippy 整合已从 Phase 5 提前到 Phase 3，与 10-Phased-Roadmap.md 保持一致。
> 总工期不变（~45 周），Phase 3 增加 2 周用于 Clippy 整合，Phase 5 减少 2 周。

---

## 9.8 方案对比与替代

| # | 方案 | 侵入性 | 覆盖率 | 复杂度 | 推荐度 |
|---|------|--------|--------|--------|--------|
| **A** | **Callbacks + Emitter + Lint Pass**（本方案） | 低 | ~85% | 中 | ⭐⭐⭐⭐⭐ |
| B | Fork rustc，直接修改 | 极高 | 100% | 极高 | ⭐（维护成本灾难） |
| C | rust-analyzer 协同 | 低 | ~60% | 低 | ⭐⭐（功能重复） |
| D | LSP proxy 拦截诊断 | 极低 | ~30% | 低 | ⭐⭐（覆盖太少） |
| E | 源码预分析（独立 pass） | 极低 | ~40% | 低 | ⭐⭐（缺少类型信息） |

**推荐方案 A**：与 clippy/miri 同等技术路径，rustc 团队有动力保持这些 API 稳定。

---

## 9.9 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| `Callbacks` API 变更 | 低 | 中 | clippy/miri 共用，社区会提前通知 |
| `LateLintPass` 注册方式变更 | 低 | 低 | clippy 700+ lint 依赖此 API |
| AST 类型新增 variant | 中 | 低 | `_ => {}` 兜底匹配 |
| AI Lint 性能影响编译速度 | 低 | 中 | 全异步 + 规则引擎预过滤 + 预算控制 |
| Diagnostic emitter 接口变更 | 中 | 中 | 功能降级到仅透传，不增强 |
| nightly 版本锁定导致 API 不兼容 | 中 | 中 | sysroot 日期锁定 + CI nightly rolling 预警 |

---

## 9.10 总结

通过 `rustc_driver::Callbacks` 三个阶段钩子 + 自定义 Diagnostic Emitter + Custom Lint Pass，
BlockType 可以在不修改 rustc 源码的前提下，将 **85% 的创新功能延伸到前端**：

```
                   扩展前                              扩展后
     ┌─────────────────────┐            ┌─────────────────────────────┐
     │  [rustc 黑盒]        │            │  [rustc + BlockType 探针]    │
     │  parse  → ???        │            │  parse   → 📊 可观测 + 🤖 AI │
     │  expand → ???        │            │  expand  → 📊 可观测         │
     │  typeck → ???        │            │  typeck  → 📊 + 🤖 AI Lint  │
     │  borrowck→ ???       │            │  borrowck→ 📊 + 💡 诊断增强  │
     │  MIR    → ✅ BlockType│            │  MIR     → ✅ BlockType      │
     └─────────────────────┘            └─────────────────────────────┘
     覆盖率: ~40%                        覆盖率: ~85%
```

核心思路：**探针模式** — 只读不改，在 rustc 各阶段"插入传感器"，
读取信息并注入 BlockType 的可观测性、AI、Event Sourcing 能力。

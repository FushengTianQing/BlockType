# Phase 4 — AI 增强 + 可观测性 + LSP

> **目标**：AI 辅助优化 + IDE 集成 + 全链路可观测 + 双后端
> **估计**：~10 周（50 天）| 4 个 Sprint | 14 个 Task
> **里程碑**：AI 辅助优化 + 双后端 + IDE 集成 + 实时监控全部可用

---

## Sprint 4-1: bt-ai AI 编排器（2 周）

### T-04-01: bt-ai — AIProvider trait + 多 Provider 实现

- **ID**: `T-04-01`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-04-1

**描述**：实现 AIProvider trait 和 OpenAI/Anthropic/本地规则引擎三个 provider。

**产出文件**：
- `crates/bt-ai/src/{lib.rs, provider.rs, providers/openai.rs, providers/anthropic.rs, providers/rule_engine.rs}`

**设计规格**（REF）：
- `07-AI-Native.md §5.2` — AIOrchestrator
- `07-AI-Native.md §5.3` — AIProvider trait

**前置依赖**（DEP）：
- `T-00-02`：bt-core

**验收标准**（TST）：
- [ ] `AIProvider` trait: async analyze/stream/fallback
- [ ] `OpenAIProvider`: HTTP 调用 GPT-4/Claude
- [ ] `AnthropicProvider`: HTTP 调用 Claude
- [ ] `RuleBasedProvider`: 零延迟静态规则分析
- [ ] 每个分析结果含 suggestions/confidence/tokens_used
- [ ] `cargo test -p bt-ai` 通过

---

### T-04-02: bt-ai — AIOrchestrator + fallback 链 + TokenBudget

- **ID**: `T-04-02`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P0
- **Sprint**: S-04-1

**描述**：实现 AI 编排器，管理多 Provider、fallback 链、Token 预算。

**产出文件**：
- `crates/bt-ai/src/orchestrator.rs`
- `crates/bt-ai/src/budget.rs`
- `crates/bt-ai/src/cache.rs`

**设计规格**（REF）：
- `07-AI-Native.md §5.2` — AIOrchestrator
- `05-Unified-API.md §2.10.6` — TokenBudget

**前置依赖**（DEP）：
- `T-04-01`

**验收标准**（TST）：
- [ ] `AIOrchestrator::new(providers, budget, cache)`
- [ ] `analyze(module)` → 遍历 provider 链，首个成功返回
- [ ] `analyze_stream(module)` → SSE 流式输出
- [ ] Fallback: OpenAI 失败 → Anthropic → RuleBased
- [ ] `TokenBudget`: per_request/daily/hourly 限制
- [ ] `ExhaustionPolicy`: Reject / FallbackToRuleBased
- [ ] `AIResultCache`: 按 IR hash 缓存分析结果
- [ ] 预算耗尽自动降级

---

### T-04-03: bt-ai — AIAction + 自动变换

- **ID**: `T-04-03`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P1
- **Sprint**: S-04-1

**描述**：实现 AI 分析结果的 Action 模型和自动变换。

**产出文件**：
- `crates/bt-ai/src/action.rs`

**设计规格**（REF）：
- `07-AI-Native.md §5.5`

**前置依赖**（DEP）：
- `T-04-02`

**验收标准**（TST）：
- [ ] `AIAction` 枚举：Suggestion/AutoApply/RequireReview
- [ ] `AutoApply` 自动应用高置信度变换
- [ ] `RequireReview` 低置信度需要人工确认
- [ ] AI Pass 集成到编译管线

---

## Sprint 4-2: AI Pass 集成 + 可观测性完善（2 周）

### T-04-04: AI Pass 集成到 Rust 编译管线

- **ID**: `T-04-04`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-04-2

**描述**：将 AI 分析集成到编译管线的三个插槽（PostAnalysis/PostLower/PostCodegen）。

**产出文件**：
- `crates/bt-service/src/ai_pass.rs`
- `crates/bt-service/src/compile_service.rs` — 更新

**设计规格**（REF）：
- `07-AI-Native.md §5.1` — AI 插槽

**前置依赖**（DEP）：
- `T-04-02`, `T-01-08`：bt-passes

**验收标准**（TST）：
- [ ] `bt build --ai=auto` 启用 AI 辅助
- [ ] PostAnalysis: 类型推断辅助 + 反模式检测
- [ ] PostLower: Pass 推荐 + 热点预测
- [ ] PostCodegen: 代码质量评估
- [ ] AI 结果记录到 EventStore
- [ ] `bt build --ai=off` 不调用 AI（默认）

---

### T-04-05: bt-telemetry — OpenTelemetry 全链路追踪

- **ID**: `T-04-05`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P0
- **Sprint**: S-04-2

**描述**：完善全链路追踪，每个编译阶段有 Span。

**产出文件**：
- `crates/bt-telemetry/src/tracer.rs`
- `crates/bt-telemetry/src/pipeline_node.rs`

**设计规格**（REF）：
- `06-Observability.md §4.1`

**前置依赖**（DEP）：
- `T-00-06`

**验收标准**（TST）：
- [ ] 每个编译阶段（lex/parse/analyze/lower/optimize/codegen）有 Span
- [ ] Span 属性含阶段统计（tokens/nodes/instructions 等）
- [ ] Trace ID 贯穿整个编译流程
- [ ] 可导出 Jaeger/Zipkin 格式

---

### T-04-06: bt-api — 可观测性 API + WebSocket

- **ID**: `T-04-06`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-04-2

**描述**：添加可观测性和 WebSocket 端点。

**产出文件**：
- `crates/bt-api/src/handlers/telemetry.rs`
- `crates/bt-api/src/handlers/websocket.rs`

**设计规格**（REF）：
- `05-Unified-API.md §2.6` — 可观测性 API
- `05-Unified-API.md §2.7` — WebSocket

**前置依赖**（DEP）：
- `T-00-07`, `T-04-05`

**验收标准**（TST）：
- [ ] `GET /api/v1/telemetry/pipeline|traces|metrics|logs|events`
- [ ] `GET /api/v1/telemetry/events/{task_id}` 按任务回放
- [ ] `WS /ws/v1/pipeline|diagnostics|progress` 实时推送

---

## Sprint 4-3: LSP + Cranelift 后端（2 周）

### T-04-07: bt-api — LSP 适配器

- **ID**: `T-04-07`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P1
- **Sprint**: S-04-3

**描述**：内置 LSP 适配器，支持 VS Code 基本代码补全和诊断。

**产出文件**：
- `crates/bt-api/src/lsp/{mod.rs, server.rs, handler.rs}`

**设计规格**（REF）：
- `05-Unified-API.md` — LSP stdin/stdout

**前置依赖**（DEP）：
- `T-00-07`

**验收标准**（TST）：
- [ ] LSP stdin/stdout 通信
- [ ] textDocument/didOpen + didChange + diagnostics
- [ ] textDocument/completion 基本补全
- [ ] textDocument/hover 类型信息
- [ ] VS Code 可连接并显示诊断

---

### T-04-08: bt-backend-cranelift — Cranelift 后端

- **ID**: `T-04-08`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P1
- **Sprint**: S-04-3

**描述**：实现 Cranelift 后端，支持 WASM 目标。

**产出文件**：
- `crates/bt-backend-cranelift/src/{lib.rs, backend.rs, codegen.rs, type_conv.rs}`

**前置依赖**（DEP）：
- `T-00-15`：bt-backend-common
- `T-01-01`：完整 IRType

**验收标准**（TST）：
- [ ] `CraneliftBackend` impl `Backend`
- [ ] `emit_object()` 支持 WASM 目标
- [ ] IRType → Cranelift 类型映射
- [ ] IRInstruction → Cranelift IR 映射
- [ ] `cargo test -p bt-backend-cranelift` 通过

---

## Sprint 4-4: Dashboard + 回归（2 周）

### T-04-09: Dashboard Web 监控仪表盘 + CLI 命令

- **ID**: `T-04-09`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P2
- **Sprint**: S-04-4

**描述**：Web 监控仪表盘 + `bt dashboard` / `bt trace` CLI 命令。

**产出文件**：
- `dashboard/` — React/Vue SPA
- `crates/bt-cli/src/commands/dashboard.rs` — `bt dashboard` 命令
- `crates/bt-cli/src/commands/trace.rs` — `bt trace <task_id>` 命令

**设计规格**（REF）：
- `06-Observability.md §4.5` — 监控仪表盘
- `08-Rust-Ecosystem-Integration.md §8.7` — CLI `bt dashboard` / `bt trace`

**前置依赖**（DEP）：
- `T-04-06`：WebSocket 端点

**验收标准**（TST）：
- [ ] 实时显示编译进度（WebSocket）
- [ ] 显示性能指标图表
- [ ] 事件历史浏览
- [ ] 管线节点状态可视化
- [ ] `bt dashboard` 启动 Web 仪表盘
- [ ] `bt trace <task_id>` 查看编译 Trace

---

### T-04-10: SSE 流式响应

- **ID**: `T-04-10`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P1
- **Sprint**: S-04-4

**描述**：AI 分析端点支持 SSE 流式输出。

**产出文件**：
- `crates/bt-api/src/handlers/sse.rs`

**前置依赖**（DEP）：
- `T-03-10`

**验收标准**（TST）：
- [ ] `POST /api/v1/ai/analyze` 支持 `Accept: text/event-stream`
- [ ] 逐 token 推送分析结果
- [ ] 客户端可实时显示 AI 分析

---

### T-04-11: bt-api — IR Diff API 实现

- **ID**: `T-04-11`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P1
- **Sprint**: S-04-4

**描述**：实现 IR Diff API 的三种格式输出。

**产出文件**：
- `crates/bt-api/src/handlers/ir_diff.rs`

**设计规格**（REF）：
- `05-Unified-API.md §2.3.1`

**前置依赖**（DEP）：
- `T-01-09`

**验收标准**（TST）：
- [ ] text 格式：统一 diff 文本
- [ ] structured 格式：按 IR 节点的结构化 diff
- [ ] visual 格式：Dashboard 可视化数据

---

### T-04-12: 端到端测试 — AI + 双后端

- **ID**: `T-04-12`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-04-4

**验收标准**（TST）：
- [ ] `bt build --ai=auto` 生成 AI 建议
- [ ] LLVM x86_64 + AArch64 目标编译
- [ ] Cranelift WASM 目标编译
- [ ] LSP 连接成功显示诊断
- [ ] Dashboard 实时显示编译进度

---

### T-04-13: bt-api — 认证中间件完善

- **ID**: `T-04-13`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P2
- **Sprint**: S-04-4

**描述**：完善 JWT 多租户模式和权限校验。

**产出文件**：
- `crates/bt-api/src/middleware/auth.rs` — 完善
- `crates/bt-api/src/middleware/project_context.rs`

**设计规格**（REF）：
- `05-Unified-API.md §2.10.4-2.10.7`

**前置依赖**（DEP）：
- `T-03-10`

**验收标准**（TST）：
- [ ] JWT RS256/HS256 验证
- [ ] ProjectContext 注入
- [ ] 权限矩阵强制执行
- [ ] E3003-E3006 错误码

---

### T-04-14: Phase 4 回归测试

- **ID**: `T-04-14`
- **状态**: TODO
- **估计**: 1 天
- **优先级**: P1
- **Sprint**: S-04-4

**验收标准**（TST）：
- [ ] `cargo build/test/fmt/clippy --workspace` 通过
- [ ] AI + 双后端 + LSP + 可观测性全部可用

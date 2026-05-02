# 05 — AI 原生架构

## 5.1 AI 是管线内一等公民

传统编译器中 AI 是"事后分析器"。BlockType Next 中 AI 是管线内的**可插拔 Pass**，通过 **AI 编排器** 统一管理：

```
标准管线：Lex → Parse → Analyze → Lower → Optimize → Codegen

AI 增强：
  Lex → Parse → Analyze → [AI_S1: 语义建议] → Lower → [Dialect降级] → Optimize → [AI_S2: 优化推荐] → Codegen → [AI_S3: 质量评估]
```

| 插槽 | 阶段 | 分析内容 |
|------|------|---------|
| AI_S1 | PostAnalysis | 类型推断辅助、代码模式识别、反模式检测 |
| AI_S2 | PostLower + PostDialectLower | Pass 推荐策略、热点预测、内联决策 |
| AI_S3 | PostCodegen | 生成代码质量评估、性能预测 |

## 5.2 AI 编排器 (AIOrchestrator)

统一管理多 Provider，提供 fallback 链、token 预算、结果缓存：

```rust
/// AI 编排器 — 统一入口
pub struct AIOrchestrator {
    /// 多 provider 按优先级排序
    providers: Vec<Box<dyn AIProvider>>,
    /// 规则引擎（零延迟 fallback）
    rule_engine: RuleBasedProvider,
    /// 结果缓存
    cache: Arc<AIResultCache>,
    /// Token 预算管理
    budget: Arc<AtomicBudget>,
}

/// Token 预算管理
pub struct AtomicBudget {
    limit: AtomicUsize,       // 单次编译的 AI token 预算上限
    used: AtomicUsize,        // 已使用量
}

impl AtomicBudget {
    pub fn remaining(&self) -> usize {
        self.limit.load(Ordering::Relaxed) - self.used.load(Ordering::Relaxed)
    }

    pub fn deduct(&self, tokens: usize) {
        self.used.fetch_add(tokens, Ordering::Relaxed);
    }

    pub fn reset(&self) {
        self.used.store(0, Ordering::Relaxed);
    }
}

impl AIOrchestrator {
    pub async fn analyze(&self, req: AIAnalysisRequest) -> Result<Vec<AISuggestion>> {
        // 1. 检查缓存
        if let Some(cached) = self.cache.get(&req.cache_key()).await {
            return Ok(cached);
        }

        // 2. 预算检查
        let estimated = self.providers.first()
            .map(|p| p.estimate_tokens(&req.context))
            .unwrap_or(0);

        if self.budget.remaining() < estimated {
            // 预算不足，用规则引擎 fallback
            return self.rule_engine.analyze(req).await;
        }

        // 3. 依次尝试 provider（fallback 链）
        for provider in &self.providers {
            if !provider.is_healthy().await {
                continue;
            }
            match provider.analyze(req.clone()).await {
                Ok(suggestions) => {
                    self.cache.insert(req.cache_key(), &suggestions).await;
                    self.budget.deduct(provider.estimate_tokens(&req.context));
                    return Ok(suggestions);
                }
                Err(_) => continue,  // fallback 到下一个
            }
        }

        // 4. 全部失败，规则引擎兜底
        self.rule_engine.analyze(req).await
    }

    /// 流式分析（SSE）
    pub async fn analyze_stream(
        &self,
        req: AIAnalysisRequest,
    ) -> Result<Pin<Box<dyn Stream<Item = AISuggestion> + Send>>> {
        // 预算检查后，直接调用主 provider 的流式接口
        let provider = self.providers.first()
            .ok_or(AIError::NoProvider)?;

        if !provider.is_healthy().await {
            // fallback 到规则引擎的非流式接口，包装为 Stream
            let suggestions = self.rule_engine.analyze(req).await?;
            return Ok(Box::pin(futures::stream::iter(suggestions)));
        }

        self.budget.deduct(provider.estimate_tokens(&req.context));
        provider.analyze_stream(req).await
    }
}
```

## 5.3 AI Provider 接口

```rust
/// AI Provider trait — 支持流式、健康检查、Token 估算
#[async_trait]
pub trait AIProvider: Send + Sync {
    /// Provider 名称
    fn name(&self) -> &str;

    /// 标准分析（非流式）
    async fn analyze(
        &self,
        request: AIAnalysisRequest,
    ) -> Result<Vec<AISuggestion>, AIError>;

    /// 流式分析（SSE，返回 Stream）
    async fn analyze_stream(
        &self,
        request: AIAnalysisRequest,
    ) -> Result<Pin<Box<dyn Stream<Item = AISuggestion> + Send>>, AIError>;

    /// Token 预算估算
    fn estimate_tokens(&self, context: &CompilationContext) -> usize;

    /// 健康检查
    async fn is_healthy(&self) -> bool;
}

// 内置提供商：
// - OpenAIProvider (GPT-4o / o3)
// - ClaudeProvider (Anthropic Claude)
// - QwenProvider (阿里通义)
// - LocalProvider (本地 Ollama / llama.cpp)
// - RuleBasedProvider (规则引擎，零延迟，离线可用，始终作为 fallback)
```

## 5.4 AI 上下文注入

AI 服务获得的不是孤立代码片段，而是**完整编译上下文**：

```rust
#[derive(Debug, Clone, Serialize)]
pub struct CompilationContext {
    pub task_id: Uuid,
    pub frontend: String,
    pub source_language: String,
    pub target_triple: String,
    pub source_hash: String,
    pub diagnostics: Vec<Diagnostic>,
    pub phase_metrics: HashMap<String, PhaseMetric>,
    pub optimization_level: OptLevel,
    pub enabled_features: Vec<String>,
    pub active_dialects: Vec<String>,        // 已注册的 Dialect
    pub event_history: Vec<StoredEvent>,     // 当前编译的事件历史
}

/// 缓存 key（基于源码哈希 + 上下文参数）
impl AIAnalysisRequest {
    pub fn cache_key(&self) -> String {
        format!(
            "{}:{}:{}:{:?}",
            self.context.source_hash,
            self.context.frontend,
            self.analysis_type,
            self.options
        )
    }
}
```

## 5.5 AI 自动变换（不只是建议）

```rust
#[derive(Debug, Clone, Serialize)]
pub enum AIAction {
    /// 仅建议，需用户确认
    Suggest(AISuggestion),

    /// 自动应用（低风险变换，如简单优化）
    AutoApply {
        pass_name: String,
        confidence: f32,           // > 0.95 自动应用
        description: String,
    },

    /// 需审核的应用（中等风险）
    RequireReview {
        pass_name: String,
        diff: IRDiff,              // IR 变更差异
        confidence: f32,
        rationale: String,         // AI 推理过程
    },
}

#[derive(Debug, Clone, Serialize)]
pub struct AISuggestion {
    pub category: SuggestionCategory,
    pub confidence: f32,
    pub message: String,
    pub location: Option<SourceLocation>,
    pub action: Option<AIAction>,
}

#[derive(Debug, Clone, Serialize)]
pub enum SuggestionCategory {
    Performance,
    Safety,
    Style,
    ErrorPrevention,
    OptimizationPass,
    PatternDetection,
    DialectLowering,      // 新增：Dialect 降级建议
}
```

## 5.6 AI 对话接口（上下文感知）

```rust
/// 编译上下文感知的 AI 对话
///
/// POST /api/v1/ai/chat
/// Body: { "message": "为什么这段代码编译失败？", "task_id": "abc123" }
///
/// AI 对话时自动注入：
/// - 源码内容
/// - 诊断信息
/// - 管线阶段数据
/// - IR 结构摘要
/// - EventStore 事件历史
/// - 已注册 Dialect 信息
pub struct AIChatService {
    orchestrator: Arc<AIOrchestrator>,
    context_extractor: ContextExtractor,
    event_store: Arc<EventStore>,
}

impl AIChatService {
    pub async fn chat(&self, message: &str, task_id: Option<Uuid>) -> Result<String> {
        let context = match task_id {
            Some(id) => {
                let mut ctx = self.context_extractor.extract(id).await?;
                // 注入事件历史
                ctx.event_history = self.event_store.replay_task(id).await;
                ctx
            }
            None => CompilationContext::default(),
        };

        let prompt = format!(
            "编译器上下文:\n\
             前端: {}\n\
             目标: {}\n\
             活跃 Dialect: {:?}\n\
             诊断: {:?}\n\
             管线状态: {:?}\n\
             事件历史: {} 条\n\n\
             用户问题: {}",
            context.frontend, context.target_triple,
            context.active_dialects,
            context.diagnostics, context.phase_metrics,
            context.event_history.len(),
            message
        );

        let req = AIAnalysisRequest {
            context,
            analysis_type: AnalysisType::Chat,
            options: AIAnalysisOptions::default(),
        };
        let suggestions = self.orchestrator.analyze(req).await?;
        // 将建议组装为自然语言回复
        Ok(format_suggestions_as_reply(&suggestions, message))
    }
}
```

## 5.7 AI 驱动的自动优化示例

```
用户代码:
  for i in 0..n {
    result += data[i] * 2.0;    // 标量循环
  }

AI_S2 建议:
  检测到: 可向量化的循环
  置信度: 0.92
  建议: 自动应用 SIMD 向量化 Pass
  预期加速: 4-8x
  Provider: openai (fallback from claude)

AIAction: AutoApply { pass_name: "auto_vectorize", confidence: 0.92 }

EventStore 记录:
  AIActionApplied { action: "auto_vectorize", confidence: 0.92 }
```

# Phase 5 — 高级特性 + TypeScript 前端

> **目标**：BlockType Rust 全功能可用
> **估计**：~12 周（60 天）| 5 个 Sprint | 16 个 Task
> **里程碑**：优化 Pass + 插件系统 + 增量编译 + AI 流式 + TS 前端 + 端到端测试

---

## Sprint 5-1: bt-passes 优化 Pass + Dialect 降级完善（2 周）

### T-05-01: bt-passes — DCE (Dead Code Elimination)

- **ID**: `T-05-01`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-05-1

**描述**：实现死代码消除 Pass。

**产出文件**：
- `crates/bt-passes/src/passes/dce.rs`

**前置依赖**（DEP）：
- `T-01-08`：Pass trait + PassManager

**验收标准**（TST）：
- [ ] `DCEPass` impl `Pass`
- [ ] 移除不可达基本块
- [ ] 移除无副作用的无用指令
- [ ] 保留有副作用的指令（Store/Call）
- [ ] 测试：IR 中无用指令被正确消除

---

### T-05-02: bt-passes — 常量折叠 + 内联

- **ID**: `T-05-02`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P0
- **Sprint**: S-05-1

**描述**：实现常量折叠和函数内联 Pass。

**产出文件**：
- `crates/bt-passes/src/passes/const_fold.rs`
- `crates/bt-passes/src/passes/inline.rs`

**前置依赖**（DEP）：
- `T-01-08`

**验收标准**（TST）：
- [ ] `ConstFoldPass`: 编译期计算常量表达式
- [ ] `InlinePass`: 内联小函数（阈值可配置）
- [ ] 支持 OptLevel 控制激进度
- [ ] Pass 依赖声明正确

---

### T-05-03: bt-passes — CFG 简化

- **ID**: `T-05-03`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P1
- **Sprint**: S-05-1

**描述**：实现控制流图简化 Pass。

**产出文件**：
- `crates/bt-passes/src/passes/cfg_simplify.rs`

**前置依赖**（DEP）：
- `T-01-08`

**验收标准**（TST）：
- [ ] 合并只有一个前驱/后继的基本块
- [ ] 消除空基本块
- [ ] 简化分支条件（常量条件）

---

### T-05-04: Dialect 降级完善

- **ID**: `T-05-04`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P1
- **Sprint**: S-05-1

**描述**：bt_rust/bt_ts → bt_core 完整降级。

**产出文件**：
- `crates/bt-dialect-rust/src/lower.rs` — 完善
- `crates/bt-dialect-ts/src/lower.rs` — 完善

**前置依赖**（DEP）：
- `T-01-04`, `T-01-05`

**验收标准**（TST）：
- [ ] bt_rust 10 个操作码全部可降级
- [ ] bt_ts 10 个操作码全部可降级
- [ ] 降级后的 IR 通过验证
- [ ] 降级后指令数减少（统计）

---

## Sprint 5-2: 插件系统 + 增量编译（2 周）

### T-05-05: bt-plugin-host — WASM 插件宿主

- **ID**: `T-05-05`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P1
- **Sprint**: S-05-2

**描述**：实现 WASM Component Model 插件宿主。

**产出文件**：
- `crates/bt-plugin-host/src/{lib.rs, host.rs, sandbox.rs, instance.rs}`
- `plugin-sdk/wit/compiler-pass.wit`
- `plugin-sdk/wit/ai-provider.wit`

**设计规格**（REF）：
- `04-Project-Structure.md §4.1` — plugin-sdk
- `01-Design-Philosophy.md §1.1 P3` — 可插拔原则

**前置依赖**（DEP）：
- `T-01-08`：Pass trait

**验收标准**（TST）：
- [ ] `PluginHost::load(path)` 加载 WASM 插件
- [ ] WIT 接口：compiler-pass / ai-provider
- [ ] 沙箱隔离：内存/CPU 限制
- [ ] 插件可注册为 Pass
- [ ] `cargo test -p bt-plugin-host` 通过

---

### T-05-06: plugin-sdk — WIT 定义 + 插件示例

- **ID**: `T-05-06`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P2
- **Sprint**: S-05-2

**描述**：WIT 接口定义和插件开发示例。

**产出文件**：
- `plugin-sdk/wit/*.wit`
- `plugin-sdk/examples/hello_pass.rs`

**前置依赖**（DEP）：
- `T-05-05`

**验收标准**（TST）：
- [ ] compiler-pass.wit 定义完整
- [ ] ai-provider.wit 定义完整
- [ ] Rust 插件示例可编译和加载

---

### T-05-07: Salsa 增量编译完善

- **ID**: `T-05-07`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-05-2

**描述**：完善 Salsa 增量编译，实现文件修改后秒级重编译。

**产出文件**：
- `crates/bt-query/src/engine.rs` — 完善
- `crates/bt-cargo/src/incremental.rs` — 完善

**前置依赖**（DEP）：
- `T-01-07`, `T-02-04`

**验收标准**（TST）：
- [ ] 文件修改后增量重编译 < 2s（小项目）
- [ ] Salsa 依赖追踪精确到函数级
- [ ] 增量编译结果与全量编译一致
- [ ] 依赖图可视化

---

### T-05-08: AI 流式分析 + 自动变换

- **ID**: `T-05-08`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P1
- **Sprint**: S-05-2

**描述**：完善 AI 流式分析和 AutoApply 自动变换。

**产出文件**：
- `crates/bt-ai/src/stream.rs`
- `crates/bt-ai/src/auto_apply.rs`

**前置依赖**（DEP）：
- `T-04-04`

**验收标准**（TST）：
- [ ] `analyze_stream()` 逐 chunk 输出
- [ ] `AutoApply` 自动应用高置信度（>0.9）变换
- [ ] 变换后 IR 通过验证
- [ ] 变换记录到 EventStore

---

## Sprint 5-3: TypeScript 前端 — Lexer + Parser（2 周）

### T-05-09: ts-frontend — TS Lexer

- **ID**: `T-05-09`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P1
- **Sprint**: S-05-3

**描述**：实现 TypeScript 词法分析器（Deno）。

**产出文件**：
- `ts-frontend/src/lexer.ts`

**前置依赖**（DEP）：无（独立 TypeScript 项目）

**验收标准**（TST）：
- [ ] 支持 TypeScript 全部 token 类型
- [ ] 处理模板字符串/正则/JSX
- [ ] 输出 Token 流（JSON 格式）
- [ ] Deno 测试通过

---

### T-05-10: ts-frontend — TS Parser

- **ID**: `T-05-10`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P1
- **Sprint**: S-05-3

**描述**：实现 TypeScript 语法分析器。

**产出文件**：
- `ts-frontend/src/parser.ts`

**前置依赖**（DEP）：
- `T-05-09`

**验收标准**（TST）：
- [ ] 生成 AST（兼容 estree 格式）
- [ ] 支持 TypeScript 特有语法（type/interface/generic/decorator）
- [ ] 错误恢复和友好报错

---

## Sprint 5-4: TypeScript 前端 — TypeCheck + IR Emitter（2 周）

### T-05-11: ts-frontend — TS Type Checker + Type Narrower

- **ID**: `T-05-11`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P1
- **Sprint**: S-05-4

**描述**：实现 TypeScript 类型检查和类型缩窄。

**产出文件**：
- `ts-frontend/src/type_checker.ts`
- `ts-frontend/src/type_narrower.ts`

**前置依赖**（DEP）：
- `T-05-10`

**验收标准**（TST）：
- [ ] 类型推断（局部类型推断）
- [ ] 类型缩窄（typeof/instanceof/判空）
- [ ] 泛型实例化
- [ ] 类型错误检测

---

### T-05-12: ts-frontend — IR Emitter (TS → bt_ts IR)

- **ID**: `T-05-12`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P1
- **Sprint**: S-05-4

**描述**：将 TypeScript AST 转换为 bt_ts Dialect IR（JSON 格式）。

**产出文件**：
- `ts-frontend/src/ir_emitter.ts`

**前置依赖**（DEP）：
- `T-05-11`

**验收标准**（TST）：
- [ ] 生成 bt_ts Dialect IR（JSON 格式）
- [ ] 支持 type_narrow/nullish_coalesce/optional_chain 等 TS 特有操作
- [ ] IR 通过 bt-ir-verifier 验证

---

### T-05-13: bt-frontend-ts — Deno 进程池 + JSON-RPC 桥接

- **ID**: `T-05-13`
- **状态**: TODO
- **估计**: 4 天
- **优先级**: P1
- **Sprint**: S-05-4

**描述**：实现 Rust 侧的 Deno 进程池管理和 JSON-RPC 桥接。

**产出文件**：
- `crates/bt-frontend-ts/src/{lib.rs, pool.rs, bridge.rs, worker.rs}`

**设计规格**（REF）：
- `05-Unified-API.md §2.11` — JSON-RPC 协议
- `04-Project-Structure.md §4.3` — TS 桥接架构

**前置依赖**（DEP）：
- `T-00-15`：bt-frontend-common

**验收标准**（TST）：
- [ ] `TsFrontendPool` 管理 Deno 进程
- [ ] JSON-RPC 2.0 通信：compile/lex/parse/typeCheck/ping
- [ ] 进程池：min/max 进程数 + 负载均衡
- [ ] 健康检查：5s 心跳 + 超时重启
- [ ] `TsFrontend` impl `Frontend` trait
- [ ] `cargo test -p bt-frontend-ts` 通过

---

## Sprint 5-5: 端到端集成 + 交付（2 周）

### T-05-14: ts-runtime — 最小子集

- **ID**: `T-05-14`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P2
- **Sprint**: S-05-5

**描述**：TypeScript 运行时最小子集（string/array 基础操作）。

**产出文件**：
- `runtime/ts-runtime/{lib.rs, string_ops.rs, array_ops.rs}`

**验收标准**（TST）：
- [ ] String: length/concat/slice/split/trim
- [ ] Array: push/pop/map/filter/reduce
- [ ] 可被生成的代码调用

---

### T-05-15: 端到端集成测试 — Rust + TS 双前端

- **ID**: `T-05-15`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-05-5

**描述**：Rust + TypeScript 双前端的端到端集成测试。

**产出文件**：
- `tests/integration/test_dual_frontend.rs`
- `tests/fixtures/ts_project/`

**前置依赖**（DEP）：
- `T-05-13`, `T-05-12`

**验收标准**（TST）：
- [ ] Rust 项目编译运行
- [ ] TypeScript 代码编译运行（通过 bt_ts Dialect）
- [ ] 双前端结果可对比
- [ ] API 驱动整个流程
- [ ] EventStore 记录完整事件链

---

### T-05-16: Phase 5 回归测试 + 最终交付

- **ID**: `T-05-16`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-05-5

**验收标准**（TST）：
- [ ] `cargo build/test/fmt/clippy --workspace` 通过
- [ ] 全量集成测试通过
- [ ] README.md 完整文档
- [ ] CONTRIBUTING.md 更新
- [ ] CI 配置完成
- [ ] BlockType Rust 全功能可用

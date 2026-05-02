# Phase 1 — IR 核心 + Dialect 系统 + 查询引擎

> **目标**：完整的 BTIR 数据模型 + 运行时 Dialect 注册 + 序列化 + 验证 + 查询引擎
> **估计**：~6 周（29 天）| 3 个 Sprint | 12 个 Task
> **里程碑**：可通过 API 构造、验证、序列化、查询 IR 模块；Dialect 可运行时注册/注销

---

## Sprint 1-1: bt-ir 完善 + bt-dialect-rust + bt-dialect-ts（2 周）

### T-01-01: bt-ir — 完整 IRType（泛型/参数化/TraitObject/闭包/FnPointer）

- **ID**: `T-01-01`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-01-1

**描述**：在 Phase 0 最小子集基础上，补充 IRType 的完整泛型/参数化类型。

**产出文件**：
- `crates/bt-ir/src/types.rs` — 补充 Generic/Parameterized/TraitObject/FnPointer/Closure 变体

**设计规格**（REF）：
- `02-Core-Types.md §7.2` — 完整 IRType 定义

**前置依赖**（DEP）：
- `T-00-10`：bt-ir 最小子集

**验收标准**（TST）：
- [ ] IRType 含 Generic{name, constraints: Vec<TraitBound>}
- [ ] IRType 含 Parameterized{name, args: Vec<IRType>}
- [ ] IRType 含 TraitObject{traits, lifetime}
- [ ] IRType 含 FnPointer{params, ret, abi, unsafe_}
- [ ] IRType 含 Closure{captures: Vec<CaptureType>, fn_signature}
- [ ] TraitBound / CaptureType / LifetimeId 结构体
- [ ] Never 变体
- [ ] 所有新变体的序列化/反序列化测试

---

### T-01-02: bt-ir — DialectInstruction + IRAttribute

- **ID**: `T-01-02`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-01-1

**描述**：补充 DialectInstruction 和 IRAttribute。

**产出文件**：
- `crates/bt-ir/src/instruction.rs` — 补充 DialectInstruction + IRAttribute

**设计规格**（REF）：
- `02-Core-Types.md §7.3`

**前置依赖**（DEP）：
- `T-00-10`

**验收标准**（TST）：
- [ ] `DialectInstruction` 含 dialect/opcode/operands/attributes/span
- [ ] `IRAttribute` 枚举：String/Integer/Float/Bool/Type/List
- [ ] IRInstruction 含 `Dialect(DialectInstruction)` 变体
- [ ] 补全：ExtractValue/InsertValue/PHI/Switch/Invoke/Unreachable

---

### T-01-03: bt-ir — 序列化（JSON + 二进制）

- **ID**: `T-01-03`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-01-1

**描述**：实现 IR 模块的 JSON 和二进制序列化。

**产出文件**：
- `crates/bt-ir/src/serde.rs` — 序列化模块

**设计规格**（REF）：
- `10-Phased-Roadmap.md §Phase 1`

**前置依赖**（DEP）：
- `T-01-01`, `T-01-02`

**验收标准**（TST）：
- [ ] `serde_json::to_string(&module)` / `from_str()` 正确序列化/反序列化
- [ ] `bincode::serialize(&module)` / `deserialize()` 正确序列化/反序列化
- [ ] round-trip 测试：serialize → deserialize → assert_eq
- [ ] 大型 IR 模块（100+ 函数）序列化性能测试

---

### T-01-04: bt-dialect-rust — bt_rust Dialect

- **ID**: `T-01-04`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-01-1

**描述**：实现 bt_rust Dialect，含 10 个操作码的降级规则。

**产出文件**：
- `crates/bt-dialect-rust/src/lib.rs`
- `crates/bt-dialect-rust/src/rust_dialect.rs` — RustDialect struct
- `crates/bt-dialect-rust/src/lower.rs` — 10 个操作码的 lower_to_core 实现

**设计规格**（REF）：
- `02-Core-Types.md §7.4` — bt_rust 操作码表 (224-233)

**前置依赖**（DEP）：
- `T-00-16`：bt-dialect-core
- `T-01-01`：完整 IRType

**验收标准**（TST）：
- [ ] `RustDialect` impl `Dialect` trait
- [ ] `opcode_range()` 返回 (224, 233)
- [ ] `operations()` 返回 10 个 OperationDef
- [ ] `lower_to_core()` 实现 10 个操作码降级
- [ ] `ownership_transfer(224)` → memcpy/refcount ops
- [ ] `trait_dispatch(228)` → GEP + Load + Call (vtable)
- [ ] `async_await(230)` → coroutine 状态机
- [ ] `verify()` 校验指令合法性
- [ ] `cargo test -p bt-dialect-rust` 通过

---

### T-01-05: bt-dialect-ts — bt_ts Dialect

- **ID**: `T-01-05`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P1
- **Sprint**: S-01-1

**描述**：实现 bt_ts Dialect。

**产出文件**：
- `crates/bt-dialect-ts/src/{lib.rs, ts_dialect.rs, lower.rs}`

**设计规格**（REF）：
- `02-Core-Types.md §7.4` — bt_ts 操作码表 (240-249)

**前置依赖**（DEP）：
- `T-00-16`, `T-01-01`

**验收标准**（TST）：
- [ ] `TsDialect` impl `Dialect`，opcode_range (240, 249)
- [ ] 10 个操作码的 lower_to_core 实现
- [ ] `type_narrow(240)` → ICmp + Branch
- [ ] `nullish_coalesce(242)` → ICmp + Select

---

## Sprint 1-2: bt-ir-verifier + bt-query（2 周）

### T-01-06: bt-ir-verifier — IR 完整性验证

- **ID**: `T-01-06`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-01-2

**描述**：实现 IR 模块的完整性验证，包括类型检查、控制流验证、Dialect 验证。

**产出文件**：
- `crates/bt-ir-verifier/src/{lib.rs, verifier.rs, type_check.rs, cfg_check.rs}`

**设计规格**（REF）：
- `10-Phased-Roadmap.md §Phase 1`
- `02-Core-Types.md §7.4` — Dialect::verify

**前置依赖**（DEP）：
- `T-01-01`, `T-01-02`

**验收标准**（TST）：
- [ ] `verify(module: &IRModule) -> Result<(), Vec<VerifyError>>`
- [ ] 类型检查：每条指令的 operand 类型匹配
- [ ] 控制流检查：terminator 在 BB 末尾、Branch 目标存在
- [ ] 引用完整性：IRValueId 存在对应 value
- [ ] Dialect 验证：调用各 Dialect::verify
- [ ] 函数签名匹配：Call 指令参数类型与函数签名一致
- [ ] 错误信息含具体位置（function/block/instruction）
- [ ] `cargo test -p bt-ir-verifier` 通过

---

### T-01-07: bt-query — Salsa 风格增量查询引擎

- **ID**: `T-01-07`
- **状态**: TODO
- **估计**: 5 天
- **优先级**: P0
- **Sprint**: S-01-2

**描述**：实现基于 Salsa 的增量查询引擎。

**产出文件**：
- `crates/bt-query/src/{lib.rs, engine.rs, queries.rs, dependency.rs}`

**设计规格**（REF）：
- `03-Communication-Bus.md §3.7` — QueryEngine 完整设计

**前置依赖**（DEP）：
- `T-01-01`：完整 IRType
- `T-00-02`：bt-core

**验收标准**（TST）：
- [ ] `#[salsa::query_group(BlockTypeQueryStorage)]` trait BlockTypeQueries
- [ ] 输入查询：source_text / compile_options
- [ ] 派生查询：lex / parse / type_check / lower_to_ir
- [ ] `SalsaQueryEngine` 含 `db: salsa::Db`
- [ ] `set_source()` → 自动触发依赖失效
- [ ] 增量测试：修改 source_text 后仅重算受影响查询
- [ ] `dependency_graph()` 导出依赖图
- [ ] `impl QueryEngine for SalsaQueryEngine`
- [ ] `cargo test -p bt-query` 通过

---

### T-01-08: bt-passes — Pass trait + PassManager

- **ID**: `T-01-08`
- **状态**: TODO
- **估计**: 3 天
- **优先级**: P0
- **Sprint**: S-01-2

**描述**：实现 Pass trait（含依赖声明）和 PassManager。

**产出文件**：
- `crates/bt-passes/src/{lib.rs, pass.rs, pass_manager.rs}`

**设计规格**（REF）：
- `02-Core-Types.md §7.8` — Pass trait

**前置依赖**（DEP）：
- `T-01-01`, `T-00-03`

**验收标准**（TST）：
- [ ] `Pass` trait: name/category/dependencies/invalidates/async run
- [ ] `PassCategory`: Analysis/Transformation/Verification/DialectLowering/AIAnalysis
- [ ] `PassContext` 含 tracer/metrics/diagnostics/cancelled
- [ ] `PassResult` 含 modified/metrics/diagnostics
- [ ] `PassManager::new()` + `register(pass)` + `verify(module)` + `optimize(module, level)`
- [ ] 依赖拓扑排序执行 Pass
- [ ] 支持 `cancelled` flag 取消
- [ ] `cargo test -p bt-passes` 通过

---

## Sprint 1-3: IR 操作 API + 查询集成（1 周）

### T-01-09: bt-api — IR 操作 API 端点

- **ID**: `T-01-09`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-01-3

**描述**：在 bt-api 中添加 `/api/v1/ir/*` 端点。

**产出文件**：
- `crates/bt-api/src/handlers/ir.rs`

**设计规格**（REF）：
- `05-Unified-API.md §2.3` — IR 操作 API
- `05-Unified-API.md §2.3.1` — IR Diff API

**前置依赖**（DEP）：
- `T-00-07`：bt-api
- `T-01-03`：IR 序列化
- `T-01-06`：IR 验证

**验收标准**（TST）：
- [ ] `POST /api/v1/ir/load` + `GET /api/v1/ir/modules/{id}` + functions/types
- [ ] `POST /api/v1/ir/verify` + `POST /api/v1/ir/optimize`
- [ ] `POST /api/v1/ir/diff`（三格式：text/structured/visual）
- [ ] `GET /api/v1/ir/dialects` + `GET /api/v1/ir/dialects/{name}`

---

### T-01-10: CompileService — 集成查询引擎

- **ID**: `T-01-10`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P0
- **Sprint**: S-01-3

**描述**：将 SalsaQueryEngine 集成到 CompileService 编译流程中。

**产出文件**：
- `crates/bt-service/src/compile_service.rs` — 更新

**前置依赖**（DEP）：
- `T-00-04`, `T-01-07`

**验收标准**（TST）：
- [ ] CompileService::compile 使用 QueryEngine 执行增量编译
- [ ] 输入变更后仅重算受影响阶段

---

### T-01-11: bt-api — 管理操作 API

- **ID**: `T-01-11`
- **状态**: TODO
- **估计**: 2 天
- **优先级**: P1
- **Sprint**: S-01-3

**描述**：添加 `/api/v1/admin/*` 端点。

**产出文件**：
- `crates/bt-api/src/handlers/admin.rs`

**设计规格**（REF）：
- `05-Unified-API.md §2.4`

**前置依赖**（DEP）：
- `T-00-07`

**验收标准**（TST）：
- [ ] frontends/backends 列表和详情端点
- [ ] plugins 列表/加载/卸载/状态端点
- [ ] `/api/v1/admin/status` 系统状态

---

### T-01-12: Phase 1 回归测试

- **ID**: `T-01-12`
- **状态**: TODO
- **估计**: 1 天
- **优先级**: P1
- **Sprint**: S-01-3

**验收标准**（TST）：
- [ ] `cargo build/test/fmt/clippy --workspace` 通过
- [ ] IR 模块可通过 API 构造→验证→序列化→反序列化→查询
- [ ] Dialect 可运行时注册→降级→注销

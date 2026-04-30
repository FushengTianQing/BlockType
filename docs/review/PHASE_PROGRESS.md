# BlockType 项目进度看板

> **最后更新**: 2026-04-30
> **当前阶段**: 多前端后端重构 Phase C 完成，准备进入 Phase D
> **整体完成度**: ~80%
> **权威规划**: [docs/plan/PROJECT_PLAN_MASTER.md](../plan/PROJECT_PLAN_MASTER.md)
> **重构规划**: [docs/plan/多前端后端重构方案/05-渐进式改造方案-PhaseAB.md](../plan/多前端后端重构方案/05-渐进式改造方案-PhaseAB.md)

---

## 编译器核心流水线进度

| Phase | 名称 | 状态 | 完成度 | 关键问题 |
|-------|------|------|--------|---------|
| Phase 0 | 基础设施 | ✅ 完成 | ~90% | Lit 测试和 CI/CD 待完善 |
| Phase 0.5 | 双语设计 | ✅ 完成 | 100% | — |
| Phase 0.6 | AI 基础设施 | ✅ 完成 | 100% | — |
| Phase 1 | 词法分析+预处理 | ✅ 完成 | 100% | — |
| Phase 2 | 表达式/语句解析 | ✅ 完成 | 98% | — |
| Phase 3 | 声明/类解析 | ✅ 完成 | ~85% | 56 项缺失已逐步修复 |
| Phase 4 | 语义分析基础 | ✅ 完成 | ~90% | 枚举提升、访问控制简化等已知问题 |
| Phase 4.5 | 可视化系统 | ❌ 未开始 | 0% | — |
| Phase 5 | 模板与泛型 | ✅ 完成 | ~95% | 性能优化待做 |
| Phase 6 | IR 生成 | ✅ 完成 | 96% | 7 个 P2 问题待改进 |
| Phase 7 | C++26 特性 | 🔄 进行中 | ~40% | Contracts P0 问题；C++26 覆盖率 31% |
| Phase 7.5 | AI 集成 | ⚠️ 基础框架 | ~30% | 框架已有，深度集成未做 |
| Phase 8 | 目标平台支持 | ❌ 未开始 | 0% | — |
| Phase 9 | 集成与发布 | ❌ 未开始 | 0% | — |

---

## 多前端后端重构进度

> 规划文档：`docs/plan/多前端后端重构方案/`
> 核心目标：建立 IR 中间层，实现前端/后端自由组合

### Phase 总览

| Phase | 名称 | 状态 | 完成度 | 关键提交 |
|-------|------|------|--------|---------|
| **Phase A** | **IR 层基础设施** | **✅ 完成** | **100%** | A.1~A.8 + A-F1~A-F13 全部完成 |
| **Phase B** | **前端抽象层** | **✅ 完成** | **100%** | B.1~B.10 + B-F1~B-F10 全部完成 |
| Phase C | 后端抽象层 | ✅ 完成 | 100% | C.1~C.9 全部完成 |
| Phase D | 管线重构 | ⏳ 待开始 | 0% | — |
| Phase F | 高级特性 | ⏳ 待开始 | 0% | — |
| Phase G | 增量编译 | ⏳ 待开始 | 0% | — |
| Phase H | AI 辅助与极致优化 | ⏳ 待开始 | 0% | — |

### Phase A 详情：IR 层基础设施 ✅

| Task | 内容 | 状态 | 提交 |
|------|------|------|------|
| A.1 | IRType 体系 + IRTypeContext | ✅ 完成 | — |
| A.1.1 | IRContext + BumpPtrAllocator | ✅ 完成 | — |
| A.1.2 | IRThreadingMode + seal 接口 | ✅ 完成 | — |
| A.2 | TargetLayout（独立于 LLVM DataLayout） | ✅ 完成 | — |
| A.3 | IRValue + IRConstant + Use/User 体系 | ✅ 完成 | — |
| A.3.1 | IRFormatVersion + IRFileHeader | ✅ 完成 | — |
| A.4 | IRModule/IRFunction/IRBasicBlock | ✅ 完成 | — |
| A.5 | IRBuilder（含常量工厂） | ✅ 完成 | — |
| A.6 | IRVerifier | ✅ 完成 | — |
| A.7 | IRSerializer（文本+二进制） | ✅ 完成 | — |
| A.8 | CMake 集成 + 单元测试 | ✅ 完成 | — |
| A-F1 | IRInstruction/IRType DialectID 字段 | ✅ 完成 | — |
| A-F2 | DialectCapability + BackendCapability | ✅ 完成 | — |
| A-F3 | TelemetryCollector 基础框架 + PhaseGuard | ✅ 完成 | — |
| A-F4 | IRInstruction DbgInfo 字段 | ✅ 完成 | — |
| A-F5 | IRDebugMetadata 基础类型定义 | ✅ 完成 | — |
| A-F6 | StructuredDiagnostic 基础结构 | ✅ 完成 | — |
| A-F7 | CacheKey/CacheEntry 基础类型 | ✅ 完成 | — |
| A-F8 | IRIntegrityChecksum 基础实现 | ✅ 完成 | — |
| A-F9~A-F13 | 后续增强任务 | ✅ 完成 | — |

### Phase B 详情：前端抽象层 ✅

| Task | 内容 | 状态 | 提交 |
|------|------|------|------|
| B.1 | FrontendBase + FrontendRegistry + autoSelect | ✅ 完成 | — |
| B.2 | IRMangler（独立于 CodeGenModule） | ✅ 完成 | — |
| B.2.1 | IRConversionResult + 错误占位策略 | ✅ 完成 | — |
| B.3 | IRTypeMapper: QualType→IRType | ✅ 完成 | `1f04cc6` |
| B.3.1 | DeadFunctionEliminationPass | ✅ 完成 | — |
| B.3.2 | ConstantFoldingPass | ✅ 完成 | — |
| B.3.3 | TypeCanonicalizationPass | ✅ 完成 | — |
| B.3.4 | createDefaultIRPipeline() | ✅ 完成 | — |
| B.4 | ASTToIRConverter: 表达式基础 | ✅ 完成 | `86091e2` |
| B.4.1 | 契约验证函数 + FrontendIRContractTest | ✅ 完成 | — |
| B.5 | IREmitExpr: 表达式（含 C++ 特有） | ✅ 完成 | `6e68fb1` |
| B.6 | IREmitStmt: 语句 + 控制流 | ✅ 完成 | `7f6f857` |
| B.7 | IREmitCXX: C++ 特有发射器（4 个子发射器） | ✅ 完成 | `d5f8f62` |
| B.8 | IRMangler: 后端无关名称修饰 | ✅ 完成 | `cff4366` |
| B.9 | IRConstantEmitter 常量工厂 | ✅ 完成 | `bbadf40` |
| B.10 | CppFrontend 集成 + 契约验证 | ✅ 完成 | `e442df2` |
| B-F1 | PluginManager（插件注册框架） | ✅ 完成 | `72218f9` |
| B-F2 | StructuredDiagnostic（详细结构化诊断） | ✅ 完成 | `72218f9` |
| B-F3 | FrontendFuzzer（前端模糊测试框架） | ✅ 完成 | `72218f9` |
| B-F4 | IREquivalenceChecker（IR 等价性检查器） | ✅ 完成 | `72218f9` |
| B-F5 | TelemetryCollector + CompilerInstance 集成 | ✅ 完成 | `2cfb8bb` → `d9a4399` |
| B-F6 | StructuredDiagEmitter（Text/JSON/SARIF） | ✅ 完成 | `d4f1a3f` → `d9a4399` |
| B-F7 | DiagnosticGroupManager 扩展 | ✅ 完成 | `d4f1a3f` → `d9a4399` |
| B-F8 | LocalDiskCache 基础实现 | ✅ 完成 | 已有基础 → `d9a4399` |
| B-F9 | PassInvariantChecker（SSA/类型不变量） | ✅ 完成 | `b9c21fe` → `d9a4399` |
| B-F10 | IR 调试信息：前端→IR 元数据传递 | ✅ 完成 | `23a5539` → `d9a4399` |

### Phase C 详情：后端抽象层 ✅

| Task | 内容 | 状态 | 说明 |
|------|------|------|------|
| C.1 | BackendBase + BackendRegistry + BackendOptions | ✅ 完成 | 与 Frontend 对称设计 |
| C.2 | IRToLLVMConverter 类型转换 | ✅ 完成 | mapType 完整映射所有 IRType→LLVM Type |
| C.3 | IRToLLVMConverter 指令转换 | ✅ 完成 | 覆盖所有 Opcode（含 atomic/FFI/dialect） |
| C.4 | IRToLLVMConverter 模块转换 | ✅ 完成 | convert/convertFunction/convertGlobalVariable/convertConstant |
| C.5 | LLVMBackend 优化 pipeline | ✅ 完成 | getCapability/canHandle/optimize/checkCapability |
| C.6 | LLVMBackend 代码生成 | ✅ 完成 | emitObject/emitAssembly/emitIRText |
| C.7 | LLVMBackend 调试信息 | ✅ 完成 | DWARF5 + IR 调试元数据映射 |
| C.8 | LLVMBackend VTable/RTTI | ✅ 完成 | Itanium ABI: __dynamic_cast/vtable dispatch/typeinfo |
| C.9 | 集成测试 | ✅ 完成 | Registry+autoSelect+调试信息+多平台 |

### 测试状态

| 指标 | 数值 |
|------|------|
| 总测试数 | 1109 |
| 通过 | 1103 |
| 失败（预存问题） | 6 |
| 通过率 | 99% |

---

## Phase 7 进度详情

### Stage 状态

| Stage | 名称 | 状态 | 完成度 | 关键问题 |
|-------|------|------|--------|---------|
| 7.1 | 基础 C++26 特性 | ✅ 完成 | 100% | 包索引 T...[N]、#embed |
| 7.2 | 静态反射 | ⚠️ 基础框架 | ~30% | 功能有限，需完善元编程 API |
| 7.3 | Contracts | ⚠️ 部分实现 | ~40% | **P0: EmitContractCheck 零调用** |
| 7.4 | C++26 P1 特性 | ❌ 未开始 | 0% | = delete("reason")、占位符 _ 等 |
| 7.5 | 其他特性 + 测试 | ❌ 未开始 | 0% | — |

### C++ 特性支持率

| 标准 | 已实现 | 部分实现 | 未实现 | 支持率 |
|------|--------|---------|--------|--------|
| C++23 | 13 | 1 | 4 | 72% |
| C++26 | 2 | 3 | 11 | 31% |

---

## 模块完成度

| 模块 | 完成度 | 关键问题 |
|------|--------|---------|
| Basic | ~90% | Lit 测试和 CI/CD 待完善 |
| Lex | 100% | — |
| Parse | ~85% | C-style cast 限制 |
| AST | ~95% | classof() 需验证 |
| Sema | ~80% | Early return 问题 |
| CodeGen | 96% | 7 个 P2 问题 |
| AI | ~30% | 基础框架完成，深度集成未做 |
| Module | ~60% | C++20 modules 基本完整 |
| **IR** | **100%** | **Phase A 完成，独立于 LLVM** |
| **Frontend** | **~90%** | **Phase B 完成，抽象层+CppFrontend 已实现** |
| **Backend** | **~85%** | **Phase C 完成，BackendBase+IRToLLVMConverter+LLVMBackend 已实现** |

---

## 问题追踪

### P0（阻塞）

| # | 问题 | 来源 |
|---|------|------|
| 1 | EmitContractCheck 零调用，Contract IR 代码从未生成 | Phase 7.3 审计 |

### P1（严重）

| # | 问题 | 来源 |
|---|------|------|
| 1 | Parser 155+ 处直接创建 AST 节点不经过 Sema | AST 重构规划 |
| 2 | CheckContractPlacement Assert 分支未实现验证 | Phase 7.3 审计 |
| 3 | AttachContractsToFunction 为死代码 | Phase 7.3 审计 |
| 4 | 枚举类型提升缺失 | Phase 4.4 审计 |
| 5 | nullptr->bool 转换缺失 | Phase 4.4 审计 |
| 6 | CheckMemberAccess 诊断代码被注释掉 | Phase 4.5 审计 |
| 7 | CheckBaseClassAccess 始终返回 false | Phase 4.5 审计 |

### P2（中等）

| # | 问题 | 来源 |
|---|------|------|
| 1 | 协程完整实现 | CodeGen 审计 |
| 2 | 依赖类型表达式 | CodeGen 审计 |
| 3 | 虚继承完整实现 | CodeGen 审计 |
| 4 | RTTI 完整实现 | CodeGen 审计 |
| 5 | 复数/向量类型支持 | CodeGen 审计 |
| 6 | 属性查找统一接口 | CodeGen 审计 |
| 7 | NRVO 分析移至 Sema | CodeGen 审计 |

> 完整问题清单见 [docs/plan/PROJECT_PLAN_MASTER.md](../plan/PROJECT_PLAN_MASTER.md) 第七章

---

## 项目审查进度

| 审查 Phase | 状态 | 完成度 |
|-----------|------|--------|
| Phase 1: 流程地图与基础设施 | ✅ 完成 | 100% |
| Phase 2: 功能域分析与映射 | ✅ 完成 | 100% |
| Phase 3: 问题诊断与根因分析 | ⏳ 待开始 | 0% |
| Phase 4: 整合方案设计 | ⏳ 待开始 | 0% |
| Phase 5: 工程质量保障审查 | ⏳ 待开始 | 0% |
| Phase 6: 性能优化与深度技术审查 | ⏳ 待开始 | 0% |
| Phase 7: 执行验证 | ⏳ 待开始 | 0% |

---

## 里程碑

- [x] M1 - 基础设施完成 (Phase 0)
- [x] M2 - 词法分析完成 (Phase 1)
- [x] M3 - 语法分析完成 (Phase 2-3)
- [x] M4 - 语义分析完成 (Phase 4)
- [x] M5 - 代码生成完成 (Phase 6)
- [x] **M-IR-A - IR 层基础设施完成 (多前端后端 Phase A)**
- [x] **M-IR-B - 前端抽象层完成 (多前端后端 Phase B)**
- [x] **M-IR-C - 后端抽象层完成 (多前端后端 Phase C)** ← 最新完成
- [ ] M6 - Alpha 发布 (Phase 7)
- [ ] **M-IR-D - 管线重构 (多前端后端 Phase D)** ← 下一步
- [ ] M7 - Beta 发布 (Phase 8)
- [ ] M8 - 正式发布 (Phase 9)

---

*本看板反映项目实际进度，数据来源为各 Phase 审计报告。权威规划文档为 [docs/plan/PROJECT_PLAN_MASTER.md](../plan/PROJECT_PLAN_MASTER.md)。多前端后端重构进度见 [docs/plan/多前端后端重构方案/](../plan/多前端后端重构方案/)。*

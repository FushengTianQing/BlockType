# P1 审查报告：Parse 模块 — 直接创建 AST 节点问题评估

**审查人员**: reviewer  
**审查日期**: 2026-04-23  
**审查范围**: `src/Parse/` 全部文件  
**审查重点**: Parser 直接创建 AST 节点而非通过 Sema Actions 的问题

---

## 审查摘要

| 严重程度 | 数量 |
|---------|------|
| P0 (必须修复) | 2 |
| P1 (高优先级) | 3 |
| P2 (中优先级) | 2 |

**总体评价**: Parse 模块最大的架构问题是 Parser 直接创建 AST 节点（通过 `Context.create<T>(...)`），而非通过 Sema Actions 接口。这是 Clang 明确避免的做法——Clang 的 Parser 只负责语法分析，所有语义分析和 AST 节点创建都通过 `Sema::ActOn*` 系列方法完成。当前做法导致：

1. **类型信息缺失**: Parser 创建的节点没有经过类型检查，`setType()` 可能在 Parse 阶段被调用但类型可能不正确
2. **语义检查缺失**: 跳过了 Sema 层的所有语义检查（重载解析、访问控制、隐式转换等）
3. **重构困难**: AST 节点的创建逻辑分散在 Parse 和 Sema 两层，修改任何节点创建逻辑需要同时修改两处

---

## P0 问题（必须修复）

### P0-1: Parser 大量直接创建 AST 节点绕过 Sema

**文件**: `src/Parse/Parser.cpp` 及其他 Parse 文件  
**问题描述**: Parser 中存在大量 `Context.create<T>(...)` 调用直接创建 AST 节点。根据搜索结果，Parser 中有约 20+ 处 `Context.create` 调用。这些调用绕过了 Sema 层的语义分析，导致：
- 创建的节点没有经过类型检查
- 隐式转换节点没有被插入
- 重载解析没有被触发
- 作用域查找没有被正确执行  
**影响**: 这是整个编译器最严重的架构问题之一。任何依赖语义分析的代码（重载、模板、ADL 查找等）在 Parse 阶段都无法正确工作。  
**修改建议**: 参考 Clang 的做法，将所有 AST 节点创建迁移到 `Sema::ActOn*` 方法中。Parser 只负责：
1. 语法分析和 token 消费
2. 调用 Sema Actions 接口
3. 传递 Sema 返回的 AST 节点  
这是一个大型重构，建议分阶段实施：
- Phase 1: 将表达式节点创建迁移到 Sema
- Phase 2: 将声明节点创建迁移到 Sema
- Phase 3: 将语句节点创建迁移到 Sema  
**预期完成时间**: 20 天（分阶段）

### P0-2: Parser 中直接调用 setType() 设置类型

**文件**: `src/Parse/Parser.cpp` 多处  
**问题描述**: Parser 在创建节点后直接调用 `setType()` 设置类型信息。类型推导和检查是 Sema 的职责，Parser 不应有类型信息。例如：
```cpp
auto *E = Context.create<DeclRefExpr>(Loc, VD);
E->setType(VD->getType()); // Parser 不应设置类型
```
**修改建议**: 移除 Parser 中所有 `setType()` 调用，类型设置应在 Sema 的 `ActOn*` 方法中完成。  
**预期完成时间**: 3 天

---

## P1 问题（高优先级）

### P1-1: Parser 缺少完整的错误恢复机制

**文件**: `src/Parse/Parser.cpp`  
**问题描述**: 虽然有 `skipUntil` 和 `skipUntilBalanced` 等错误恢复方法，但：
1. `skipUntilNextDeclaration` 有 50 token 的硬编码限制，可能跳过太少或太多
2. 没有根据上下文选择恢复策略（声明级 vs 语句级 vs 表达式级）
3. 错误恢复后可能产生级联错误  
**修改建议**: 
1. 增加上下文感知的错误恢复策略
2. 添加级联错误抑制机制（连续 N 个错误后停止报告）
3. 参考 Clang 的 `Parser::SkipUntil` 实现  
**预期完成时间**: 5 天

### P1-2: Parser 没有与 Sema 作用域同步

**文件**: `src/Parse/Parser.cpp`  
**问题描述**: Parser 没有在进入/退出作用域时通知 Sema。Clang 的做法是 Parser 调用 `Sema::PushDeclContext()`/`Sema::PopDeclContext()` 来同步作用域。当前实现中，Sema 的作用域管理与 Parse 的括号匹配是独立的，可能导致：
- 名称查找在错误的作用域中执行
- 前向引用可能找不到正确的声明  
**修改建议**: 在 Parser 的 `parseCompoundStmt`、`parseDeclaration` 等方法中添加 Sema 作用域推送/弹出调用。  
**预期完成时间**: 5 天

### P1-3: 预处理指令处理过于简化

**文件**: `src/Parse/Parser.cpp:44-48`  
**问题描述**: 预处理指令（`#include`、`#define` 等）只是简单跳过，没有：
1. `#include` 的文件包含处理
2. `#define`/`#undef` 的宏定义
3. `#if`/`#ifdef`/`#ifndef` 的条件编译
4. `#pragma` 的编译指示处理  
**修改建议**: 实现完整的预处理器，或集成现有预处理器（如 clang 的 Preprocessor）。  
**预期完成时间**: 15 天

---

## P2 问题（中优先级）

### P2-1: Parser 的 lookahead 机制简单

**文件**: `src/Parse/Parser.cpp`  
**问题描述**: Parser 只有一个 token 的 lookahead（`NextTok`），对于需要多个 token lookahead 的语法（如区分函数声明和变量声明）不够用。  
**修改建议**: 实现可回溯的 lookahead 机制（如 Clang 的 `TentativeParsing`）。  
**预期完成时间**: 5 天

### P2-2: 中英双语关键字支持不完整

**文件**: `src/Parse/Parser.cpp`  
**问题描述**: 项目声称支持中英双语，但 Parser 中的关键字匹配可能只处理了英文关键字。中文关键字（如 `类` 代替 `class`、`如果` 代替 `if`）的词法分析和语法分析支持需要验证。  
**修改建议**: 验证中文关键字的完整支持，确保 Lexer 和 Parser 都正确处理。  
**预期完成时间**: 3 天

---

## 修改优先级建议

1. **立即**: P0-2（移除 Parser 中的 setType 调用）— 风险低、收益高
2. **短期**: P1-2（Parser 与 Sema 作用域同步）— 为后续重构铺路
3. **中期**: P0-1（AST 节点创建迁移到 Sema）— 最重要的架构改进
4. **长期**: P1-3（完整预处理器）— 功能完整性要求

---
name: ast-phase2-parser-sema-delegation
overview: 制定 AST 完整重构第二阶段详细方案文档，输出到 docs/dev status/ast功能重构/ 目录。全面改造 Parser 委托 Sema 创建节点，解决所有 AST 职责分散问题。
todos:
  - id: write-phase2-plan
    content: 编写阶段2完整重构方案文档到 docs/dev status/AST功能重构/AST功能分阶段重大重构-阶段2.md
    status: completed
---

## 用户需求

用户要求制定 AST 完整重构的第二阶段详细方案，输出到 `docs/dev status/AST功能重构/` 目录。核心目标：

1. 全面改造 Parser 委托 Sema 创建 AST 节点（172 处 Context.create<> 调用）
2. 消除 Parser 直接创建未类型化节点的架构偏差
3. 解决所有潜在风险，不留后遗症
4. 保持 662 个测试全部通过

## 产品概述

将方案文档写入 `docs/dev status/AST功能重构/AST功能分阶段重大重构-阶段2.md`，内容包括完整的重构路线图、分批执行计划、新增 ActOn* 方法签名、修改文件清单、风险分析和回滚策略。

## 核心功能

- 输出一份完整的、可执行的重构方案 Markdown 文档
- 文档需覆盖：前置准备、基础设施改造、8 个子阶段的分批改造计划、测试策略、风险预案

## 技术栈

C++ 编译器项目，LLVM 基础设施（BumpPtrAllocator、raw_ostream、dyn_cast），CMake 构建系统。

## 实现方案

### 核心策略：分 8 个子阶段渐进式改造

**原理**：172 处修改不可能一次性完成，必须按依赖关系和风险等级分批。每批完成后编译+全量测试验证，确保可随时暂停和回滚。

**改造模式**：每个 `Context.create<T>(args)` 调用替换为 `SemaRef.ActOnT(args)` 调用，Sema 内部完成节点创建+类型设置+语义检查。

### 前置条件（已满足）

- 阶段 1 已完成：CodeGen 不再修改 AST，Sema::ProcessAST 已激活
- CMake 依赖已就绪：`blocktype-parse` 已链接 `blocktype-sema`

### 架构约束

- Parser 需要新增 `Sema &SemaRef` 成员，构造函数签名变为 `Parser(Preprocessor&, ASTContext&, Sema&)`
- driver.cpp 需要在创建 Parser 前先创建 Sema 实例
- Sema 内部的 TemplateInstantiator 使用 Context.create<> 创建节点，这些**不需要改造**（Sema 内部有权创建节点）
- Scope 管理暂时保留双轨制（Parser 维护解析用 Scope，Sema 维护语义用 Scope），后续阶段统一

### 文档结构

文档写入 `docs/dev status/AST功能重构/AST功能分阶段重大重构-阶段2.md`，包含：

1. 概述与目标
2. 前置准备（Parser 构造函数改造、driver.cpp 调整）
3. 8 个子阶段详细计划（按风险从低到高排序）
4. 新增 Sema ActOn* 方法完整签名清单
5. 修改文件清单
6. 测试策略
7. 风险分析与回滚方案
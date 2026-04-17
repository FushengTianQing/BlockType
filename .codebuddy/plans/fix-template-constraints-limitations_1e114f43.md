---
name: fix-template-constraints-limitations
overview: 改进两个已知局限：(1) 模板特化中 `class Vector<int>` 的 `<` 歧义问题 — 需要添加显式特化测试验证当前解析路径是否正确；(2) requires/constraint 表达式中 `&&`/`||` 合取析取的测试覆盖
todos:
  - id: verify-precedence
    content: 使用 [subagent:code-explorer] 确认 PrecedenceLevel 枚举定义和 LogicalOr 常量位置
    status: completed
  - id: add-spec-tests
    content: 在 template.test 中新增显式特化和偏特化测试用例并运行验证
    status: completed
    dependencies:
      - verify-precedence
  - id: fix-constraint-parse
    content: 修改 parseConstraintExpression() 使用 LogicalOr 优先级替代全表达式
    status: completed
    dependencies:
      - verify-precedence
  - id: add-constraint-tests
    content: 在 concept.test 中新增复合约束表达式测试 (&&, ||, 嵌套 requires)
    status: completed
    dependencies:
      - fix-constraint-parse
  - id: update-gap-doc
    content: 更新 PHASE3-GAP-ANALYSYS.md 将两个局限标记为已解决
    status: completed
    dependencies:
      - add-spec-tests
      - add-constraint-tests
  - id: build-test-all
    content: 构建并运行全部测试确保无回归 (目标 387+ tests pass)
    status: completed
    dependencies:
      - update-gap-doc
      - fix-constraint-parse
---

## 产品概述

消除 GAP 分析文档中 Stage 3.3 的两个已知局限：

1. **模板特化 `<` 歧义问题** — `class Vector<int>` 中模板参数的解析验证与修复
2. **约束表达式高级语法** — 合取 `&&`、析取 `||` 的测试覆盖与语义正确性保证

## 核心功能

### 局限 1: 模板特化 `<` 歧义（代码已存在，缺测试+可能需微调）

- **现状分析**: `parseClassDeclaration()` (ParseClass.cpp:50) 在消耗 identifier 后检查 `Tok.is(TokenKind::less)` 并调用 `parseTemplateArgumentList()`。代码逻辑**看起来正确**
- **需要做的事**:

1. 编写显式特化/偏特化的 lit 测试用例来验证完整路径：`template<> class Vector<int> {}`
2. 如果测试发现 `<` 确实被词法分析器或解析器错误处理为比较运算符，则在 `parseClassDeclaration` / `parseStructDeclaration` 入口增加 lookahead 修复
3. 验证特化参数通过 `ParsedTemplateArgs` 正确回传到 `parseTemplateDeclaration`

### 局限 2: 约束表达式 `&&` / `||`（底层支持已有，缺约束级语法限制和测试）

- **现状分析**: 
- `parseConstraintExpression()` (ParseTemplate.cpp:838) 直接调用 `parseExpression()`
- `parseExpression()` 通过 precedence climbing 已支持 `&&` (LAnd) 和 `||` (LOr)
- 但 C++20 标准要求约束表达式是 **logical-or-expression**（不含赋值、逗号等），当前实现允许过多操作符
- **需要做的事**:

1. 将 `parseConstraintExpression()` 从 `parseExpression()` 改为调用 `parseAssignmentExpression()` 或限制到 logical-or 层级
2. 编写复合约束表达式的 lit 测试：`requires Addable<T> && Swappable<T>`、`C1<T> || C2<T>`
3. 更新 GAP 文档标记两个局限为已解决

## Tech Stack

- 语言: C++17
- 测试框架: LLVM Lit + FileCheck (项目现有测试基础设施)
- 构建系统: CMake

## 实现方案

### 方案概述

这是一个"验证+加固+补测"任务，不是大规模重构。两个局限的核心代码已经存在，主要工作是：

1. **局限1**: 通过测试驱动的方式验证 `class Vector<int>` 解析路径是否正常工作，如有问题则做最小修复
2. **局限2**: 收紧 `parseConstraintExpression()` 的语法范围使其符合 C++20 标准，并补充测试

### 关键技术决策

**局限1 - `<` 歧义分析:**

- 词法层面：`<` 和 `>` 在 C++ 中是多义符号，但 BlockType 的 Lexer 应该已经将它们统一 token 化为 `TokenKind::less` / `TokenKind::greater`
- 解析层面：`parseClassDeclaration` 在 identifier 后直接检查 `less` token，不经过表达式解析器，所以**不存在比较运算符歧义**
- 真正的风险点在 `template<> class Vector<int> {}` 路径中 `parseDeclaration(&SpecTemplateArgs)` 是否正确将参数传回
- 如果发现问题，修复方式是在 `parseDeclaration` → `kw_class` 分支确保 `ParsedTemplateArgs` 始终向下传递

**局限2 - 约束表达式收紧:**

- C++20 标准 [temp.constr] 规定 constraint-expression 是 logical-or expression
- 当前 `parseExpression()` 允许逗号表达式（precedence 从 Comma 开始），这对约束表达式过于宽松
- 改进方案：使用 `parseAssignmentExpression()` 作为入口（从 Assignment precedence 开始），排除逗号表达式；或者更好的方案是使用 `parseExpressionWithPrecedence(PrecedenceLevel::LogicalOr)` 来精确限定到 logical-or 级别
- 这保证了 `&&` 和 `||` 正常工作，同时拒绝 `,` 和 `=` 等不应出现在约束中的操作符

### 架构影响

- 仅修改 `ParseTemplate.cpp` 中的 `parseConstraintExpression()` 一个函数
- 仅新增/修改 `.test` 文件（lit 测试）
- 仅更新 `PHASE3-GAP-ANALYSIS.md` 文档状态
- 不影响任何现有 API 或其他模块

## 目录结构

```
tests/lit/parser/
  template.test              # [MODIFY] 新增显式特化、偏特化测试用例
  concept.test               # [MODIFY] 新增复合约束表达式测试用例
src/Parse/
  ParseTemplate.cpp          # [MODIFY] parseConstraintExpression() 收紧语法范围
docs/
  PHASE3-GAP-ANALYSIS.md     # [MODIFY] 更新两个局限的状态标记
```

## 关键代码结构

```cpp
// ParseTemplate.cpp - 修改后的 parseConstraintExpression
// 约束表达式 = logical-or-expression (C++20 [temp.constr])
Expr *Parser::parseConstraintExpression() {
  // 使用 LogicalOr 优先级级别，允许 || 和 && 但拒绝逗号和赋值
  return parseExpressionWithPrecedence(PrecedenceLevel::LogicalOr);
}
```

## Implementation Notes

- **性能**: 无性能影响 — 只是改变了一个函数调用的起始优先级
- **回归风险**: 极低。`parseExpressionWithPrecedence` 已有且稳定；收紧只会让之前错误接受的非法输入被拒绝。但仍需跑全部 387 个测试确认
- **Blast radius**: 仅影响 requires 子句和 concept 定义中的约束表达式解析
- **测试策略**: 先运行新测试确认当前行为，再修改代码，再确认测试仍然通过

## Agent Extensions

### SubAgent

- **code-explorer**
- Purpose: 在实施过程中探索相关代码路径，确认 `PrecedenceLevel` 枚举值定义位置以及 `parseExpressionWithPrecedence` 的公开性
- Expected outcome: 确认 `LogicalOr` 枚举值存在且可访问
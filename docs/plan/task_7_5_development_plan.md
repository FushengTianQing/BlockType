# Task 7.5 Stage 7.5 统一开发计划

> **创建日期**: 2026-04-23  
> **作者**: planner  
> **状态**: 待审阅  
> **包含任务**: 7.5.1 + 7.5.2 (8 项子特性)

---

## 0. 总览

### 8 项任务现状总表

| # | 任务 | 提案 | 模块 | 现状 | 复杂度 | 工作量 |
|---|------|------|------|------|--------|--------|
| 1 | 转义序列扩展 | P2290R3 + P2071R2 | Lexer | **缺失** | 中 | 4-6h |
| 2 | for init-statement using | P2360R0 | Parser | **缺失** (但入口已有) | 低 | 1-2h |
| 3 | @/$/反引号字符集 | P2558R2 | Lexer | **部分** (@ 已有) | 低 | 1-2h |
| 4 | [[indeterminate]] 属性 | P2795R5 | AST+Parse+CodeGen | **缺失** | 中 | 2-3h |
| 5 | 平凡可重定位 | P2786R13 | Type+Sema | **缺失** | 中 | 2-3h |
| 6 | 概念模板模板参数 | P2841R7 | Parse+Sema | **缺失** | 高 | 4-6h |
| 7 | 可变参数友元 | P2893R3 | Parse+AST | **缺失** | 低 | 1-2h |
| 8 | constexpr 异常 | P3068R6 | Sema+ConstantExpr | **部分** (需要放宽) | 中 | 2-3h |

**总工作量**: 17-27h

---

## 1. Task 7.5.1 转义序列扩展

### 1.1 现状

**完全缺失**。当前 `processEscapeSequence()` (`src/Lex/Lexer.cpp:347-422`) 仅支持：
- `\e` / `\E` — ESC 字符 (B2.5)
- `\xHH` — 传统十六进制转义
- `\OOO` — 八进制转义
- `\uXXXX` / `\UXXXXXXXX` — Unicode 转义
- 简单转义 (`\n`, `\t`, `\r`, `\a`, `\b`, `\f`, `\v`, `\\`, `\'`, `\"`, `\?`)

**缺失**:
- `\x{HHHH...}` 分隔转义 (P2290R3)
- `\N{name}` 命名转义 (P2071R2)
- Unicode 字符名数据库

### 1.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `include/blocktype/Lex/Lexer.h` | 新增 `processDelimitedHexEscape()`、`processNamedEscape()`、`lookupUnicodeName()` 声明 |
| `src/Lex/Lexer.cpp:360-373` | 在 `\x` 分支中添加 `{` 检测，调用 `processDelimitedHexEscape()` |
| `src/Lex/Lexer.cpp:418-421` | 在简单转义之前添加 `\N` 分支 |
| `src/Lex/Lexer.cpp` (新增) | 实现 `processDelimitedHexEscape()`、`processNamedEscape()`、`lookupUnicodeName()` |
| `include/blocktype/Basic/DiagnosticLexKinds.def` | 新增 6 个诊断 ID |
| `third_party/unicode_names.inc` | 新增 Unicode 字符名查找表 |
| `scripts/generate_unicode_names.py` | 新增生成脚本 |

### 1.3 开发步骤

1. **新增诊断 ID** (0.5h)
2. **实现 `processDelimitedHexEscape()`** (1h) — 解析 `\x{...}`，收集十六进制数字，范围检查
3. **实现 `processNamedEscape()` + `lookupUnicodeName()`** (1.5h) — 解析 `\N{...}`，查找字符名
4. **生成 Unicode 字符名数据** (1h) — 脚本 + `unicode_names.inc`
5. **集成到 `processEscapeSequence()`** (0.5h) — 在 `\x` 和 `\N` 分支添加入口
6. **测试** (0.5h) — 5+ 测试用例

### 1.4 风险

| 风险 | 缓解 |
|------|------|
| Unicode 数据表过大 | 方案 B (`.inc` 头文件) 编译慢但可控；可后期迁移到完美哈希 |
| 命名转义查找性能 | 使用排序数组 + 二分查找，O(log n) |

---

## 2. E7.5.2.1 for init-statement 中 using (P2360R0)

### 2.1 现状

**入口已存在但未实现**。`parseForStatement()` (`ParseStmt.cpp:768-875`) 的 init 解析逻辑在 `isDeclarationStatement()` 中已包含 `kw_using` (第 42 行)，会将其识别为声明语句。但 `parseDeclarationStatement()` → `parseUsingDeclaration()` 的 using 声明解析路径需要确认是否正确处理 for-init 作用域。

**关键代码路径**:
```
parseForStatement() 
  → isDeclarationStatement() [返回 true，因为 kw_using]
    → parseDeclarationStatement()
      → parseUsingDeclaration()
```

### 2.2 需要验证/修改

| 文件 | 修改内容 |
|------|----------|
| `src/Parse/ParseStmt.cpp:826` | 确认 `parseDeclarationStatement()` 正确处理 using |
| `src/Parse/ParseDecl.cpp` 或等效文件 | 确认 `parseUsingDeclaration()` 存在并工作 |
| `src/Sema/Sema.cpp` | 确认 `ActOnUsingDecl()` 正确处理作用域 |

### 2.3 开发步骤

1. **验证现有路径** (0.5h) — 编写测试确认 `for (using T = int; ...)` 是否已经工作
2. **修复作用域问题**（如有）(0.5h) — 确保 using 声明作用域限于 for 循环
3. **补充测试** (0.5h) — 2+ 测试用例

**评估**: 可能已经工作（`isDeclarationStatement` 已包含 `kw_using`），主要工作是验证和测试。

---

## 3. E7.5.2.2 @/$/反引号字符集扩展 (P2558R2)

### 3.1 现状

**部分完成**：
- `@` 已有 `TokenKind::at` (`TokenKinds.def:141`)
- `$` 和反引号**无 token 定义**
- `isIdentifierStartChar()` 仅允许 `[a-zA-Z_]` (`Lexer.cpp:1363-1364`)
- `isIdentifierContinueChar()` 仅允许 `[a-zA-Z0-9_]` (`Lexer.cpp:1367-1368`)

### 3.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `include/blocktype/Lex/TokenKinds.def` | 新增 `dollar` 和 `backtick` token |
| `src/Lex/Lexer.cpp:1363-1368` | 扩展 `isIdentifierStartChar()` 和 `isIdentifierContinueChar()` |
| `include/blocktype/Lex/Lexer.h` | 新增 `isExtendedIdentifierChar()` 声明 |
| `src/Lex/Lexer.cpp` (lex 标识符部分) | 修改标识符词法分析以支持 $ 和反引号作为标识符字符 |

### 3.3 开发步骤

1. **扩展标识符字符函数** (0.5h)
2. **新增 token** (0.5h)
3. **修改词法分析** — 让 `$` 和反引号可以进入标识符 (0.5h)
4. **测试** (0.5h) — 3+ 测试用例

### 3.4 注意事项

- `$` 和反引号只在 `isIdentifierContinueChar()` 中添加（不能作为起始字符？P2558R2 需确认）
- 需要确保不与现有的运算符 token 冲突
- `@` 已有 token 但不在标识符字符集中，需要同时修改

---

## 4. E7.5.2.3 [[indeterminate]] 属性 (P2795R5)

### 4.1 现状

**完全缺失**。
- `Attr.h` 仅有 `ContractAttr`，无通用属性体系
- `AttributeDecl` 存在 (`Decl.h:1689`)，支持属性名 + 命名空间 + 参数
- `parseAttributeSpecifier()` (`ParseDecl.cpp:1611+`) 已能解析 `[[attr]]` 语法
- `VarDecl` 有 `getAttrs()`/`setAttrs()` (`Decl.h:166-167`)
- CodeGen 的 `EmitVarDecl()` 需要检查（但整体变量声明代码生成已存在）

### 4.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/CodeGen/CodeGenDecl.cpp` (或 `CodeGenFunction.cpp`) | 在 `EmitVarDecl()` 中检查 `Indeterminate` 属性，跳过零初始化 |
| `src/Sema/SemaDecl.cpp` (或等效) | 添加 Sema 警告：在 const 变量上使用 `[[indeterminate]]` |
| 无新增 AST 节点 | 使用现有 `AttributeDecl` + 名称检查即可 |

### 4.3 开发步骤

1. **CodeGen 层** — 在变量声明的初始化逻辑中检查属性名 "indeterminate" (1h)
2. **Sema 层** — 添加对 const 变量使用 indeterminate 的警告 (0.5h)
3. **测试** (0.5h) — 2+ 测试用例

### 4.4 注意

不需要新增 AST Attr 类。可以利用现有 `AttributeDecl` 的 `getAttributeName()` 在 CodeGen 和 Sema 中通过名称检查。这简化了实现。

---

## 5. E7.5.2.4 平凡可重定位 (P2786R13)

### 5.1 现状

**完全缺失**。
- `Type.h` 无 `isTriviallyRelocatable()` 方法
- 无 `hasUserDeclaredMoveConstructor()` / `hasUserDeclaredDestructor()` 等辅助方法
- `CXXRecordDecl` 缺少这些查询方法

### 5.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `include/blocktype/AST/Type.h` | 添加 `isTriviallyRelocatable()` |
| `include/blocktype/AST/Decl.h` | CXXRecordDecl 添加 `hasUserDeclaredMoveConstructor()` 和 `hasUserDeclaredDestructor()` |
| `src/Sema/SemaType.cpp` (或等效) | 实现 type trait 检查逻辑 |
| `src/Sema/Sema.cpp` | 添加 `isTriviallyRelocatable(CXXRecordDecl*)` 方法 |

### 5.3 开发步骤

1. **CXXRecordDecl 添加查询方法** (1h)
2. **Type::isTriviallyRelocatable() 实现** (0.5h)
3. **Sema trait 注册** (0.5h) — 使 `__is_trivially_relocatable(T)` 可用
4. **测试** (0.5h) — 3+ 测试用例

### 5.4 风险

- 需要准确的"用户定义"判断 — 依赖 CXXRecordDecl 的成员遍历能力
- 基类和成员的递归检查需要完整的类布局信息

---

## 6. E7.5.2.5 概念模板模板参数 (P2841R7)

### 6.1 现状

**完全缺失**。现有模板参数解析 (`ParseTemplate.cpp`) 支持：
- 类型模板参数 (`typename T`)
- 非类型模板参数 (`int N`)
- 模板模板参数 (`template <typename> class C`)
- 受约束的类型参数 (`ConceptName T`)
- 受约束的带参参数 (`ConceptName<Args> T`)

**不支持**：
- `template <typename> concept C` — 模板模板参数上的概念约束

### 6.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/Parse/ParseTemplate.cpp` | 修改 `parseTemplateParameter()` 支持 `concept` 关键字在模板模板参数中 |
| `src/Sema/SemaTemplate.cpp` | 添加约束检查逻辑 |
| `include/blocktype/AST/Decl.h` | `TemplateTemplateParmDecl` 添加概念约束字段 |

### 6.3 开发步骤

1. **AST 修改** — TemplateTemplateParmDecl 添加 ConceptDecl 引用 (1h)
2. **Parser 修改** — 解析 `template <typename> concept C` 语法 (1.5h)
3. **Sema 约束检查** — 实例化时验证概念约束 (1.5h)
4. **测试** (1h) — 2+ 测试用例

### 6.4 风险

- **复杂度高** — 涉及 Parser + Sema + AST 三层
- 依赖 Concept 系统的完整程度
- 需要仔细处理模板模板参数的实例化语义

---

## 7. E7.5.2.6 可变参数友元 (P2893R3)

### 7.1 现状

**完全缺失**。
- `FriendDecl` (`Decl.h:1581-1602`) 无 `IsPackExpansion` 字段
- `parseFriendDeclaration()` (`ParseClass.cpp:1141-1235`) 不处理 `friend Ts...;` 中的 `...`
- 当前友元声明仅支持 `friend class X;` 和 `friend void f(int);`

### 7.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `include/blocktype/AST/Decl.h:1581-1602` | `FriendDecl` 添加 `IsPackExpansion` 字段和访问器 |
| `src/Parse/ParseClass.cpp:1141-1172` | 在解析 `friend type-name` 后检测 `...` |
| `src/Sema/TemplateInstantiator.cpp` | 在类模板实例化时展开 pack friend 声明 |

### 7.3 开发步骤

1. **AST 修改** — 添加 `IsPackExpansion` 字段 (0.5h)
2. **Parser 修改** — 检测 `...` 并设置标记 (0.5h)
3. **实例化支持** — 展开参数包友元 (0.5h)
4. **测试** (0.5h) — 2+ 测试用例

### 7.4 风险

低风险。AST + Parser 修改简单。实例化部分可能需要在类模板实例化循环中添加分支。

---

## 8. E7.5.2.7 constexpr 异常 (P3068R6)

### 8.1 现状

**部分支持**。
- `ActOnCXXThrowExpr()` (`SemaExpr.cpp:282-287`) — 创建 `CXXThrowExpr`，但**不检查 constexpr 上下文**
- `ExceptionAnalysis::CheckThrowExpr()` (`ExceptionAnalysis.cpp:46-125`) — 完整的 throw 语义检查
- `ConstantExprEvaluator` (`ConstantExpr.cpp`) — **不处理 CXXThrowExpr**
- `ConstraintSatisfaction::canThrow()` — 已能识别 `CXXThrowExpr`

**缺失**：
1. Sema 不阻止 constexpr 函数中有 throw（C++26 应该允许）
2. ConstantExprEvaluator 遇到 throw 时不会报错

### 8.2 需要修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/Sema/SemaExpr.cpp:282-287` | 移除（如果有）constexpr 函数中 throw 的限制 |
| `src/Sema/ConstantExpr.cpp:182+` | `EvaluateExpr()` 添加 `CXXThrowExpr` 分支 |
| `src/Sema/SemaCXX.cpp` (或等效) | 如果有检查 constexpr 函数体的 throw 限制代码，修改为不报错 |

### 8.3 开发步骤

1. **搜索现有的 constexpr + throw 限制** (0.5h)
2. **移除 Sema 中的 throw-in-constexpr 禁止** (0.5h)
3. **ConstantExprEvaluator 添加 throw 处理** (1h)
4. **测试** (0.5h) — 3+ 测试用例

### 8.4 注意

- C++26 允许 constexpr 函数中有 throw，但 throw 在编译期求值时如果执行到则失败
- 需要在 `EvaluateExpr` 的分发中添加 `CXXThrowExpr` 处理

---

## 9. 依赖关系和并行策略

### 9.1 依赖图

```
独立任务（无依赖，可完全并行）:
┌─────────────────────────────────────────────────────┐
│  Task 7.5.1 (转义序列)     — Lexer 层               │
│  E7.5.2.1 (for using)      — Parser 层              │
│  E7.5.2.2 (@/$/反引号)     — Lexer 层               │
│  E7.5.2.3 ([[indeterminate]]) — AST+Parse+CodeGen    │
│  E7.5.2.4 (平凡可重定位)   — Type+Sema               │
│  E7.5.2.6 (可变参数友元)   — Parse+AST               │
│  E7.5.2.7 (constexpr异常)  — Sema+ConstantExpr       │
└─────────────────────────────────────────────────────┘

有依赖:
  E7.5.2.5 (概念模板模板参数) — 依赖 Concept 系统完整性
```

### 9.2 推荐实施顺序

#### 波次 1 (低悬果实，1-2h 每个，总计约 6-8h)

| 顺序 | 任务 | 原因 |
|------|------|------|
| 1 | E7.5.2.1 for using | 最简单，可能已经工作，只需验证 |
| 2 | E7.5.2.6 可变参数友元 | 低复杂度，AST+Parser 小改 |
| 3 | E7.5.2.2 @/$/反引号 | 低复杂度，Lexer 小改 |
| 4 | E7.5.2.7 constexpr异常 | 修改量小，主要在 ConstantExpr |

#### 波次 2 (中等复杂度，2-3h 每个，总计约 6-9h)

| 顺序 | 任务 | 原因 |
|------|------|------|
| 5 | E7.5.2.3 [[indeterminate]] | 需要跨层（Parse → CodeGen）但各层改动小 |
| 6 | E7.5.2.4 平凡可重定位 | 需要递归类型检查，但有明确的判断逻辑 |

#### 波次 3 (高复杂度，单独执行)

| 顺序 | 任务 | 原因 |
|------|------|------|
| 7 | Task 7.5.1 转义序列 | 最大工作量（Unicode 数据库），独立 Lexer 任务 |
| 8 | E7.5.2.5 概念模板模板参数 | 最复杂，涉及 Parser+Sema+Concept 系统 |

### 9.3 可并行开发的任务组

以下任务完全不重叠，可以并行开发：

| 组 | 可并行的任务 | 不重叠原因 |
|------|------------|----------|
| Lexer 组 | 7.5.1 + E7.5.2.2 | 只修改 Lexer 相关文件 |
| Parser/AST 组 | E7.5.2.1 + E7.5.2.6 + E7.5.2.3 | 分别修改不同文件 |
| Sema 组 | E7.5.2.4 + E7.5.2.7 | 分别修改 Type/Sema 和 ConstantExpr |

---

## 10. 总工作量汇总

| 波次 | 任务 | 工作量 | 累计 |
|------|------|--------|------|
| 波次 1 | E7.5.2.1 + E7.5.2.6 + E7.5.2.2 + E7.5.2.7 | 5-9h | 5-9h |
| 波次 2 | E7.5.2.3 + E7.5.2.4 | 4-6h | 9-15h |
| 波次 3 | 7.5.1 + E7.5.2.5 | 8-12h | 17-27h |

**MVP 建议**: 先完成波次 1 的 4 个简单任务（5-9h），快速提升 C++26 特性覆盖率。

---

## 11. 验收标准总结

| 任务 | 最低测试要求 | 关键验证 |
|------|------------|----------|
| 7.5.1 转义序列 | 5+ 测试 | `\x{1F600}` 和 `\N{LATIN CAPITAL LETTER A}` 正确解析 |
| E7.5.2.1 for using | 2+ 测试 | `for (using T = int; ...)` 合法且 T 作用域正确 |
| E7.5.2.2 字符集 | 3+ 测试 | `$var`, `@var` 作为标识符 |
| E7.5.2.3 indeterminate | 2+ 测试 | `[[indeterminate]] int x;` 跳过零初始化 |
| E7.5.2.4 平凡可重定位 | 3+ 测试 | `is_trivially_relocatable_v<int>` → true |
| E7.5.2.5 概念模板参数 | 2+ 测试 | `template <template <typename> concept C>` 解析正确 |
| E7.5.2.6 可变参数友元 | 2+ 测试 | `friend Ts...;` 解析正确 |
| E7.5.2.7 constexpr异常 | 3+ 测试 | constexpr 函数中 throw 允许，运行到 throw 时求值失败 |

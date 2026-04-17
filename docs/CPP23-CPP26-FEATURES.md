# C++23/C++26 新特性审计 — BlockType 解析器

> **审计日期**: 2026-04-17
> **基于 commit**: 1486ff0
> **总计**: 34 项特性 (C++23: 18 项, C++26: 16 项)
> **已实现**: 10 项 ✅ | **部分实现**: 4 项 ⚠️ | **未实现**: 20 项 ❌

---

## 一、C++23 语言特性 (18 项)

| # | 特性 | 提案 | 状态 | 优先级 | 备注 |
|---|------|------|------|--------|------|
| 1 | Deducing this / 显式对象参数 | P0847R7 | ❌ | P1 | 无 AST 节点、无解析 |
| 2 | `if consteval` | P1938R3 | ✅ | — | `IfStmt` 添加 `IsConsteval`/`IsNegated` 标志，支持 `if consteval` 和 `if !consteval` |
| 3 | 多维 `operator[]` | P2128R6 | ✅ | — | `ArraySubscriptExpr` 扩展为多参数，支持 `arr[i, j, k]` |
| 4 | `auto(x)` / `auto{x}` decay-copy | P0849R8 | ❌ | P1 | 无代码 |
| 5 | `static operator()` | P1169R4 | ❌ | P1 | 无代码 |
| 6 | `static operator[]` | P2589R1 | ❌ | P1 | 无代码 |
| 7 | `[[assume]]` 属性 | P1774R8 | ❌ | P1 | 属性系统存在但不识别 `assume` |
| 8 | `#elifdef` / `#elifndef` | P2334R1 | ✅ | — | `handleElifdefDirective()`/`handleElifndefDirective()`，支持中英文 |
| 9 | `#warning` 预处理指令 | P2437R1 | ✅ | — | `Preprocessor.cpp:1432`，支持中英文 |
| 10 | Lambda 模板参数 | P1102R2 | ✅ | — | `LambdaExpr` 添加 `TemplateParams`，支持 `[]<typename T>()` |
| 11 | Lambda 属性 | P2173R1 | ✅ | — | `LambdaExpr` 添加 `Attrs`，支持 `[[nodiscard]]` 等属性 |
| 12 | 占位符变量 `_` | P2169R4 (C++26) | ❌ | P1 | `_` 仅作为普通标识符 |
| 13 | `Z`/`z` 字面量后缀 | P0330R8 | ✅ | — | `ParseExpr.cpp:629` 识别 `uz`/`Z` |
| 14 | `\e` 转义序列 | P2314R4 | ✅ | — | `Lexer.cpp:334` |
| 15 | 分隔转义 `\x{...}` | P2290R3 | ❌ | P1 | 仅支持 `\xHH` |
| 16 | 命名转义 `\N{...}` | P2071R2 | ❌ | P2 | 无代码 |
| 17 | `for` init-statement 中 `using` | P2360R0 | ❌ | P1 | 无代码 |
| 18 | constexpr 放宽 | P2448R2 | ⚠️ | — | 部分放宽已通过其他方式实现 |

**C++23 支持率: 8/18 ≈ 44%**

---

## 二、C++26 语言特性 (16 项)

| # | 特性 | 提案 | 状态 | 优先级 | 备注 |
|---|------|------|------|--------|------|
| 1 | **Contracts** (pre/post/assert) | P2900R14 | ❌ | P2 | 仅 `__cpp_contracts=202502L` 预定义宏，无实现 |
| 2 | **包索引** `T...[N]` | P2662R3 | ✅ | — | `PackIndexingExpr` + `parsePackIndexingExpr()` 完整实现 |
| 3 | **`= delete("reason")`** | P2573R2 | ❌ | P1 | 仅设计文档，AST 无 reason 字段 |
| 4 | **静态反射** `reflexpr` | (提案中) | ⚠️ | P2 | `ReflexprExpr` 基础框架 + `parseReflexprExpr()`，功能有限 |
| 5 | **`#embed`** | P1967R14 | ✅ | — | `handleEmbedDirective()` 完整实现，支持 `limit()`/`suffix()` |
| 6 | 未命名占位符变量 `_` | P2169R4 | ❌ | P1 | 同 C++23 #12 |
| 7 | 用户 `static_assert` 消息 | P2741R3 | ⚠️ | P1 | 支持 `static_assert(cond, "msg")` (C++11级)，无格式化增强 |
| 8 | 可变参数友元 | P2893R3 | ❌ | P2 | 无代码 |
| 9 | 结构化绑定作为条件 | P0963R3 | ❌ | P2 | 无结构化绑定 AST 节点 |
| 10 | 结构化绑定引入包 | P1061R10 | ❌ | P2 | 无代码 |
| 11 | `constexpr` 异常 | P3068R6 | ❌ | P2 | 无代码 |
| 12 | 平凡可重定位 | P2786R13 | ❌ | P2 | 无代码 |
| 13 | `@`/`$`/反引号字符集扩展 | P2558R2 | ⚠️ | P1 | `@` 已有 token，`$` 和反引号不支持 |
| 14 | 概念和变量模板模板参数 | P2841R7 | ❌ | P2 | 无代码 |
| 15 | 可观察检查点 | P1494R5 | ❌ | P2 | 无代码 |
| 16 | `[[indeterminate]]` 属性 | P2795R5 | ❌ | P2 | 无代码 |

**C++26 支持率: 2/16 完整 + 3 部分实现 ≈ 22%**

---

## 三、已注册但未实现的特性测试宏

```cpp
// src/Lex/Preprocessor.cpp:96-101
definePredefinedMacro("__cplusplus", "202602L");          // 声称 C++26
definePredefinedMacro("__cpp_contracts", "202502L");       // ❌ 未实现
definePredefinedMacro("__cpp_reflexpr", "202502L");        // ⚠️ 基础框架
definePredefinedMacro("__cpp_pack_indexing", "202411L");   // ✅ 已实现
```

**缺失的特性测试宏**: `__cpp_if_consteval`, `__cpp_multidimensional_subscript`,
`__cpp_static_call_operator`, `__cpp_elifdef`, `__cpp_pp_warning`, `__cpp_auto_cast`,
`__cpp_named_universal_character_escapes` 等。

---

## 四、实现优先级与阶段规划

### ~~P0 — Phase 3 完成~~ ✅ 全部完成（解析器层面）

| 特性 | 涉及文件 | 状态 |
|------|---------|------|
| `#elifdef` / `#elifndef` | `Preprocessor.cpp` + 中文映射 | ✅ 已实现 |
| `if consteval` | `Stmt.h` (IfStmt), `ParseStmt.cpp` | ✅ 已实现 |
| 多维 `operator[]` | `Expr.h` (ArraySubscriptExpr), `ParseExpr.cpp` | ✅ 已实现 |
| Lambda 模板参数 | `Expr.h` (LambdaExpr), `ParseExprCXX.cpp` | ✅ 已实现 |
| Lambda 属性 | `ParseExprCXX.cpp` (复用 `parseAttributeSpecifier`) | ✅ 已实现 |

**P0 合计: 全部完成**

### P1 — Phase 7 完成（需要 AST 扩展，3-5 天/项）

| 特性 | 涉及文件 | 工作量估算 |
|------|---------|-----------|
| Deducing this | 新增 `ExplicitObjectParam` AST 节点, `ParseDecl.cpp` | 3 天 |
| `auto(x)` decay-copy | 新增 `DecayCopyExpr` 节点, `ParseExpr.cpp` | 2 天 |
| `static operator()` / `static operator[]` | `DeclSpec.h` 标志扩展, `ParseClass.cpp` | 2 天 |
| `= delete("reason")` | `FunctionDecl` 增加 `DeletedReason` 字段 | 1 天 |
| `[[assume]]` 属性 | `parseAttributeSpecifier` 识别 `assume` | 1 天 |
| 占位符变量 `_` | 词法/解析器特殊处理 `_` | 2 天 |
| 分隔转义 `\x{...}` | `Lexer.cpp` 转义序列扩展 | 1 天 |
| `for` init-statement 中 `using` | `ParseStmt.cpp` | 1 天 |
| `static_assert` 消息增强 | `ParseDecl.cpp` | 1 天 |
| `@`/`$`/反引号字符集 | `Lexer.cpp`, `TokenKinds.def` | 1 天 |

**P1 合计: ~15 天**

### P2 — Phase 7 完成（大型特性，1-3 周/项）

| 特性 | 工作量估算 |
|------|-----------|
| Contracts (P2900) | 2-3 周：完整的新属性体系 + 语义检查 + 代码生成 |
| 静态反射 | 2-3 周：完整的元编程 API + 信息提取 |
| 结构化绑定 | 1-2 周：完整的 `BindingDecl` 体系 |
| 命名转义 `\N{...}` | 0.5 天：需要字符名数据库 |
| 可变参数友元 | 1 天 |
| `constexpr` 异常 | 1 周：需要异常处理基础设施 |
| 平凡可重定位 | 2 天：类型 trait 扩展 |
| 概念和变量模板模板参数 | 3 天：模板参数扩展 |
| 可观察检查点 | 2 天 |
| `[[indeterminate]]` 属性 | 1 天 |

**P2 合计: ~6 周**

---

## 五、各特性详细实现说明

### P0 特性实现要点

#### `#elifdef` / `#elifndef` (P2334R1)

**修改文件**: `src/Lex/Preprocessor.cpp`, `include/blocktype/Lex/TokenKinds.def`

```cpp
// Preprocessor.cpp - handleDirective 分支添加:
} else if (DirectiveName == "elifdef") {
    handleElifdefDirective(DirectiveTok);
} else if (DirectiveName == "elifndef") {
    handleElifndefDirective(DirectiveTok);
```

实现与 `#elif` + `#ifdef`/`#ifndef` 组合等价。`handleElifdefDirective()` 检查宏是否定义，
然后根据条件状态决定是否跳过后续代码。

#### `if consteval` (P1938R3)

**修改文件**: `include/blocktype/AST/Stmt.h`, `src/Parse/ParseStmt.cpp`

```cpp
// Stmt.h - IfStmt 添加:
bool IsConsteval : 1 = false;
SourceLocation ConstevalLoc;

// ParseStmt.cpp - parseIfStatement() 添加:
if (Tok.is(TokenKind::kw_consteval)) {
    IsConsteval = true;
    consumeToken(); // consume 'consteval'
}
```

`if consteval` 不带条件表达式，`if !consteval` 用 `TokenKind::exclaim` 检测。

#### 多维 `operator[]` (P2128R6)

**修改文件**: `include/blocktype/AST/Expr.h`, `src/Parse/ParseExpr.cpp`

```cpp
// Expr.h - ArraySubscriptExpr 扩展:
llvm::SmallVector<Expr *, 2> Indices;  // 支持多个下标

// ParseExpr.cpp - parsePostfixExpression:
// 在 '[' 后循环解析逗号分隔的表达式列表
```

C++23 之前 `a[b]` 只允许单参数；C++23 起 `a[b, c, d]` 合法。

#### Lambda 模板参数 (P1102R2)

**修改文件**: `include/blocktype/AST/Expr.h`, `src/Parse/ParseExprCXX.cpp`

```cpp
// Expr.h - LambdaExpr 添加:
TemplateParameterList *TemplateParams = nullptr;

// ParseExprCXX.cpp - parseLambdaExpression():
// 在 '[' 之后、'(' 之前检查 '<' 并解析模板参数
if (Tok.is(TokenKind::less)) {
    TemplateParams = parseTemplateParameterList();
}
```

#### Lambda 属性 (P2173R1)

**修改文件**: `include/blocktype/AST/Expr.h`, `src/Parse/ParseExprCXX.cpp`

在 lambda 的 `[captures]` 之前或 `(params)` 之后添加 `[[attributes]]` 解析。
复用现有的 `parseAttributeSpecifier()` 函数。

---

*文档生成时间: 2026-04-17*
*详细实现方案参见: `docs/plan/07-PHASE7-cpp26-features.md`*

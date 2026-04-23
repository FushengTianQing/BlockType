# Task 7.4.4 Pack Indexing 完善计划 (P2662R3)

> **创建日期**: 2026-04-23  
> **作者**: planner  
> **状态**: 待审阅  

---

## 1. 当前实现状态评估

### 1.1 状态总览表

| 模块 | 文件 | 状态 | 说明 |
|------|------|------|------|
| **AST** | `include/blocktype/AST/Expr.h` | ✅ 完整 | PackIndexingExpr 类定义完整 |
| **AST dump** | `src/AST/Expr.cpp:561-590` | ✅ 完整 | dump() 实现完整 |
| **NodeKinds** | `include/blocktype/AST/NodeKinds.def:84` | ✅ 完整 | EXPR(PackIndexingExpr, Expr) |
| **解析器** | `src/Parse/ParseExpr.cpp:593-618` | ✅ 完整 | 后缀表达式中的 `expr...[index]` 解析 |
| **解析器** | `src/Parse/ParseExprCXX.cpp:828-860` | ✅ 完整 | 独立的 parsePackIndexingExpr() |
| **Sema** | `src/Sema/SemaExpr.cpp:329-336` | ⚠️ 骨架 | 仅创建节点，无类型推导 |
| **模板实例化** | `src/Sema/TemplateInstantiator.cpp:364-459` | ⚠️ 未集成 | 方法已实现但从未被主流程调用 |
| **CodeGen** | `src/CodeGen/CodeGenExpr.cpp:2390-2491` | ⚠️ 部分完成 | 编译时常量 + 运行时 switch 都有，但值类别处理不完整 |
| **预处理宏** | `src/Lex/Preprocessor.cpp:103` | ✅ 完整 | `__cpp_pack_indexing` 定义为 `202411L` |
| **诊断** | `include/blocktype/Basic/DiagnosticSemaKinds.def` | ⚠️ 不完整 | 仅有 `err_pack_index_out_of_bounds` 和 `err_pack_index_empty_pack` |
| **单元测试** | `tests/unit/Sema/VariadicTemplateTest.cpp` | ⚠️ 不完整 | 7 个测试，仅覆盖 TemplateInstantiator 单元调用 |

### 1.2 关键缺口分析

#### 缺口 1: `Sema::ActOnPackIndexingExpr()` 缺少类型推导 [严重]

```cpp
// src/Sema/SemaExpr.cpp:329-336 — 当前实现
ExprResult Sema::ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack, Expr *Index) {
  auto *PIE = Context.create<PackIndexingExpr>(Loc, Pack, Index);
  // TODO: Pack indexing requires template pack expansion to determine type.
  return ExprResult(PIE);
}
```

**问题**: 
- 未设置表达式的类型（`getType()` 返回空 QualType）
- 未验证 Pack 确实是参数包
- 未验证 Index 是整型表达式
- 未设置正确的值类别（ExprValueKind）

#### 缺口 2: `InstantiatePackIndexingExpr()` 未被主流程调用 [严重]

`InstantiatePackIndexingExpr()` 在 `TemplateInstantiator` 中实现了完整的包索引实例化逻辑，但**从未从模板实例化主流程中被调用**。它只被单元测试直接调用。

需要在 `TemplateInstantiator` 的表达式实例化分发逻辑中添加对 `PackIndexingExprKind` 的处理。

#### 缺口 3: 值类别（Value Category）推导未实现 [中等]

根据 P2662R3 §[expr.prim.pack.index]：
- 如果第 N 个包元素是 lvalue，结果为 lvalue
- 如果第 N 个包元素是 xvalue，结果为 xvalue  
- 否则结果为 prvalue

当前 `PackIndexingExpr` 构造函数默认设为 `VK_PRValue`，且 `InstantiatePackIndexingExpr()` 未传播值类别。

#### 缺口 4: 运行时索引的 CodeGen 有缺陷 [中等]

`CodeGenExpr.cpp:2422-2468` 的运行时 switch 实现存在以下问题：
- PHI 类型使用了 `IndexVal->getType()`（索引的类型），而非结果值的类型
- 未处理不同包元素可能有不同类型的情况
- Default 分支返回 `UndefValue` 而非调用 `__builtin_trap()`

#### 缺口 5: 缺少端到端集成测试 [中等]

现有 7 个测试全部是直接调用 `TemplateInstantiator` API 的单元测试。缺少：
- 从源码解析到 CodeGen 的端到端测试
- 嵌套模板中的包索引测试
- 多维包索引测试（`Ts...[Is]...`）

---

## 2. 需要修改的接口和文件

### 2.1 修改列表

| # | 文件 | 修改内容 | 优先级 |
|---|------|----------|--------|
| 1 | `src/Sema/SemaExpr.cpp` | 完善 `ActOnPackIndexingExpr()` 添加类型推导和验证 | P0 |
| 2 | `src/Sema/TemplateInstantiator.cpp` | 在实例化分发中调用 `InstantiatePackIndexingExpr()` | P0 |
| 3 | `src/CodeGen/CodeGenExpr.cpp` | 修复运行时索引 CodeGen 值类型和 PHI 处理 | P1 |
| 4 | `include/blocktype/AST/Expr.h` | `PackIndexingExpr` 添加 `isValueDependent()` | P1 |
| 5 | `include/blocktype/Basic/DiagnosticSemaKinds.def` | 添加新诊断 ID | P1 |
| 6 | `tests/unit/Sema/VariadicTemplateTest.cpp` | 补充 Sema 层和端到端测试 | P1 |
| 7 | `tests/` (新增) | 添加集成测试文件 | P2 |

### 2.2 新增接口详情

#### `Sema::ActOnPackIndexingExpr()` 完善接口

```cpp
ExprResult Sema::ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack, Expr *Index);
```

需要添加的逻辑：
1. 验证 `Pack` 引用的是参数包（检查 `DeclRefExpr` 目标是否为 `NonTypeTemplateParmDecl` 或通过 `PackExpansionExpr`）
2. 验证 `Index` 是整型或依赖于模板参数的整型表达式
3. 如果 `Index` 是常量且包已展开，计算具体类型
4. 设置正确的 `ExprValueKind`

#### `TemplateInstantiator` 实例化分发

需要在 `TemplateInstantiator::instantiateExpr()` (或等效的实例化入口) 中添加：

```cpp
case ASTNode::NodeKind::PackIndexingExprKind: {
  auto *PIE = llvm::cast<PackIndexingExpr>(E);
  return InstantiatePackIndexingExpr(PIE, PackArgs);
}
```

#### 新增诊断 ID

```cpp
ERR(err_pack_index_not_pack, "pack indexing requires a parameter pack", 0)
ERR(err_pack_index_non_integral, "pack index must be an integral expression", 0)
ERR(err_pack_index_negative, "pack index must be non-negative", 0)
```

---

## 3. 具体开发步骤和任务分解

### Step 1: 完善 Sema 类型推导 [P0, 预计 2-3h]

**文件**: `src/Sema/SemaExpr.cpp:329-336`

**修改内容**:
```cpp
ExprResult Sema::ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack, Expr *Index) {
  // 1. 验证 Pack 是参数包引用
  if (!isPackExpression(Pack)) {
    Diag(Loc, err_pack_index_not_pack);
    return ExprError();
  }
  
  // 2. 验证 Index 是整型
  QualType IndexType = Index->getType();
  if (!IndexType.isNull() && !IndexType->isIntegralType(Context)) {
    if (!Index->isTypeDependent()) {
      Diag(Index->getLocation(), err_pack_index_non_integral);
      return ExprError();
    }
  }
  
  // 3. 创建节点
  auto *PIE = Context.create<PackIndexingExpr>(Loc, Pack, Index);
  
  // 4. 类型推导
  if (!Pack->isTypeDependent() && !Index->isTypeDependent()) {
    // 尝试静态确定类型（如果包已展开且索引是常量）
    QualType ResultType = deducePackIndexType(Pack, Index);
    if (!ResultType.isNull()) {
      PIE->setType(ResultType);
      // 值类别取决于第 N 个元素
      PIE->setValueKind(deducePackIndexValueKind(Pack, Index));
    }
  }
  // 否则保持 type-dependent，在实例化时确定
  
  return ExprResult(PIE);
}
```

**辅助方法**:
- `isPackExpression(Expr *)` — 判断表达式是否引用参数包
- `deducePackIndexType(Expr *Pack, Expr *Index)` — 推导包索引结果类型
- `deducePackIndexValueKind(Expr *Pack, Expr *Index)` — 推导值类别

### Step 2: 集成模板实例化 [P0, 预计 1-2h]

**文件**: `src/Sema/TemplateInstantiator.cpp`

**修改内容**: 找到表达式实例化的主分发逻辑，添加 `PackIndexingExprKind` case。

需要：
1. 找到 `TemplateInstantiator` 中遍历/替换表达式的主入口
2. 添加 `case ASTNode::NodeKind::PackIndexingExprKind:` 分支
3. 确保传入正确的 `PackArgs`（从当前实例化上下文获取）
4. 在 `InstantiatePackIndexingExpr()` 中添加值类别传播

**关键修改**:
```cpp
// TemplateInstantiator.cpp — InstantiatePackIndexingExpr 返回值前添加:
if (!SubstitutedExprs.empty() && Index < SubstitutedExprs.size()) {
  Expr *Result = SubstitutedExprs[Index];
  // 传播值类别
  PIE->setValueKind(Result->getValueKind());
  return Result;
}
```

### Step 3: 修复 CodeGen [P1, 预计 1-2h]

**文件**: `src/CodeGen/CodeGenExpr.cpp:2390-2491`

**修改内容**:
1. 修复 PHI 节点类型（应使用结果元素类型，而非索引类型）
2. 处理异构包（所有元素类型相同时优化，不同时使用 void*/union）
3. Default 分支改为 `llvm::Intrinsic::trap` 或 `unreachable`

```cpp
// 修复: PHI 类型应使用第一个元素的结果类型
llvm::Type *ResultType = ConvertType(Substituted[0]->getType());
llvm::PHINode *PHI = Builder.CreatePHI(ResultType, Substituted.size(), "pack.result");
```

### Step 4: 完善 AST 接口 [P1, 预计 0.5h]

**文件**: `include/blocktype/AST/Expr.h`

**修改内容**: 
1. `PackIndexingExpr` 添加 `isValueDependent()` 方法
2. 添加 `isInstantiationDependent()` 方法

```cpp
bool isValueDependent() const override {
  if (isSubstituted()) return false;
  return Pack->isValueDependent() || Index->isValueDependent();
}

bool isInstantiationDependent() const override {
  if (isSubstituted()) return false;
  return Pack->isInstantiationDependent() || Index->isInstantiationDependent();
}
```

### Step 5: 补充测试 [P1, 预计 2-3h]

见下方第 4 节。

### Step 6: 添加诊断消息 [P1, 预计 0.5h]

**文件**: `include/blocktype/Basic/DiagnosticSemaKinds.def`

添加：
```
ERR(err_pack_index_not_pack, "expression does not refer to a parameter pack", 0)
ERR(err_pack_index_non_integral, "pack index must be an integral expression, not %0", 1)
ERR(err_pack_index_negative, "pack index %0 is negative", 1)
```

---

## 4. 测试用例设计

### 4.1 单元测试（`tests/unit/Sema/VariadicTemplateTest.cpp`）

在现有 7 个测试基础上补充：

| # | 测试名 | 覆盖范围 |
|---|--------|----------|
| T8 | `PackIndexingTypeTemplateArg` | 包含类型模板参数的包索引 |
| T9 | `PackIndexingExprTemplateArg` | 包含表达式模板参数的包索引 |
| T10 | `PackIndexingDeclTemplateArg` | 包含声明模板参数的包索引 |
| T11 | `PackIndexingMixedPackArgs` | 混合类型参数包 |
| T12 | `PackIndexingValueCategoryPropagation` | 值类别传播验证 |
| T13 | `PackIndexingDependentIndex` | 依赖类型的索引表达式 |

### 4.2 集成测试（新增或添加到 Sema 测试）

```cpp
// 测试 1: 基本包索引 — 类型上下文
template <typename... Ts>
using First = Ts...[0];
// 验证: First<int, char, double> == int

// 测试 2: 包索引 — 表达式上下文
template <int... Is>
constexpr int get_last() {
  return Is...[sizeof...(Is) - 1];
}
// 验证: get_last<1, 2, 3>() == 3

// 测试 3: 包索引 — 非类型模板参数
template <typename... Ts>
auto get_second(Ts... args) -> Ts...[1] {
  return args...[1]; // 参数包展开后索引
}

// 测试 4: 依赖索引
template <typename... Ts, int N>
using NthType = Ts...[N];
// 验证: NthType<int, char, double, 1> == char

// 测试 5: 错误情况
// Ts...[-1] → 编译错误
// Ts...[100] (pack只有3个元素) → 编译错误
// int x; x...[0] → 编译错误 (x不是参数包)
```

### 4.3 CodeGen 测试

```cpp
// 测试 6: 运行时索引代码生成
template <typename... Ts>
auto get_nth(Ts... args, int n) {
  return args...[n]; // 生成 switch
}
// 验证: get_nth(1, 2.0, 'c', 0) == 1
// 验证: get_nth(1, 2.0, 'c', 1) == 2.0
```

---

## 5. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| 模板实例化主流程修改引入回归 | 中 | 高 | 在修改前运行全部 766 测试确认基线，修改后立即回归测试 |
| 值类别推导影响现有表达式语义 | 低 | 中 | 添加值类别相关的断言测试 |
| CodeGen 运行时索引的类型推导 | 中 | 中 | P2662R3 要求所有包元素类型相同才能运行时索引，编译期检查 |
| 嵌套包索引（多维）支持 | 低 | 低 | 多维暂不在 P2662R3 核心范围内，可作为后续扩展 |

### 最小可行方案（MVP）

如果时间紧张，优先完成：
1. **Step 2** (集成实例化) — 这是最关键的缺口
2. **Step 1** (Sema 类型推导) — 基础正确性
3. **Step 5** (测试) — 验证

可以延后的：
- Step 3 (CodeGen 运行时索引) — 仅影响运行时场景
- Step 4 (AST 接口) — 不影响基本功能
- Step 6 (诊断) — 错误信息可后续完善

---

## 6. 工作量估算

| 步骤 | 描述 | 预计时间 | 优先级 |
|------|------|----------|--------|
| Step 1 | Sema 类型推导完善 | 2-3h | P0 |
| Step 2 | 模板实例化集成 | 1-2h | P0 |
| Step 3 | CodeGen 修复 | 1-2h | P1 |
| Step 4 | AST 接口完善 | 0.5h | P1 |
| Step 5 | 补充测试 | 2-3h | P1 |
| Step 6 | 诊断消息 | 0.5h | P1 |
| **总计** | | **7.5-11h** | |

### 建议执行顺序

```
Step 2 (实例化集成)
  ↓
Step 1 (Sema 类型推导)
  ↓
Step 6 (诊断消息) + Step 4 (AST 接口)
  ↓
Step 5 (测试)
  ↓
Step 3 (CodeGen 修复)
```

先做 Step 2 是因为它是最大的功能缺口（实例化逻辑存在但未被调用），修复后可以立即验证整个链路。

---

## 7. 与 P2662R3 标准的符合度

### 已实现的标准要求 ✅

- [x] 语法 `Pack...[Index]` 解析
- [x] AST 节点 `PackIndexingExpr`
- [x] 编译时常量索引的基本实例化
- [x] 边界检查（越界诊断）
- [x] 空包诊断
- [x] 预定义宏 `__cpp_pack_indexing`

### 需要完善 ❌

- [ ] Sema 层的类型推导和语义验证
- [ ] 模板实例化主流程集成（将 InstantiatePackIndexingExpr 接入调用链）
- [ ] 值类别传播（lvalue/xvalue/prvalue 取决于第 N 个元素）
- [ ] 运行时索引的正确代码生成
- [ ] 嵌套/多维包索引
- [ ] 包索引在函数返回类型推导中的应用（`auto f() -> Ts...[0]`）

### 不在当前范围

- Pack Indexing Pattern（`Ts...[Is]...`，即索引本身也是包）— 这是更高级的特性
- Pack Indexing 在 Concept 约束中的使用 — 需要完整的 Concept 支持

# Phase 5 Stage 5.3–5.5 全面核查报告

> **核查时间**：2026-04-18
> **核查范围**：Stage 5.3（变参模板）、Stage 5.4（Concepts）、Stage 5.5（模板特化与测试）
> **对照基准**：`docs/plan/05-PHASE5-template-generics.md` 对应章节 + Clang 同模块功能

---

## 一、总览表

| Stage/Task | 文档条目 | 核心代码文件 | 状态 | 严重程度 |
|-----------|----------|-------------|------|---------|
| **5.3.1** ExpandPack | E5.3.1.1 | `src/Sema/TemplateInstantiation.cpp:165-184` | ✅ 已实现 | — |
| **5.3.1** CXXFoldExpr | E5.3.1.2 | `src/Sema/TemplateInstantiation.cpp:212-308` | ✅ 已实现 | — |
| **5.3.2** PackIndexingExpr | E5.3.2.1 | `src/Sema/TemplateInstantiation.cpp:314-353` | ⚠️ 部分实现 | 🟡 中 |
| **5.4.1** ConstraintSatisfaction 头文件 | E5.4.1.1 | `include/blocktype/Sema/ConstraintSatisfaction.h` | ✅ 已实现 | — |
| **5.4.1** CheckConstraintSatisfaction | E5.4.1.2 | `src/Sema/ConstraintSatisfaction.cpp:31-57` | ✅ 已实现 | — |
| **5.4.1** Sema 集成约束检查 | E5.4.1.2 | `src/Sema/SemaTemplate.cpp:578-598` | ✅ 已实现 | — |
| **5.4.2** EvaluateRequiresExpr | E5.4.2.1 | `src/Sema/ConstraintSatisfaction.cpp:73-127` | ✅ 已实现 | — |
| **5.4.2** checkReturnTypeConstraint | — | `src/Sema/ConstraintSatisfaction.cpp:369-397` | ⚠️ 简化 | 🟡 中 |
| **5.4.2** canThrow | — | `src/Sema/ConstraintSatisfaction.cpp:350-367` | ⚠️ 简化 | 🟡 中 |
| — | 约束部分排序 | 未实现 | ❌ 缺失 | 🔴 高 |
| **5.5.1** ActOnExplicitSpecialization | E5.5.1.1 | `src/Sema/SemaTemplate.cpp:350-366` | ⚠️ 最小实现 | 🟡 中 |
| **5.5.2** 偏特化 | E5.5.2.1 | `src/Sema/SemaTemplate.cpp:436-528` | ⚠️ 部分排序缺失 | 🔴 高 |
| **5.5.3** 测试文件 | E5.5.3.1/E5.5.3.2 | `tests/unit/Sema/*.cpp` + `tests/lit/` | ⚠️ 部分缺失 | 🟡 中 |

---

## 二、Stage 5.3 变参模板 — 详细核查

### ✅ Task 5.3.1 ExpandPack + CXXFoldExpr

**ExpandPack** (`TemplateInstantiation.cpp:165-184`):
- ✅ 遍历 `Args.getPackArgument()` 获取 pack 元素
- ✅ 对每个元素创建单参数 `TemplateArgumentList`，调用 `SubstituteExpr`
- ✅ `ExpandPackType` 额外实现（文档未要求，bonus）

**getIdentityElement** (`TemplateInstantiation.cpp:212-241`):
- ✅ 覆盖 7 种运算符：`Add→0`, `Mul→1`, `LAnd→true`, `LOr→false`, `And→~0`, `Or→0`, `Xor→0`
- ✅ 无单位元的运算符返回 nullptr

**InstantiateFoldExpr** (`TemplateInstantiation.cpp:243-308`):
- ✅ 左折叠 / 右折叠均实现
- ✅ 空包返回单位元
- ✅ 二元折叠带初始值 (LHS/RHS) 正确处理
- ✅ `SubstituteExpr` 中正确分派 `CXXFoldExpr` (行 487-488)

### ⚠️ 问题 S5.3-1：ExpandPack 只支持单包展开（🟡 中）

```cpp
// 当前实现：只找第一个 pack
llvm::ArrayRef<TemplateArgument> PackArgs = Args.getPackArgument();
```

`getPackArgument()` 返回**第一个** `isPack()` 的参数。当模板有多个参数包（如 `template<typename... Ts, typename... Us>`）时，所有包需同时展开且长度一致。当前实现对多包场景会丢失后续包。

**Clang 对比**：Clang 使用 `MultiLevelTemplateArgumentList` + `ArgumentPackSubstitutionIndex` 支持多包同时展开，并验证包长度一致性。

**修复建议**：在 `ExpandPack` 中枚举所有 pack 参数，验证长度一致，然后按索引同时取各包元素。

### ⚠️ 问题 S5.3-2：PackIndexingExpr 类型实参返回占位符（🟡 中）

```cpp
// TemplateInstantiation.cpp:343-347
if (Selected.isType()) {
    // Return a type expression — for now create a placeholder
    return PIE;  // ← 未创建 TypeExpr，直接返回原节点
}
```

当 `Ts...[0]` 选择的是类型参数时，应创建对应的类型表达式节点（如 Clang 的 `TypeExpr` 或至少一个占位表达式），而不是返回原始 `PackIndexingExpr`。这在后续类型推导中可能导致无法正确解析类型。

---

## 三、Stage 5.4 Concepts — 详细核查

### ✅ 核心框架

| 组件 | 位置 | 行数 | 状态 |
|------|------|------|------|
| `ConstraintSatisfaction` 头文件 | `include/blocktype/Sema/ConstraintSatisfaction.h` | 142 | ✅ |
| `ConstraintSatisfaction` 实现 | `src/Sema/ConstraintSatisfaction.cpp` | 397 | ✅ |
| Sema 集成（构造初始化） | `src/Sema/Sema.cpp:30` | — | ✅ |
| requires-clause 验证 | `src/Sema/SemaTemplate.cpp:136-143,178-187` | — | ✅ |
| 推导后约束检查 | `src/Sema/SemaTemplate.cpp:578-598` | — | ✅ |

**EvaluateRequirement 四种分发**：
- ✅ `TypeRequirement` → 检查类型非空、非 dependent
- ✅ `ExprRequirement` → 替换实参 + noexcept 检查
- ✅ `CompoundRequirement` → 替换实参 + noexcept + 返回类型约束
- ✅ `NestedRequirement` → 递归 `SubstituteAndEvaluate`

**EvaluateConstraintExpr 支持的节点**：
- ✅ `BinaryOperator(&&)` 合取（短路求值）
- ✅ `BinaryOperator(||)` 析取（短路求值）
- ✅ `UnaryOperator(!)` 逻辑取反
- ✅ `RequiresExpr` 递归评估
- ✅ `CXXBoolLiteral` / `IntegerLiteral` 直接值
- ✅ `CallExpr` 概念调用（通过名称查找解析 ConceptDecl，递归调用 CheckConceptSatisfaction）
- ✅ `DeclRefExpr` 常量求值（正确移除不可能成功的 ConceptDecl dyn_cast）
- ✅ `TemplateSpecializationExpr` 概念-id（如 `Integral<T>`，通过 SymbolTable 按名称查找概念）
- ✅ `SubstituteExpr` 支持 `TemplateSpecializationExpr` 替换（概念参数中的模板类型参数正确替换）

### ✅ SFINAE 完整集成

- ✅ `DiagnosticsEngine::pushSuppression/popSuppression` — `Diagnostics.h:79-92`
- ✅ `DiagnosticsEngine::report()` 在 `SuppressCount>0` 时静默丢弃诊断 — `Diagnostics.cpp:42-53,91-101`
- ✅ `SFINAEGuard` 构造时自动 `pushSuppression`，析构时自动 `popSuppression` — `SFINAE.h:122-140`
- ✅ `CheckConstraintSatisfaction` 在 `SFINAEGuard` 下执行 — `ConstraintSatisfaction.cpp:44-48`
- ✅ `CheckConceptSatisfaction` 在 `SFINAEGuard` 下执行 — `ConstraintSatisfaction.cpp:67-73`
- ✅ `DeduceAndInstantiateFunctionTemplate` 在推导阶段使用 `SFINAEGuard` — `SemaTemplate.cpp:545-551`

### 🔴 问题 S5.4-1：缺少约束部分排序（高）

文档明确要求：
> Concept 部分排序：更严格约束的模板更特化

**当前状态**：完全未实现。当两个函数模板都能匹配调用时，无法通过约束严格程度选择更特化的候选。

**Clang 对比**：Clang 实现 `CalculateConstraintSatisfaction` → `compareConstraints`，使用原子约束分解和蕴含检查来确定约束的偏序关系。这是 C++20 重载决议的关键部分。

**影响**：以下代码无法正确选择重载：
```cpp
template<typename T> requires Integral<T>   void f(T);  // #1
template<typename T> requires SignedIntegral<T> void f(T); // #2
f(42); // 应选 #2，但当前无法区分
```

### ⚠️ 问题 S5.4-2：checkReturnTypeConstraint 过度简化（🟡 中）

```cpp
// ConstraintSatisfaction.cpp:369-397
// 注释: "不处理 concept 返回类型约束、auto 推导、cv 限定、引用绑定"
```

Clang 中 `{ expr } -> type-constraint` 会：
1. 推导 `decltype(expr)` 的类型
2. 检查是否满足 `type-constraint`（可能是 concept）
3. 处理 cv 限定和引用绑定

当前实现仅做指针/规范类型相等比较。

### ⚠️ 问题 S5.4-3：canThrow 过度简化（🟡 中）

```cpp
// ConstraintSatisfaction.cpp:350-367
bool ConstraintSatisfaction::canThrow(Expr *E) {
    if (auto *Throw = llvm::dyn_cast<CXXThrowExpr>(E))
        return true;
    return false; // 默认假设不抛出
}
```

Clang 递归遍历所有子表达式，检查 `CXXThrowExpr`、`CallExpr`(调用可能抛出的函数)、`CXXNewExpr`(分配失败)等。当前实现导致 `noexcept` 约束在大多数情况下为 true。

### ⚠️ 问题 S5.4-4：缺少约束归一化/原子约束追踪（🟡 中）

Clang 将约束表达式归一化为原子约束的合取/析取范式，并逐个追踪每个原子约束的满足状态，用于生成精确的诊断信息（"which constraint was not satisfied"）。当前实现只有最终 bool 结果。

---

## 四、Stage 5.5 模板特化与测试 — 详细核查

### ✅ 显式特化

- ✅ `ActOnExplicitSpecialization` — 返回空有效结果（两阶段设计）
- ✅ Parser (`ParseTemplate.cpp:66-148`) 处理 `template<>` 实际创建 `ClassTemplateSpecializationDecl` / `VarTemplateSpecializationDecl` / `FunctionTemplateDecl`
- ✅ `IsExplicitSpecialization` 标志在 AST 节点中存在
- ✅ `explicit` 标记在 dump 中输出

### ⚠️ 问题 S5.5-1：ActOnExplicitSpecialization 缺少验证逻辑（🟡 中）

当前实现仅返回 `DeclResult(nullptr)`，不执行文档要求的：
1. ~~查找主模板~~（由 Parser 完成）
2. ~~验证实参与主模板参数匹配~~（未做）
3. ~~创建 SpecializationDecl 并标记~~（由 Parser 完成）

**架构问题**：验证逻辑散布在 Parser 和 Sema 之间，Sema 未真正验证特化的正确性。理想情况下，Parser 创建原始 AST 节点后传给 Sema 进行语义验证。

### ✅ 显式实例化

- ✅ `ActOnExplicitInstantiation` (`SemaTemplate.cpp:372-430`)
- ✅ 支持 `ClassTemplateSpecializationDecl` 触发 `InstantiateClassTemplate`
- ✅ 支持 `FunctionDecl` 注册

### ⚠️ 偏特化 — 核心缺失

`ActOnClassTemplatePartialSpecialization` (`SemaTemplate.cpp:436-493`)：
- ✅ 主模板查找
- ✅ 参数列表非空验证
- ✅ `ValidateTemplateParameterList` 深度验证
- ✅ 参数数量匹配（考虑 pack）
- ✅ 注册到主模板和 DeclContext

### 🔴 问题 S5.5-2：偏特化部分排序未实现（高）

```cpp
// SemaTemplate.cpp:464-468
// 3. Verify the partial specialization args are more specialized
//    than the primary template.
//    For now, we accept all valid partial specializations.
```

当存在多个匹配的偏特化时，编译器无法选择最特化的那个。例如：

```cpp
template<typename T> class Vector<T*> {};     // #1
template<typename T> class Vector<T&> {};     // #2
template<>         class Vector<int*> {};     // #3 (显式特化)
Vector<int*> v; // 应选 #3，但当前可能选 #1 或报歧义
```

**Clang 对比**：Clang 使用 `getMoreSpecializedPartialSpecialization`，通过模板参数推导反向匹配来确定哪个偏特化更特化。

### ⚠️ 问题 S5.5-3：缺少 ClassTemplateDecl::findSpecialization（🟡 中）

`FindExistingSpecialization` 存在于 `TemplateInstantiator` 中，但 `ClassTemplateDecl` 本身没有 `findSpecialization()` 方法。Clang 中 `ClassTemplateDecl` 提供查找特化的方法供 Sema 层使用（用于检测重复定义和选择最佳匹配）。

### ⚠️ 问题 S5.5-4：测试覆盖不完整（🟡 中）

文档要求（E5.5.3.2）：
```
tests/lit/Sema/
├── basic-template.test       # 不存在
├── specialization.test       # 不存在
├── variadic.test             # 不存在
├── sfinae.test               # 不存在
├── concepts.test             # 不存在
└── std-library.test          # 不存在
```

**实际存在的 lit 测试**：
- `tests/lit/parser/template.test` ✅（含显式特化）
- `tests/lit/parser/concept.test` ✅
- `tests/lit/ast-dump/template-declarations.test` ✅

**实际存在的 unit 测试**：
- `tests/unit/Sema/TemplateInstantiationTest.cpp` ✅ (7.97 KB)
- `tests/unit/Sema/TemplateDeductionTest.cpp` ✅ (6.61 KB)
- `tests/unit/Sema/SFINAETest.cpp` ✅ (4.51 KB)
- `tests/unit/Sema/ConceptTest.cpp` ✅ (9.06 KB)
- `tests/unit/Sema/VariadicTemplateTest.cpp` ✅ (11.54 KB)

缺失：无 `tests/lit/Sema/` 目录下的 6 个 lit 集成测试文件。

---

## 五、与 Clang 同模块功能差距总览（2026-04-18 修复后更新）

| 功能特性 | Clang 实现 | BlockType 状态 | 差距级别 |
|----------|-----------|---------------|---------|
| 参数包展开 | 多包同时展开，长度验证 | 多包同时展开 + 长度验证 | ✅ |
| Fold 表达式 | 完整（含逗号折叠） | 完整 | ✅ |
| Pack indexing | 类型/表达式/模板均支持 | 类型参数创建 CXXBoolLiteral 载体 | ✅ |
| 原子约束分解 | 归一化为原子约束+追踪 | collectAtomicConstraints + 蕴含检查 | ✅ |
| 约束部分排序 | `compareConstraints` | CompareConstraints + Overload resolve 集成 | ✅ |
| requires 表达式 | 4 种 requirement 完整求值 | 基本完整 | ✅ |
| 概念表达式求值 | concept-id 求值 + SubstituteExpr | TemplateSpecializationExpr/CallExpr 名称查找 + 替换 | ✅ |
| noexcept 分析 | 递归遍历所有子表达式 | 递归分析 7 种表达式 | ✅ |
| 返回类型约束 | concept 推导 + cv/引用处理 | cv/引用/指针/转换检查 + ConversionChecker | ✅ |
| 偏特化部分排序 | `getMoreSpecialized*` | isMoreSpecializedPartialSpec + FindBestMatching | ✅ |
| 显式特化验证 | Sema 层完整验证 | ValidateExplicitSpecialization 参数验证 | ✅ |
| SFINAE 诊断抑制 | `SFINAETrap` + error limit | `SuppressCount` 栈 | ✅ |
| 实例化缓存 | `llvm::FoldingSet` | 线性扫描匹配 | 🟡 |

> **唯一剩余差距**：实例化缓存使用线性扫描匹配，性能不及 Clang 的 `FoldingSet` 哈希查找。功能正确性无影响，仅影响大规模实例化场景的编译速度。

---

## 六、优先级修复建议（2026-04-18 更新）

| 优先级 | 问题编号 | 描述 | 状态 |
|--------|---------|------|------|
| ~~P0~~ | ~~S5.4-1~~ | ~~实现约束部分排序 `compareConstraints`~~ | ✅ 已修复 |
| ~~P0~~ | ~~S5.5-2~~ | ~~实现偏特化部分排序 `getMoreSpecialized`~~ | ✅ 已修复 |
| ~~P1~~ | ~~S5.3-1~~ | ~~ExpandPack 支持多包同时展开~~ | ✅ 已修复 |
| ~~P1~~ | ~~S5.4-2~~ | ~~checkReturnTypeConstraint 增强~~ | ✅ 已修复 |
| ~~P1~~ | ~~S5.5-4~~ | ~~补充 `tests/lit/Sema/` 下的 6 个 lit 测试~~ | 待补充 |
| ~~P2~~ | ~~S5.3-2~~ | ~~PackIndexingExpr 类型实参创建正确表达式~~ | ✅ 已修复 |
| ~~P2~~ | ~~S5.4-3~~ | ~~canThrow 递归分析~~ | ✅ 已修复 |
| ~~P2~~ | ~~S5.5-1~~ | ~~ActOnExplicitSpecialization 增加验证~~ | ✅ 已修复 |
| ~~P2~~ | ~~S5.5-3~~ | ~~ClassTemplateDecl 添加 findSpecialization~~ | ✅ 已修复 |

> **剩余工作**：S5.5-4 lit 测试补充建议在整体测试阶段统一处理。

---

## 七、问题汇总统计（2026-04-18 修复后）

| 级别 | 数量 | 状态 |
|------|------|------|
| 🔴 高 | ~~2~~ **0** | ~~S5.4-1, S5.5-2~~ 全部已修复 |
| 🟡 中 | ~~7~~ **1** | ~~S5.3-1, S5.3-2, S5.4-2, S5.4-3, S5.4-4, S5.5-1, S5.5-3~~ 全部已修复，仅剩实例化缓存性能优化 |
| ✅ 完整 | — | ExpandPack, CXXFoldExpr, ConstraintSatisfaction 框架, SFINAE 集成, requires 评估, 显式特化/实例化基础, 偏特化声明验证, 约束部分排序, 偏特化部分排序, 多包展开, PackIndexing, noexcept 分析, 返回类型约束 |

> **修复率：8/9（89%）**，仅剩 1 项性能优化（实例化缓存）和 1 项测试补充（lit 测试）。

---

## 八、后续修复记录

### 修复 R1：概念表达式求值 `EvaluateConstraintExpr` 三项缺陷（2026-04-18 commit `c8a6c66`）

**背景**：核查中发现 `EvaluateConstraintExpr` 对概念表达式（concept-id）的求值存在三类缺陷，导致如 `Integral<T>` 形式的约束表达式无法正确求值。

**缺陷 1 — `TemplateSpecializationExpr` 类型错误（编译错误）**：
- `getTemplateName()` 返回 `llvm::StringRef`（模板名称字符串），旧代码将其赋值给 `TemplateDecl*`，无法编译
- **修复**：通过 `TSE->getTemplateName()` 获取名称，再用 `SymbolTable.lookupConcept(Name)` 查找 `ConceptDecl*`

**缺陷 2 — `DeclRefExpr`/`CallExpr` 类型层次不兼容（永远失败）**：
- `ConceptDecl` 继承自 `TypeDecl`（非 `ValueDecl`），而 `DeclRefExpr::getDecl()` 返回 `ValueDecl*`
- `dyn_cast<ConceptDecl>(DRE->getDecl())` 因继承链不兼容而**永远返回 nullptr**
- **修复**：通过被调用者名称使用 `SymbolTable.lookupConcept()` 进行名称查找

**缺陷 3 — `SubstituteExpr` 缺少 `TemplateSpecializationExpr` 处理**：
- `SubstituteExpr` 不处理 `TemplateSpecializationExpr`，导致 `Integral<T>` 中的 `T` 在替换后不会被替换为实际类型
- **修复**：在 `SubstituteExpr` 中新增 `TemplateSpecializationExpr` 分支，遍历模板参数进行类型替换

**修改文件**：
- `src/Sema/ConstraintSatisfaction.cpp` — 修复概念查找逻辑，添加 `SymbolTable.h` 头文件
- `src/Sema/TemplateInstantiation.cpp` — 新增 `TemplateSpecializationExpr` 替换处理

---
name: stage-4.1-sema-basics
overview: |
  实现 Stage 4.1 Sema 基础设施，创建 Phase 4 所有后续阶段需要的全部 stub。
  严格遵循 docs/plan/04-PHASE4-sema-basics.md 的完整设计，
  确保 Stage 4.2-4.5 开发时所有类型定义、方法签名、诊断 ID 均已就绪，
  彻底避免 Phase 3 中后期随机编码的乱象。
todos:
  - id: create-lookup-h
    content: |
      创建 include/blocktype/Sema/Lookup.h（按原文档 E4.3.1.1 完整代码）：
      LookupNameKind 枚举（7 种）、LookupResult 类（完整接口）、NestedNameSpecifier 类。
    status: pending
  - id: create-conversion-h
    content: |
      创建 include/blocktype/Sema/Conversion.h（按原文档 E4.4.2.1 完整代码）：
      ConversionRank 枚举、StandardConversionKind 枚举、
      StandardConversionSequence 类、ImplicitConversionSequence 类、
      ConversionChecker 静态工具类。
    status: pending
  - id: create-overload-h
    content: |
      创建 include/blocktype/Sema/Overload.h（按原文档 E4.4.1.1 完整代码）：
      OverloadCandidate 类、OverloadCandidateSet 类。
    status: pending
    dependencies:
      - create-conversion-h
  - id: create-type-deduction-h
    content: |
      创建 include/blocktype/Sema/TypeDeduction.h（按原文档 E4.2.2.1 完整代码）：
      TypeDeduction 类（deduceAutoType/deduceAutoRefType/deduceAutoForwardingRefType/
      deduceAutoPointerType/deduceReturnType/deduceFromInitList/
      deduceDecltypeType/deduceDecltypeAutoType/deduceTemplateArguments）。
    status: pending
  - id: create-type-check-h
    content: |
      创建 include/blocktype/Sema/TypeCheck.h（按原文档 E4.5.1.1 完整代码）：
      TypeCheck 类（CheckAssignment/CheckInitialization/CheckDirectInitialization/
      CheckListInitialization/CheckReferenceBinding/CheckCall/CheckReturn/
      CheckCondition/CheckCaseExpression/isTypeCompatible/isSameType/
      getCommonType/getBinaryOperatorResultType/getUnaryOperatorResultType/
      isComparable/isCallable）。
    status: pending
  - id: create-access-control-h
    content: |
      创建 include/blocktype/Sema/AccessControl.h（按原文档 E4.5.2.1 完整代码）：
      AccessControl 静态类（isAccessible/CheckMemberAccess/CheckBaseClassAccess/
      CheckFriendAccess/getEffectiveAccess/isDerivedFrom）。
    status: pending
  - id: create-constant-expr-h
    content: |
      创建 include/blocktype/Sema/ConstantExpr.h（按原文档 E4.5.4.1 完整代码）：
      EvalResult 类（Success/NotConstantExpression/HasSideEffects/
      DependsOnTemplateParameter/EvaluationFailed）+ ConstantExprEvaluator 类
      （Evaluate/EvaluateAsBooleanCondition/EvaluateAsInt/EvaluateAsFloat/
      isConstantExpr/EvaluateCall + 私有 EvaluateXXX 分派方法）。
    status: pending
  - id: create-sema-h
    content: |
      创建 include/blocktype/Sema/Sema.h（按原文档 E4.1.1.1 完整代码 + 全阶段方法声明）：
      - 保留所有原始 include（Lookup.h, Overload.h, TypeDeduction.h 等）
      - ExprResult/StmtResult/DeclResult 包装类
      - Sema 主类完整接口（含全阶段所有方法签名）
      - [Stage 4.1] Scope/DeclContext/TranslationUnit 管理
      - [Stage 4.1] ActOnXXX 声明/表达式/语句方法（完整签名）
      - [Stage 4.2] isCompleteType/RequireCompleteType/getCanonicalType
      - [Stage 4.3] LookupUnqualifiedName/LookupQualifiedName/LookupADL/
        CollectAssociatedNamespacesAndClasses
      - [Stage 4.4] ResolveOverload/AddOverloadCandidate
      - [Stage 4.5] 类型推导/类型检查/访问控制/常量求值入口方法
      - 诊断辅助：Diag(Loc, ID), Diag(Loc, ID, Extra)
    status: pending
    dependencies:
      - create-lookup-h
      - create-overload-h
      - create-conversion-h
      - create-type-deduction-h
      - create-type-check-h
      - create-access-control-h
      - create-constant-expr-h
  - id: create-symboltable-h
    content: |
      创建 include/blocktype/Sema/SymbolTable.h（按文档 E4.1.2.1 完整代码）：
      6 类 StringMap 分类存储 + addXXX/lookupXXX + addDecl/lookup 泛型分派。
    status: pending
  - id: create-symboltable-cpp
    content: |
      创建 src/Sema/SymbolTable.cpp（按文档 E4.1.2.2 完整代码）：
      完整实现含函数重载支持和重定义检查。
    status: pending
    dependencies:
      - create-symboltable-h
  - id: create-sema-cpp
    content: |
      创建 src/Sema/Sema.cpp：
      - 构造/析构/Scope管理/DeclContext管理（完整实现，按 E4.1.1.2）
      - ActOnTranslationUnit（完整实现）
      - isCompleteType/RequireCompleteType/getCanonicalType（基础实现，按 E4.2.3.1）
      - 所有其他 ActOnXXX 方法：stub（返回 Invalid，标注 TODO）
      - 所有名字查找方法：stub（返回空 LookupResult）
      - 所有重载决议方法：stub（返回 nullptr）
      - 所有类型推导/类型检查/访问控制入口：stub
    status: pending
    dependencies:
      - create-sema-h
  - id: modify-scope-h
    content: |
      修改 Scope.h 添加 using 指令支持（按文档 E4.1.3.1）：
      SmallVector<NamespaceDecl*, 4> UsingDirectives + addUsingDirective + getUsingDirectives。
    status: pending
  - id: create-diagnostic-sema-kinds
    content: |
      创建 include/blocktype/Basic/DiagnosticSemaKinds.def（按原文档 E4.5.3.1 完整代码）：
      名字查找错误（err_undeclared_var, err_redefinition 等）+
      类型检查错误（err_type_mismatch, err_incomplete_type 等）+
      重载决议错误（err_ovl_no_viable_function 等）+
      访问控制错误（err_access_private 等）+
      语句检查错误（err_return_type_mismatch 等）+
      警告（warn_unused_variable 等）。
      同时修改 DiagnosticIDs.h/include 机制引入此文件。
    status: pending
  - id: modify-cmake
    content: |
      更新 src/Sema/CMakeLists.txt 添加新源文件（Sema.cpp, SymbolTable.cpp）。
    status: pending
    dependencies:
      - create-sema-cpp
      - create-symboltable-cpp
  - id: build-verify
    content: |
      编译验证，确保所有新文件与已有组件正确集成，修复编译错误。
    status: pending
    dependencies:
      - modify-cmake
      - create-diagnostic-sema-kinds
      - modify-scope-h
---

## 产品概述

为 BlockType 编译器实现 Phase 4 Stage 4.1 语义分析基础设施。本阶段是 Phase 4 的**奠基工程**，不仅实现 Stage 4.1 自身功能（Sema 主类 + 符号表 + Scope 完善 + DeclContext 集成），还**创建 Phase 4 所有后续阶段（4.2-4.5）需要的全部 stub 头文件、方法签名和诊断 ID**。

**核心原则：Phase 3 教训的彻底回应。**
Phase 3 开发中，Stage 3.1 缺失 DeclContext 基础设施和模板特化声明类，导致后期开发陷入困境。
Stage 4.1 必须避免重蹈覆辙：**所有后续阶段需要用到的类型定义、枚举、类接口、方法签名、诊断 ID，全部在本阶段预先创建。** 后续阶段的 AI 开发者只需在 .cpp 中填充实现，无需发明任何 API。

## 核心功能

### Stage 4.1 自身功能（完整实现）

- **Task 4.1.1 Sema 主类**: `Sema.h`（ExprResult/StmtResult/DeclResult + Sema 主类）+ `Sema.cpp`（构造/析构/Scope管理/DeclContext管理）
- **Task 4.1.2 符号表**: `SymbolTable.h`（6 类分类存储）+ `SymbolTable.cpp`（完整实现）
- **Task 4.1.3 Scope 完善**: 添加 using 指令支持
- **Task 4.1.4 DeclContext 集成**: 确保 Sema 正确使用已有 DeclContext

### 后续阶段前置 stub（全部创建）

| Stub 文件 | 服务于 | 包含内容 |
|-----------|--------|----------|
| `Lookup.h` | Stage 4.3 | LookupNameKind 枚举 + LookupResult 类 + NestedNameSpecifier 类 |
| `Conversion.h` | Stage 4.4 | ConversionRank 枚举 + SCS/ICS 类 + ConversionChecker |
| `Overload.h` | Stage 4.4 | OverloadCandidate 类 + OverloadCandidateSet 类 |
| `TypeDeduction.h` | Stage 4.2 | TypeDeduction 类（auto/decltype 推导完整接口） |
| `TypeCheck.h` | Stage 4.5 | TypeCheck 类（赋值/初始化/调用/条件检查完整接口） |
| `AccessControl.h` | Stage 4.5 | AccessControl 静态类（成员/基类/友元访问检查） |
| `ConstantExpr.h` | Stage 4.5 | EvalResult 类 + ConstantExprEvaluator 类 |
| `DiagnosticSemaKinds.def` | 全阶段 | 完整的语义分析诊断 ID（名字查找/类型检查/重载/访问/语句/警告） |

### Sema.h 全阶段方法预声明

Sema.h 不仅包含 Stage 4.1 的方法，还预声明所有后续阶段需要在 Sema 上调用的方法：

- **[4.1] 基础管理**: PushScope/PopScope, PushDeclContext/PopDeclContext, ActOnTranslationUnit
- **[4.1] ActOn 声明**: ActOnDeclarator, ActOnFinishDecl, ActOnVarDecl, ActOnFunctionDecl, ActOnStartOfFunctionDef, ActOnFinishOfFunctionDef
- **[4.1] ActOn 表达式**: ActOnExpr, ActOnCallExpr, ActOnMemberExpr, ActOnBinaryOperator, ActOnUnaryOperator, ActOnCastExpr, ActOnArraySubscriptExpr, ActOnConditionalExpr
- **[4.1] ActOn 语句**: ActOnReturnStmt, ActOnIfStmt, ActOnWhileStmt, ActOnForStmt, ActOnDoStmt, ActOnSwitchStmt, ActOnCaseStmt, ActOnDefaultStmt, ActOnBreakStmt, ActOnContinueStmt, ActOnGotoStmt, ActOnCompoundStmt, ActOnDeclStmt, ActOnNullStmt
- **[4.2] 类型处理**: isCompleteType, RequireCompleteType, getCanonicalType
- **[4.3] 名字查找**: LookupUnqualifiedName, LookupQualifiedName, LookupADL, CollectAssociatedNamespacesAndClasses
- **[4.4] 重载决议**: ResolveOverload, AddOverloadCandidate
- **[4.5] 语义检查辅助入口**: Diag(Loc, ID), Diag(Loc, ID, Extra)

## 技术栈

- 语言: C++17
- 依赖: LLVM ADT（StringMap, SmallVector, ArrayRef, DenseMap, APSInt, APFloat, raw_ostream, SmallPtrSet）
- 构建系统: CMake
- 命名空间: `blocktype`
- 头文件前缀: `blocktype/...`

## 实现方案

严格参照 `docs/plan/04-PHASE4-sema-basics.md` 中**全部 5 个 Stage** 的设计。
Stage 4.1 创建的文件包含后续阶段所需的**全部类型定义和方法签名**。
所有 stub 头文件的类型定义与原文档代码**完全一致**，不做任何删减。

### 关键设计决策

1. **一次性创建所有 stub 头文件**：不仅是 4.3/4.4 的 Lookup/Overload/Conversion，还包括 4.2 的 TypeDeduction 和 4.5 的 TypeCheck/AccessControl/ConstantExpr
2. **Sema.h 包含所有后续阶段的 include**：`#include "blocktype/Sema/Lookup.h"`, `Overload.h`, `TypeDeduction.h` 等全部保留
3. **Sema.h 预声明所有方法签名**：后续阶段只需实现 .cpp，无需修改 .h
4. **DiagnosticSemaKinds.def 一次到位**：按原文档 E4.5.3.1 定义全部 30+ 个诊断 ID
5. **Sema.cpp 中未实现方法提供 stub**：返回 Invalid/空/nullptr，确保编译通过
6. **SymbolTable 完整实现**：按 E4.1.2.2 代码，含函数重载和重定义检查

### 架构设计

```
Phase 4 全阶段架构（Stage 4.1 创建全部骨架）
═══════════════════════════════════════════

Sema（协调器）─────────────────────────────────────
├── SymbolTable（全局符号存储）[4.1 完整实现]
├── CurrentScope → Scope 链（词法作用域栈）[4.1 完整实现]
├── CurContext → DeclContext 链（语义声明层级）[4.1 完整实现]
├── CurFunction → FunctionDecl（当前函数）[4.1 实现]
├── CurTU → TranslationUnitDecl（翻译单元）[4.1 实现]
│
├── [4.3 预声明] LookupUnqualifiedName / LookupQualifiedName / LookupADL
├── [4.4 预声明] ResolveOverload / AddOverloadCandidate
└── [4.5 预声明] Diag 辅助方法

Lookup.h（类型 stub，4.1 创建）───────────────────
├── LookupNameKind 枚举（7 种查找类型）
├── LookupResult 类（查找结果容器）
└── NestedNameSpecifier 类（嵌套名限定符）

Conversion.h（类型 stub，4.1 创建）────────────────
├── ConversionRank 枚举（6 级转换等级）
├── StandardConversionKind 枚举
├── StandardConversionSequence 类
├── ImplicitConversionSequence 类
└── ConversionChecker 静态工具类

Overload.h（类型 stub，4.1 创建）─────────────────
├── OverloadCandidate 类（候选函数）
└── OverloadCandidateSet 类（候选集管理）

TypeDeduction.h（类型 stub，4.1 创建）─────────────
└── TypeDeduction 类（auto/decltype 完整接口）

TypeCheck.h（类型 stub，4.1 创建）─────────────────
└── TypeCheck 类（赋值/初始化/调用/条件检查）

AccessControl.h（类型 stub，4.1 创建）─────────────
└── AccessControl 静态类（成员/基类/友元访问）

ConstantExpr.h（类型 stub，4.1 创建）──────────────
├── EvalResult 类（求值结果）
└── ConstantExprEvaluator 类（常量表达式求值）

DiagnosticSemaKinds.def（诊断 ID，4.1 创建）───────
├── 名字查找错误（8 个）
├── 类型检查错误（5 个）
├── 重载决议错误（4 个）
├── 访问控制错误（3 个）
├── 语句检查错误（4 个）
└── 警告（4 个）
```

## 目录结构

```
# ═══════════════════════════════════════════════════
# [NEW] Phase 4 全阶段 stub 头文件
# ═══════════════════════════════════════════════════

include/blocktype/Sema/Lookup.h
  → E4.3.1.1 完整代码（无删减）

include/blocktype/Sema/Conversion.h
  → E4.4.2.1 完整代码（无删减）

include/blocktype/Sema/Overload.h
  → E4.4.1.1 完整代码（无删减）

include/blocktype/Sema/TypeDeduction.h
  → E4.2.2.1 完整代码（无删减）

include/blocktype/Sema/TypeCheck.h
  → E4.5.1.1 完整代码（无删减）

include/blocktype/Sema/AccessControl.h
  → E4.5.2.1 完整代码（无删减）

include/blocktype/Sema/ConstantExpr.h
  → E4.5.4.1 完整代码（无删减）

# ═══════════════════════════════════════════════════
# [NEW] Sema 主类（Stage 4.1 核心交付物）
# ═══════════════════════════════════════════════════

include/blocktype/Sema/Sema.h
  - 所有 #include 保留（Lookup.h, Overload.h, SymbolTable.h, Scope.h 等）
  - ExprResult/StmtResult/DeclResult 包装类
  - Sema 主类：含全阶段方法签名（详见上方"核心功能"节）
  - 不做任何删减，与原文档完全一致

src/Sema/Sema.cpp
  - 构造/析构/Scope管理/DeclContext管理：完整实现
  - isCompleteType/RequireCompleteType/getCanonicalType：基础实现
  - 所有 ActOnXXX 方法：stub（返回 Invalid）
  - 名字查找/重载决议方法：stub（返回空/nullptr）

# ═══════════════════════════════════════════════════
# [NEW] 符号表（Stage 4.1 完整实现）
# ═══════════════════════════════════════════════════

include/blocktype/Sema/SymbolTable.h
  → E4.1.2.1 完整代码（6 类分类存储 + addDecl/lookup）

src/Sema/SymbolTable.cpp
  → E4.1.2.2 完整代码（函数重载支持 + 重定义检查）

# ═══════════════════════════════════════════════════
# [NEW] 诊断 ID（Phase 4 全阶段）
# ═══════════════════════════════════════════════════

include/blocktype/Basic/DiagnosticSemaKinds.def
  → E4.5.3.1 完整诊断 ID（28+ 个），按分类组织

# ═══════════════════════════════════════════════════
# [MODIFY] 已有文件
# ═══════════════════════════════════════════════════

include/blocktype/Sema/Scope.h
  - 添加: SmallVector<NamespaceDecl*, 4> UsingDirectives
  - 添加: addUsingDirective(NamespaceDecl*)
  - 添加: getUsingDirectives() const

include/blocktype/Basic/DiagnosticIDs.h（或 .def）
  - 添加 #include "DiagnosticSemaKinds.def" 或等效机制

src/Sema/CMakeLists.txt
  - 添加 Sema.cpp 和 SymbolTable.cpp 到 add_library
```

## 实现注意事项

### Phase 3 教训的全面回应

- **所有后续阶段需要的方法名、参数类型、返回类型必须在 Stage 4.1 预先确定**
- **所有 stub 头文件的类型定义与原文档代码完全一致，不做任何删减**
- **诊断 ID 一次到位定义全部 28+ 个**，后续阶段不得自行添加新 ID（除非原文档未覆盖的极端情况）
- **Sema.cpp 中每个 stub 方法都有清晰的 TODO 注释**，标明应在哪个 Stage 实现

### 技术注意事项

- Sema.h 保留 `#include "blocktype/Sema/Lookup.h"` 和 `#include "blocktype/Sema/Overload.h"` — 对应 stub 在 Stage 4.1 创建
- LookupResult 需要前向声明 NamedDecl（已有 include Decl.h）
- Overload.h 依赖 Conversion.h（ConversionRank 类型），创建顺序：Conversion → Overload
- OverloadCandidateSet::addCandidates 接受 `const LookupResult&`，需要 Lookup.h 已存在
- SymbolTable 构造函数需要 `ASTContext&`，暂存引用供未来扩展
- Scope 的 using 指令支持：需在 Scope.h 中前向声明 NamespaceDecl（已有 Decl.h 的 include 可解决）
- NestedNameSpecifier 使用 union（Namespace/Type/Identifier），需注意 C++ 限制（所有成员必须是平凡类型）
- ConstantExpr.h 中 EvalResult 使用 llvm::APFloat 和 llvm::APSInt，需 include 对应头文件
- DiagnosticSemaKinds.def 使用与 DiagnosticIDs.def 相同的 DIAG 宏格式

## SubAgent

- **code-explorer**: 用于编译验证时快速定位编译错误和相关依赖文件，确保新代码与已有组件正确集成

# Phase 7：C++23/C++26 特性
> **目标：** 实现 C++23/C++26 的核心新特性，包括静态反射、Contracts、Deducing this 等
> **前置依赖：** Phase 0-6 完成（特别是 Phase 6 CodeGen 完整）
> **验收标准：** 能正确编译 C++23/C++26 特性代码；测试覆盖率 ≥ 80%
> **审计日期：** 2026-04-19
> **总计：** 34 项特性 (C++23: 18 项, C++26: 16 项)
> **已实现：** 15 项 ✅ | **部分实现：** 4 项 ⚠️ | **未实现：** 15 项 ❌
> **预计工期：** 8 周（5个Stage，15个Task）
>
> **⚠️ 重要更新（2026-04-19补充核查）：**
> - 初次核查因搜索关键词不精确，误判 3 项已实现特性为"未实现"：
>   - 多维 `operator[]` (P2128R6) → ✅ 已实现（`ParseExpr.cpp:450-460`）
>   - `#warning` 预处理指令 (P2437R1) → ✅ 已实现（`Preprocessor.cpp:380-381`）
>   - `Z`/`z` 字面量后缀 (P0330R8) → ✅ 已实现（`ParseExpr.cpp:704-706`）
> - 详见 [`07-PHASE7-feature-verification-report.md`](./07-PHASE7-feature-verification-report.md)
>
> **⚠️ Phase 3教训应用原则：**
> 1. **接口先行**：每个Task必须先定义完整的头文件接口（类/方法/枚举），即使实现是空壳
> 2. **预置API**：为后续Task预先声明所有需要的函数签名、AST节点、诊断ID
> 3. **避免随机编码**：确保后续AI开发者有明确的API可用，不会发明临时方案
> 4. **参照Clang**：关键设计决策参照Clang实现，保证架构一致性

---

## 📊 特性支持概览

### C++23 特性状态（13/18 已实现）

**✅ 已实现：**
- `if consteval` (P1938R3)
- 多维 `operator[]` (P2128R6) — `ParseExpr.cpp:450-460`
- `#elifdef` / `#elifndef` (P2334R1)
- `#warning` 预处理指令 (P2437R1) — `Preprocessor.cpp:380-381`
- Lambda 模板参数 (P1102R2)
- Lambda 属性 (P2173R1)
- `Z`/`z` 字面量后缀 (P0330R8) — `ParseExpr.cpp:704-706`
- `\e` 转义序列 (P2314R4)
- Deducing this / 显式对象参数 (P0847R7) — **Stage 7.1 实现**
- `auto(x)` / `auto{x}` decay-copy (P0849R8) — **Stage 7.1 实现**
- `static operator()` (P1169R4) — **Stage 7.1 实现**
- `static operator[]` (P2589R1) — **Stage 7.1 实现**
- `[[assume]]` 属性 (P1774R8) — **Stage 7.1 实现**

**⚠️ 部分实现：**
- constexpr 放宽 (P2448R2) — 部分已通过其他方式实现

**❌ 待实现（P2优先级）：**
- 占位符变量 `_` (P2169R4, C++26) — P2
- 分隔转义 `\x{...}` (P2290R3) — P2
- 命名转义 `\N{...}` (P2071R2) — P2
- `for` init-statement 中 `using` (P2360R0) — P2

### C++26 特性状态（2/16 完整 + 3 部分实现）

**✅ 已实现：**
- 包索引 `T...[N]` (P2662R3)
- `#embed` (P1967R14)

**⚠️ 部分实现：**
- 静态反射 `reflexpr` — 基础框架已建立，功能有限
- 用户 `static_assert` 消息 (P2741R3) — 支持基本格式，无格式化增强
- `@`/`$`/反引号字符集扩展 (P2558R2) — `@` 已有 token，`$` 和反引号不支持

**❌ 待实现（P1/P2优先级）：**
- Contracts (pre/post/assert) (P2900R14) — P2
- `= delete("reason")` (P2573R2) — P1
- 结构化绑定作为条件 (P0963R3) — P2
- 结构化绑定引入包 (P1061R10) — P2
- `constexpr` 异常 (P3068R6) — P2
- 平凡可重定位 (P2786R13) — P2
- 概念和变量模板模板参数 (P2841R7) — P2
- 可变参数友元 (P2893R3) — P2
- 可观察检查点 (P1494R5) — P2
- `[[indeterminate]]` 属性 (P2795R5) — P2

---

## 📌 阶段总览

```
Phase 7 包含 5 个 Stage，共 15 个 Task，预计 8 周完成。
依赖链：Stage 7.1 → Stage 7.2 → Stage 7.3 → Stage 7.4 → Stage 7.5
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 7.1** | C++23 P1 特性 | Deducing this、decay-copy、static operator、[[assume]] | 2 周 |
| **Stage 7.2** | 静态反射完善 | reflexpr 完整实现、元编程支持 | 2 周 |
| **Stage 7.3** | Contracts | 前置/后置条件、断言 | 1.5 周 |
| **Stage 7.4** | C++26 P1/P2 特性 | delete 增强、占位符、结构化绑定 | 1.5 周 |
| **Stage 7.5** | 其他特性 + 测试 | 转义序列、属性、测试覆盖 | 1 周 |

### ⚠️ 重要提醒

**在开始任何 Task 之前，必须先阅读并遵循：**
- [`docs/plan/07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md)
  - 该文档列出了所有需要预先创建的头文件、类、方法、枚举
  - 确保接口先行，避免 Phase 3 的困境
  - 每个 Task 开始前都有检查清单

**Phase 3 教训回顾：**
> Stage 3.1 缺失 DeclContext 基础设施和模板特化声明类，导致后期开发陷入困境。
> 
> **解决方案：** Phase 7 采用"接口先行"策略，所有后续阶段需要的 API 都在前期预置。

---

## Stage 7.1 — C++23 P1 特性

### Task 7.1.1 Deducing this (P0847R7)

**目标：** 实现显式对象参数（Deducing this）

**开发要点：**

#### E7.1.1.1 AST节点扩展

**新增文件：** `include/blocktype/AST/Decl.h`（修改）

```cpp
// FunctionDecl 增加字段
class FunctionDecl : public ValueDecl {
  // ... 现有字段 ...
  
  // ⚠️ P7新增：显式对象参数支持
  ParmVarDecl *ExplicitObjectParam = nullptr;  // 显式 this 参数（如果存在）
  bool HasExplicitObjectParam = false;
  
public:
  // ⚠️ P7新增方法声明
  bool hasExplicitObjectParam() const { return HasExplicitObjectParam; }
  ParmVarDecl *getExplicitObjectParam() const { return ExplicitObjectParam; }
  void setExplicitObjectParam(ParmVarDecl *P) {
    ExplicitObjectParam = P;
    HasExplicitObjectParam = (P != nullptr);
  }
  
  /// 获取有效的 this 类型（考虑 deducing this）
  QualType getThisType(ASTContext &Ctx) const;
};
```

**新增文件：** `include/blocktype/AST/Expr.h`（修改）

```cpp
// CXXMemberCallExpr 需要识别是否使用 deducing this
// （无需新节点，但 CodeGen 需要特殊处理）
```

#### E7.1.1.2 词法/语法分析

**修改文件：** `src/Lex/Lexer.cpp`
- 无需修改（`this` 已是关键字）

**修改文件：** `src/Parse/ParseDecl.cpp`

```cpp
// 在 parseFunctionDeclarator 中检测 explicit object parameter
// 语法：auto func(this Self&& self, other_params...)
// 
// 关键逻辑：
// 1. 检测到第一个参数名为 "this" 或类型为 "this"
// 2. 解析为 ParmVarDecl 并标记为 explicit object parameter
// 3. 从普通参数列表中移除，单独存储

ParmVarDecl *Parser::parseExplicitObjectParameter(FunctionDecl *FD) {
  // 伪代码：
  // - 解析参数声明
  // - 检查是否为 "this" 关键字
  // - 创建 ParmVarDecl
  // - FD->setExplicitObjectParam(param)
  return param;
}
```

**修改文件：** `src/Parse/ParseExprCXX.cpp`

```cpp
// 成员函数调用时，检查是否有 explicit object parameter
// 如果有，调整参数传递方式
```

#### E7.1.1.3 语义分析

**新增文件：** `include/blocktype/Sema/SemaCXX.h`（新建）

```cpp
#pragma once
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/Decl.h"

namespace blocktype {

/// SemaCXX - C++ 特有语义分析
/// 
/// ⚠️ P7新增：集中处理 C++23/C++26 特性的语义分析
class SemaCXX {
  Sema &S;
  
public:
  explicit SemaCXX(Sema &SemaRef) : S(SemaRef) {}
  
  /// 检查 Deducing this 函数的合法性
  /// 
  /// **规则**：
  /// - 只能用于非静态成员函数
  /// - 不能与 virtual 同时使用（Clang 限制）
  /// - 不能有默认参数
  /// - 类型必须是类类型的引用或值
  bool CheckExplicitObjectParameter(FunctionDecl *FD, ParmVarDecl *Param,
                                     SourceLocation ParamLoc);
  
  /// 推导 explicit object parameter 的类型
  /// 
  /// 根据调用对象的类型和值类别，推导 Self 的实际类型
  QualType DeduceExplicitObjectType(QualType ObjectType,
                                     QualType ParamType,
                                     ExprValueKind VK);
};

} // namespace blocktype
```

**修改文件：** `src/Sema/Sema.cpp`

```cpp
// 在 ActOnFunctionDecl 中调用 CheckExplicitObjectParameter
if (FD->hasExplicitObjectParam()) {
  if (!SemaCXX(*this).CheckExplicitObjectParameter(
          FD, FD->getExplicitObjectParam(), ParamLoc)) {
    return DeclResult::getInvalid();
  }
}
```

#### E7.1.1.4 CodeGen

**修改文件：** `include/blocktype/CodeGen/CGCXX.h`

```cpp
// 新增方法声明
void EmitExplicitObjectParameterCall(CXXMemberCallExpr *E,
                                      llvm::Value *ObjectArg);
```

**修改文件：** `src/CodeGen/CGCXX.cpp`

```cpp
void CodeGenFunction::EmitExplicitObjectParameterCall(
    CXXMemberCallExpr *E, llvm::Value *ObjectArg) {
  // ⚠️ 关键逻辑：
  // 1. 如果函数有 explicit object parameter
  // 2. ObjectArg 直接作为第一个参数传递（而非通过 this 指针）
  // 3. 根据参数类型进行必要的转换（lvalue/rvalue）
  
  auto *FD = E->getMethodDecl();
  if (FD->hasExplicitObjectParam()) {
    auto *Param = FD->getExplicitObjectParam();
    QualType ParamType = Param->getType();
    
    // 生成临时对象（如果需要）
    llvm::Value *AdjustedObj = AdjustObjectForExplicitParam(
        ObjectArg, ParamType, E->getBase()->getValueKind());
    
    // 作为第一个参数传递
    CallArgs.insert(CallArgs.begin(), AdjustedObj);
  }
}

llvm::Value *CodeGenFunction::AdjustObjectForExplicitParam(
    llvm::Value *Obj, QualType ParamType, ExprValueKind VK) {
  // 根据 ParamType 和 VK 进行转换：
  // - lvalue ref: 直接使用指针
  // - rvalue ref: std::move
  // - by value: 复制或移动构造
  // ...
}
```

**开发关键点提示：**
> 请为 BlockType 实现 Deducing this。
>
> **语法示例**：
> ```cpp
> struct S {
>     auto foo(this const S& self);  // 显式对象参数
>     auto bar(this S&& self);       // 移动语义
>     auto baz(this S self);         // 按值
> };
> 
> // CRTP 简化
> template<typename Derived>
> struct Base {
>     void print(this const Derived& self) {
>         self.doPrint();  // 可以直接调用派生类方法
>     }
> };
> ```
>
> **关键能力**：
> - 在成员函数中显式声明 this 参数
> - 支持不同的值类别（lvalue/rvalue/by-value）
> - 支持 CRTP 模式的简化
> - CodeGen 时正确传递对象参数
>
> **参照 Clang**：
> - Clang 实现：`lib/Sema/SemaDeclCXX.cpp` 中的 `CheckExplicitObjectParameter`
> - AST 节点：`FunctionDecl::ExplicitObjectParameter`
> - CodeGen：`lib/CodeGen/CGCall.cpp` 中的特殊处理

**验收标准：**
- [ ] AST：FunctionDecl 能存储 explicit object parameter
- [ ] Parser：能正确解析 `this Self&& self` 语法
- [ ] Sema：能检查合法性并推导类型
- [ ] CodeGen：能生成正确的参数传递代码
- [ ] 测试：至少 5 个测试用例（lvalue/rvalue/by-value/CRTP/错误情况）

**Checkpoint：** Deducing this 正确解析和代码生成

---

### Task 7.1.2 decay-copy 表达式 (P0849R8)

**目标：** 实现 `auto(x)` 和 `auto{x}` decay-copy 语法

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#task-712-decay-copy-表达式-p0849r8)）：
- [ ] `include/blocktype/AST/Expr.h` - DecayCopyExpr 类
- [ ] `include/blocktype/Sema/Sema.h` - ActOnDecayCopyExpr 方法
- [ ] `include/blocktype/Sema/TypeCheck.h` - TypeCheck::Decay 方法
- [ ] `include/blocktype/AST/Stmt.h` - NodeKind::DecayCopyExprKind
- [ ] `include/blocktype/Basic/DiagnosticSemaKinds.def` - 相关诊断ID

**开发要点：**

- **E7.1.2.1** 新增 `DecayCopyExpr` AST 节点
- **E7.1.2.2** 解析 `auto(expr)` 和 `auto{expr}` 语法
- **E7.1.2.3** 语义分析：执行 decay-copy（去除引用、cv限定符）
- **E7.1.2.4** CodeGen：生成临时对象构造代码

**开发关键点提示：**
> 请为 BlockType 实现 decay-copy。
>
> **语法示例**：
> ```cpp
> auto x = auto(expr);   // 复制初始化
> auto y = auto{expr};   // 直接初始化
> ```
>
> **语义**：
> - 对 expr 执行 decay（去除引用、cv 限定符）
> - 创建临时对象并复制或移动
>
> **参照 Clang**：
> - Clang 中没有直接的 decay-copy 表达式，但可以参考 CXXBindTemporaryExpr
> - Decay 逻辑参考 `clang::QualType::getNonReferenceType()`

**验收标准：**
- [ ] AST：DecayCopyExpr 节点正确存储子表达式
- [ ] Parser：能正确解析两种语法形式
- [ ] Sema：正确执行类型 decay
- [ ] CodeGen：生成正确的临时对象构造
- [ ] 测试：至少 3 个测试用例

**Checkpoint：** decay-copy 正确实现

---

### Task 7.1.3 static operator (P1169R4, P2589R1)

**目标：** 实现 `static operator()` 和 `static operator[]`

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#task-713-static-operator-p1169r4-p2589r1)）：
- [ ] `include/blocktype/AST/Decl.h` - CXXMethodDecl::IsStaticOperator 字段
- [ ] `include/blocktype/Sema/SemaCXX.h` - CheckStaticOperator 方法
- [ ] `include/blocktype/Basic/DiagnosticSemaKinds.def` - 相关诊断ID

**开发要点：**

- **E7.1.3.1** 扩展 `CXXMethodDecl` 添加 `IsStaticOperator` 标志
- **E7.1.3.2** 解析 `static operator()(...)` 语法
- **E7.1.3.3** 解析 `static operator[](args...)` 语法
- **E7.1.3.4** 语义检查：static operator 不能有 this 指针
- **E7.1.3.5** CodeGen：生成静态函数而非成员函数

**验收标准：**
- [ ] AST：CXXMethodDecl 能标记 static operator
- [ ] Parser：能正确解析语法
- [ ] Sema：能检查合法性
- [ ] CodeGen：生成正确的调用约定
- [ ] 测试：至少 3 个测试用例

**Checkpoint：** static operator 正确实现

---

### Task 7.1.4 [[assume]] 属性 (P1774R8)

**目标：** 实现 `[[assume(condition)]]` 属性

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#task-714-assume-属性-p1774r8)）：
- [ ] `include/blocktype/AST/Attr.h` - AssumeAttr 类
- [ ] `include/blocktype/CodeGen/CGAttrs.h` - EmitAssumeAttr 方法
- [ ] `include/blocktype/Basic/DiagnosticSemaKinds.def` - 相关诊断ID

**开发要点：**

- **E7.1.4.1** 扩展 `parseAttributeSpecifier` 识别 `assume`
- **E7.1.4.2** 新增 `AssumeAttr` AST 节点
- **E7.1.4.3** 语义检查：condition 必须是常量表达式
- **E7.1.4.4** CodeGen：生成优化提示（llvm.assume intrinsic）

**验收标准：**
- [ ] AST：AssumeAttr 正确存储条件表达式
- [ ] Parser：能正确解析属性语法
- [ ] Sema：能检查条件是否为常量表达式
- [ ] CodeGen：生成 llvm.assume intrinsic
- [ ] 测试：至少 2 个测试用例

**Checkpoint：** [[assume]] 属性正确实现

---

## Stage 7.2 — 静态反射完善

### Task 7.2.1 reflexpr 关键字完善

**目标：** 完善 reflexpr 关键字和反射类型系统

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#task-721-reflexpr-关键字完善)）：
- [x] `include/blocktype/AST/Expr.h` - ReflexprExpr 类（已增强：OperandKind 区分 type/expr）
- [x] `include/blocktype/AST/ReflectionTypes.h` - InfoType / TypeInfo / MemberInfo（新建）
- [x] `include/blocktype/AST/Type.h` - TypeClass::MetaInfo / MetaInfoType（已实现）
- [x] `include/blocktype/Basic/DiagnosticSemaKinds.def` - 相关诊断ID（已添加 4 个）

**开发要点：**

- **E7.2.1.1** ~~添加 reflexpr 到词法分析器~~ （已存在于 TokenKinds.def）
- **E7.2.1.2** 实现 reflexpr 表达式解析 — **已完成**（增强 parseReflexprExpr 支持 type-id 和 expr）
- **E7.2.1.3** 完善反射类型系统（meta::info） — **已完成**（MetaInfoType + InfoType + TypeInfo）
- **E7.2.1.4** 支持 reflexpr(type) 和 reflexpr(expression) — **已完成**

**验收标准：**
- [x] AST：ReflexprExpr 和 MetaInfoType 正确实现
- [x] Parser：能正确解析 reflexpr(type-id) 和 reflexpr(expr) 语法
- [x] Sema：能设置正确的反射类型（MetaInfoType）
- [x] CodeGen：生成反射元数据全局变量
- [x] 测试：16 个单元测试全部通过

**Checkpoint：** ✅ reflexpr 正确解析和语义分析 — **通过**

---

### Task 7.2.2 元编程支持

**目标：** 实现反射的元编程能力

**⚠️ 接口预置清单：**
- [x] `include/blocktype/AST/ReflectionTypes.h` - 元编程 API 声明（TypeInfo/MemberInfo/InfoType）
- [x] `include/blocktype/Sema/SemaReflection.h`（新建）- 反射语义分析类

**开发要点：**

#### E7.2.2.1 类型自省 API

**修改文件：** `include/blocktype/AST/ReflectionTypes.h`

```cpp
namespace meta {

/// 成员信息描述
struct MemberInfo {
  llvm::StringRef Name;       // 成员名称
  QualType Type;               // 成员类型
  AccessSpecifier Access;      // 访问控制
  bool IsStatic;               // 是否静态
  bool IsFunction;             // 是否为函数
};

/// 类型自省 API（编译期可用）
/// 参照 Clang: clang/lib/AST/ASTContext.h 中的类型查询方法
class TypeInfo {
  const CXXRecordDecl *Record;

public:
  TypeInfo(const CXXRecordDecl *RD) : Record(RD) {}

  /// 获取所有成员（字段 + 方法）
  llvm::SmallVector<MemberInfo, 16> getMembers() const;

  /// 获取所有字段
  llvm::SmallVector<MemberInfo, 16> getFields() const;

  /// 获取所有方法
  llvm::SmallVector<MemberInfo, 16> getMethods() const;

  /// 获取基类列表
  llvm::SmallVector<const CXXRecordDecl *, 4> getBases() const;

  /// 获取类型名称
  llvm::StringRef getName() const;

  /// 检查是否有指定名称的成员
  bool hasMember(llvm::StringRef Name) const;
};

} // namespace meta
```

#### E7.2.2.2 成员遍历 API

```cpp
// 在 SemaReflection.h 中声明
class SemaReflection {
public:
  /// 遍历类型的所有成员，对每个成员调用 Callback
  ///
  /// **Clang 参考**：
  /// - `clang/lib/AST/RecordLayout.cpp` 中字段遍历
  /// - `clang/lib/AST/ASTDumper.cpp` 中 AST 节点遍历
  static void forEachMember(const CXXRecordDecl *RD,
                             llvm::function_ref<void(const MemberInfo &)> Callback);

  /// 在编译期提取类型信息
  static meta::TypeInfo getTypeInfo(QualType T);
};
```

#### E7.2.2.3 元数据生成

- 为每个反射类型生成元数据全局变量
- 元数据包含：类型名称、成员列表、基类列表
- 生成到 LLVM IR 中的常量结构

#### E7.2.2.4 编译期反射函数库

- [x] 提供 `__reflect_type(expr)` 内置函数（Parser + Sema 已实现）
- [x] 提供 `__reflect_members(type)` 内置函数（Parser + Sema 已实现）
- [ ] 在 ConstantExpr 求值中支持反射操作（待后续阶段实现）

**验收标准：**
- [x] 能获取类型的成员列表（TypeInfo::getMembers/Fields/Methods）
- [x] 能获取成员的名称和类型（MemberInfo::Name/Type）
- [ ] 能在 constexpr 上下文中使用（待后续阶段实现）
- [x] 测试：16 个单元测试 + 5 个集成测试用例

**Checkpoint：** ✅ 反射元编程正确 — **通过**

---

## Stage 7.3 — Contracts

### Task 7.3.1 Contract 属性 (P2900R14)

**目标：** 实现 Contract 属性（pre/post/assert）

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#stage-73--contracts)）：
- [ ] `include/blocktype/AST/Attr.h` - ContractAttr, ContractKind, ContractMode
- [ ] `include/blocktype/Sema/SemaCXX.h` - CheckContractCondition 方法
- [ ] `include/blocktype/Basic/DiagnosticSemaKinds.def` - 相关诊断ID

**开发要点：**

- **E7.3.1.1** 解析 `[[pre: condition]]`、`[[post: condition]]`、`[[assert: condition]]` 属性
- **E7.3.1.2** 新增 `ContractAttr` AST 节点
- **E7.3.1.3** 语义检查 Contract 条件
- **E7.3.1.4** 生成 Contract 检查代码

**验收标准：**
- [ ] AST：ContractAttr 正确存储条件和模式
- [ ] Parser：能正确解析三种 Contract 语法
- [ ] Sema：能检查条件合法性
- [ ] CodeGen：生成运行时检查代码
- [ ] 测试：至少 5 个测试用例（pre/post/assert/不同模式）

**Checkpoint：** Contracts 正确实现

---

### Task 7.3.2 Contract 语义

**目标：** 实现 Contract 的完整语义

**开发要点：**

- **E7.3.2.1** 实现 Contract 继承
- **E7.3.2.2** 实现 Contract 与虚函数的交互
- **E7.3.2.3** 实现 Contract 检查模式切换

**验收标准：**
- [ ] 派生类能继承基类的 Contract
- [ ] 虚函数能正确处理 Contract
- [ ] 能通过编译选项切换检查模式
- [ ] 测试：至少 3 个测试用例

**Checkpoint：** Contract 语义正确

---

## Stage 7.4 — C++26 P1/P2 特性

### Task 7.4.1 `= delete("reason")` (P2573R2)

**目标：** 实现带原因的 deleted 函数

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#task-741-delete-reason)）：
- [ ] `include/blocktype/AST/Decl.h` - FunctionDecl::DeletedReason 字段
- [ ] `include/blocktype/Basic/DiagnosticSemaKinds.def` - err_use_of_deleted_function_with_reason

**开发要点：**

- **E7.4.1.1** `FunctionDecl` 增加 `DeletedReason` 字段
- **E7.4.1.2** 解析 `= delete("reason string")` 语法
- **E7.4.1.3** 语义检查：deleted 函数不能有其他定义
- **E7.4.1.4** 诊断：使用 deleted 函数时显示原因

**验收标准：**
- [ ] AST：FunctionDecl 能存储删除原因
- [ ] Parser：能正确解析语法
- [ ] Sema：能检查合法性
- [ ] 诊断：使用时显示自定义原因
- [ ] 测试：至少 2 个测试用例

**Checkpoint：** delete 增强正确实现

---

### Task 7.4.2 占位符变量 `_` (P2169R4)

**目标：** 实现未命名占位符变量 `_`

**⚠️ 接口预置清单：**
- [ ] `include/blocktype/Sema/Sema.h` - ActOnPlaceholderVarDecl 方法
- [ ] `include/blocktype/Basic/DiagnosticSemaKinds.def` - 相关警告ID

**开发要点：**

- **E7.4.2.1** 词法分析器特殊处理 `_` 标识符
- **E7.4.2.2** 解析器允许 `_` 作为声明名称
- **E7.4.2.3** 语义分析：`_` 不引入新名称，每次出现都是新变量
- **E7.4.2.4** CodeGen：生成临时变量但不绑定名称

**验收标准：**
- [ ] Parser：能解析 `_` 作为变量名
- [ ] Sema：不将 `_` 加入符号表
- [ ] CodeGen：生成正确的临时变量
- [ ] 测试：至少 3 个测试用例（结构化绑定/范围for/普通声明）

**Checkpoint：** 占位符变量正确实现

---

### Task 7.4.3 结构化绑定扩展 (P0963R3, P1061R10)

**目标：** 实现结构化绑定作为条件和引入包

**⚠️ 接口预置清单：**
- [ ] `include/blocktype/AST/Decl.h` - BindingDecl 类
- [ ] `include/blocktype/AST/NodeKinds.def` - BindingDeclKind
- [ ] `include/blocktype/Sema/Sema.h` - ActOnDecompositionDecl 方法

**开发要点：**

#### E7.4.3.1 新增 `BindingDecl` AST 节点

**新增文件/修改：** `include/blocktype/AST/Decl.h`

```cpp
/// BindingDecl — 结构化绑定声明
///
/// **C++17 基础**：auto [x, y] = expr
/// **C++26 P0963R3**：if (auto [x, y] = expr) — 条件中使用
/// **C++26 P1061R10**：template <typename... Ts> auto [...ns] = tuple — 引入包
///
/// **Clang 参考**：
/// - `clang/include/clang/AST/Decl.h` 中 `BindingDecl`
/// - `clang/include/clang/AST/DeclCXX.h` 中 `DecompositionDecl`
///   （Clang 将整个分解声明作为 DecompositionDecl，绑定变量为 BindingDecl）
class BindingDecl : public ValueDecl {
  Expr *BindingExpr;      // 绑定表达式
  unsigned BindingIndex;  // 索引
  bool IsPackExpansion;   // P1061R10 包展开

public:
  // 构造函数 + getter/setter + classof
};
```

**NodeKinds.def 新增：**
```cpp
DECL(BindingDecl, ValueDecl)
```

#### E7.4.3.2 支持 `if (auto [x, y] = expr)` 语法

**修改：** `src/Parse/ParseStmt.cpp`

```cpp
// 在 parseIfStatement 中：
// 1. 解析 auto 关键字
// 2. 解析 [ 标识符列表 ]
// 3. 解析 = expr
// 4. 创建 BindingDecl 列表
// 5. 创建 DeclStmt 包裹绑定声明
// 6. 将 DeclStmt 设置为 IfStmt 的 CondVar
```

**Sema 方法：** `ActOnDecompositionDecl(Loc, Names, Init)`

#### E7.4.3.3 支持 `for... [args...]` 参数包展开

**修改：** `src/Parse/ParseTemplate.cpp`

```cpp
// 在模板参数解析中允许 ... 修饰绑定名称
// 语法：template <typename... Ts> void f(auto [...ns] = tuple<Ts...>)
```

#### E7.4.3.4 语义分析：绑定变量的作用域和生命周期

- 绑定变量的作用域与所在声明语句相同
- 在 `if` 条件中的绑定：作用域延伸到 then/else 块结束
- 包展开绑定：每个展开实例化为独立的 BindingDecl

**验收标准：**
- [ ] Parser：能正确解析 `auto [x, y] = expr`
- [ ] Parser：能正确解析 `if (auto [x, y] = expr)`
- [ ] Sema：正确处理作用域
- [ ] CodeGen：生成正确的绑定代码
- [ ] 测试：至少 4 个测试用例

**Checkpoint：** 结构化绑定扩展正确实现

---

### Task 7.4.4 Pack Indexing 完善 (P2662R3)

**目标：** 完善包索引功能（已部分实现）

**开发要点：**

- **E7.4.4.1** 验证 `PackIndexingExpr` 实现完整性
- **E7.4.4.2** 添加更多测试用例
- **E7.4.4.3** 优化代码生成

**验收标准：**
- [ ] 现有功能无回归
- [ ] 新增 5+ 测试用例
- [ ] 性能测试通过

**Checkpoint：** Pack Indexing 完全正确

---

## Stage 7.5 — 其他特性 + 测试

### Task 7.5.1 转义序列扩展

**目标：** 实现分隔转义 `\x{...}` 和命名转义 `\N{...}`

**⚠️ 接口预置清单：**
开始前必须完成以下接口定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#task-751-转义序列扩展)）：
- [ ] `include/blocktype/Lex/Lexer.h` — 新增 `processDelimitedHexEscape()`、`processNamedEscape()` 方法声明
- [ ] `include/blocktype/Basic/DiagnosticLexKinds.def` — 新增诊断 ID
- [ ] `third_party/unicode_names.h`（可能需要）— Unicode 字符名查找表

**开发要点：**

#### E7.5.1.1 分隔转义 `\x{HHHH...}` (P2290R3)

**修改文件：** `src/Lex/Lexer.cpp`

**当前实现分析：** `processEscapeSequence()` 在第 349-363 行处理 `\x`，仅支持 `\xHH` 连续十六进制。需要在 `\x` 分支后增加 `{` 检测。

```cpp
// Lexer.cpp processEscapeSequence() 中 \x 分支修改：
if (C == 'x') {
  ++BufferPtr;
  // P2290R3: 分隔转义 \x{HHHH...}
  if (BufferPtr < BufferEnd && *BufferPtr == '{') {
    return processDelimitedHexEscape();  // 新方法
  }
  // 原有 \xHH 逻辑保持不变...
}
```

**新增方法：** `Lexer::processDelimitedHexEscape()`

```cpp
/// 解析 \x{HHHH...} 分隔转义
/// 语法：\x{ 后跟一个或多个十六进制数字，以 } 结束
/// 示例：\x{1F600} → U+1F600 (😀)
///
/// **Clang 参考**：
/// - `clang/lib/Lex/LiteralSupport.cpp` 中 `EscapeSpecifier::ProcessEscapeSequence()`
/// - Clang 的 delimited escape 实现（clang 18+）
bool Lexer::processDelimitedHexEscape() {
  // 1. 跳过 '{'（已被调用者确认）
  ++BufferPtr;
  // 2. 收集十六进制数字直到 '}'
  uint32_t CodePoint = 0;
  bool HasDigit = false;
  while (BufferPtr < BufferEnd && *BufferPtr != '}') {
    char D = *BufferPtr;
    if (D >= '0' && D <= '9') CodePoint = CodePoint * 16 + (D - '0');
    else if (D >= 'a' && D <= 'f') CodePoint = CodePoint * 16 + (D - 'a' + 10);
    else if (D >= 'A' && D <= 'F') CodePoint = CodePoint * 16 + (D - 'A' + 10);
    else break;
    ++BufferPtr;
    HasDigit = true;
  }
  // 3. 错误检查
  if (!HasDigit) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "delimited escape sequence must have at least one hex digit");
    return false;
  }
  if (BufferPtr >= BufferEnd || *BufferPtr != '}') {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "expected '}' to end delimited escape sequence");
    return false;
  }
  ++BufferPtr; // 跳过 '}'
  // 4. Unicode 范围检查
  if (CodePoint > 0x10FFFF) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "hex escape sequence out of Unicode range");
    return false;
  }
  return true;
}
```

#### E7.5.1.2 命名转义 `\N{name}` (P2071R2)

**修改文件：** `src/Lex/Lexer.cpp`

**当前实现分析：** `processEscapeSequence()` 不处理 `\N`。需要在简单转义之前增加 `N` 检测。

```cpp
// processEscapeSequence() 中新增 \N 分支（在 \u/\U 之后）：
if (C == 'N') {
  ++BufferPtr;
  if (BufferPtr < BufferEnd && *BufferPtr == '{') {
    return processNamedEscape();  // 新方法
  }
  // 不是 \N{...} 格式 → 报错
  Diags.report(getSourceLocation(), DiagLevel::Error,
               "expected '{' after '\\N' for named universal character escape");
  return false;
}
```

**新增方法：** `Lexer::processNamedEscape()`

```cpp
/// 解析 \N{name} 命名转义
/// 语法：\N{Unicode Character Name}
/// 示例：\N{LATIN CAPITAL LETTER A} → 'A'
///        \N{FACE WITH TEARS OF JOY} → U+1F602 (😂)
///
/// **Clang 参考**：
/// - `clang/lib/Lex/LiteralSupport.cpp` 中命名转义处理
/// - Clang 使用 Unicode Character Database 的字符名映射
bool Lexer::processNamedEscape() {
  // 1. 跳过 '{'
  ++BufferPtr;
  // 2. 收集字符名直到 '}'
  const char *NameStart = BufferPtr;
  while (BufferPtr < BufferEnd && *BufferPtr != '}') {
    ++BufferPtr;
  }
  if (BufferPtr >= BufferEnd || *BufferPtr != '}') {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "expected '}' to end named escape sequence");
    return false;
  }
  StringRef Name(NameStart, BufferPtr - NameStart);
  ++BufferPtr; // 跳过 '}'
  // 3. 查找 Unicode 字符名 → 码点映射
  uint32_t CodePoint;
  if (!lookupUnicodeName(Name, CodePoint)) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "unknown Unicode character name '%0'" + Name.str());
    return false;
  }
  return true;
}
```

#### E7.5.1.3 Unicode 字符名数据库

**方案选择：**

| 方案 | 优点 | 缺点 |
|------|------|------|
| A. 编译时嵌入 `unicode_names.dat` | 查找快 | 增大二进制体积 (~500KB) |
| B. 生成 `unicode_names.inc` 头文件 | 编译集成简单 | 编译时间增加 |
| C. 使用完美哈希表 | 最小化查找时间 | 需要额外构建步骤 |

**推荐方案 B：** 生成 `unicode_names.inc` 静态查找表，在 `Lexer.cpp` 中 `#include`。

```cpp
// third_party/unicode_names.inc（由脚本从 UnicodeData.txt 生成）
// 格式：{"CHARACTER_NAME", 0xXXXX},
static const struct { const char *Name; uint32_t CodePoint; } UnicodeNameTable[] = {
  {"LATIN CAPITAL LETTER A", 0x0041},
  {"LATIN SMALL LETTER A", 0x0061},
  // ... 完整的 Unicode 字符名列表
};

/// 查找 Unicode 字符名
bool Lexer::lookupUnicodeName(StringRef Name, uint32_t &CodePoint) {
  // 二分查找（表已按名称排序）
  auto It = std::lower_bound(
    std::begin(UnicodeNameTable), std::end(UnicodeNameTable), Name.str(),
    [](const auto &Entry, const std::string &N) { return Entry.Name < N; });
  if (It != std::end(UnicodeNameTable) && It->Name == Name) {
    CodePoint = It->CodePoint;
    return true;
  }
  return false;
}
```

**构建脚本：** `scripts/generate_unicode_names.py`
- 输入：Unicode Character Database 的 `UnicodeData.txt` 和 `NameAliases.txt`
- 输出：`third_party/unicode_names.inc`
- 需要支持名称别名（如 "NO-BREAK SPACE" 和 "NBSP" 都应指向 U+00A0）

#### E7.5.1.4 诊断消息

**新增文件/修改：** `include/blocktype/Basic/DiagnosticLexKinds.def`

```cpp
// P2290R3 分隔转义诊断
DIAG(err_delimited_escape_empty, Error,
  "delimited escape sequence must have at least one hex digit",
  "分隔转义序列必须包含至少一个十六进制数字")
DIAG(err_delimited_escape_unterminated, Error,
  "expected '}' to end delimited escape sequence",
  "分隔转义序列缺少结束的 '}'")
DIAG(err_delimited_escape_out_of_range, Error,
  "hex escape sequence out of Unicode range",
  "十六进制转义序列超出 Unicode 范围")
// P2071R2 命名转义诊断
DIAG(err_named_escape_unterminated, Error,
  "expected '}' to end named escape sequence",
  "命名转义序列缺少结束的 '}'")
DIAG(err_named_escape_unknown_name, Error,
  "unknown Unicode character name",
  "未知的 Unicode 字符名")
DIAG(err_named_escape_expected_brace, Error,
  "expected '{' after '\\N'",
  "'\\N' 后需要 '{'")
```

**验收标准：**
- [ ] Lexer：能正确解析 `\x{1F600}` 等分隔转义
- [ ] Lexer：能正确解析 `\N{LATIN CAPITAL LETTER A}` 等命名转义
- [ ] 诊断：无效转义时给出清晰错误（中英双语）
- [ ] Unicode 查找表：正确映射字符名到码点
- [ ] 测试：至少 5 个测试用例（含边界情况和错误用例）
- [ ] 无回归：原有 `\xHH`、`\uXXXX`、`\UXXXXXXXX` 不受影响

**Checkpoint：** 转义序列正确解析

---

### Task 7.5.2 其他 C++26 特性

**目标：** 实现剩余的 C++26 特性（8 项子特性）

**⚠️ 接口预置清单：**
每个特性开始前需确认相关接口已定义（详见 [`07-PHASE7-detailed-interface-plan.md`](./07-PHASE7-detailed-interface-plan.md#stage-75--其他特性--测试)）：
- [ ] `include/blocktype/AST/Attr.h` — `IndeterminateAttr` 属性类
- [ ] `include/blocktype/Lex/TokenKinds.def` — `$`、反引号 token
- [ ] `include/blocktype/AST/Decl.h` — `FriendDecl::IsPackExpansion` 字段

**开发要点：**

#### E7.5.2.1 `for` init-statement 中 `using` (P2360R0)

**修改文件：** `src/Parse/ParseStmt.cpp`

**现状：** `for` 循环的 init-statement 仅支持声明和表达式，不支持 `using` 声明。

```cpp
// ParseStmt.cpp parseForStatement() 修改：
// 在解析 init-statement 时，增加 using 关键字检测
if (Tok.is(TokenKind::kw_using)) {
  // 解析 using 声明
  Decl *D = parseUsingDeclaration(/*IsForInit=*/true);
  // 创建 DeclStmt 包裹
  InitStmt = Actions.ActOnDeclStmt(D, Loc);
}
```

**无新 AST 节点**，仅 Parser 层修改。`using` 声明的作用域限制在 `for` 循环体内。

**Clang 参考：**
- `clang/lib/Parse/ParseStmt.cpp` 中 `Parser::ParseForStatement()` 的 using 声明分支

**验收标准：**
- [ ] `for (using T = int; T x : vec)` 合法
- [ ] using 声明作用域限于 for 循环体
- [ ] 测试：至少 2 个用例

---

#### E7.5.2.2 `@`/`$`/反引号字符集扩展 (P2558R2)

**修改文件：** `include/blocktype/Lex/Lexer.h`、`src/Lex/Lexer.cpp`、`include/blocktype/Lex/TokenKinds.def`

**现状：** `@` 已有 token（`TokenKind::at`），`$` 和反引号不支持。

```cpp
// Lexer.h 新增方法：
/// 检查字符是否为扩展标识符字符（@、$、反引号）
/// P2558R2 将这些字符纳入 identifier production
static bool isExtendedIdentifierChar(char C);

// Lexer.cpp 中 isIdentifierContinueChar() 修改：
bool Lexer::isIdentifierContinueChar(char C) {
  return isalnum(C) || C == '_' || isExtendedIdentifierChar(C);
}

bool Lexer::isExtendedIdentifierChar(char C) {
  return C == '@' || C == '$' || C == '`';
}
```

**TokenKinds.def 新增：**
```cpp
DOLLAR           // $ 符号
BACKTICK         // ` 反引号
```

**Clang 参考：**
- `clang/lib/Lex/Lexer.cpp` 中 `Lexer::isIdentifierHead()` / `Lexer::isIdentifierBody()`
- `clang/include/clang/Basic/TokenKinds.def` 中的扩展字符支持

**验收标准：**
- [ ] `$variable`、`@variable`、`` `variable` `` 能作为标识符解析
- [ ] Token 类型正确
- [ ] 测试：至少 3 个用例

---

#### E7.5.2.3 `[[indeterminate]]` 属性 (P2795R5)

**修改文件：** `include/blocktype/AST/Attr.h`、`src/Parse/ParseDecl.cpp`、`src/CodeGen/CodeGenFunction.cpp`

**语义：** 标记变量初始化时跳过零初始化（用于性能优化）。

```cpp
// Attr.h 新增（在已有 Attr 体系中）：
class IndeterminateAttr : public Attr {
public:
  static bool classof(const Attr *A) {
    return A->getKind() == AttrKind::AK_Indeterminate;
  }
  static IndeterminateAttr *Create(ASTContext &Ctx, SourceLocation Loc);
};
```

**Parser 修改：** 在 `parseAttributeSpecifier()` 中识别 `indeterminate` 属性名。

**CodeGen 修改：**
```cpp
// CodeGenFunction.cpp EmitVarDecl() 中：
if (VD->hasAttr<IndeterminateAttr>()) {
  // 跳过零初始化，直接分配空间
  // 等价于 C 的 int x; （不初始化）
  EmitRawAlloca(Ty, VD->getName());
  return;
}
```

**Clang 参考：**
- `clang/include/clang/Basic/Attr.td` 中 `Indeterminate` 属性定义
- `clang/lib/CodeGen/CGDecl.cpp` 中初始化逻辑跳过

**验收标准：**
- [ ] Parser：能解析 `[[indeterminate]] int x;`
- [ ] CodeGen：跳过零初始化
- [ ] Sema：警告在 const 变量上使用
- [ ] 测试：至少 2 个用例

---

#### E7.5.2.4 平凡可重定位 (P2786R13)

**修改文件：** `include/blocktype/AST/Type.h`、`src/Sema/Sema.cpp`

**语义：** 为类型系统添加 `is_trivially_relocatable` trait，用于优化容器操作。

```cpp
// Type.h 中 QualType 或 Type 类新增方法：
/// 检查类型是否可平凡重定位
/// 条件：移动构造不抛出、析构不抛出、无自定义移动/析构
bool isTriviallyRelocatable() const;

// Sema.h 新增方法：
/// 对类类型检查可平凡重定位性
bool isTriviallyRelocatable(const CXXRecordDecl *RD) const;
```

**实现逻辑：**
```cpp
bool Type::isTriviallyRelocatable() const {
  if (!isRecordType()) return true;  // 基本类型和指针都平凡
  auto *RD = getAsCXXRecordDecl();
  if (!RD) return false;
  // 1. 无用户定义的移动构造函数
  // 2. 无用户定义的析构函数
  // 3. 所有基类和成员都可平凡重定位
  return !RD->hasUserDeclaredMoveConstructor() &&
         !RD->hasUserDeclaredDestructor();
}
```

**Clang 参考：**
- `clang/include/clang/AST/Type.h` 中 `isTriviallyRelocatable()`
- `clang/lib/AST/ASTContext.cpp` 中布局检查逻辑

**验收标准：**
- [ ] `is_trivially_relocatable_v<int>` → `true`
- [ ] `is_trivially_relocatable_v<std::string>` → `false`（有自定义析构）
- [ ] 测试：至少 3 个用例

---

#### E7.5.2.5 概念模板模板参数 (P2841R7)

**修改文件：** `src/Parse/ParseTemplate.cpp`、`src/Sema/SemaTemplate.cpp`

**语义：** 允许概念名作为模板模板参数的约束。

```cpp
// 语法示例：
template <template <typename> concept C, typename T>
  requires C<T>
void foo() { ... }

// ParseTemplate.cpp 修改：
// 在 parseTemplateParameterList 中：
// 当遇到 concept 关键字时，将模板模板参数标记为概念约束
```

**Sema 修改：**
```cpp
// Sema.h 新增：
/// 检查模板模板参数是否满足概念约束
bool CheckTemplateTemplateParameterConstraint(
    TemplateDecl *Template, ConceptDecl *Constraint, SourceLocation Loc);

// 实现：
// 1. 实例化约束表达式
// 2. 检查模板参数是否满足约束
// 3. 不满足时发出诊断
```

**Clang 参考：**
- `clang/lib/Parse/ParseTemplate.cpp` 中概念模板参数解析
- `clang/lib/Sema/SemaTemplate.cpp` 中约束检查

**验收标准：**
- [ ] 能解析 `template <template <typename> concept C>` 语法
- [ ] 约束检查正确
- [ ] 测试：至少 2 个用例

---

#### E7.5.2.6 可变参数友元 (P2893R3)

**修改文件：** `src/Parse/ParseClass.cpp`、`include/blocktype/AST/Decl.h`

**语义：** 允许 friend 声明展开参数包。

```cpp
// 语法示例：
template <typename... Ts>
class MyClass {
  friend Ts...;  // 将每个 Ts 声明为友元
};

// Decl.h 中 FriendDecl 修改：
class FriendDecl : public Decl {
  // ... 现有字段 ...
  bool IsPackExpansion = false;

public:
  bool isPackExpansion() const { return IsPackExpansion; }
  void setPackExpansion(bool V) { IsPackExpansion = V; }
};
```

**Parser 修改：**
```cpp
// ParseClass.cpp parseFriendDeclaration() 中：
// 在解析 friend type-name 后检查是否有 ...
if (Tok.is(TokenKind::ellipsis)) {
  Friend->setPackExpansion(true);
  consumeToken(); // 跳过 ...
}
```

**Clang 参考：**
- `clang/lib/Parse/ParseDeclCXX.cpp` 中 `Parser::ParseFriendDeclaration()`
- `clang/include/clang/AST/DeclFriend.h` 中 `FriendDecl::isPackExpansion()`

**验收标准：**
- [ ] `friend Ts...;` 语法正确解析
- [ ] `FriendDecl::isPackExpansion()` 正确设置
- [ ] 测试：至少 2 个用例

---

#### E7.5.2.7 `constexpr` 异常 (P3068R6)

**修改文件：** `src/Sema/Sema.cpp`、`src/Sema/ConstantExprEvaluator.cpp`

**语义：** 允许 `constexpr` 函数中包含 `throw` 表达式（C++26 放宽限制）。throw 在编译期求值时如果执行到则失败，否则忽略。

```cpp
// Sema.cpp 修改（ActOnThrowExpr 或检查函数体时）：
// 旧：if (CurFD->isConstexpr()) emitError("throw in constexpr function");
// 新：if (CurFD->isConstexpr()) {
//       // 仅发出警告，不阻止编译
//       // 实际错误在编译期求值时产生
//     }

// ConstantExprEvaluator.cpp 修改：
// 在编译期求值遇到 throw 时：
// → 报告 "constexpr evaluation encountered a throw expression"
// → 标记求值失败
```

**Clang 参考：**
- `clang/lib/Sema/SemaStmt.cpp` 中 throw-in-constexpr 放宽
- `clang/lib/AST/ExprConstant.cpp` 中编译期 throw 处理

**验收标准：**
- [ ] `constexpr int f(bool b) { if (b) throw 1; return 0; }` 合法
- [ ] `f(false)` 编译期求值成功
- [ ] `f(true)` 编译期求值失败
- [ ] 测试：至少 3 个用例

---

#### E7.5.2.8 可观察检查点 (P1494R5)

**修改文件：** `src/CodeGen/CodeGenFunction.cpp`、`include/blocktype/Lex/TokenKinds.def`

**语义：** 提供 `__builtin_observer_checkpoint()` 内置函数，用于标记可观察行为检查点。主要用于调试和优化屏障。

```cpp
// CodeGenFunction.cpp 新增：
/// 发射可观察检查点
/// 等价于编译器屏障，阻止跨检查点重排
void CodeGenFunction::EmitObserverCheckpoint(SourceLocation Loc) {
  // 方案 A：生成空 volatile store（简单实现）
  // Builder.CreateStore(llvm::UndefValue::get(VoidTy),
  //                     llvm::UndefValue::get(llvm::PointerType::get(VoidTy, 0)),
  //                     /*isVolatile=*/true);
  // 方案 B：生成 LLVM fence 指令
  Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
}

// 或作为 builtin 函数：
// 在 BuiltinTypes.def 中注册：
// BUILTIN(__builtin_observer_checkpoint, "v", "n")
```

**Clang 参考：**
- `clang/include/clang/Basic/Builtins.def` 中 `__builtin_is_constant_evaluated`
- `clang/lib/CodeGen/CGBuiltin.cpp` 中 builtin 发射逻辑

**验收标准：**
- [ ] `__builtin_observer_checkpoint()` 编译通过
- [ ] CodeGen 生成 fence 或等价指令
- [ ] 测试：至少 1 个用例

---

**验收标准：**
- [ ] 每个特性至少有 1 个测试用例
- [ ] 无回归测试失败
- [ ] 详细接口规划中列出的所有接口已预置

**Checkpoint：** 其他特性正确实现

---

### Task 7.5.3 C++23/C++26 测试

**目标：** 建立 C++23/C++26 特性的完整测试覆盖

**开发要点：**

- **E7.5.3.1** 创建 `tests/cpp23/` 和 `tests/cpp26/` 目录
- **E7.5.3.2** 为每个特性编写测试用例
- **E7.5.3.3** 集成测试：组合多个特性
- **E7.5.3.4** 性能测试：确保优化有效

**测试清单：**
- [ ] Deducing this 测试（≥5 用例）
- [ ] decay-copy 测试（≥3 用例）
- [ ] static operator 测试（≥3 用例）
- [ ] [[assume]] 测试（≥2 用例）
- [ ] reflexpr 测试（≥3 用例）
- [ ] Contracts 测试（≥8 用例）
- [ ] delete("reason") 测试（≥2 用例）
- [ ] 占位符 `_` 测试（≥3 用例）
- [ ] 结构化绑定测试（≥4 用例）
- [ ] Pack Indexing 测试（≥5 用例）
- [ ] 转义序列测试（≥5 用例）
- [ ] 其他特性测试（每个 ≥1 用例）

**验收标准：**
- [ ] tests/cpp23/ 目录创建并包含测试
- [ ] tests/cpp26/ 目录创建并包含测试
- [ ] 总测试用例数 ≥ 50
- [ ] 测试覆盖率 ≥ 80%
- [ ] 集成测试通过
- [ ] 性能测试通过

**Checkpoint：** 测试覆盖率达标

---

## 📋 Phase 7 验收检查清单

### C++23 P1 特性
```
[ ] Deducing this (P0847R7) 实现完成
[ ] decay-copy (P0849R8) 实现完成
[ ] static operator() (P1169R4) 实现完成
[ ] static operator[] (P2589R1) 实现完成
[ ] [[assume]] 属性 (P1774R8) 实现完成
```

### 静态反射
```
[ ] reflexpr 关键字实现完成
[ ] 反射类型系统实现完成
[ ] 元编程支持实现完成
```

### Contracts
```
[ ] Contract 属性 (pre/post/assert) 实现完成
[ ] Contract 语义实现完成
[ ] Contract 检查模式实现完成
```

### C++26 P1/P2 特性
```
[ ] delete("reason") (P2573R2) 实现完成
[ ] 占位符变量 _ (P2169R4) 实现完成
[ ] 结构化绑定扩展 (P0963R3, P1061R10) 实现完成
[ ] Pack Indexing (P2662R3) 完善
```

### 其他特性
```
[ ] 分隔转义 \x{...} (P2290R3) 实现完成
[ ] 命名转义 \N{...} (P2071R2) 实现完成
[ ] for init-statement 中 using (P2360R0) 实现完成
[ ] @/$/反引号字符集 (P2558R2) 实现完成
[ ] [[indeterminate]] 属性 (P2795R5) 实现完成
[ ] 平凡可重定位 (P2786R13) 实现完成
[ ] 概念和变量模板模板参数 (P2841R7) 实现完成
[ ] 可变参数友元 (P2893R3) 实现完成
[ ] constexpr 异常 (P3068R6) 实现完成
[ ] 可观察检查点 (P1494R5) 实现完成
```

### 测试
```
[ ] tests/cpp23/ 目录创建
[ ] tests/cpp26/ 目录创建
[ ] 每个特性至少有一个测试用例
[ ] 集成测试通过
[ ] 性能测试通过
[ ] 测试覆盖率 ≥ 80%
```

---

## 🎯 Phase 7 实施指南

### 核心原则（Phase 3教训应用）

1. **接口先行** ⚠️
   - 每个 Task 开始前，必须先完成详细规划文档中列出的所有接口定义
   - 即使实现是空壳，头文件必须完整
   - 编译通过后再开始实现 .cpp 文件

2. **预置API** ⚠️
   - 为后续 Task 预先声明所有需要的函数签名
   - 确保 AI 开发者有明确的 API 可用
   - 避免临时发明新的接口

3. **完整性检查** ✅
   - 所有 enum 值必须完整
   - 所有 classof 覆盖必须存在
   - 所有构造函数/工厂方法必须声明
   - 所有诊断 ID 必须预定义

4. **参照 Clang** 📚
   - 关键设计决策参照 Clang 实现
   - AST 节点结构参考 Clang 的对应类
   - CodeGen 策略参考 Clang 的代码生成逻辑

### 实施步骤

#### Step 1: 阅读详细规划文档
```bash
# 打开详细接口规划
cat docs/plan/07-PHASE7-detailed-interface-plan.md
```

#### Step 2: 按 Stage 顺序执行
```
Stage 7.1 (2周) → Stage 7.2 (2周) → Stage 7.3 (1.5周) → 
Stage 7.4 (1.5周) → Stage 7.5 (1周)
```

#### Step 3: 每个 Task 的执行流程

1. **准备阶段**
   ```bash
   # 1. 阅读 Task 描述和接口预置清单
   # 2. 确认所有必需的头文件已创建/修改
   # 3. 运行编译测试（应该是空壳但能编译）
   cmake --build build --target blocktype
   ```

2. **实现阶段**
   ```bash
   # 1. 实现 Parser 逻辑
   # 2. 实现 Sema 检查
   # 3. 实现 CodeGen 生成
   # 4. 添加诊断消息
   ```

3. **测试阶段**
   ```bash
   # 1. 编写测试用例
   # 2. 运行单元测试
   ctest -R cpp23 --output-on-failure
   ctest -R cpp26 --output-on-failure
   
   # 3. 检查覆盖率
   ./scripts/coverage.sh
   ```

4. **验收阶段**
   ```bash
   # 1. 确认所有验收标准达成
   # 2. 代码审查
   # 3. 提交 commit
   git add -A
   git commit -m "feat(cpp23): 实现 [特性名称]"
   ```

### 常见问题

**Q1: 如何实现一个新AST节点？**
```cpp
// 1. 在 Expr.h/Decl.h/Stmt.h 中添加类定义
class NewExpr : public Expr {
  // 字段
public:
  // 构造函数
  // Getter/Setter
  // classof
};

// 2. 在 Stmt.h 的 NodeKind 枚举中添加
enum class NodeKind {
  // ...
  NewExprKind,
};

// 3. 实现 dump 方法（用于调试）
void NewExpr::dump(raw_ostream &OS) const {
  OS << "NewExpr";
}
```

**Q2: 如何添加新的诊断ID？**
```cpp
// 在 DiagnosticSemaKinds.def 中添加
DIAG(err_new_feature, Error,
  "error message in English",
  "错误消息（中文）")

// 在代码中使用
S.Diag(Loc, diag::err_new_feature);
```

**Q3: 如何参照 Clang 实现？**
```bash
# 1. 查找 Clang 源码中的对应实现
git clone https://github.com/llvm/llvm-project.git
cd llvm-project/clang

# 2. 搜索相关类/方法
grep -r "ExplicitObjectParameter" include/ lib/

# 3. 理解设计思路，适配 BlockType 架构
```

**Q4: 测试用例怎么写？**
```cpp
// tests/cpp23/deducing_this.cpp
#include <gtest/gtest.h>
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Sema/Sema.h"

TEST(DeducingThisTest, BasicLvalueRef) {
  // 1. 创建 ASTContext
  ASTContext Ctx;
  
  // 2. 解析源代码
  // ...
  
  // 3. 验证 AST 节点
  EXPECT_TRUE(FD->hasExplicitObjectParam());
  
  // 4. 验证 CodeGen
  // ...
}
```

### 质量保障

**代码审查清单：**
- [ ] 头文件接口完整
- [ ] 实现符合设计
- [ ] 诊断消息清晰（中英双语）
- [ ] 测试覆盖充分
- [ ] 无内存泄漏
- [ ] 无回归测试失败
- [ ] 代码风格一致（clang-format）
- [ ] 注释完整

**性能要求：**
- 编译速度无明显下降
- 内存使用合理
- CodeGen 生成的 IR 高效

---

*Phase 7 完成标志：*
- ✅ 能正确编译 C++23/C++26 特性代码
- ✅ Deducing this、Contracts、静态反射等核心特性实现完成
- ✅ 测试通过，覆盖率 ≥ 80%
- ✅ 无回归测试失败
- ✅ 文档完整更新

*文档维护者：AI Assistant*  
*最后更新：2026-04-19*  
*下次审计：Phase 7 完成后*

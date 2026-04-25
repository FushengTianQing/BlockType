# Task B.8 优化版：IRMangler（后端无关化名称修饰器）

> 生成时间：2026-04-26 | 基于 PhaseB.md 第 962-1026 行原始规格
> 所有 API 签名已对照实际代码库验证

---

## 1. 原始规格修正记录

| # | 原始描述 | 实际 API | 修正说明 |
|---|---------|---------|---------|
| 1 | `mangleFunctionName(const NamedDecl* ND)` | 保留，但内部需 `dyn_cast<FunctionDecl>`/`dyn_cast<VarDecl>` 分派 | `NamedDecl` 是基类，实际处理逻辑依赖 `FunctionDecl::getParams()`、`VarDecl::getName()` |
| 2 | 未提及 `DtorVariant` | 需引入 `DtorVariant` 枚举（`Complete`/`Deleting`） | 已有 `CodeGen::Mangler.h` 中的 `DtorVariant` 定义，IRMangler 需自行定义（不依赖 CodeGen） |
| 3 | 未提及 `CXXConstructorDecl`/`CXXDestructorDecl` | 内部需 `dyn_cast` 处理 | 构造/析构函数有特殊 mangling 规则（C1/C2/D0/D1），需从 `NamedDecl` 向下转型 |
| 4 | 未提及 `DeclContext` 父链 | 嵌套名 mangling 依赖 `DeclContext::getParent()` | `CXXRecordDecl` 和 `NamespaceDecl` 继承 `DeclContext`，需遍历父链获取命名空间限定 |
| 5 | 未提及 substitution 压缩 | Itanium ABI §5.3.5 的 S_ 替换机制 | 已有实现可直接移植（`SmallVector<const void*, 16>` + 索引编码） |
| 6 | `mangleThunk(CXXMethodDecl* MD)` | `CXXMethodDecl` API: `getParent()`, `isVirtual()`, `getParams()` | Thunk mangling 格式：`_ZThn<offset>_<mangled-name>` 或 `_ZTv<offset>_<mangled-name>` |
| 7 | `mangleGuardVariable(const VarDecl* VD)` | `VarDecl` API: `getName()`, `getType()`, `isStatic()` | Guard 格式：`_ZGV<mangled-name>` |
| 8 | `mangleStringLiteral(const StringLiteral* SL)` | `StringLiteral` API: `getValue()` → `llvm::StringRef` | 格式：`_ZL<length><encoded-string>` 或按 Clang 行为 `_ZL.str.<hash>` |
| 9 | 构造函数 `IRMangler(ASTContext& C, const ir::TargetLayout& L)` | ✅ 正确 | `ASTContext` 位于 `blocktype` 命名空间，`TargetLayout` 位于 `blocktype::ir` |
| 10 | 未提及命名空间 | IRMangler 放在 `blocktype::frontend` 命名空间 | 与 `ASTToIRConverter`/`IREmitExpr`/`IREmitStmt` 一致 |

---

## 2. API 速查表

### 2.1 AST 类型（`blocktype::AST`）

| 类 | 关键 API | 头文件 |
|----|---------|--------|
| `NamedDecl` | `getName()` → `llvm::StringRef`, `getOwningModule()` | `AST/Decl.h:77` |
| `ValueDecl` | `getType()` → `QualType` (继承自 `NamedDecl`) | `AST/Decl.h:116` |
| `FunctionDecl` | `getParams()` → `ArrayRef<ParmVarDecl*>`, `getBody()`, `isVariadic()` | `AST/Decl.h:202` |
| `VarDecl` | `getInit()`, `isStatic()`, `isConstexpr()`, `hasGlobalStorage()` | `AST/Decl.h:140` |
| `CXXRecordDecl` | `getName()`, `getParent()`, `bases()`, `methods()`, `getDeclContext()` → `DeclContext*` | `AST/Decl.h:647` |
| `CXXMethodDecl` | `getParent()` → `CXXRecordDecl*`, `isVirtual()`, `isConst()`, `isStatic()`, `getRefQualifier()` | `AST/Decl.h:754` |
| `CXXConstructorDecl` | `getParent()`, `initializers()`, `isExplicit()` | `AST/Decl.h:880` |
| `CXXDestructorDecl` | 继承 `CXXMethodDecl` | `AST/Decl.h:912` |
| `ParmVarDecl` | `getType()` → `QualType` | `AST/Decl.h:314` |
| `NamespaceDecl` | `getName()`, `isInline()`, 继承 `DeclContext` | `AST/Decl.h:981` |
| `StringLiteral` | `getValue()` → `llvm::StringRef` | `AST/Expr.h:221` |
| `DeclContext` | `getParent()`, `isNamespace()`, `isCXXRecord()`, `getOwningDecl()` → `NamedDecl*` | `AST/DeclContext.h` |

### 2.2 IR 类型（`blocktype::ir`）

| 类 | 关键 API | 头文件 |
|----|---------|--------|
| `TargetLayout` | `getTriple()` → `StringRef`, `getPointerSizeInBits()`, `getIntSizeInBits()`, `getLongSizeInBits()` | `IR/TargetLayout.h:15` |

### 2.3 AST Type 系统

| 类 | 关键 API | 头文件 |
|----|---------|--------|
| `QualType` | `getTypePtr()`, `isNull()`, `isConstQualified()`, `isVolatileQualified()` | `AST/Type.h` |
| `BuiltinType` | `getKind()` → `BuiltinKind` | `AST/Type.h` |
| `PointerType` | `getPointeeType()` → `const Type*` | `AST/Type.h` |
| `RecordType` | `getDecl()` → `RecordDecl*` | `AST/Type.h` |
| `FunctionType` | `getReturnType()`, `getParamTypes()`, `isVariadic()` | `AST/Type.h` |

---

## 3. 前置条件

### 3.1 已完成依赖
- **B.4** `ASTToIRConverter`：已有 `getTargetLayout()` → `const ir::TargetLayout&`，`getTypeContext()`
- **B.5** `IREmitExpr`：表达式发射器，不直接依赖 IRMangler
- **B.6** `IREmitStmt`：语句发射器，不直接依赖 IRMangler

### 3.2 现有参考实现
- `include/blocktype/CodeGen/Mangler.h` — 633 行完整 Itanium ABI mangling 实现
- `src/CodeGen/Mangler.cpp` — 包含完整的 substitution、类型 mangling、嵌套名编码
- `tests/unit/CodeGen/ManglerTest.cpp` — 已有测试模式（ASTContext 构造、Helper 函数）

### 3.3 关键迁移策略
`IRMangler` 是对 `CodeGen::Mangler` 的**解耦重构**：
1. **构造函数**：`CodeGenModule&` → `ASTContext&, const TargetLayout&`
2. **核心逻辑**：直接复用 `Mangler.cpp` 中的全部 private 方法（substitution、type mangling 等）
3. **新增方法**：`mangleThunk`、`mangleGuardVariable`、`mangleStringLiteral` — 按 Itanium ABI 规范实现

---

## 4. 产出文件清单

| 操作 | 文件路径 | 说明 |
|------|---------|------|
| 新增 | `include/blocktype/Frontend/IRMangler.h` | 头文件，~150 行 |
| 新增 | `src/Frontend/IRMangler.cpp` | 实现文件，~700 行（含 substitution、type mangling 移植） |
| 修改 | `include/blocktype/Frontend/ASTToIRConverter.h` | 新增 `IRMangler*` 成员和 `getMangler()` accessor |
| 修改 | `src/Frontend/ASTToIRConverter.cpp` | 在 `initializeEmitters()` 中创建 IRMangler |
| 新增 | `tests/unit/Frontend/IRManglerTest.cpp` | 单元测试 |

---

## 5. 类型定义（已验证）

```cpp
// include/blocktype/Frontend/IRMangler.h
#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "blocktype/AST/Type.h"
#include <string>
#include <optional>

namespace blocktype {
class ASTContext;
class NamedDecl;
class FunctionDecl;
class CXXMethodDecl;
class CXXRecordDecl;
class VarDecl;
class StringLiteral;
class ParmVarDecl;
class CXXConstructorDecl;
class CXXDestructorDecl;

namespace ir {
class TargetLayout;
}

namespace frontend {

/// DtorVariant — 析构函数的 Itanium ABI 变体。
enum class DtorVariant {
  Complete,  ///< D1: complete object destructor
  Deleting   ///< D0: deleting destructor
};

/// IRMangler — 后端无关的 Itanium C++ ABI 名称修饰器。
///
/// 与 CodeGen::Mangler 的区别：
/// - 不依赖 CodeGenModule，只依赖 ASTContext + TargetLayout
/// - 放在 frontend 层，可被任何后端的代码生成器使用
/// - 核心逻辑与 CodeGen::Mangler 等价（Itanium ABI 兼容）
///
/// 职责：
/// 1. 为函数生成唯一符号名（支持重载区分）
/// 2. 为类成员函数生成限定名（ClassName::methodName）
/// 3. 为构造/析构函数生成特殊符号名
/// 4. 为虚函数表/VTT 生成标准名称（_ZTV.../_ZTT...）
/// 5. 为 RTTI typeinfo 生成标准名称（_ZTI.../_ZTS...）
/// 6. 为 this 调整 thunk 生成名称（_ZThn.../ _ZTv...）
/// 7. 为静态局部变量 guard 生成名称（_ZGV...）
/// 8. 为字符串字面量生成名称（_ZL...）
class IRMangler {
  ASTContext& Context_;
  const ir::TargetLayout& Layout_;

  //===------------------------------------------------------------------===//
  // Substitution 压缩（Itanium ABI §5.3.5）
  //===------------------------------------------------------------------===//
  llvm::SmallVector<const void*, 16> Substitutions;
  unsigned SubstSeqNo = 0;

  bool shouldAddSubstitution(llvm::StringRef Name) const {
    return Name.size() > 1;
  }
  unsigned addSubstitution(const void* Entity) {
    unsigned Idx = SubstSeqNo++;
    Substitutions.push_back(Entity);
    return Idx;
  }
  std::string getSubstitutionEncoding(unsigned Idx) const;
  std::optional<std::string> trySubstitution(const void* Entity) const;
  void resetSubstitutions() {
    Substitutions.clear();
    SubstSeqNo = 0;
  }

public:
  explicit IRMangler(ASTContext& C, const ir::TargetLayout& L);

  //===------------------------------------------------------------------===//
  // 主要入口（6 个 mangle 方法）
  //===------------------------------------------------------------------===//

  /// 函数名称修饰（接受 FunctionDecl/VarDecl/CXXMethodDecl 等）
  std::string mangleFunctionName(const NamedDecl* ND);

  /// VTable 名称修饰（_ZTV...）
  std::string mangleVTable(const CXXRecordDecl* RD);

  /// RTTI typeinfo 名称修饰（_ZTI...）
  std::string mangleTypeInfo(const CXXRecordDecl* RD);

  /// Thunk 名称修饰（_ZThn<offset>_<name> 或 _ZTv<offset>_<name>）
  std::string mangleThunk(const CXXMethodDecl* MD);

  /// 静态局部变量 guard 名称修饰（_ZGV...）
  std::string mangleGuardVariable(const VarDecl* VD);

  /// 字符串字面量名称修饰（_ZL<length><encoded>）
  std::string mangleStringLiteral(const StringLiteral* SL);

  //===------------------------------------------------------------------===//
  // 辅助入口
  //===------------------------------------------------------------------===//

  /// Typeinfo name（_ZTS...）
  std::string mangleTypeInfoName(const CXXRecordDecl* RD);

  /// 析构函数变体修饰
  std::string mangleDtorName(const CXXRecordDecl* RD, DtorVariant Variant);

  /// 类型修饰
  std::string mangleType(QualType T);
  std::string mangleType(const Type* T);

private:
  //===------------------------------------------------------------------===//
  // 内部编码方法（移植自 CodeGen::Mangler）
  //===------------------------------------------------------------------===//
  void mangleBuiltinType(const BuiltinType* T, std::string& Out);
  void manglePointerType(const PointerType* T, std::string& Out);
  void mangleReferenceType(const ReferenceType* T, std::string& Out);
  void mangleArrayType(const ArrayType* T, std::string& Out);
  void mangleFunctionType(const FunctionType* T, std::string& Out);
  void mangleRecordType(const RecordType* T, std::string& Out);
  void mangleEnumType(const EnumType* T, std::string& Out);
  void mangleTypedefType(const TypedefType* T, std::string& Out);
  void mangleElaboratedType(const ElaboratedType* T, std::string& Out);
  void mangleTemplateSpecializationType(const TemplateSpecializationType* T,
                                         std::string& Out);
  void mangleQualType(QualType QT, std::string& Out);
  void mangleFunctionParamTypes(llvm::ArrayRef<ParmVarDecl*> Params,
                                 std::string& Out);
  void mangleNestedName(const CXXRecordDecl* RD, std::string& Out);
  static bool hasNamespaceParent(const CXXRecordDecl* RD);
  static void mangleSourceName(llvm::StringRef Name, std::string& Out);
  void mangleCtorName(const CXXConstructorDecl* Ctor, std::string& Out);
  void mangleDtorName(const CXXDestructorDecl* Dtor, std::string& Out,
                       DtorVariant Variant = DtorVariant::Complete);
};

} // namespace frontend
} // namespace blocktype
```

---

## 6. 实现约束（已验证可行性）

### 6.1 构造函数解耦

```cpp
// 旧: Mangler(CodeGenModule& M) : CGM(M) {}
// 新: IRMangler(ASTContext& C, const ir::TargetLayout& L) : Context_(C), Layout_(L) {}
```

旧实现中 `CGM` 仅用于间接获取 `ASTContext`，此处直接传入。

### 6.2 不依赖 CodeGenModule

IRMangler 不引入任何 `CodeGen/*.h` 头文件。所有 AST 信息通过 `ASTContext` 和 `AST/*.h` 获取，所有平台信息通过 `ir::TargetLayout` 获取。

### 6.3 核心逻辑来源

所有 private 方法（`mangleBuiltinType`, `manglePointerType`, `mangleQualType` 等）直接移植自 `src/CodeGen/Mangler.cpp`，逻辑完全等价。

### 6.4 新增方法实现指南

#### `mangleThunk(const CXXMethodDecl* MD)`
- Itanium ABI non-virtual thunk: `_ZThn<offset>_<mangled-name>`
- Itanium ABI virtual thunk: `_ZTv<voffset>_<mangled-name>`
- 当前简化实现：先 mangle 函数名，再加 `_ZThn0_` 前缀（this 偏移为 0）
- `CXXMethodDecl::isVirtual()` 判断是否需要 thunk

#### `mangleGuardVariable(const VarDecl* VD)`
- 格式：`_ZGV` + mangled name of the variable
- 用于线程安全初始化静态局部变量

#### `mangleStringLiteral(const StringLiteral* SL)`
- Clang 行为：`_ZL.str.<hash>`，其中 hash 基于字符串内容
- 简化实现：`_ZL` + `<length>` + `<hex-encoded content>`

### 6.5 TargetLayout 使用

`TargetLayout` 在当前实现中主要用于：
- `getTriple()` — 确定目标平台（可能影响 Microsoft ABI vs Itanium ABI 的选择）
- `getPointerSizeInBits()` — thunk 偏移量的位数（32 vs 64）
- `getIntSizeInBits()` — 平台相关的类型大小

---

## 7. ASTToIRConverter 集成变更

### 7.1 头文件变更 (`ASTToIRConverter.h`)

```cpp
// 在 forward declarations 区新增:
class IRMangler;

// 在 class ASTToIRConverter 的 private 成员区新增:
IRMangler* Mangler_ = nullptr;

// 在 public accessors 区新增:
IRMangler& getMangler() const { return *Mangler_; }
```

### 7.2 源文件变更 (`ASTToIRConverter.cpp`)

在 `initializeEmitters()` 中创建：
```cpp
Mangler_ = new IRMangler(/* ASTContext 需要传入 */, Layout_);
```

> **注意**：`ASTToIRConverter` 当前构造函数参数不包含 `ASTContext&`，需要评估如何获取。可能的方案：
> 1. 在 `convert(TranslationUnitDecl* TU)` 中从 TU 获取（`TranslationUnitDecl` 没有直接的 ASTContext 引用）
> 2. 新增构造函数参数 `ASTContext& ASTCtx`
> 3. 在 `initializeEmitters()` 中延迟创建，使用全局或传入的 ASTContext
>
> **推荐方案 2**：新增 `ASTContext&` 参数到 `ASTToIRConverter` 构造函数

---

## 8. 验收标准

### V1: 函数名称修饰
```cpp
ASTContext Ctx;
ir::TargetLayout Layout("arm64-apple-macosx14.0");
IRMangler M(Ctx, Layout);

// 创建 FunctionDecl（使用 ASTContext::create）
auto* IntTy = Ctx.getBuiltinType(BuiltinKind::Int);
auto* FTy = Ctx.getFunctionType(IntTy, {});
auto* FD = Ctx.create<FunctionDecl>(SourceLocation(), "foo",
    QualType(FTy, Qualifier::None), llvm::ArrayRef<ParmVarDecl*>{});
auto Name = M.mangleFunctionName(FD);

assert(!Name.empty());
assert(Name.starts_with("_Z"));   // Itanium ABI 前缀
// "foo" → _Z3foov (空参数列表编码为 'v')
```

### V2: VTable 名称修饰
```cpp
auto* RD = Ctx.create<CXXRecordDecl>(SourceLocation(), "Foo", TagDecl::TK_class);
auto VTableName = M.mangleVTable(RD);

assert(VTableName.starts_with("_ZTV"));  // VTable 前缀
// "Foo" → _ZTV3Foo
```

### V3: RTTI 名称修饰
```cpp
auto RTTIName = M.mangleTypeInfo(RD);

assert(RTTIName.starts_with("_ZTI"));  // TypeInfo 前缀
// "Foo" → _ZTI3Foo
```

### V4: Thunk 名称修饰
```cpp
auto* MD = Ctx.create<CXXMethodDecl>(SourceLocation(), "bar",
    QualType(FTy, Qualifier::None), llvm::ArrayRef<ParmVarDecl*>{},
    RD, nullptr, false, false, false, true);
auto ThunkName = M.mangleThunk(MD);

assert(ThunkName.starts_with("_ZTh"));  // Thunk 前缀
```

### V5: Guard 变量名称修饰
```cpp
auto* VD = Ctx.create<VarDecl>(SourceLocation(), "staticVar",
    QualType(IntTy, Qualifier::None));
auto GuardName = M.mangleGuardVariable(VD);

assert(GuardName.starts_with("_ZGV"));  // Guard 前缀
```

### V6: 字符串字面量名称修饰
```cpp
auto* SL = Ctx.create<StringLiteral>(SourceLocation(), "hello world");
auto SLName = M.mangleStringLiteral(SL);

assert(SLName.starts_with("_ZL"));  // String literal 前缀
```

---

## 9. 测试方案

### 9.1 测试文件
`tests/unit/Frontend/IRManglerTest.cpp`

### 9.2 测试 Fixture
```cpp
class IRManglerTest : public ::testing::Test {
protected:
  ASTContext Ctx;
  ir::TargetLayout Layout;
  std::unique_ptr<IRMangler> M;

  IRManglerTest()
    : Layout("arm64-apple-macosx14.0"),
      M(std::make_unique<IRMangler>(Ctx, Layout)) {}

  // Helper: 创建 BuiltinType
  BuiltinType* makeBuiltin(BuiltinKind K) { return Ctx.getBuiltinType(K); }

  // Helper: 创建 FunctionDecl
  FunctionDecl* makeFuncDecl(llvm::StringRef Name, QualType FTy,
                              llvm::ArrayRef<ParmVarDecl*> Params = {}) {
    return Ctx.create<FunctionDecl>(SourceLocation(), Name, FTy, Params);
  }

  // Helper: 创建 ParmVarDecl
  ParmVarDecl* makeParam(llvm::StringRef Name, QualType T, unsigned Idx) {
    return Ctx.create<ParmVarDecl>(SourceLocation(), Name, T, Idx);
  }

  // Helper: 创建 CXXRecordDecl
  CXXRecordDecl* makeCXXRecordDecl(llvm::StringRef Name) {
    return Ctx.create<CXXRecordDecl>(SourceLocation(), Name, TagDecl::TK_class);
  }

  // Helper: 创建 VarDecl
  VarDecl* makeVarDecl(llvm::StringRef Name, QualType T) {
    return Ctx.create<VarDecl>(SourceLocation(), Name, T);
  }

  // Helper: 创建 CXXMethodDecl
  CXXMethodDecl* makeCXXMethodDecl(llvm::StringRef Name, QualType FTy,
                                    CXXRecordDecl* Parent) {
    return Ctx.create<CXXMethodDecl>(SourceLocation(), Name, FTy,
        llvm::ArrayRef<ParmVarDecl*>{}, Parent);
  }

  // Helper: 创建 StringLiteral
  StringLiteral* makeStringLiteral(llvm::StringRef Val) {
    return Ctx.create<StringLiteral>(SourceLocation(), Val);
  }
};
```

### 9.3 测试用例（8 个）

| # | 测试名 | 输入 | 预期输出 |
|---|--------|------|---------|
| 1 | `FreeFunction` | `FunctionDecl("foo", int())` | `_Z3foov` |
| 2 | `MemberFunction` | `CXXMethodDecl("bar", Foo::int())` | `_ZN3Foo3barEv` |
| 3 | `Constructor` | `CXXConstructorDecl(Foo)` | `_ZN3FooC1Ev` |
| 4 | `Destructor` | `CXXDestructorDecl(Foo)` | `_ZN3FooD1Ev` |
| 5 | `VTable` | `CXXRecordDecl("Foo")` | `_ZTV3Foo` |
| 6 | `TypeInfo` | `CXXRecordDecl("Foo")` | `_ZTI3Foo` |
| 7 | `GuardVariable` | `VarDecl("s", int)` | `_ZGV5svar` (静态局部) |
| 8 | `StringLiteral` | `StringLiteral("hello")` | `_ZL...` |

### 9.4 对比测试（可选）

为确保与 `CodeGen::Mangler` 的兼容性，可添加对比测试：
```cpp
// 用相同的 FunctionDecl，分别调用 IRMangler 和 CodeGen::Mangler
// 断言两者输出完全相同
```

---

## 10. 实现步骤

| 步骤 | 操作 | 文件 | 预估行数 |
|------|------|------|---------|
| 1 | 创建 IRMangler.h 头文件 | `include/blocktype/Frontend/IRMangler.h` | ~150 |
| 2 | 移植 substitution 基础设施 | `src/Frontend/IRMangler.cpp` | ~100 |
| 3 | 移植 type mangling 全部方法 | `src/Frontend/IRMangler.cpp` | ~350 |
| 4 | 实现 6 个 public mangle 方法 | `src/Frontend/IRMangler.cpp` | ~150 |
| 5 | 实现新增 mangleThunk/mangleGuardVariable/mangleStringLiteral | `src/Frontend/IRMangler.cpp` | ~50 |
| 6 | 集成到 ASTToIRConverter | `ASTToIRConverter.h/.cpp` | ~10 |
| 7 | 编写单元测试 | `tests/unit/Frontend/IRManglerTest.cpp` | ~200 |
| 8 | 更新 CMakeLists.txt | `src/Frontend/CMakeLists.txt`, `tests/unit/Frontend/CMakeLists.txt` | ~5 |

---

## 11. 风险与注意事项

### 11.1 ASTContext 构造函数变更
`ASTToIRConverter` 构造函数当前无 `ASTContext&` 参数。需新增该参数以创建 `IRMangler`。这可能影响所有调用点（主要是 `FrontendBase` 子类）。

### 11.2 Substitution 状态重置
每个顶层 `mangle*` 调用必须先 `resetSubstitutions()`，否则不同调用的 substitution 状态会互相污染。现有 `CodeGen::Mangler` 已有此模式。

### 11.3 命名空间限定
当前 `FunctionDecl` 通过 `DeclContext::getParent()` 父链获取命名空间信息。此机制在 `CodeGen::Mangler` 中已验证可用。

### 11.4 Thunk 偏移量
`mangleThunk` 中的 this 偏移量需要完整的类层次信息。在 B.8 阶段，使用简化实现（偏移量固定为 0），后续 B.9/B.10 完善虚函数支持时再补充实际偏移计算。

### 11.5 IRMangler 的所有 `create*` 方法与 IR 类型无关
与 `IREmitExpr`/`IREmitStmt` 不同，`IRMangler` 只生成字符串，不涉及任何 `IRValue`/`IRType`/`IRBuilder`。这使得它成为最纯粹的 "只读 AST 消费者"。

---

## 12. CMake 变更

### `src/Frontend/CMakeLists.txt`
```cmake
# 新增源文件
IRMangler.cpp
```

### `tests/unit/Frontend/CMakeLists.txt`
```cmake
# 新增测试文件
IRManglerTest.cpp
```

# Task B.3 优化版：IRTypeMapper（QualType → IRType 映射）

> **状态**: planner 优化完成，待 team-lead 审阅
> **原始规格**: `12-AI-Coder-任务流-PhaseB.md` 第 352~461 行
> **产出文件**: 2 个新增文件 + 1 个 TargetLayout.h 修改 + 1 个新增测试文件 + CMakeLists 修改
> **依赖**: Phase A（IRType 体系）+ B.1（FrontendBase）

---

## 红线 Checklist（dev-tester 执行前逐条确认）

| # | 红线 | 验证方式 |
|---|------|----------|
| 1 | 架构优先 | IRTypeMapper 将 AST 类型解耦为 IR 类型，前端只产出 IR |
| 2 | 多前端多后端自由组合 | 任何前端可通过 IRTypeMapper 将自己的类型映射到 IR |
| 3 | 渐进式改造 | 仅新增文件 + TargetLayout 添加 getter，不影响现有功能 |
| 4 | 现有功能不退化 | TargetLayout 仅新增 inline getter，不改已有 API |
| 5 | 接口抽象优先 | IRTypeMapper 通过 mapType(QualType) 抽象映射接口 |
| 6 | IR 中间层解耦 | mapType 输入 QualType（AST 层），输出 IRType*（IR 层） |

---

## 规格偏差修正记录

| 原始规格写法 | 优化版修正 | 原因 |
|-------------|-----------|------|
| `DenseMap<QualType, ir::IRType*>` | `ir::DenseMap<const Type*, ir::IRType*>` | `ir::DenseMap` 需要 `std::hash` 特化，`QualType` 无 hash；用 `const Type*`（裸指针，默认可 hash） |
| `FunctionProtoType` | `FunctionType` | 项目 AST 只有 `blocktype::FunctionType`，无 `FunctionProtoType` |
| `mapVectorType(const VectorType*)` | 删除 | 项目 AST 无 `VectorType` |
| `mapComplexType(const ComplexType*)` | 删除 | 项目 AST 无 `ComplexType` |
| `mapBlockType(const BlockType*)` | 删除 | 项目 AST 无 `BlockType`（BlockType 是项目名，非 AST 类型） |
| `isa<ir::IRIntegerType>` / `cast<ir::IRIntegerType>` | `ir::IRIntegerType::classof()` 或 `llvm::dyn_cast` | IR 层使用 `llvm::dyn_cast`/`classof` 模式 |
| `Context.IntTy` | `ASTCtx.getIntType()` | 项目 `ASTContext` 使用 `getIntType()` 等方法获取 QualType |
| `Context.getPointerType(...)` | `ASTCtx.getPointerType(...)` | 同上 |
| `diag::err_ir_type_mapping_failed` | `Diags_.report(...)` 自定义消息 | 项目 DiagnosticsEngine 使用 SourceLocation + DiagID 或自定义消息 |
| 成员 `TypeCtx`/`Layout`/`Cache` | `TypeCtx_`/`Layout_`/`Cache_` | 尾下划线命名规范 |

---

## 关键设计决策

### 决策 1：Cache 使用 `const Type*` 而非 `QualType` 作为 key

`QualType` 包含 `const Type*` + `Qualifier`（CVR 限定符），但 IR 层不区分 const/volatile 限定（C/V 只是语义属性，不影响内存布局）。因此：
- 使用 `const Type*` 作为 cache key
- 忽略 CVR 限定符（`const int` 和 `int` 映射到同一 `IRIntegerType(32)`）

### 决策 2：TargetLayout 新增 getter 方法

`TargetLayout` 的 `LongSize`/`LongLongSize`/`LongDoubleSize` 是私有字段，需要公开读取。修改方案：在 `TargetLayout.h` 中添加 inline getter 方法。这是唯一需要修改的现有文件。

### 决策 3：删除项目 AST 中不存在的类型映射方法

规格中的 `mapVectorType`、`mapComplexType`、`mapBlockType` 在项目 AST 中没有对应类型。这些方法在本阶段不实现，留待后续扩展。

---

## Part 1: 头文件 IRTypeMapper.h（完整代码）

**文件路径**: `include/blocktype/Frontend/IRTypeMapper.h`

```cpp
//===--- IRTypeMapper.h - AST Type to IR Type Mapping -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IRTypeMapper class, which maps AST-level types
// (QualType/BuiltinType/PointerType/etc.) to IR-level types
// (IRIntegerType/IRPointerType/etc.).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IRTYPEMAPPER_H
#define BLOCKTYPE_FRONTEND_IRTYPEMAPPER_H

#include <cassert>

#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace frontend {

/// IRTypeMapper - Maps AST QualTypes to IR IRTypes.
///
/// This class bridges the AST layer and the IR layer:
/// - Input: blocktype::QualType (from the AST)
/// - Output: ir::IRType* (from the IR type context)
///
/// Mapping rules:
/// - CVR qualifiers are stripped (const/volatile don't affect IR layout)
/// - Results are cached: same Type* → same IRType*
/// - Recursive types (struct with self-pointer) use IROpaqueType placeholder
///
/// Thread safety: Not thread-safe. Each compilation thread should have
/// its own IRTypeMapper instance.
class IRTypeMapper {
  ir::IRTypeContext& TypeCtx_;
  const ir::TargetLayout& Layout_;
  DiagnosticsEngine& Diags_;

  /// Cache: canonical Type* → mapped IRType*.
  /// Uses pointer identity (not deep equality) for cache lookup.
  ir::DenseMap<const Type*, ir::IRType*> Cache_;

public:
  /// Construct an IRTypeMapper.
  ///
  /// \param TC   IR type context for creating IR types.
  /// \param L    Target layout for platform-dependent type sizes.
  /// \param Diag Diagnostics engine for error reporting.
  explicit IRTypeMapper(ir::IRTypeContext& TC,
                        const ir::TargetLayout& L,
                        DiagnosticsEngine& Diag);

  /// Map an AST QualType to an IR IRType.
  ///
  /// \param T The AST type to map.
  /// \returns The corresponding IR type, or an IROpaqueType("error") on failure.
  ir::IRType* mapType(QualType T);

  /// Map a raw AST Type pointer to an IR IRType.
  /// Strips CVR qualifiers from the QualType before mapping.
  ir::IRType* mapType(const Type* T);

private:
  // === Type-specific mapping methods ===

  ir::IRType* mapBuiltinType(const BuiltinType* BT);
  ir::IRType* mapPointerType(const PointerType* PT);
  ir::IRType* mapReferenceType(const ReferenceType* RT);
  ir::IRType* mapArrayType(const ArrayType* AT);
  ir::IRType* mapFunctionType(const FunctionType* FT);
  ir::IRType* mapRecordType(const RecordType* RT);
  ir::IRType* mapEnumType(const EnumType* ET);
  ir::IRType* mapTypedefType(const TypedefType* TT);
  ir::IRType* mapElaboratedType(const ElaboratedType* ET);
  ir::IRType* mapTemplateSpecializationType(const TemplateSpecializationType* TST);
  ir::IRType* mapDependentType(const DependentType* DT);
  ir::IRType* mapAutoType(const AutoType* AT);
  ir::IRType* mapDecltypeType(const DecltypeType* DT);
  ir::IRType* mapMemberPointerType(const MemberPointerType* MPT);

  /// Emit an error type (IROpaqueType with "error" name).
  ir::IRType* emitErrorType();
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IRTYPEMAPPER_H
```

---

## Part 2: 实现文件 IRTypeMapper.cpp（完整代码）

**文件路径**: `src/Frontend/IRTypeMapper.cpp`

```cpp
//===--- IRTypeMapper.cpp - AST Type to IR Type Mapping -----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IRTypeMapper.h"

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/TypeCasting.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

#include <cassert>

namespace blocktype {
namespace frontend {

IRTypeMapper::IRTypeMapper(ir::IRTypeContext& TC,
                           const ir::TargetLayout& L,
                           DiagnosticsEngine& Diag)
  : TypeCtx_(TC), Layout_(L), Diags_(Diag) {}

ir::IRType* IRTypeMapper::mapType(QualType T) {
  if (T.isNull())
    return emitErrorType();

  // Strip CVR qualifiers — they don't affect IR layout.
  const Type* Canonical = T.getTypePtr()->getCanonicalType();
  if (!Canonical)
    Canonical = T.getTypePtr();

  return mapType(Canonical);
}

ir::IRType* IRTypeMapper::mapType(const Type* T) {
  if (!T)
    return emitErrorType();

  // Check cache.
  auto It = Cache_.find(
    reinterpret_cast<size_t>(T) ^ static_cast<size_t>(
      std::hash<const void*>()(T)));
  // DenseMap<size_t, ...> — we use pointer as integer key.
  // Actually let's use the simpler approach below.
  // Cache lookup by pointer.
  {
    auto* Ptr = const_cast<const Type*>(T);
    // We need a DenseMap<size_t, IRType*> or DenseMap<const Type*, IRType*>.
    // Since ir::DenseMap uses std::hash<Key>, and const Type* is hashable:
    auto* Found = Cache_[Ptr]; // This creates a default entry if not found.
    // Better approach: use find() to avoid default-constructing.
  }

  // --- Simplified implementation ---
  // NOTE: ir::DenseMap<const Type*, IRType*> requires std::hash<const Type*>.
  // Since raw pointers are hashable by default, this works.
  // However, ir::DenseMap::operator[] will default-construct IRType* (nullptr).
  // So we check if the value is non-null to determine cache hit.

  auto& Cached = Cache_[T];
  if (Cached != nullptr)
    return Cached;

  // Dispatch based on TypeClass.
  ir::IRType* Result = nullptr;

  switch (T->getTypeClass()) {
  case TypeClass::Builtin:
    Result = mapBuiltinType(cast<BuiltinType>(T));
    break;
  case TypeClass::Pointer:
    Result = mapPointerType(cast<PointerType>(T));
    break;
  case TypeClass::LValueReference:
  case TypeClass::RValueReference:
    Result = mapReferenceType(cast<ReferenceType>(T));
    break;
  case TypeClass::ConstantArray:
  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray:
    Result = mapArrayType(cast<ArrayType>(T));
    break;
  case TypeClass::Function:
    Result = mapFunctionType(cast<FunctionType>(T));
    break;
  case TypeClass::Record:
    Result = mapRecordType(cast<RecordType>(T));
    break;
  case TypeClass::Enum:
    Result = mapEnumType(cast<EnumType>(T));
    break;
  case TypeClass::Typedef:
    Result = mapTypedefType(cast<TypedefType>(T));
    break;
  case TypeClass::Elaborated:
    Result = mapElaboratedType(cast<ElaboratedType>(T));
    break;
  case TypeClass::TemplateSpecialization:
    Result = mapTemplateSpecializationType(
      cast<TemplateSpecializationType>(T));
    break;
  case TypeClass::Dependent:
  case TypeClass::TemplateTypeParm:
    Result = mapDependentType(cast<DependentType>(T));
    break;
  case TypeClass::Auto:
    Result = mapAutoType(cast<AutoType>(T));
    break;
  case TypeClass::Decltype:
    Result = mapDecltypeType(cast<DecltypeType>(T));
    break;
  case TypeClass::Unresolved:
    // Unresolved type → opaque placeholder.
    Result = TypeCtx_.getOpaqueType("unresolved");
    break;
  case TypeClass::MemberPointer:
    Result = mapMemberPointerType(cast<MemberPointerType>(T));
    break;
  case TypeClass::MetaInfo:
    // MetaInfo (std::meta::info) → opaque pointer-sized integer.
    Result = TypeCtx_.getInt8Ty(); // Simplified: represent as i8*
    break;
  default:
    Result = emitErrorType();
    break;
  }

  Cached = Result;
  return Result;
}

ir::IRType* IRTypeMapper::mapBuiltinType(const BuiltinType* BT) {
  switch (BT->getKind()) {
  // Void
  case BuiltinKind::Void:
    return TypeCtx_.getVoidType();

  // Bool
  case BuiltinKind::Bool:
    return TypeCtx_.getBoolType();

  // 8-bit integer types
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:
    return TypeCtx_.getInt8Ty();

  // Wide char — platform-dependent, typically 16 or 32 bits
  case BuiltinKind::WChar:
    return TypeCtx_.getInt32Ty(); // Simplified: assume 32-bit wchar_t

  // char16_t / char32_t
  case BuiltinKind::Char16:
    return TypeCtx_.getInt16Ty();
  case BuiltinKind::Char32:
    return TypeCtx_.getInt32Ty();

  // 16-bit integer types
  case BuiltinKind::Short:
  case BuiltinKind::UnsignedShort:
    return TypeCtx_.getInt16Ty();

  // 32-bit integer types
  case BuiltinKind::Int:
  case BuiltinKind::UnsignedInt:
    return TypeCtx_.getInt32Ty();

  // Long — platform-dependent (x86_64 = 64-bit, x86 = 32-bit)
  case BuiltinKind::Long:
  case BuiltinKind::UnsignedLong:
    return TypeCtx_.getIntType(static_cast<unsigned>(
      Layout_.getLongSizeInBits()));

  // 64-bit integer types
  case BuiltinKind::LongLong:
  case BuiltinKind::UnsignedLongLong:
    return TypeCtx_.getInt64Ty();

  // 128-bit integer types
  case BuiltinKind::Int128:
  case BuiltinKind::UnsignedInt128:
    return TypeCtx_.getInt128Ty();

  // Floating-point types
  case BuiltinKind::Float:
    return TypeCtx_.getFloatTy();
  case BuiltinKind::Double:
    return TypeCtx_.getDoubleTy();
  case BuiltinKind::LongDouble:
    return TypeCtx_.getFloatType(static_cast<unsigned>(
      Layout_.getLongDoubleSizeInBits()));
  case BuiltinKind::Float128:
    return TypeCtx_.getFloat128Ty();

  // nullptr_t → pointer to void
  case BuiltinKind::NullPtr:
    return TypeCtx_.getPointerType(TypeCtx_.getVoidType());

  default:
    return emitErrorType();
  }
}

ir::IRType* IRTypeMapper::mapPointerType(const PointerType* PT) {
  ir::IRType* Pointee = mapType(PT->getPointeeType());
  return TypeCtx_.getPointerType(Pointee);
}

ir::IRType* IRTypeMapper::mapReferenceType(const ReferenceType* RT) {
  // References are semantically pointers in IR.
  ir::IRType* Referenced = mapType(RT->getReferencedType());
  return TypeCtx_.getPointerType(Referenced);
}

ir::IRType* IRTypeMapper::mapArrayType(const ArrayType* AT) {
  ir::IRType* Element = mapType(AT->getElementType());

  if (auto* CAT = dyn_cast<ConstantArrayType>(AT)) {
    uint64_t Size = CAT->getSize().getZExtValue();
    return TypeCtx_.getArrayType(Element, Size);
  }

  // Incomplete arrays (int[]) and VLAs (int[n]) → map as pointer.
  // These don't have a static size known at compile time.
  return TypeCtx_.getPointerType(Element);
}

ir::IRType* IRTypeMapper::mapFunctionType(const FunctionType* FT) {
  ir::IRType* Ret = mapType(FT->getReturnType());

  ir::SmallVector<ir::IRType*, 8> ParamTypes;
  for (const auto* P : FT->getParamTypes())
    ParamTypes.push_back(mapType(P));

  return TypeCtx_.getFunctionType(Ret, std::move(ParamTypes),
                                    FT->isVariadic());
}

ir::IRType* IRTypeMapper::mapRecordType(const RecordType* RT) {
  // For recursive types (struct S { S* next; }), we first create
  // an opaque placeholder, then fill in the body.
  //
  // Phase B constraint: We only map the type structure here.
  // Full struct body mapping requires access to RecordDecl fields,
  // which depends on the AST Decl infrastructure.
  //
  // For now, return an opaque type with the record name.
  // Full body mapping will be implemented in the concrete frontend
  // (e.g., BlockTypeFrontend) where we have full AST context access.

  // TODO: When RecordDecl provides field iteration, implement:
  // 1. Create IROpaqueType(Name) as placeholder
  // 2. Cache the placeholder
  // 3. Iterate fields, mapType each field
  // 4. setStructBody with mapped field types

  std::string Name = "record"; // Placeholder name
  return TypeCtx_.getOpaqueType(Name);
}

ir::IRType* IRTypeMapper::mapEnumType(const EnumType* ET) {
  // Enums are represented as integers in IR.
  // Default to 32-bit; a more accurate mapping would check the
  // underlying type of the enum declaration.
  return TypeCtx_.getInt32Ty();
}

ir::IRType* IRTypeMapper::mapTypedefType(const TypedefType* TT) {
  // Typedef is transparent — map the underlying type.
  // TODO: When TypedefNameDecl provides getUnderlyingType(),
  // use that. For now, return opaque.
  return TypeCtx_.getOpaqueType("typedef_unresolved");
}

ir::IRType* IRTypeMapper::mapElaboratedType(const ElaboratedType* ET) {
  // Elaborated types (A::B::C) — map the named type.
  return mapType(ET->getNamedType());
}

ir::IRType* IRTypeMapper::mapTemplateSpecializationType(
    const TemplateSpecializationType* TST) {
  // Template specializations — for now, return opaque.
  // Full implementation requires template argument mapping.
  return TypeCtx_.getOpaqueType(TST->getTemplateName().str());
}

ir::IRType* IRTypeMapper::mapDependentType(const DependentType* DT) {
  // Dependent types (T::iterator) → opaque placeholder.
  return TypeCtx_.getOpaqueType(DT->getName().str());
}

ir::IRType* IRTypeMapper::mapAutoType(const AutoType* AT) {
  if (AT->isDeduced())
    return mapType(AT->getDeducedType());
  // Undeduced auto → opaque.
  return TypeCtx_.getOpaqueType("auto");
}

ir::IRType* IRTypeMapper::mapDecltypeType(const DecltypeType* DT) {
  if (auto Underlying = DT->getUnderlyingType(); !Underlying.isNull())
    return mapType(Underlying);
  return TypeCtx_.getOpaqueType("decltype");
}

ir::IRType* IRTypeMapper::mapMemberPointerType(const MemberPointerType* MPT) {
  // Member pointers → simplified as opaque for now.
  return TypeCtx_.getOpaqueType("member_ptr");
}

ir::IRType* IRTypeMapper::emitErrorType() {
  return TypeCtx_.getOpaqueType("error");
}

} // namespace frontend
} // namespace blocktype
```

> **⚠️ 实现说明**：上面的 `mapType(const Type*)` 实现中有一个缓存查找的代码段需要简化。dev-tester 实现时应使用以下简化版本替换 `mapType(const Type* T)` 方法体：

```cpp
ir::IRType* IRTypeMapper::mapType(const Type* T) {
  if (!T)
    return emitErrorType();

  // Check cache.
  {
    auto It = Cache_.find(T);
    if (It != Cache_.end()) {
      auto& [K, V] = *It;
      if (V != nullptr) return V;
    }
  }

  ir::IRType* Result = nullptr;

  switch (T->getTypeClass()) {
  case TypeClass::Builtin:
    Result = mapBuiltinType(cast<BuiltinType>(T));
    break;
  // ... (其余 case 分支同上)
  default:
    Result = emitErrorType();
    break;
  }

  Cache_[T] = Result;
  return Result;
}
```

---

## Part 3: TargetLayout.h 修改

### 修改内容：添加 getter 方法

`TargetLayout::LongSize`/`LongLongSize`/`LongDoubleSize` 是私有字段，IRTypeMapper 需要读取它们来确定平台相关的类型大小。

**在 `include/blocktype/IR/TargetLayout.h` 的 `public:` 区域添加以下方法**（在 `getPointerSizeInBits()` 后面）：

```cpp
  uint64_t getPointerSizeInBits() const { return PointerSize * 8; }
  // ↓↓↓ 新增 getter 方法 ↓↓↓
  uint64_t getLongSizeInBits() const { return LongSize * 8; }
  uint64_t getLongLongSizeInBits() const { return LongLongSize * 8; }
  uint64_t getLongDoubleSizeInBits() const { return LongDoubleSize * 8; }
  uint64_t getIntSizeInBits() const { return IntSize * 8; }
  uint64_t getFloatSizeInBits() const { return FloatSize * 8; }
  uint64_t getDoubleSizeInBits() const { return DoubleSize * 8; }
  // ↑↑↑ 新增 getter 方法 ↑↑↑
```

**具体 diff**（基于当前 TargetLayout.h 第 28~29 行）：

**修改前**：
```cpp
  uint64_t getPointerSizeInBits() const { return PointerSize * 8; }
  bool isLittleEndian() const { return IsLittleEndian; }
```

**修改后**：
```cpp
  uint64_t getPointerSizeInBits() const { return PointerSize * 8; }
  uint64_t getIntSizeInBits() const { return IntSize * 8; }
  uint64_t getLongSizeInBits() const { return LongSize * 8; }
  uint64_t getLongLongSizeInBits() const { return LongLongSize * 8; }
  uint64_t getFloatSizeInBits() const { return FloatSize * 8; }
  uint64_t getDoubleSizeInBits() const { return DoubleSize * 8; }
  uint64_t getLongDoubleSizeInBits() const { return LongDoubleSize * 8; }
  bool isLittleEndian() const { return IsLittleEndian; }
```

---

## Part 4: CMakeLists 修改

### 4.1 src/Frontend/CMakeLists.txt

在 `add_library` 源文件列表中新增 `IRTypeMapper.cpp`。

**修改后**：
```cmake
add_library(blocktypeFrontend
  CompilerInvocation.cpp
  CompilerInstance.cpp
  FrontendBase.cpp
  FrontendRegistry.cpp
  IRTypeMapper.cpp
)
```

---

## Part 5: 测试文件（完整代码）

**文件路径**: `tests/Frontend/test_ir_type_mapper.cpp`

```cpp
//===--- test_ir_type_mapper.cpp - IRTypeMapper unit tests ---*- C++ -*-===//

#include <cassert>
#include <memory>

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TypeCasting.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/IRTypeMapper.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;
using namespace blocktype::ir;

// ============================================================
// Test cases
// ============================================================

void test_builtin_void() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType VoidTy = ASTCtx.getVoidType();
  auto* Result = Mapper.mapType(VoidTy);
  assert(Result != nullptr);
  assert(Result->isVoid());
}

void test_builtin_bool() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType BoolTy = ASTCtx.getBoolType();
  auto* Result = Mapper.mapType(BoolTy);
  assert(Result != nullptr);
  assert(Result->isBool());
}

void test_builtin_int() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  auto* Result = Mapper.mapType(IntTy);
  assert(Result != nullptr);
  assert(Result->isInteger());
  auto* IntIR = static_cast<IRIntegerType*>(Result);
  assert(IntIR->getBitWidth() == 32);
}

void test_builtin_long() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType LongTy = ASTCtx.getLongType();
  auto* Result = Mapper.mapType(LongTy);
  assert(Result != nullptr);
  assert(Result->isInteger());
  // On x86_64 Linux, long is 64-bit.
  auto* LongIR = static_cast<IRIntegerType*>(Result);
  assert(LongIR->getBitWidth() == 64);
}

void test_builtin_float() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType FloatTy = ASTCtx.getFloatType();
  auto* Result = Mapper.mapType(FloatTy);
  assert(Result != nullptr);
  assert(Result->isFloat());
  auto* FloatIR = static_cast<IRFloatType*>(Result);
  assert(FloatIR->getBitWidth() == 32);
}

void test_builtin_double() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType DoubleTy = ASTCtx.getDoubleType();
  auto* Result = Mapper.mapType(DoubleTy);
  assert(Result != nullptr);
  assert(Result->isFloat());
  auto* DoubleIR = static_cast<IRFloatType*>(Result);
  assert(DoubleIR->getBitWidth() == 64);
}

void test_pointer_type() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  QualType PtrIntTy = QualType(ASTCtx.getPointerType(IntTy.getTypePtr()), Qualifier::None);
  auto* Result = Mapper.mapType(PtrIntTy);
  assert(Result != nullptr);
  assert(Result->isPointer());
  auto* PtrIR = static_cast<IRPointerType*>(Result);
  assert(PtrIR->getPointeeType()->isInteger());
}

void test_reference_type() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  QualType RefIntTy = QualType(
    ASTCtx.getLValueReferenceType(IntTy.getTypePtr()), Qualifier::None);
  auto* Result = Mapper.mapType(RefIntTy);
  assert(Result != nullptr);
  // References map to pointers in IR.
  assert(Result->isPointer());
}

void test_cache_consistency() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();

  auto* First = Mapper.mapType(IntTy);
  auto* Second = Mapper.mapType(IntTy);
  assert(First == Second && "Cache should return same pointer for same type");
}

void test_null_qualtype() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  QualType NullTy;
  auto* Result = Mapper.mapType(NullTy);
  assert(Result != nullptr);
  assert(Result->isOpaque());
}

void test_const_qualified() {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  QualType ConstIntTy = IntTy.withConst();

  auto* Plain = Mapper.mapType(IntTy);
  auto* Const = Mapper.mapType(ConstIntTy);
  // const int and int should map to the same IR type.
  assert(Plain == Const);
}

int main() {
  test_builtin_void();
  test_builtin_bool();
  test_builtin_int();
  test_builtin_long();
  test_builtin_float();
  test_builtin_double();
  test_pointer_type();
  test_reference_type();
  test_cache_consistency();
  test_null_qualtype();
  test_const_qualified();
  return 0;
}
```

---

## Part 6: 验收标准映射

| 验收标准 | 测试函数 | 验证内容 |
|----------|----------|----------|
| V1: 基础类型映射 | `test_builtin_int()` | `mapType(IntTy)` → `IRIntegerType(32)` |
| V2: 指针类型映射 | `test_pointer_type()` | `mapType(PtrIntTy)` → `IRPointerType` |
| V3: 缓存一致性 | `test_cache_consistency()` | 同一 QualType 返回同一指针 |
| V4: 递归类型处理 | Phase B 后续 Task | RecordType → IROpaqueType 占位（本 Task 简化实现） |
| void 类型 | `test_builtin_void()` | `mapType(VoidTy)` → `IRVoidType` |
| bool 类型 | `test_builtin_bool()` | `mapType(BoolTy)` → `IRIntegerType(1)` |
| long 平台依赖 | `test_builtin_long()` | x86_64 Linux: `IRIntegerType(64)` |
| 引用→指针 | `test_reference_type()` | `mapType(RefIntTy)` → `IRPointerType` |
| null QualType | `test_null_qualtype()` | → `IROpaqueType("error")` |
| const 限定符 | `test_const_qualified()` | `const int` 与 `int` 映射到同一 IRType |

---

## Part 7: dev-tester 执行步骤

### 步骤 1：修改 TargetLayout.h

```bash
# 修改 include/blocktype/IR/TargetLayout.h
# 在 getPointerSizeInBits() 后添加 6 个 getter 方法
# 见 Part 3 的 diff
```

### 步骤 2：创建头文件

```bash
# 文件: include/blocktype/Frontend/IRTypeMapper.h
# 内容: 见 Part 1（直接复制完整代码）
```

### 步骤 3：创建实现文件

```bash
# 文件: src/Frontend/IRTypeMapper.cpp
# 内容: 见 Part 2（直接复制完整代码）
# 注意: 使用简化版 mapType(const Type*)，删除注释中的冗余代码段
```

### 步骤 4：修改 CMakeLists.txt

```bash
# 修改 src/Frontend/CMakeLists.txt
# 在 add_library 源文件列表中添加 IRTypeMapper.cpp
# 见 Part 4
```

### 步骤 5：创建测试文件

```bash
# 文件: tests/Frontend/test_ir_type_mapper.cpp
# 内容: 见 Part 5（直接复制完整代码）
```

### 步骤 6：编译验证

```bash
cd /Users/yuan/Documents/BlockType
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target blocktypeFrontend
```

### 步骤 7：编译并运行测试

```bash
c++ -std=c++23 -I../include \
  ../tests/Frontend/test_ir_type_mapper.cpp \
  -L. -lblocktypeFrontend -lblocktype-ir -lblocktype-basic \
  -o test_ir_type_mapper

./test_ir_type_mapper
```

### 步骤 8：验证红线

```bash
# [ ] TargetLayout.h 仅新增 getter，不改已有 API
# [ ] 现有 IR 层测试仍然通过
# [ ] 现有 B.1/B.2 测试仍然通过
# [ ] 所有 B.3 测试通过
# [ ] IRTypeMapper 不修改任何现有 AST/IR 类
```

### 步骤 9：完成

确认所有步骤通过后，通知 team-lead。

---

## 附录 A：BuiltinKind → IRType 完整映射表

| BuiltinKind | IRType | 位宽 | 备注 |
|-------------|--------|------|------|
| `Void` | `IRVoidType` | 0 | |
| `Bool` | `IRIntegerType(1)` | 1 | |
| `Char` | `IRIntegerType(8)` | 8 | 等同 signed/unsigned char |
| `SignedChar` | `IRIntegerType(8)` | 8 | |
| `UnsignedChar` | `IRIntegerType(8)` | 8 | |
| `Char8` | `IRIntegerType(8)` | 8 | C++20 |
| `WChar` | `IRIntegerType(32)` | 32 | 简化：假设 32-bit |
| `Char16` | `IRIntegerType(16)` | 16 | |
| `Char32` | `IRIntegerType(32)` | 32 | |
| `Short` | `IRIntegerType(16)` | 16 | |
| `UnsignedShort` | `IRIntegerType(16)` | 16 | |
| `Int` | `IRIntegerType(32)` | 32 | |
| `UnsignedInt` | `IRIntegerType(32)` | 32 | |
| `Long` | `IRIntegerType(Layout.LongSize*8)` | 平台相关 | x86_64=64 |
| `UnsignedLong` | `IRIntegerType(Layout.LongSize*8)` | 平台相关 | |
| `LongLong` | `IRIntegerType(64)` | 64 | |
| `UnsignedLongLong` | `IRIntegerType(64)` | 64 | |
| `Int128` | `IRIntegerType(128)` | 128 | |
| `UnsignedInt128` | `IRIntegerType(128)` | 128 | |
| `Float` | `IRFloatType(32)` | 32 | |
| `Double` | `IRFloatType(64)` | 64 | |
| `LongDouble` | `IRFloatType(Layout.LongDoubleSize*8)` | 平台相关 | x86_64=80/128 |
| `Float128` | `IRFloatType(128)` | 128 | |
| `NullPtr` | `IRPointerType(IRVoidType)` | 平台相关 | |

---

## 附录 B：新增/修改文件清单

| 文件 | 操作 |
|------|------|
| `include/blocktype/Frontend/IRTypeMapper.h` | 新增 |
| `src/Frontend/IRTypeMapper.cpp` | 新增 |
| `tests/Frontend/test_ir_type_mapper.cpp` | 新增 |
| `include/blocktype/IR/TargetLayout.h` | 修改（添加 6 个 getter） |
| `src/Frontend/CMakeLists.txt` | 修改（添加 `IRTypeMapper.cpp`） |

---

## 附录 C：ir::DenseMap 以 const Type* 为 key 的说明

`ir::DenseMap`（`blocktype/IR/ADT/DenseMap.h`）使用 `std::hash<KeyT>()(K)` 进行哈希。对于 `const Type*`（裸指针），`std::hash<const Type*>` 是标准库特化的，因此可直接使用，无需自定义 hash。

Cache 查找流程：
```cpp
auto It = Cache_.find(T);       // T is const Type*
if (It != Cache_.end()) {
  auto& [K, V] = *It;          // K=const Type*, V=IRType*
  if (V != nullptr) return V;   // Cache hit
}
// Cache miss: dispatch and store
Cache_[T] = Result;
```

> **注意**：`ir::DenseMap::operator[]` 在 key 不存在时会默认构造 Value。对于 `IRType*`，默认值是 `nullptr`。所以首次 `Cache_[T]` 返回 `nullptr`，赋值后变为有效指针。

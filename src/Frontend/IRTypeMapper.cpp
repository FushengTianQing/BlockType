//===--- IRTypeMapper.cpp - AST Type to IR Type Mapping -----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IRTypeMapper.h"

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
  {
    auto It = Cache_.find(T);
    if (It != Cache_.end()) {
      auto Pair = *It;
      if (Pair.second != nullptr) return Pair.second;
    }
  }

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
    // MetaInfo (std::meta::info) → simplified as i8*.
    Result = TypeCtx_.getPointerType(TypeCtx_.getVoidType());
    break;
  default:
    Result = emitErrorType();
    break;
  }

  Cache_[T] = Result;
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
  // For recursive types, we first create an opaque placeholder.
  // Full body mapping will be implemented in the concrete frontend.
  std::string Name = "record"; // Placeholder name
  return TypeCtx_.getOpaqueType(Name);
}

ir::IRType* IRTypeMapper::mapEnumType(const EnumType* ET) {
  // Enums are represented as integers in IR.
  return TypeCtx_.getInt32Ty();
}

ir::IRType* IRTypeMapper::mapTypedefType(const TypedefType* TT) {
  // Typedef is transparent — map the underlying type.
  // TODO: When TypedefNameDecl provides getUnderlyingType(), use that.
  return TypeCtx_.getOpaqueType("typedef_unresolved");
}

ir::IRType* IRTypeMapper::mapElaboratedType(const ElaboratedType* ET) {
  // Elaborated types (A::B::C) — map the named type.
  return mapType(ET->getNamedType());
}

ir::IRType* IRTypeMapper::mapTemplateSpecializationType(
    const TemplateSpecializationType* TST) {
  // Template specializations — for now, return opaque.
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

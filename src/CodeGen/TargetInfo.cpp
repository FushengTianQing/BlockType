//===--- TargetInfo.cpp - Target Platform Information ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/TargetInfo.h"
#include "llvm/TargetParser/Triple.h"

namespace blocktype {

/// 根据目标三元组生成数据布局字符串
static std::string getDataLayoutForTriple(llvm::StringRef TripleStr) {
  llvm::Triple T(TripleStr);
  if (T.isArch64Bit()) {
    // 64-bit: pointers are 8 bytes, 64-bit aligned
    // e-i64:64-f80:128-n8:16:32:64-S128
    return "e-m:o-i64:64-i128:128-n32:64-S128";
  } else if (T.isArch32Bit()) {
    // 32-bit
    return "e-m:o-p:32:32-f64:32:64-f80:128-n8:16:32-S128";
  }
  // Default 64-bit
  return "e-m:o-i64:64-i128:128-n32:64-S128";
}

TargetInfo::TargetInfo(llvm::StringRef TargetTriple)
    : DL(getDataLayoutForTriple(TargetTriple)), TripleStr(TargetTriple.str()) {}

uint64_t TargetInfo::getTypeSize(QualType T) const {
  if (T.isNull()) return 0;
  const Type *Ty = T.getTypePtr();

  if (auto *BT = llvm::dyn_cast<BuiltinType>(Ty))
    return getBuiltinSize(BT->getKind());
  if (Ty->isPointerType())
    return getPointerSize();
  if (Ty->isReferenceType())
    return getPointerSize();
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty)) {
    uint64_t ElemSize = getTypeSize(QualType(AT->getElementType(), Qualifier::None));
    if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(Ty))
      return ElemSize * CAT->getSize().getZExtValue();
    return ElemSize; // Incomplete/Variable array
  }
  // Records, enums, etc. — use pointer size as approximation
  return DL.getPointerSize(0);
}

uint64_t TargetInfo::getTypeAlign(QualType T) const {
  if (T.isNull()) return 1;
  const Type *Ty = T.getTypePtr();

  if (auto *BT = llvm::dyn_cast<BuiltinType>(Ty))
    return getBuiltinAlign(BT->getKind());
  if (Ty->isPointerType() || Ty->isReferenceType())
    return getPointerAlign();
  return 4; // Default alignment for unknown types
}

uint64_t TargetInfo::getBuiltinSize(BuiltinKind K) const {
  switch (K) {
  case BuiltinKind::Void:      return 0;
  case BuiltinKind::Bool:      return 1;
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:     return 1;
  case BuiltinKind::Short:
  case BuiltinKind::UnsignedShort:
  case BuiltinKind::Char16:    return 2;
  case BuiltinKind::Int:
  case BuiltinKind::UnsignedInt:
  case BuiltinKind::Float:
  case BuiltinKind::WChar:
  case BuiltinKind::Char32:    return 4;
  case BuiltinKind::Long:
  case BuiltinKind::UnsignedLong:
  case BuiltinKind::Double:    return 8;
  case BuiltinKind::LongLong:
  case BuiltinKind::UnsignedLongLong: return 8;
  case BuiltinKind::LongDouble:
    // macOS / Darwin: long double = double (8 bytes)
    // Linux x86_64: long double = x87 extended (16 bytes, 80-bit padded)
    // 其他平台默认 16 字节
    {
      llvm::Triple T(TripleStr);
      if (T.isOSDarwin())
        return 8;  // macOS: long double == double
      return 16;   // Linux 等: long double == 80-bit extended padded to 16
    }
  case BuiltinKind::Float128:  return 16;
  case BuiltinKind::Int128:
  case BuiltinKind::UnsignedInt128: return 16;
  case BuiltinKind::NullPtr:   return getPointerSize();
  default:                     return 0;
  }
}

uint64_t TargetInfo::getBuiltinAlign(BuiltinKind K) const {
  uint64_t Size = getBuiltinSize(K);
  if (Size == 0) return 1;
  if (Size == 1) return 1;
  if (Size <= 2) return 2;
  if (Size <= 4) return 4;
  if (Size <= 8) return 8;
  return 16;
}

bool TargetInfo::isStructReturnInRegister(QualType T) const {
  return getTypeSize(T) <= 16;
}

} // namespace blocktype

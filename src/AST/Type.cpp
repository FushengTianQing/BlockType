//===--- Type.cpp - Type System Implementation --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/Type.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// BuiltinType Implementation
//===----------------------------------------------------------------------===//

bool BuiltinType::isSignedInteger() const {
  switch (Kind) {
  case BuiltinKind::Char:  // Platform-dependent
  case BuiltinKind::SignedChar:
  case BuiltinKind::Short:
  case BuiltinKind::Int:
  case BuiltinKind::Long:
  case BuiltinKind::LongLong:
  case BuiltinKind::Int128:
    return true;
  default:
    return false;
  }
}

bool BuiltinType::isUnsignedInteger() const {
  switch (Kind) {
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::UnsignedShort:
  case BuiltinKind::UnsignedInt:
  case BuiltinKind::UnsignedLong:
  case BuiltinKind::UnsignedLongLong:
  case BuiltinKind::UnsignedInt128:
    return true;
  default:
    return false;
  }
}

bool BuiltinType::isFloatingPoint() const {
  switch (Kind) {
  case BuiltinKind::Float:
  case BuiltinKind::Double:
  case BuiltinKind::LongDouble:
  case BuiltinKind::Float128:
    return true;
  default:
    return false;
  }
}

bool BuiltinType::isInteger() const {
  return isSignedInteger() || isUnsignedInteger() ||
         Kind == BuiltinKind::Bool || Kind == BuiltinKind::Char ||
         Kind == BuiltinKind::WChar || Kind == BuiltinKind::Char16 ||
         Kind == BuiltinKind::Char32 || Kind == BuiltinKind::Char8;
}

const char *BuiltinType::getName() const {
  switch (Kind) {
  case BuiltinKind::Void: return "void";
  case BuiltinKind::Bool: return "bool";
  case BuiltinKind::Char: return "char";
  case BuiltinKind::SignedChar: return "signed char";
  case BuiltinKind::UnsignedChar: return "unsigned char";
  case BuiltinKind::WChar: return "wchar_t";
  case BuiltinKind::Char16: return "char16_t";
  case BuiltinKind::Char32: return "char32_t";
  case BuiltinKind::Char8: return "char8_t";
  case BuiltinKind::Short: return "short";
  case BuiltinKind::Int: return "int";
  case BuiltinKind::Long: return "long";
  case BuiltinKind::LongLong: return "long long";
  case BuiltinKind::Int128: return "__int128";
  case BuiltinKind::UnsignedShort: return "unsigned short";
  case BuiltinKind::UnsignedInt: return "unsigned int";
  case BuiltinKind::UnsignedLong: return "unsigned long";
  case BuiltinKind::UnsignedLongLong: return "unsigned long long";
  case BuiltinKind::UnsignedInt128: return "unsigned __int128";
  case BuiltinKind::Float: return "float";
  case BuiltinKind::Double: return "double";
  case BuiltinKind::LongDouble: return "long double";
  case BuiltinKind::Float128: return "__float128";
  case BuiltinKind::NullPtr: return "std::nullptr_t";
  default: llvm_unreachable("Unknown builtin type kind");
  }
}

void BuiltinType::dump(llvm::raw_ostream &OS) const {
  OS << getName();
}

//===----------------------------------------------------------------------===//
// PointerType Implementation
//===----------------------------------------------------------------------===//

void PointerType::dump(llvm::raw_ostream &OS) const {
  if (Pointee)
    Pointee->dump(OS);
  OS << "*";
}

//===----------------------------------------------------------------------===//
// ReferenceType Implementation
//===----------------------------------------------------------------------===//

void ReferenceType::dump(llvm::raw_ostream &OS) const {
  if (Referenced)
    Referenced->dump(OS);
  OS << (IsLValue ? "&" : "&&");
}

//===----------------------------------------------------------------------===//
// ArrayType Implementation
//===----------------------------------------------------------------------===//

void ArrayType::dump(llvm::raw_ostream &OS) const {
  if (ElementType)
    ElementType->dump(OS);
  OS << "[]";
}

//===----------------------------------------------------------------------===//
// FunctionType Implementation
//===----------------------------------------------------------------------===//

void FunctionType::dump(llvm::raw_ostream &OS) const {
  if (ReturnType)
    ReturnType->dump(OS);
  OS << " (";
  bool First = true;
  for (const Type *Param : ParamTypes) {
    if (!First)
      OS << ", ";
    First = false;
    if (Param)
      Param->dump(OS);
  }
  if (IsVariadic) {
    if (!First)
      OS << ", ";
    OS << "...";
  }
  OS << ")";
}

//===----------------------------------------------------------------------===//
// RecordType Implementation
//===----------------------------------------------------------------------===//

void RecordType::dump(llvm::raw_ostream &OS) const {
  // TODO: Print record name once RecordDecl is defined
  OS << "<record>";
}

//===----------------------------------------------------------------------===//
// EnumType Implementation
//===----------------------------------------------------------------------===//

void EnumType::dump(llvm::raw_ostream &OS) const {
  // TODO: Print enum name once EnumDecl is defined
  OS << "<enum>";
}

//===----------------------------------------------------------------------===//
// TypedefType Implementation
//===----------------------------------------------------------------------===//

void TypedefType::dump(llvm::raw_ostream &OS) const {
  // TODO: Print typedef name once TypedefNameDecl is defined
  OS << "<typedef>";
}

//===----------------------------------------------------------------------===//
// Type Implementation
//===----------------------------------------------------------------------===//

void Type::dump() const {
  dump(llvm::errs());
}

void Type::dump(llvm::raw_ostream &OS) const {
  OS << "Type";
}

//===----------------------------------------------------------------------===//
// QualType Implementation
//===----------------------------------------------------------------------===//

void QualType::dump() const {
  dump(llvm::errs());
}

void QualType::dump(llvm::raw_ostream &OS) const {
  if (!Ty) {
    OS << "<null>";
    return;
  }

  // Print qualifiers before the type
  if (isConstQualified())
    OS << "const ";
  if (isVolatileQualified())
    OS << "volatile ";
  if (isRestrictQualified())
    OS << "restrict ";

  Ty->dump(OS);
}

} // namespace blocktype

//===--- ReflectionTypes.cpp - Reflection Type System Implementation --*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the reflection type system for BlockType's C++26
// static reflection support:
//   - meta::TypeInfo: compile-time type introspection
//   - meta::MemberInfo: member descriptor
//   - Reflection utility functions
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/ReflectionTypes.h"
#include "blocktype/AST/ASTContext.h"
#include "llvm/Support/Casting.h"

namespace blocktype {
namespace meta {

using llvm::dyn_cast;
using llvm::cast;

//===----------------------------------------------------------------------===//
// TypeInfo implementation
//===----------------------------------------------------------------------===//

llvm::SmallVector<MemberInfo, 16> TypeInfo::getMembers() const {
  llvm::SmallVector<MemberInfo, 16> Result;
  if (!Record)
    return Result;

  // Add fields
  for (auto *Field : Record->fields()) {
    if (!Field)
      continue;
    AccessSpecifier Access = Field->getAccess();
    Result.emplace_back(Field->getName(), Field->getType(), Access,
                        false /*IsStatic*/, false /*IsFunction*/);
  }

  // Add methods
  for (auto *Method : Record->methods()) {
    if (!Method)
      continue;
    AccessSpecifier Access = Method->getAccess();
    bool IsStatic = Method->isStatic();
    Result.emplace_back(Method->getName(), Method->getType(), Access,
                        IsStatic, true /*IsFunction*/);
  }

  return Result;
}

llvm::SmallVector<MemberInfo, 16> TypeInfo::getFields() const {
  llvm::SmallVector<MemberInfo, 16> Result;
  if (!Record)
    return Result;

  for (auto *Field : Record->fields()) {
    if (!Field)
      continue;
    AccessSpecifier Access = Field->getAccess();
    Result.emplace_back(Field->getName(), Field->getType(), Access,
                        false, false);
  }

  return Result;
}

llvm::SmallVector<MemberInfo, 16> TypeInfo::getMethods() const {
  llvm::SmallVector<MemberInfo, 16> Result;
  if (!Record)
    return Result;

  for (auto *Method : Record->methods()) {
    if (!Method)
      continue;
    AccessSpecifier Access = Method->getAccess();
    bool IsStatic = Method->isStatic();
    Result.emplace_back(Method->getName(), Method->getType(), Access,
                        IsStatic, true);
  }

  return Result;
}

llvm::SmallVector<const CXXRecordDecl *, 4> TypeInfo::getBases() const {
  llvm::SmallVector<const CXXRecordDecl *, 4> Result;
  if (!Record)
    return Result;

  for (const auto &Base : Record->bases()) {
    QualType BaseType = Base.getType();
    if (BaseType.isNull())
      continue;

    // Resolve the base type to a RecordType to get the CXXRecordDecl
    if (auto *RT = dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        Result.push_back(CXXRD);
      }
    }
  }

  return Result;
}

llvm::StringRef TypeInfo::getName() const {
  if (!Record)
    return "";
  return Record->getName();
}

bool TypeInfo::hasMember(llvm::StringRef Name) const {
  if (!Record)
    return false;

  // Check fields
  for (auto *Field : Record->fields()) {
    if (Field && Field->getName() == Name)
      return true;
  }

  // Check methods
  for (auto *Method : Record->methods()) {
    if (Method && Method->getName() == Name)
      return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Reflection utility functions
//===----------------------------------------------------------------------===//

AccessSpecifier getAccessSpecifier(const Decl *D) {
  if (!D)
    return AS_none;

  // For FieldDecl, use its stored access specifier
  if (auto *FD = dyn_cast<FieldDecl>(D)) {
    return FD->getAccess();
  }

  // For CXXMethodDecl, derive from parent's current access
  if (auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    return MD->getAccess();
  }

  return AS_none;
}

llvm::StringRef getAccessSpecifierName(AccessSpecifier AS) {
  switch (AS) {
  case AccessSpecifier::AS_public:    return "public";
  case AccessSpecifier::AS_protected: return "protected";
  case AccessSpecifier::AS_private:   return "private";
  default:                            return "";
  }
}

} // namespace meta
} // namespace blocktype

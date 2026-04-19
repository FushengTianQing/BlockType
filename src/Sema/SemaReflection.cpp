//===--- SemaReflection.cpp - Reflection Semantic Analysis Implementation ===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SemaReflection class for C++26 static reflection.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/SemaReflection.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/DiagnosticIDs.h"
#include "llvm/Support/Casting.h"
#include <sstream>

namespace blocktype {

using llvm::dyn_cast;
using llvm::cast;

//===----------------------------------------------------------------------===//
// reflexpr validation
//===----------------------------------------------------------------------===//

bool SemaReflection::ValidateReflexprType(QualType T, SourceLocation Loc) {
  if (T.isNull()) {
    S.Diag(Loc, DiagID::err_reflexpr_no_type);
    return false;
  }

  // Unresolved types cannot be reflected
  if (dyn_cast<UnresolvedType>(T.getTypePtr())) {
    S.Diag(Loc, DiagID::err_reflexpr_unresolved_type,
           cast<UnresolvedType>(T.getTypePtr())->getName());
    return false;
  }

  return true;
}

bool SemaReflection::ValidateReflexprExpr(Expr *E, SourceLocation Loc) {
  if (!E) {
    S.Diag(Loc, DiagID::err_reflexpr_no_type);
    return false;
  }

  // Type-dependent expressions are allowed (will be resolved later)
  // but we validate non-dependent expressions
  if (!E->isTypeDependent()) {
    QualType ET = E->getType();
    if (ET.isNull()) {
      S.Diag(Loc, DiagID::err_reflexpr_invalid_operand);
      return false;
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Type introspection API
//===----------------------------------------------------------------------===//

meta::TypeInfo SemaReflection::getTypeInfo(QualType T) {
  if (T.isNull())
    return meta::TypeInfo(nullptr);

  // Strip references and pointers to get to the underlying record type
  const Type *Ty = T.getTypePtr();
  while (Ty) {
    if (auto *RT = dyn_cast<RecordType>(Ty)) {
      if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        return meta::TypeInfo(CXXRD);
      }
      // Plain RecordDecl without CXX features — still return null
      // since TypeInfo requires CXXRecordDecl for methods/bases.
      return meta::TypeInfo(nullptr);
    }
    if (auto *PT = dyn_cast<PointerType>(Ty)) {
      Ty = PT->getPointeeType();
      continue;
    }
    // For reference types, unwrap
    // ReferenceType is abstract; check LValueReference/RValueReference
    break;
  }

  return meta::TypeInfo(nullptr);
}

meta::TypeInfo SemaReflection::getTypeInfo(const CXXRecordDecl *RD) {
  return meta::TypeInfo(RD);
}

//===----------------------------------------------------------------------===//
// Member traversal API
//===----------------------------------------------------------------------===//

void SemaReflection::forEachMember(
    const CXXRecordDecl *RD,
    llvm::function_ref<void(const meta::MemberInfo &)> Callback) {
  if (!RD)
    return;

  meta::TypeInfo TI(RD);
  auto Members = TI.getMembers();
  for (const auto &M : Members) {
    Callback(M);
  }
}

//===----------------------------------------------------------------------===//
// Metadata generation helpers
//===----------------------------------------------------------------------===//

std::string SemaReflection::getMetadataName(QualType T) {
  if (T.isNull())
    return "meta.norefl";

  std::string Name;
  llvm::raw_string_ostream OS(Name);

  // Build a qualified name from the type
  if (auto *RT = dyn_cast<RecordType>(T.getTypePtr())) {
    OS << "meta.type." << RT->getDecl()->getName();
  } else if (auto *BT = dyn_cast<BuiltinType>(T.getTypePtr())) {
    OS << "meta.builtin.";
    // Use a simplified representation
    switch (BT->getKind()) {
    case BuiltinKind::Void:   OS << "void"; break;
    case BuiltinKind::Bool:   OS << "bool"; break;
    case BuiltinKind::Int:    OS << "int"; break;
    case BuiltinKind::Float:  OS << "float"; break;
    case BuiltinKind::Double: OS << "double"; break;
    default:                  OS << "unknown"; break;
    }
  } else {
    OS << "meta.type.anon";
  }

  OS.flush();
  return Name;
}

std::string SemaReflection::getMetadataName(const Decl *D) {
  if (!D)
    return "meta.nodecl";

  std::string Name;
  llvm::raw_string_ostream OS(Name);
  // Decl base class doesn't have getName(); use NamedDecl if available.
  if (auto *ND = dyn_cast<NamedDecl>(D)) {
    OS << "meta.decl." << ND->getName();
  } else {
    OS << "meta.decl.anon";
  }
  OS.flush();
  return Name;
}

//===----------------------------------------------------------------------===//
// Built-in reflection functions
//===----------------------------------------------------------------------===//

ExprResult SemaReflection::ActOnReflectType(SourceLocation Loc, Expr *E) {
  if (!E) {
    S.Diag(Loc, DiagID::err_reflexpr_invalid_operand);
    return ExprResult(nullptr);
  }

  QualType ET = E->getType();
  if (ET.isNull()) {
    S.Diag(Loc, DiagID::err_reflexpr_invalid_operand);
    return ExprResult(nullptr);
  }

  // __reflect_type(expr) is equivalent to reflexpr(decltype(expr))
  // Create a ReflexprExpr with the type operand
  auto &Ctx = S.getASTContext();
  auto *RE = Ctx.create<ReflexprExpr>(Loc, ET);
  RE->setType(Ctx.getMetaInfoType());
  RE->setResultType(Ctx.getMetaInfoType());
  return ExprResult(RE);
}

ExprResult SemaReflection::ActOnReflectMembers(SourceLocation Loc, QualType T) {
  if (T.isNull()) {
    S.Diag(Loc, DiagID::err_reflexpr_no_type);
    return ExprResult(nullptr);
  }

  // __reflect_members(type) reflects all members of the type.
  // This creates a ReflexprExpr whose result can be queried for members.
  auto &Ctx = S.getASTContext();
  auto *RE = Ctx.create<ReflexprExpr>(Loc, T);
  RE->setType(Ctx.getMetaInfoType());
  RE->setResultType(Ctx.getMetaInfoType());
  return ExprResult(RE);
}

} // namespace blocktype

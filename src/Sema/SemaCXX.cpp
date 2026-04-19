//===--- SemaCXX.cpp - C++ Semantic Analysis -------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements C++ specific semantic analysis for BlockType.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/SemaCXX.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/DiagnosticIDs.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Deducing this (P0847R7)
//===----------------------------------------------------------------------===//

bool SemaCXX::CheckExplicitObjectParameter(FunctionDecl *FD,
                                           ParmVarDecl *Param,
                                           SourceLocation ParamLoc) {
  if (!FD || !Param)
    return false;

  bool Valid = true;

  // Rule: Cannot be used with static member functions.
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
    if (MD->isStatic()) {
      S.Diag(ParamLoc, DiagID::err_explicit_object_param_static);
      Valid = false;
    }
    // Rule: Cannot be used with virtual functions.
    if (MD->isVirtual()) {
      S.Diag(ParamLoc, DiagID::err_explicit_object_param_virtual);
      Valid = false;
    }
  }

  // Rule: Type must be a reference to the class type, or the class type by
  // value. This is enforced as a soft check — we allow it to proceed but emit
  // a diagnostic if the type is obviously wrong.
  QualType ParamType = Param->getType();
  if (!ParamType.isNull()) {
    // Check if the parameter type is this class type or a reference to it.
    if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
      CXXRecordDecl *Parent = MD->getParent();
      if (Parent) {
        bool TypeOK = false;

        // Unwrap reference to get pointee
        QualType InnerType = ParamType;
        if (auto *RefTy = llvm::dyn_cast<ReferenceType>(ParamType.getTypePtr())) {
          InnerType = QualType(RefTy->getReferencedType(), Qualifier::None);
          TypeOK = true; // References are always provisionally accepted
        }

        // Check if inner type is the class record type (by value)
        if (!TypeOK) {
          if (auto *RecTy = llvm::dyn_cast<RecordType>(InnerType.getTypePtr())) {
            if (RecTy->getDecl() == Parent)
              TypeOK = true;
          }
        }

        if (!TypeOK) {
          S.Diag(ParamLoc, DiagID::err_explicit_object_param_type);
          Valid = false;
        }
      }
    }
  }

  return Valid;
}

QualType SemaCXX::DeduceExplicitObjectType(QualType ObjectType,
                                           QualType ParamType,
                                           ExprValueKind VK) {
  // If the parameter is a forwarding reference (T&&), deduce accordingly.
  // If it's a reference (T& or T&&), get the pointee and match.
  // If it's by value, just use the object type.

  if (ParamType.isNull() || ObjectType.isNull())
    return QualType();

  // For now, return the object type as-is.
  // Full deduction with template argument deduction will be implemented
  // when template deducing-this support is complete.
  return ObjectType;
}

//===----------------------------------------------------------------------===//
// Static operator (P1169R4, P2589R1)
//===----------------------------------------------------------------------===//

bool SemaCXX::CheckStaticOperator(CXXMethodDecl *MD, SourceLocation Loc) {
  if (!MD)
    return false;

  bool Valid = true;

  // Static operators must be member functions.
  if (!MD->getParent()) {
    S.Diag(Loc, DiagID::err_static_operator_not_method);
    Valid = false;
  }

  // Check if this is operator() or operator[]
  llvm::StringRef Name = MD->getName();
  if (Name != "operator()" && Name != "operator[]") {
    S.Diag(Loc, DiagID::err_static_operator_not_method);
    Valid = false;
  }

  return Valid;
}

//===----------------------------------------------------------------------===//
// Contracts (P2900R14) - placeholder
//===----------------------------------------------------------------------===//

bool SemaCXX::CheckContractCondition(Expr *Cond, SourceLocation Loc) {
  // Placeholder for contract condition checking.
  // Will be implemented in Stage 7.5+.
  return Cond != nullptr;
}

} // namespace blocktype

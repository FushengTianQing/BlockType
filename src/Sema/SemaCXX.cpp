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

  // If ParamType is a reference, unwrap it and return the referenced type
  // with appropriate cv-qualification based on the value category.
  if (auto *RefTy = llvm::dyn_cast<ReferenceType>(ParamType.getTypePtr())) {
    QualType Pointee = QualType(RefTy->getReferencedType(), Qualifier::None);

    // For lvalue reference (T&): the object type with added lvalue reference.
    // For rvalue reference (T&&): if VK is XValue or PRValue, std::move semantics.
    // For now, return the pointee type — the actual reference construction happens
    // at the call site when we pass the object.
    return Pointee;
  }

  // By-value parameter: return the decayed object type.
  QualType Result = ObjectType;
  Result = Result.withoutConstQualifier().withoutVolatileQualifier();

  // Array-to-pointer decay
  if (auto *ArrTy = llvm::dyn_cast<ArrayType>(Result.getTypePtr())) {
    auto *PtrTy = S.getASTContext().getPointerType(ArrTy->getElementType());
    return QualType(PtrTy, Qualifier::None);
  }

  // Function-to-pointer decay
  if (auto *FnTy = llvm::dyn_cast<FunctionType>(Result.getTypePtr())) {
    auto *PtrTy = S.getASTContext().getPointerType(Result.getTypePtr());
    return QualType(PtrTy, Qualifier::None);
  }

  return Result;
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

  // P7.1.3: Static operators cannot use 'this' pointer.
  // Check if the function body contains CXXThisExpr.
  if (Valid && MD->getBody()) {
    checkBodyForThisUse(MD->getBody(), Loc);
  }

  return Valid;
}

void SemaCXX::checkBodyForThisUse(Stmt *Body, SourceLocation Loc) {
  if (!Body) return;

  // Use the Stmt/Expr node kind to check for CXXThisExpr
  switch (Body->getKind()) {
  case ASTNode::NodeKind::CXXThisExprKind:
    S.Diag(Loc, DiagID::err_static_operator_this);
    return;
  default:
    break;
  }

  // Try to recurse into compound statements
  if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    for (auto *Child : CS->getBody()) {
      if (Child) checkBodyForThisUse(Child, Loc);
    }
  } else if (auto *RS = llvm::dyn_cast<ReturnStmt>(Body)) {
    // ReturnStmt::getRetValue() returns Expr* which inherits from ASTNode
    if (RS->getRetValue()) {
      if (RS->getRetValue()->getKind() == ASTNode::NodeKind::CXXThisExprKind)
        S.Diag(Loc, DiagID::err_static_operator_this);
    }
  } else if (auto *ES = llvm::dyn_cast<ExprStmt>(Body)) {
    if (ES->getExpr()) {
      if (ES->getExpr()->getKind() == ASTNode::NodeKind::CXXThisExprKind)
        S.Diag(Loc, DiagID::err_static_operator_this);
    }
  } else if (auto *IS = llvm::dyn_cast<IfStmt>(Body)) {
    if (IS->getCond() && IS->getCond()->getKind() == ASTNode::NodeKind::CXXThisExprKind)
      S.Diag(Loc, DiagID::err_static_operator_this);
    if (IS->getThen()) checkBodyForThisUse(IS->getThen(), Loc);
    if (IS->getElse()) checkBodyForThisUse(IS->getElse(), Loc);
  } else if (auto *WS = llvm::dyn_cast<WhileStmt>(Body)) {
    if (WS->getCond() && WS->getCond()->getKind() == ASTNode::NodeKind::CXXThisExprKind)
      S.Diag(Loc, DiagID::err_static_operator_this);
    if (WS->getBody()) checkBodyForThisUse(WS->getBody(), Loc);
  } else if (auto *FS = llvm::dyn_cast<ForStmt>(Body)) {
    if (FS->getCond() && FS->getCond()->getKind() == ASTNode::NodeKind::CXXThisExprKind)
      S.Diag(Loc, DiagID::err_static_operator_this);
    if (FS->getInit()) checkBodyForThisUse(FS->getInit(), Loc);
    if (FS->getInc() && FS->getInc()->getKind() == ASTNode::NodeKind::CXXThisExprKind)
      S.Diag(Loc, DiagID::err_static_operator_this);
    if (FS->getBody()) checkBodyForThisUse(FS->getBody(), Loc);
  }
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

//===--- ExprVisitor.h - Expression Visitor Pattern ------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ExprVisitor class template for visiting and
// transforming expression AST nodes. Similar to TypeVisitor but for Exprs.
//
// P7.4.3: Used by StmtCloner for cloning expressions with type substitution.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_AST_EXPR_VISITOR_H
#define BLOCKTYPE_AST_EXPR_VISITOR_H

#include "blocktype/AST/Expr.h"
#include "llvm/ADT/STLExtras.h"

namespace blocktype {

/// ExprVisitor - CRTP base class for visiting Expr nodes.
///
/// Usage:
///   class MyVisitor : public ExprVisitor<MyVisitor> {
///   public:
///     Expr *VisitBinaryOperator(BinaryOperator *BO) { ... }
///     Expr *VisitCallExpr(CallExpr *CE) { ... }
///   };
///
///   MyVisitor Visitor;
///   Expr *Result = Visitor.Visit(SomeExpr);
template <typename ImplClass>
class ExprVisitor {
public:
  /// Visit an Expr node and dispatch to the appropriate Visit* method.
  Expr *Visit(Expr *E) {
    if (!E) {
      return nullptr;
    }

    switch (E->getKind()) {
    case ASTNode::NodeKind::BinaryOperatorKind:
      return static_cast<ImplClass *>(this)->VisitBinaryOperator(
          llvm::cast<BinaryOperator>(E));
    
    case ASTNode::NodeKind::UnaryOperatorKind:
      return static_cast<ImplClass *>(this)->VisitUnaryOperator(
          llvm::cast<UnaryOperator>(E));
    
    case ASTNode::NodeKind::CallExprKind:
      return static_cast<ImplClass *>(this)->VisitCallExpr(
          llvm::cast<CallExpr>(E));
    
    case ASTNode::NodeKind::DeclRefExprKind:
      return static_cast<ImplClass *>(this)->VisitDeclRefExpr(
          llvm::cast<DeclRefExpr>(E));
    
    case ASTNode::NodeKind::IntegerLiteralKind:
      return static_cast<ImplClass *>(this)->VisitIntegerLiteral(
          llvm::cast<IntegerLiteral>(E));
    
    case ASTNode::NodeKind::FloatingLiteralKind:
      return static_cast<ImplClass *>(this)->VisitFloatingLiteral(
          llvm::cast<FloatingLiteral>(E));
    
    case ASTNode::NodeKind::StringLiteralKind:
      return static_cast<ImplClass *>(this)->VisitStringLiteral(
          llvm::cast<StringLiteral>(E));
    
    case ASTNode::NodeKind::CXXConstructExprKind:
      return static_cast<ImplClass *>(this)->VisitCXXConstructExpr(
          llvm::cast<CXXConstructExpr>(E));
    
    case ASTNode::NodeKind::CastExprKind:
      return static_cast<ImplClass *>(this)->VisitCastExpr(
          llvm::cast<CastExpr>(E));
    
    case ASTNode::NodeKind::MemberExprKind:
      return static_cast<ImplClass *>(this)->VisitMemberExpr(
          llvm::cast<MemberExpr>(E));
    
    case ASTNode::NodeKind::ArraySubscriptExprKind:
      return static_cast<ImplClass *>(this)->VisitArraySubscriptExpr(
          llvm::cast<ArraySubscriptExpr>(E));
    
    case ASTNode::NodeKind::PackIndexingExprKind:
      return static_cast<ImplClass *>(this)->VisitPackIndexingExpr(
          llvm::cast<PackIndexingExpr>(E));
    
    default:
      // For unsupported types, return original
      return E;
    }
  }

  /// Default implementation: return unchanged
  Expr *VisitExpr(Expr *E) {
    return E;
  }

  //===------------------------------------------------------------------===//
  // Visit methods for common Expr subclasses
  //===------------------------------------------------------------------===//

  Expr *VisitBinaryOperator(BinaryOperator *BO) {
    // Recurse into LHS and RHS
    Expr *NewLHS = Visit(BO->getLHS());
    Expr *NewRHS = Visit(BO->getRHS());
    
    if (NewLHS == BO->getLHS() && NewRHS == BO->getRHS()) {
      return BO; // No change
    }
    
    // Create new BinaryOperator with cloned operands
    return new BinaryOperator(BO->getLocation(), NewLHS, NewRHS, BO->getOpcode());
  }

  Expr *VisitUnaryOperator(UnaryOperator *UO) {
    // Recurse into subexpression
    Expr *NewSub = Visit(UO->getSubExpr());
    
    if (NewSub == UO->getSubExpr()) {
      return UO; // No change
    }
    
    // Create new UnaryOperator with cloned subexpression
    return new UnaryOperator(UO->getLocation(), NewSub, UO->getOpcode());
  }

  Expr *VisitCallExpr(CallExpr *CE) {
    // Recurse into callee and arguments
    Expr *NewCallee = Visit(CE->getCallee());
    
    llvm::SmallVector<Expr *, 4> NewArgs;
    bool Changed = false;
    
    for (Expr *Arg : CE->getArgs()) {
      Expr *NewArg = Visit(Arg);
      NewArgs.push_back(NewArg);
      if (NewArg != Arg) {
        Changed = true;
      }
    }
    
    if (NewCallee == CE->getCallee() && !Changed) {
      return CE; // No change
    }
    
    // Create new CallExpr with cloned callee and arguments
    return new CallExpr(CE->getLocation(), NewCallee, NewArgs);
  }

  Expr *VisitDeclRefExpr(DeclRefExpr *DRE) {
    // TODO: Look up cloned Decl if this is a variable reference
    return static_cast<ImplClass *>(this)->VisitExpr(DRE);
  }

  Expr *VisitIntegerLiteral(IntegerLiteral *IL) {
    // Literals don't need substitution
    return static_cast<ImplClass *>(this)->VisitExpr(IL);
  }

  Expr *VisitFloatingLiteral(FloatingLiteral *FL) {
    return static_cast<ImplClass *>(this)->VisitExpr(FL);
  }

  Expr *VisitStringLiteral(StringLiteral *SL) {
    return static_cast<ImplClass *>(this)->VisitExpr(SL);
  }

  Expr *VisitCXXConstructExpr(CXXConstructExpr *CE) {
    // TODO: Recurse into constructor arguments
    return static_cast<ImplClass *>(this)->VisitExpr(CE);
  }

  Expr *VisitCastExpr(CastExpr *CE) {
    // Recurse into subexpression
    Expr *NewSub = Visit(CE->getSubExpr());
    
    if (NewSub == CE->getSubExpr()) {
      return CE; // No change
    }
    
    // TODO: Create new CastExpr with NewSub
    return CE; // Placeholder
  }

  Expr *VisitMemberExpr(MemberExpr *ME) {
    // Recurse into base
    Expr *NewBase = Visit(ME->getBase());
    
    if (NewBase == ME->getBase()) {
      return ME; // No change
    }
    
    // TODO: Create new MemberExpr with NewBase
    return ME; // Placeholder
  }

  Expr *VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
    // Recurse into base and index
    Expr *NewBase = Visit(ASE->getBase());
    Expr *NewIndex = Visit(ASE->getIndex());
    
    if (NewBase == ASE->getBase() && NewIndex == ASE->getIndex()) {
      return ASE; // No change
    }
    
    // TODO: Create new ArraySubscriptExpr
    return ASE; // Placeholder
  }

  Expr *VisitPackIndexingExpr(PackIndexingExpr *PIE) {
    // Recurse into pack and index sub-expressions
    Expr *NewPack = Visit(PIE->getPack());
    Expr *NewIndex = Visit(PIE->getIndex());

    if (NewPack == PIE->getPack() && NewIndex == PIE->getIndex()) {
      return PIE; // No change
    }

    // TODO: Create new PackIndexingExpr with substituted sub-expressions
    return PIE;
  }

  // Add more Visit* methods as needed
};

} // namespace blocktype

#endif // BLOCKTYPE_AST_EXPR_VISITOR_H

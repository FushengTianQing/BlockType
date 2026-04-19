//===--- StmtCloner.cpp - Statement Cloner Implementation ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/StmtCloner.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

/// ExprCloner - Internal visitor that clones expressions with type substitution.
class ExprCloner : public ExprVisitor<ExprCloner> {
  StmtCloner &Parent;

public:
  explicit ExprCloner(StmtCloner &P) : Parent(P) {}

  /// Clone a DeclRefExpr, looking up cloned Decl if available.
  Expr *VisitDeclRefExpr(DeclRefExpr *DRE) {
    // Look up cloned Decl
    if (auto *ClonedDecl = Parent.lookupClonedDecl(DRE->getDecl())) {
      // TODO: Create new DeclRefExpr with ClonedDecl
      // For now, return original
    }
    return DRE;
  }

  // Use default implementations for other expressions (they recurse automatically)
};

Stmt *StmtCloner::Clone(Stmt *OriginalStmt) {
  if (!OriginalStmt) {
    return nullptr;
  }

  switch (OriginalStmt->getKind()) {
  case ASTNode::NodeKind::CompoundStmtKind:
    return CloneCompoundStmt(llvm::cast<CompoundStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::DeclStmtKind:
    return CloneDeclStmt(llvm::cast<DeclStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::ReturnStmtKind:
    return CloneReturnStmt(llvm::cast<ReturnStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::IfStmtKind:
    return CloneIfStmt(llvm::cast<IfStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::WhileStmtKind:
    return CloneWhileStmt(llvm::cast<WhileStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::ForStmtKind:
    return CloneForStmt(llvm::cast<ForStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::BreakStmtKind:
    return CloneBreakStmt(llvm::cast<BreakStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::ContinueStmtKind:
    return CloneContinueStmt(llvm::cast<ContinueStmt>(OriginalStmt));
    
  case ASTNode::NodeKind::ExprStmtKind:
    return CloneExprStmt(llvm::cast<ExprStmt>(OriginalStmt));
    
  default:
    // TODO: Add support for more statement types
    llvm::errs() << "Warning: Unsupported Stmt kind in cloning: "
                 << static_cast<int>(OriginalStmt->getKind()) << "\n";
    return OriginalStmt; // Return original as fallback
  }
}

Expr *StmtCloner::CloneExpr(Expr *OriginalExpr) {
  if (!OriginalExpr) {
    return nullptr;
  }

  // Use ExprCloner visitor to recursively clone the expression
  ExprCloner Cloner(*this);
  return Cloner.Visit(OriginalExpr);
}

Stmt *StmtCloner::CloneCompoundStmt(CompoundStmt *CS) {
  llvm::SmallVector<Stmt *, 8> ClonedBody;
  
  for (Stmt *S : CS->getBody()) {
    Stmt *Cloned = Clone(S);
    if (Cloned) {
      ClonedBody.push_back(Cloned);
    }
  }
  
  // Create new CompoundStmt with cloned body
  return new CompoundStmt(CS->getLocation(), ClonedBody);
}

Stmt *StmtCloner::CloneDeclStmt(DeclStmt *DS) {
  // TODO: Clone declarations with type substitution
  // This is complex because it involves Decl cloning
  return DS; // Placeholder
}

Stmt *StmtCloner::CloneReturnStmt(ReturnStmt *RS) {
  Expr *ClonedExpr = CloneExpr(RS->getRetValue());
  
  if (ClonedExpr == RS->getRetValue()) {
    return RS; // No change
  }
  
  // TODO: Create new ReturnStmt with ClonedExpr
  // For now, return original
  return RS;
}

Stmt *StmtCloner::CloneIfStmt(IfStmt *IS) {
  Expr *ClonedCond = CloneExpr(IS->getCond());
  Stmt *ClonedThen = Clone(IS->getThen());
  Stmt *ClonedElse = IS->getElse() ? Clone(IS->getElse()) : nullptr;
  
  bool Changed = (ClonedCond != IS->getCond()) ||
                 (ClonedThen != IS->getThen()) ||
                 (ClonedElse != IS->getElse());
  
  if (!Changed) {
    return IS; // No change
  }
  
  // TODO: Create new IfStmt with cloned components
  return IS; // Placeholder
}

Stmt *StmtCloner::CloneWhileStmt(WhileStmt *WS) {
  Expr *ClonedCond = CloneExpr(WS->getCond());
  Stmt *ClonedBody = Clone(WS->getBody());
  
  if (ClonedCond == WS->getCond() && ClonedBody == WS->getBody()) {
    return WS; // No change
  }
  
  // TODO: Create new WhileStmt
  return WS; // Placeholder
}

Stmt *StmtCloner::CloneForStmt(ForStmt *FS) {
  Stmt *ClonedInit = FS->getInit() ? Clone(FS->getInit()) : nullptr;
  Expr *ClonedCond = FS->getCond() ? CloneExpr(FS->getCond()) : nullptr;
  Expr *ClonedInc = FS->getInc() ? CloneExpr(FS->getInc()) : nullptr;
  Stmt *ClonedBody = Clone(FS->getBody());
  
  bool Changed = (ClonedInit != FS->getInit()) ||
                 (ClonedCond != FS->getCond()) ||
                 (ClonedInc != FS->getInc()) ||
                 (ClonedBody != FS->getBody());
  
  if (!Changed) {
    return FS; // No change
  }
  
  // TODO: Create new ForStmt
  return FS; // Placeholder
}

Stmt *StmtCloner::CloneBreakStmt(BreakStmt *BS) {
  return new BreakStmt(BS->getLocation());
}

Stmt *StmtCloner::CloneContinueStmt(ContinueStmt *CS) {
  return new ContinueStmt(CS->getLocation());
}

Stmt *StmtCloner::CloneExprStmt(ExprStmt *ES) {
  Expr *ClonedExpr = CloneExpr(ES->getExpr());
  // TODO: Create new ExprStmt with cloned expression
  return ES; // Placeholder
}

} // namespace blocktype

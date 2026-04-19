//===--- StmtCloner.h - Statement Cloner -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the StmtCloner class, which performs deep copying of
// AST statement nodes. Used during template instantiation to clone function
// bodies with substituted types.
//
// P7.4.3: Supports cloning template function bodies.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_AST_STMT_CLONER_H
#define BLOCKTYPE_AST_STMT_CLONER_H

#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/ExprVisitor.h"  // For ExprVisitor
#include "blocktype/Sema/TemplateInstantiation.h"
#include "llvm/ADT/DenseMap.h"

namespace blocktype {

/// StmtCloner - Deep copies AST statements with optional type substitution.
///
/// Usage:
///   TemplateInstantiation Inst;
///   // ... add substitutions ...
///   StmtCloner Cloner(Inst);
///   Stmt *Cloned = Cloner.Clone(OriginalStmt);
class StmtCloner {
  TemplateInstantiation &Instantiator;
  /// Map from original Decls to cloned Decls (for variable references).
  llvm::DenseMap<const Decl *, Decl *> DeclMapping;

public:
  explicit StmtCloner(TemplateInstantiation &Inst) : Instantiator(Inst) {}

  /// Clone a statement node and all its children.
  Stmt *Clone(Stmt *S);

  /// Clone an expression node and all its children.
  Expr *CloneExpr(Expr *E);

  /// Register a mapping from original Decl to cloned Decl.
  void registerDeclMapping(const Decl *Original, Decl *Cloned) {
    DeclMapping[Original] = Cloned;
  }

  /// Look up a cloned Decl by original Decl.
  Decl *lookupClonedDecl(const Decl *Original) const {
    auto It = DeclMapping.find(Original);
    return It != DeclMapping.end() ? It->second : nullptr;
  }

private:
  //===------------------------------------------------------------------===//
  // Clone methods for each Stmt subclass
  //===------------------------------------------------------------------===//

  Stmt *CloneCompoundStmt(CompoundStmt *CS);
  Stmt *CloneDeclStmt(DeclStmt *DS);
  Stmt *CloneReturnStmt(ReturnStmt *RS);
  Stmt *CloneIfStmt(IfStmt *IS);
  Stmt *CloneWhileStmt(WhileStmt *WS);
  Stmt *CloneForStmt(ForStmt *FS);
  Stmt *CloneBreakStmt(BreakStmt *BS);
  Stmt *CloneContinueStmt(ContinueStmt *CS);
  
  // Expression statements
  Stmt *CloneExprStmt(ExprStmt *ES);
  
  // Add more Clone* methods as needed
};

} // namespace blocktype

#endif // BLOCKTYPE_AST_STMT_CLONER_H

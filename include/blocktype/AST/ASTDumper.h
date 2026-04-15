//===--- ASTDumper.h - AST Debugging ------------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ASTDumper class for debugging AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

/// ASTDumper - Dumps AST nodes in a human-readable format.
class ASTDumper {
  llvm::raw_ostream &OS;
  unsigned Indent = 0;
  bool LastChild = false;

public:
  explicit ASTDumper(llvm::raw_ostream &OS) : OS(OS) {}

  /// Dumps an AST node.
  void dump(const ASTNode *Node);

  /// Dumps an expression.
  void dump(const Expr *E);

  /// Dumps a statement.
  void dump(const Stmt *S);

  /// Dumps a type.
  void dump(const Type *T);

  /// Dumps a qualified type.
  void dump(QualType QT);

private:
  /// Increases indentation.
  void indent() { Indent += 2; }

  /// Decreases indentation.
  void dedent() { Indent -= 2; }

  /// Prints indentation.
  void printIndent();

  /// Prints a child marker.
  void printChildMarker(bool IsLast);

  /// Dumps the children of a node.
  template <typename T>
  void dumpChildren(llvm::ArrayRef<T *> Children) {
    for (size_t i = 0; i < Children.size(); ++i) {
      bool IsLast = (i == Children.size() - 1);
      printChildMarker(IsLast);
      bool OldLast = LastChild;
      LastChild = IsLast;
      indent();
      dump(Children[i]);
      dedent();
      LastChild = OldLast;
    }
  }

  /// Dumps an expression node.
  void dumpExpr(const Expr *E);

  /// Dumps a statement node.
  void dumpStmt(const Stmt *S);

  /// Dumps a type node.
  void dumpType(const Type *T);
};

} // namespace blocktype

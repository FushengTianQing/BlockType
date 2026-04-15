//===--- ASTVisitor.h - AST Visitor Pattern ------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ASTVisitor template for traversing AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/Expr.h"
#include "llvm/Support/Casting.h"

namespace blocktype {

// Forward declarations
class Stmt;
class Decl;

/// ASTVisitor - CRTP base class for AST visitors.
///
/// This implements the visitor pattern for AST traversal. Derived classes
/// should implement visit methods for specific node types.
///
/// Usage:
/// ```cpp
/// class MyVisitor : public ASTVisitor<MyVisitor> {
/// public:
///   void visitIntegerLiteral(IntegerLiteral *E) {
///     // Handle integer literal
///   }
///   void visitBinaryOperator(BinaryOperator *E) {
///     // Handle binary operator
///     visit(E->getLHS());  // Traverse children
///     visit(E->getRHS());
///   }
/// };
/// ```
///
/// Traversal Order:
/// - Pre-order: Visit node before children
/// - Post-order: Visit node after children
/// - The default is pre-order traversal
///
template <typename Derived>
class ASTVisitor {
public:
  /// Visit an AST node. Dispatches to the appropriate visit method.
  void visit(ASTNode *Node) {
    if (!Node)
      return;

    // Dispatch based on node kind
    switch (Node->getKind()) {
    default:
      // TODO: Handle Stmt and Decl once they are defined
      derived().visitASTNode(Node);
      break;

// Expression nodes
#define EXPR(Type, Base)                                                       \
  case ASTNode::Type##Kind:                                                    \
    derived().visit##Type(cast<Type>(Node));                                   \
    break;
#define ABSTRACT_EXPR(Type, Base) EXPR(Type, Base)
#define STMT(Type, Base)
#define ABSTRACT_STMT(Type, Base)
#define DECL(Type, Base)
#define ABSTRACT_DECL(Type, Base)
#include "blocktype/AST/NodeKinds.def"
#undef EXPR
#undef ABSTRACT_EXPR
#undef STMT
#undef ABSTRACT_STMT
#undef DECL
#undef ABSTRACT_DECL
    }
  }

  void visit(const ASTNode *Node) {
    visit(const_cast<ASTNode *>(Node));
  }

protected:
  /// Returns the derived class.
  Derived &derived() { return *static_cast<Derived *>(this); }
  const Derived &derived() const {
    return *static_cast<const Derived *>(this);
  }

  // Default visit methods for each node type.
  // Derived classes can override these.

// Expression nodes
#define EXPR(Type, Base)                                                       \
  void visit##Type(Type *Node) { visit##Base(Node); }
#define ABSTRACT_EXPR(Type, Base)                                              \
  void visit##Type(Type *Node) { /* No-op for abstract nodes */ }
#define STMT(Type, Base)
#define ABSTRACT_STMT(Type, Base)
#define DECL(Type, Base)
#define ABSTRACT_DECL(Type, Base)
#include "blocktype/AST/NodeKinds.def"
#undef EXPR
#undef ABSTRACT_EXPR
#undef STMT
#undef ABSTRACT_STMT
#undef DECL
#undef ABSTRACT_DECL

  // Base case
  void visitASTNode(ASTNode *Node) { /* No-op */ }
};

} // namespace blocktype

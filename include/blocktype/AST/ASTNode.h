//===--- ASTNode.h - AST Node Base Class ---------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ASTNode base class and related infrastructure.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "blocktype/Basic/SourceLocation.h"

namespace blocktype {

// Forward declarations
class ASTContext;
class Expr;
class Stmt;
class Decl;

/// ASTNode - Base class for all AST nodes.
///
/// All AST nodes inherit from this class. It provides:
/// - Source location tracking
/// - Type identification (without C++ RTTI)
/// - Debug dumping
///
/// Memory Management:
/// AST nodes are allocated by ASTContext and owned by it.
/// Do not delete AST nodes manually.
///
class ASTNode {
  friend class ASTContext;

protected:
  /// The source location of this node.
  SourceLocation Loc;

public:
  /// NodeKind - Enumeration of all AST node kinds.
  /// This is used for type identification without C++ RTTI.
  enum NodeKind {
    // Expressions
#define EXPR(Type, Base) Type##Kind,
#define ABSTRACT_EXPR(Type, Base) Type##Kind,
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
    
    // Statements
#define EXPR(Type, Base)
#define ABSTRACT_EXPR(Type, Base)
#define STMT(Type, Base) Type##Kind,
#define ABSTRACT_STMT(Type, Base) Type##Kind,
#define DECL(Type, Base)
#define ABSTRACT_DECL(Type, Base)
#include "blocktype/AST/NodeKinds.def"
#undef EXPR
#undef ABSTRACT_EXPR
#undef STMT
#undef ABSTRACT_STMT
#undef DECL
#undef ABSTRACT_DECL
    
    // Declarations
#define EXPR(Type, Base)
#define ABSTRACT_EXPR(Type, Base)
#define STMT(Type, Base)
#define ABSTRACT_STMT(Type, Base)
#define DECL(Type, Base) Type##Kind,
#define ABSTRACT_DECL(Type, Base) Type##Kind,
#include "blocktype/AST/NodeKinds.def"
#undef EXPR
#undef ABSTRACT_EXPR
#undef STMT
#undef ABSTRACT_STMT
#undef DECL
#undef ABSTRACT_DECL
    
    NumNodeKinds
  };

  ASTNode(SourceLocation Loc) : Loc(Loc) {}
  virtual ~ASTNode() = default;

  // Non-copyable and non-movable
  ASTNode(const ASTNode &) = delete;
  ASTNode &operator=(const ASTNode &) = delete;
  ASTNode(ASTNode &&) = delete;
  ASTNode &operator=(ASTNode &&) = delete;

  /// Returns the source location of this node.
  SourceLocation getLocation() const { return Loc; }

  /// Returns the kind of this node.
  virtual NodeKind getKind() const = 0;

  /// Dumps this node to the given stream for debugging.
  virtual void dump(raw_ostream &OS, unsigned Indent = 0) const = 0;

  /// Dumps this node to stderr for debugging.
  void dump() const;

protected:
  /// Helper to print indentation.
  static void printIndent(raw_ostream &OS, unsigned Indent) {
    for (unsigned I = 0; I < Indent; ++I)
      OS << "  ";
  }
};

//===----------------------------------------------------------------------===//
// Type Checking Support (isa, dyn_cast, cast)
//===----------------------------------------------------------------------===//

/// classof - Static type checking support.
/// Each derived class must implement:
///   static bool classof(const ASTNode *N)

/// isa<T>(Node) - Returns true if Node is of type T.
template <typename T>
inline bool isa(const ASTNode *Node) {
  return T::classof(Node);
}

/// dyn_cast<T>(Node) - Returns Node cast to T if it is of type T, else nullptr.
template <typename T>
inline T *dyn_cast(ASTNode *Node) {
  return isa<T>(Node) ? static_cast<T *>(Node) : nullptr;
}

template <typename T>
inline const T *dyn_cast(const ASTNode *Node) {
  return isa<T>(Node) ? static_cast<const T *>(Node) : nullptr;
}

/// cast<T>(Node) - Returns Node cast to T. Asserts if Node is not of type T.
template <typename T>
inline T *cast(ASTNode *Node) {
  assert(isa<T>(Node) && "Invalid cast!");
  return static_cast<T *>(Node);
}

template <typename T>
inline const T *cast(const ASTNode *Node) {
  assert(isa<T>(Node) && "Invalid cast!");
  return static_cast<const T *>(Node);
}

} // namespace blocktype

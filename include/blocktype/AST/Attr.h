//===--- Attr.h - Attribute AST Nodes ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the attribute AST nodes, including C++26 Contract
// attributes (P2900R14): [[pre:]], [[post:]], [[assert:]].
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Contract enumerations (P2900R14)
//===----------------------------------------------------------------------===//

/// ContractKind - The kind of contract assertion.
enum class ContractKind {
  Pre,    ///< [[pre: condition]] — precondition
  Post,   ///< [[post: condition]] — postcondition
  Assert  ///< [[assert: condition]] — assertion (block-level)
};

/// ContractMode - The checking mode for contracts.
enum class ContractMode {
  Off,       ///< No checking (contracts disabled)
  Default,   ///< Default mode (implementation-defined, usually Enforce)
  Enforce,   ///< Violation handler is called, then std::terminate
  Observe,   ///< Violation handler is called, execution continues
  Quick_Enforce ///< Same as Enforce but no violation handler
};

/// Get a string representation of a ContractKind.
inline llvm::StringRef getContractKindName(ContractKind K) {
  switch (K) {
  case ContractKind::Pre:    return "pre";
  case ContractKind::Post:   return "post";
  case ContractKind::Assert: return "assert";
  }
  return "unknown";
}

/// Get a string representation of a ContractMode.
inline llvm::StringRef getContractModeName(ContractMode M) {
  switch (M) {
  case ContractMode::Off:           return "off";
  case ContractMode::Default:       return "default";
  case ContractMode::Enforce:       return "enforce";
  case ContractMode::Observe:       return "observe";
  case ContractMode::Quick_Enforce: return "quick_enforce";
  }
  return "unknown";
}

//===----------------------------------------------------------------------===//
// ContractAttr - Contract attribute AST node
//===----------------------------------------------------------------------===//

/// ContractAttr - Represents a C++26 contract assertion (P2900R14).
///
/// Contracts are attributes that specify preconditions, postconditions,
/// and assertions for functions:
///   [[pre: x > 0]]
///   [[post: result > 0]]
///   [[assert: ptr != nullptr]]
///
/// **Clang reference**:
///   - clang::PreStmt / clang::PostStmt (AST nodes for contract conditions)
///   - clang::Sema::ActOnContractAssertAttr
///
/// In BlockType, ContractAttr is stored in the AttributeListDecl of
/// FunctionDecls (for pre/post) or as standalone statement attributes
/// (for assert). It is NOT an AttributeDecl — it has richer semantics.
class ContractAttr : public Decl {
  ContractKind Kind;
  Expr *Condition;
  ContractMode Mode;
  /// P1-3: For postconditions, the implicit 'result' variable declaration.
  ValueDecl *ResultDecl = nullptr;

public:
  ContractAttr(SourceLocation Loc, ContractKind K,
               Expr *Cond, ContractMode M = ContractMode::Default)
      : Decl(Loc), Kind(K), Condition(Cond), Mode(M) {}

  /// The kind of contract (pre/post/assert).
  ContractKind getContractKind() const { return Kind; }

  /// The condition expression.
  Expr *getCondition() const { return Condition; }

  /// The checking mode.
  ContractMode getContractMode() const { return Mode; }
  void setContractMode(ContractMode M) { Mode = M; }

  /// P1-3: The implicit 'result' variable for postconditions.
  ValueDecl *getResultDecl() const { return ResultDecl; }
  void setResultDecl(ValueDecl *VD) { ResultDecl = VD; }

  /// Convenience predicates.
  bool isPrecondition() const { return Kind == ContractKind::Pre; }
  bool isPostcondition() const { return Kind == ContractKind::Post; }
  bool isAssertion() const { return Kind == ContractKind::Assert; }

  /// Node kind.
  NodeKind getKind() const override { return NodeKind::ContractAttrKind; }

  /// Support for isa/cast/dyn_cast.
  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ContractAttrKind;
  }

  /// Dump for debugging.
  void dump(raw_ostream &OS, unsigned Indent = 0) const override;
};

} // namespace blocktype

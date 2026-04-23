//===--- TypeDeduction.h - Auto/Decltype Type Deduction -----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TypeDeduction class for auto and decltype type
// deduction.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

class DiagnosticsEngine;
class Sema;

/// TypeDeduction - Handles auto and decltype type deduction.
///
/// Follows the C++ standard rules:
/// - auto deduction: [dcl.spec.auto]
/// - decltype deduction: [dcl.type.decltype]
/// - decltype(auto) deduction: [dcl.spec.auto]
class TypeDeduction {
  ASTContext &Context;
  DiagnosticsEngine *Diags = nullptr;
  Sema *SemaRef = nullptr;

public:
  explicit TypeDeduction(ASTContext &C, DiagnosticsEngine *D = nullptr)
      : Context(C), Diags(D) {}

  /// Set the Sema reference for template argument deduction.
  void setSema(Sema &S) { SemaRef = &S; }

  //===------------------------------------------------------------------===//
  // auto deduction
  //===------------------------------------------------------------------===//

  /// Deduce the type for `auto x = init;`.
  /// Rules (C++ [dcl.spec.auto]):
  /// 1. Strip top-level reference from init type
  /// 2. Strip top-level const/volatile (unless declared as `const auto`)
  /// 3. Array decays to pointer
  /// 4. Function decays to function pointer
  QualType deduceAutoType(QualType DeclaredType, Expr *Init);

  /// Deduce the type for `auto& x = init;`.
  QualType deduceAutoRefType(QualType DeclaredType, Expr *Init);

  /// Deduce the type for `auto&& x = init;` (forwarding reference).
  QualType deduceAutoForwardingRefType(Expr *Init);

  /// Deduce the type for `auto* x = &init;`.
  QualType deduceAutoPointerType(Expr *Init);

  /// Deduce return type for `auto f() { return expr; }`.
  QualType deduceReturnType(Expr *ReturnExpr);

  /// Deduce from initializer list `auto x = {1, 2, 3}`.
  QualType deduceFromInitList(llvm::ArrayRef<Expr *> Inits);

  //===------------------------------------------------------------------===//
  // decltype deduction
  //===------------------------------------------------------------------===//

  /// Deduce the type for `decltype(expr)`.
  /// Rules (C++ [dcl.type.decltype]):
  /// - decltype(id) → declared type of id (no reference stripping)
  /// - decltype(expr) → type of expr, preserving value category
  /// - decltype((id)) → reference to declared type of id (always ref)
  QualType deduceDecltypeType(Expr *E);

  /// Deduce the type for `decltype((expr))` — double parentheses case.
  /// Per C++ [dcl.type.decltype]:
  ///   decltype((x)) always yields a reference type:
  ///   - If x is an lvalue of type T → T&
  ///   - If x is an xvalue of type T → T&&
  ///   - If x is a prvalue of type T → T (but parenthesized id
  ///     expressions are always lvalues, so this case is rare)
  QualType deduceDecltypeDoubleParenType(Expr *E);

  /// Deduce the type for `decltype(auto)`.
  /// Per C++ [dcl.spec.auto]: decltype(auto) uses decltype rules
  /// for deduction, preserving value category.
  QualType deduceDecltypeAutoType(Expr *E);

  /// Deduce the type for `const auto& x = init;`.
  /// Combines const qualifier with auto reference deduction.
  QualType deduceConstAutoRefType(Expr *Init);

  /// Deduce the type for `volatile auto& x = init;`.
  QualType deduceVolatileAutoRefType(Expr *Init);

  /// Deduce the type for `const volatile auto& x = init;`.
  QualType deduceCVAutoRefType(Expr *Init);

  /// Deduce the type for `const auto&& x = init;`.
  QualType deduceConstAutoForwardingRefType(Expr *Init);

  /// Deduce return type for `decltype(auto) f() { return expr; }`.
  /// Uses decltype rules (preserving value category) for return deduction.
  QualType deduceReturnTypeDecltypeAuto(Expr *ReturnExpr);

  /// Deduce types for structured bindings `auto [a, b] = expr`.
  /// Per C++ [dcl.struct.bind]:
  ///   - For tuple-like types: uses std::tuple_element<i>::type
  ///   - For arrays: each binding is a reference to the element
  ///   - For aggregate classes: each binding references the field
  QualType deduceStructuredBindingType(QualType EType, unsigned Index);

  //===------------------------------------------------------------------===//
  // Template argument deduction (placeholder for later)
  //===------------------------------------------------------------------===//

  /// Deduce template arguments from a function call.
  /// Returns true if deduction succeeded.
  bool deduceTemplateArguments(TemplateDecl *Template,
                                llvm::ArrayRef<Expr *> Args,
                                llvm::SmallVectorImpl<TemplateArgument> &DeducedArgs);
};

} // namespace blocktype

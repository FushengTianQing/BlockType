//===--- ExceptionAnalysis.h - Exception Sema Analysis -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ExceptionAnalysis class for exception handling
// semantic analysis: throw type checking, catch matching, noexcept.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Basic/SourceLocation.h"

namespace blocktype {

class Sema;

/// CatchMatchResult - Result of catch clause matching analysis.
enum class CatchMatchResult {
  NoMatch,       ///< The catch clause does not match the exception type
  ExactMatch,    ///< The catch clause matches exactly (same type)
  WithConversion,///< The catch clause matches with qualification conversion
  DerivedToBase, ///< The catch clause matches via derived-to-base conversion
};

/// NoexceptResult - Result of noexcept(expr) evaluation.
enum class NoexceptResult {
  NoThrow,   ///< Expression cannot throw (noexcept(true))
  MayThrow,  ///< Expression may throw (noexcept(false))
  Dependent, ///< Result depends on template arguments
};

/// ExceptionAnalysis - Performs exception handling semantic analysis.
///
/// This class provides methods for:
/// - Checking throw expression types (must be copyable/movable, not abstract)
/// - Analyzing catch clause matching (exact, conversion, derived-to-base)
/// - Checking noexcept specification violations
/// - Evaluating noexcept(expr) operators
class ExceptionAnalysis {
  Sema &SemaRef;
  ASTContext &Context;

public:
  explicit ExceptionAnalysis(Sema &S);

  //===------------------------------------------------------------------===//
  // throw expression type checking
  //===------------------------------------------------------------------===//

  /// Check a throw expression for semantic validity.
  /// Per C++ [except.throw]:
  ///   - throw; (rethrow) must be inside a catch handler
  ///   - The thrown type must be copyable or movable
  ///   - The thrown type must not be abstract
  ///   - The thrown type must not be incomplete
  ///   - The thrown type must not be a reference
  bool CheckThrowExpr(CXXThrowExpr *ThrowE);

  //===------------------------------------------------------------------===//
  // catch clause matching analysis
  //===------------------------------------------------------------------===//

  /// Check if a catch clause with CatchType can catch an exception of
  /// ThrowType.
  CatchMatchResult CheckCatchMatch(QualType CatchType,
                                    QualType ThrowType) const;

  /// Check catch handler reachability — warn if a later handler is
  /// shadowed by an earlier one (e.g., catch(Base) before catch(Derived)).
  bool CheckCatchReachability(llvm::ArrayRef<CXXCatchStmt *> Handlers) const;

  //===------------------------------------------------------------------===//
  // noexcept specification checking
  //===------------------------------------------------------------------===//

  /// Check noexcept specification on a function declaration.
  /// For noexcept(true) functions, checks the body for potential throws.
  bool CheckNoexceptSpec(FunctionDecl *FD) const;

  /// Check if a noexcept(true) function body contains any throw
  /// expressions or calls to non-noexcept functions.
  bool CheckNoexceptViolation(FunctionDecl *FD) const;

  /// Evaluate a noexcept(expr) expression.
  NoexceptResult EvaluateNoexceptExpr(Expr *E) const;

  /// Check that an overriding function's exception specification is
  /// compatible with the overridden function's specification.
  /// Per C++ [except.spec]: the overrider must be at least as restrictive.
  bool CheckExceptionSpecCompatibility(
      FunctionDecl *Overrider,
      FunctionDecl::NoexceptSpec OverriddenSpec) const;

private:
  /// Check if we're currently inside a catch handler (for rethrow checking).
  bool isInsideCatchHandler() const;

  /// Check if CatchType is the same as ThrowType or more CV-qualified.
  bool isSameOrMoreCVQualified(QualType CatchType, QualType ThrowType) const;

  /// Check if MoreCV has at least all qualifiers of LessCV plus more.
  bool isMoreCVQualified(QualType MoreCV, QualType LessCV) const;

  /// Check if two types are the same ignoring CVR qualifiers.
  bool isSameUnqualifiedType(QualType T1, QualType T2) const;

  /// Check if Derived is derived from Base.
  bool isDerivedToBase(QualType Derived, QualType Base) const;

  /// Recursively check a statement tree for throw expressions
  /// and non-noexcept calls in a noexcept function.
  void checkForThrowsInStmt(Stmt *S, FunctionDecl *FD,
                             bool &HasViolation) const;
};

} // namespace blocktype

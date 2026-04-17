//===--- ConstraintSatisfaction.h - Concept Constraint Check -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines ConstraintSatisfaction for checking C++20 concept
// constraint satisfaction per [concept] and [temp.constr].
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Sema/TemplateInstantiation.h"

#include <optional>

namespace blocktype {

class Sema;

/// ConstraintSatisfaction — implements C++20 constraint satisfaction semantics.
///
/// Per C++ [temp.constr]: a constraint is satisfied when its corresponding
/// expression evaluates to true after template argument substitution.
/// Constraints can be:
///   - Primary: a concept-id or requires-clause expression
///   - Conjunction: C1 && C2 (both must be satisfied)
///   - Disjunction: C1 || C2 (at least one must be satisfied)
///   - Atomic: a single expression (the "leaf" of the constraint tree)
///
/// Integration points:
///   1. Template argument deduction: after deduction succeeds, check the
///      requires-clause of function templates / class templates.
///   2. Concept-id: when C<T> appears, check concept C with args T.
///   3. Requires-expression: evaluate each requirement individually.
class ConstraintSatisfaction {
  Sema &SemaRef;

public:
  explicit ConstraintSatisfaction(Sema &S) : SemaRef(S) {}

  //===--------------------------------------------------------------------===//
  // Public API
  //===--------------------------------------------------------------------===//

  /// Check whether a constraint expression is satisfied.
  /// @param Constraint  The constraint expression (from requires-clause or concept)
  /// @param Args        Template argument list (for substitution into the constraint)
  /// @return true if the constraint is satisfied
  bool CheckConstraintSatisfaction(Expr *Constraint,
                                   const TemplateArgumentList &Args);

  /// Check whether a concept is satisfied with the given arguments.
  /// Substitutes the arguments into the concept's constraint expression
  /// and evaluates the result.
  /// @param Concept  The concept declaration
  /// @param Args     Template arguments (must match concept's parameter count)
  /// @return true if the concept is satisfied
  bool CheckConceptSatisfaction(ConceptDecl *Concept,
                                llvm::ArrayRef<TemplateArgument> Args);

  /// Evaluate a requires-expression.
  /// A requires-expression is satisfied when all its requirements are satisfied.
  /// Per C++ [expr.prim.req]: a requirement is satisfied if:
  ///   - Type: the type is valid (not ill-formed, not dependent)
  ///   - SimpleExpr: the expression is well-formed (and noexcept if requested)
  ///   - Compound: the expression is well-formed, noexcept constraint met,
  ///               and return-type constraint satisfied
  ///   - Nested: the nested constraint expression evaluates to true
  /// @param RE  The requires expression
  /// @return true if all requirements are satisfied
  bool EvaluateRequiresExpr(RequiresExpr *RE);

  /// Evaluate a requires-expression with template argument substitution.
  /// Substitutes arguments into the requirements before evaluation.
  /// @param RE    The requires expression
  /// @param Args  Template arguments for substitution
  /// @return true if all requirements are satisfied
  bool EvaluateRequiresExprWithArgs(RequiresExpr *RE,
                                    const TemplateArgumentList &Args);

private:
  //===--------------------------------------------------------------------===//
  // Internal helpers
  //===--------------------------------------------------------------------===//

  /// Evaluate a single requirement.
  /// @param R     The requirement to evaluate
  /// @param Args  Template arguments (may be empty for non-dependent requirements)
  /// @return true if the requirement is satisfied
  bool EvaluateRequirement(Requirement *R, const TemplateArgumentList &Args);

  /// Evaluate a type requirement.
  /// A type requirement is satisfied if the type is valid and complete.
  bool EvaluateTypeRequirement(TypeRequirement *TR);

  /// Evaluate an expression requirement.
  /// An expression requirement is satisfied if the expression is well-formed.
  /// If noexcept is specified, the expression must be noexcept.
  bool EvaluateExprRequirement(ExprRequirement *ER,
                               const TemplateArgumentList &Args);

  /// Evaluate a compound requirement.
  /// A compound requirement checks:
  ///   1. Expression validity
  ///   2. noexcept constraint (if specified)
  ///   3. Return type constraint (if specified)
  bool EvaluateCompoundRequirement(CompoundRequirement *CR,
                                   const TemplateArgumentList &Args);

  /// Evaluate a nested requirement.
  /// A nested requirement evaluates the inner constraint expression.
  bool EvaluateNestedRequirement(NestedRequirement *NR,
                                 const TemplateArgumentList &Args);

  /// Evaluate an atomic constraint expression to a boolean value.
  /// Handles BinaryOperator (&&, ||), UnaryOperator (!), DeclRefExpr,
  /// RequiresExpr, CallExpr, etc.
  /// @param E  The expression to evaluate
  /// @return   The boolean result if evaluation succeeds, or std::nullopt
  std::optional<bool> EvaluateConstraintExpr(Expr *E);

  /// Substitute template arguments into an expression and then evaluate.
  /// Returns std::nullopt if substitution fails (hard error in non-SFINAE).
  std::optional<bool> SubstituteAndEvaluate(Expr *E,
                                            const TemplateArgumentList &Args);

  /// Check if an expression can throw (used for noexcept requirements).
  /// Simplified: returns false for most expressions (assumes noexcept).
  bool canThrow(Expr *E) const;

  /// Check if two types are compatible for a return type constraint.
  /// Per C++ [temp.constr]: the deduced return type must satisfy the constraint.
  bool checkReturnTypeConstraint(QualType ExprType, QualType ConstraintType);
};

} // namespace blocktype

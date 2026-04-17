//===--- ConstraintSatisfaction.cpp - Concept Constraint Check -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements constraint satisfaction checking for C++20 concepts.
// Per C++ [temp.constr], a constraint is satisfied when its expression
// evaluates to true after template argument substitution.
//
// Key semantics:
//   - Conjunction (&&): both operands must be satisfied
//   - Disjunction (||): at least one operand must be satisfied
//   - Atomic constraint: evaluated as a constant boolean expression
//   - Requires-expression: all requirements must be satisfied
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/ConstraintSatisfaction.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/ConstantExpr.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

bool ConstraintSatisfaction::CheckConstraintSatisfaction(
    Expr *Constraint, const TemplateArgumentList &Args) {
  if (!Constraint)
    return true; // No constraint → trivially satisfied

  // Substitute template arguments into the constraint and evaluate.
  // Per C++ [temp.constr], substitution failure in the immediate context
  // of constraint evaluation makes the constraint unsatisfied (not a hard error).
  auto &Instantiator = SemaRef.getTemplateInstantiator();
  SFINAEGuard Guard(Instantiator.getSFINAEContext(),
                     SemaRef.getDiagnostics().getNumErrors(),
                     &SemaRef.getDiagnostics());

  auto Result = SubstituteAndEvaluate(Constraint, Args);

  if (!Result.has_value()) {
    // Substitution failure → constraint not satisfied
    return false;
  }

  return Result.value();
}

bool ConstraintSatisfaction::CheckConceptSatisfaction(
    ConceptDecl *Concept, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Concept)
    return true;

  Expr *ConstraintExpr = Concept->getConstraintExpr();
  if (!ConstraintExpr)
    return true; // No constraint expression → trivially satisfied

  // Concept satisfaction checking is also in the SFINAE immediate context.
  auto &Instantiator = SemaRef.getTemplateInstantiator();
  SFINAEGuard Guard(Instantiator.getSFINAEContext(),
                     SemaRef.getDiagnostics().getNumErrors(),
                     &SemaRef.getDiagnostics());

  TemplateArgumentList ArgList(Args);
  return CheckConstraintSatisfaction(ConstraintExpr, ArgList);
}

bool ConstraintSatisfaction::EvaluateRequiresExpr(RequiresExpr *RE) {
  if (!RE)
    return true;

  TemplateArgumentList EmptyArgs;
  for (Requirement *R : RE->getRequirements()) {
    if (!EvaluateRequirement(R, EmptyArgs))
      return false;
  }
  return true;
}

bool ConstraintSatisfaction::EvaluateRequiresExprWithArgs(
    RequiresExpr *RE, const TemplateArgumentList &Args) {
  if (!RE)
    return true;

  for (Requirement *R : RE->getRequirements()) {
    if (!EvaluateRequirement(R, Args))
      return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Requirement Evaluation
//===----------------------------------------------------------------------===//

bool ConstraintSatisfaction::EvaluateRequirement(
    Requirement *R, const TemplateArgumentList &Args) {
  if (!R)
    return true;

  switch (R->getKind()) {
  case Requirement::RequirementKind::Type:
    return EvaluateTypeRequirement(dyn_cast<TypeRequirement>(R));

  case Requirement::RequirementKind::SimpleExpr:
    return EvaluateExprRequirement(dyn_cast<ExprRequirement>(R), Args);

  case Requirement::RequirementKind::Compound:
    return EvaluateCompoundRequirement(dyn_cast<CompoundRequirement>(R),
                                       Args);

  case Requirement::RequirementKind::Nested:
    return EvaluateNestedRequirement(
        dyn_cast<NestedRequirement>(R), Args);
  }
  return false;
}

bool ConstraintSatisfaction::EvaluateTypeRequirement(TypeRequirement *TR) {
  if (!TR)
    return false;

  QualType T = TR->getType();

  // Type must be non-null and non-dependent to be valid.
  // A dependent type means we can't check yet → assume satisfied.
  if (T.isNull())
    return false;

  if (T->isDependentType())
    return true; // Can't check yet, assume OK

  // Check if the type is complete (for non-built-in types).
  // Per C++ [temp.constr]: type requirement is satisfied if the named type
  // is valid. Incomplete types are valid as long as they're not dependent.
  return true;
}

bool ConstraintSatisfaction::EvaluateExprRequirement(
    ExprRequirement *ER, const TemplateArgumentList &Args) {
  if (!ER)
    return false;

  Expr *E = ER->getExpression();
  if (!E)
    return false;

  // Substitute template arguments if any
  if (!Args.empty()) {
    auto &Instantiator = SemaRef.getTemplateInstantiator();
    Expr *SubstE = Instantiator.SubstituteExpr(E, Args);
    if (SubstE)
      E = SubstE;
  }

  // Check noexcept constraint
  if (ER->isNoexcept() && canThrow(E))
    return false;

  // Expression validity check:
  // A non-null expression is considered well-formed for our purposes.
  // Full validity checking would require attempting type-checking on E.
  return true;
}

bool ConstraintSatisfaction::EvaluateCompoundRequirement(
    CompoundRequirement *CR, const TemplateArgumentList &Args) {
  if (!CR)
    return false;

  Expr *E = CR->getExpression();
  if (!E)
    return false;

  // Substitute template arguments if any
  if (!Args.empty()) {
    auto &Instantiator = SemaRef.getTemplateInstantiator();
    Expr *SubstE = Instantiator.SubstituteExpr(E, Args);
    if (SubstE)
      E = SubstE;
  }

  // 1. Check noexcept constraint
  if (CR->isNoexcept() && canThrow(E))
    return false;

  // 2. Check return type constraint
  if (CR->hasReturnTypeRequirement()) {
    QualType ExprType = E->getType();
    QualType ConstraintType = CR->getReturnType();

    // Substitute into constraint type
    if (!Args.empty() && !ConstraintType.isNull()) {
      auto &Instantiator = SemaRef.getTemplateInstantiator();
      QualType SubstType = Instantiator.SubstituteType(ConstraintType, Args);
      if (!SubstType.isNull())
        ConstraintType = SubstType;
    }

    if (!checkReturnTypeConstraint(ExprType, ConstraintType))
      return false;
  }

  return true;
}

bool ConstraintSatisfaction::EvaluateNestedRequirement(
    NestedRequirement *NR, const TemplateArgumentList &Args) {
  if (!NR)
    return false;

  Expr *Constraint = NR->getConstraint();
  if (!Constraint)
    return true;

  // Substitute template arguments and evaluate
  auto Result = SubstituteAndEvaluate(Constraint, Args);
  return Result.has_value() && Result.value();
}

//===----------------------------------------------------------------------===//
// Constraint Expression Evaluation
//===----------------------------------------------------------------------===//

std::optional<bool>
ConstraintSatisfaction::EvaluateConstraintExpr(Expr *E) {
  if (!E)
    return std::nullopt;

  // Handle logical operators for conjunction/disjunction semantics
  // per C++ [temp.constr.op]:
  //   - && (conjunction): both must be satisfied
  //   - || (disjunction): at least one must be satisfied

  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E)) {
    // Logical AND (conjunction): C1 && C2
    if (BO->getOpcode() == BinaryOpKind::LAnd) {
      auto LHS = EvaluateConstraintExpr(BO->getLHS());
      if (!LHS.has_value())
        return std::nullopt;
      if (!LHS.value())
        return false; // Short-circuit: left is false
      auto RHS = EvaluateConstraintExpr(BO->getRHS());
      if (!RHS.has_value())
        return std::nullopt;
      return RHS.value();
    }

    // Logical OR (disjunction): C1 || C2
    if (BO->getOpcode() == BinaryOpKind::LOr) {
      auto LHS = EvaluateConstraintExpr(BO->getLHS());
      if (!LHS.has_value())
        return std::nullopt;
      if (LHS.value())
        return true; // Short-circuit: left is true
      auto RHS = EvaluateConstraintExpr(BO->getRHS());
      if (!RHS.has_value())
        return std::nullopt;
      return RHS.value();
    }

    // Other binary operators: evaluate as constant expression
    ConstantExprEvaluator Eval(SemaRef.getASTContext());
    auto Result = Eval.Evaluate(E);
    if (Result.isSuccess() && Result.isIntegral())
      return Result.getInt().getBoolValue();
    return std::nullopt;
  }

  // Unary NOT
  if (auto *UO = llvm::dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UnaryOpKind::LNot) {
      auto Sub = EvaluateConstraintExpr(UO->getSubExpr());
      if (!Sub.has_value())
        return std::nullopt;
      return !Sub.value();
    }
  }

  // RequiresExpr — evaluate its requirements
  if (auto *RE = llvm::dyn_cast<RequiresExpr>(E)) {
    return EvaluateRequiresExpr(RE);
  }

  // CXXBoolLiteral — direct boolean value
  if (auto *BL = llvm::dyn_cast<CXXBoolLiteral>(E)) {
    return BL->getValue();
  }

  // IntegerLiteral — non-zero is truthy
  if (auto *IL = llvm::dyn_cast<IntegerLiteral>(E)) {
    return IL->getValue().getBoolValue();
  }

  // CallExpr or DeclRefExpr — try constant evaluation
  // For concept-id expressions like Integral<T>, the result should be
  // a boolean constant after substitution.
  ConstantExprEvaluator Eval(SemaRef.getASTContext());

  // Try EvaluateAsBooleanCondition first (handles more cases)
  auto BoolResult = Eval.EvaluateAsBooleanCondition(E);
  if (BoolResult.has_value())
    return BoolResult.value();

  // Fallback: try general evaluation
  auto Result = Eval.Evaluate(E);
  if (Result.isSuccess() && Result.isIntegral())
    return Result.getInt().getBoolValue();

  // Could not evaluate → assume satisfied (for forward compatibility)
  return std::nullopt;
}

std::optional<bool> ConstraintSatisfaction::SubstituteAndEvaluate(
    Expr *E, const TemplateArgumentList &Args) {
  if (!E)
    return std::nullopt;

  Expr *SubstE = E;

  // Substitute template arguments
  if (!Args.empty()) {
    auto &Instantiator = SemaRef.getTemplateInstantiator();
    Expr *Result = Instantiator.SubstituteExpr(E, Args);
    if (Result)
      SubstE = Result;
    else {
      // Substitution failure
      return std::nullopt;
    }
  }

  return EvaluateConstraintExpr(SubstE);
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

bool ConstraintSatisfaction::canThrow(Expr *E) const {
  // Simplified noexcept analysis.
  // In a full implementation, this would perform a recursive walk of the
  // expression to determine if any sub-expression can throw, following
  // C++ [except.spec].
  //
  // For now, assume expressions cannot throw (conservative for noexcept).
  // This means noexcept requirements are always satisfied unless we can
  // detect a throw expression.
  if (!E)
    return false;

  // CXXThrowExpr → can throw
  if (llvm::isa<CXXThrowExpr>(E))
    return true;

  return false;
}

bool ConstraintSatisfaction::checkReturnTypeConstraint(QualType ExprType,
                                                       QualType ConstraintType) {
  if (ExprType.isNull() || ConstraintType.isNull())
    return false;

  // Simple type compatibility check.
  // Per C++ [temp.constr]: the deduced type must be the same as or
  // convertible to the constraint type.
  //
  // For a full implementation, this would also handle:
  //   - Auto deduction against the constraint type
  //   - cv-qualifier adjustments
  //   - Reference binding
  //   - Concept satisfaction of the deduced type

  // Direct match
  if (ExprType.getTypePtr() == ConstraintType.getTypePtr())
    return true;

  // Canonical type comparison
  if (ExprType.getCanonicalType() == ConstraintType.getCanonicalType())
    return true;

  // If the constraint type is a concept (TemplateSpecializationType referencing
  // a concept), we'd need to check concept satisfaction here.
  // This is deferred to a later stage.

  return false;
}

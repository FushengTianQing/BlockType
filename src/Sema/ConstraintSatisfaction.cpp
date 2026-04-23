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
#include "blocktype/Sema/Conversion.h"
#include "blocktype/Sema/SymbolTable.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

bool ConstraintSatisfaction::CheckConstraintSatisfaction(
    Expr *Constraint, llvm::ArrayRef<TemplateArgument> Args) {
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

  // Build a TemplateInstantiation for the concept's template parameters
  // and store it for use by SubstituteAndEvaluate.
  CurrentSubstInst = TemplateInstantiation();
  HasSubstContext = false;
  if (auto *TD = Concept->getTemplate()) {
    if (auto *TPL = TD->getTemplateParameterList()) {
      auto ParamDecls = TPL->getParams();
      for (unsigned i = 0; i < std::min(Args.size(), ParamDecls.size()); ++i) {
        if (auto *ParamDecl = llvm::dyn_cast_or_null<TemplateTypeParmDecl>(ParamDecls[i])) {
          CurrentSubstInst.addSubstitution(ParamDecl, Args[i]);
        }
      }
      HasSubstContext = true;
    }
  }

  return CheckConstraintSatisfaction(ConstraintExpr, Args);
}

bool ConstraintSatisfaction::EvaluateRequiresExpr(RequiresExpr *RE) {
  if (!RE)
    return true;

  llvm::SmallVector<TemplateArgument, 0> EmptyArgs;
  for (Requirement *R : RE->getRequirements()) {
    if (!EvaluateRequirement(R, EmptyArgs))
      return false;
  }
  return true;
}

bool ConstraintSatisfaction::EvaluateRequiresExprWithArgs(
    RequiresExpr *RE, llvm::ArrayRef<TemplateArgument> Args) {
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
    Requirement *R, llvm::ArrayRef<TemplateArgument> Args) {
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
    ExprRequirement *ER, llvm::ArrayRef<TemplateArgument> Args) {
  if (!ER)
    return false;

  Expr *E = ER->getExpression();
  if (!E)
    return false;

  // Substitute template arguments if any
  if (!Args.empty() && HasSubstContext) {
    auto &Instantiator = SemaRef.getTemplateInstantiator();

    // 1. Substitute the expression's type if it is dependent.
    if (E->getType().getTypePtr() && E->getType()->isDependentType()) {
      QualType SubstType = CurrentSubstInst.substituteType(E->getType());
      // If substitution failed, the requirement is not satisfied.
      if (SubstType.isNull())
        return false;
    }

    // 2. Substitute dependent sub-expressions within E.
    // Per C++ [temp.constr], substitution must be applied to the entire
    // expression, not just its top-level type. Expressions like
    // CXXDependentScopeMemberExpr and DependentScopeDeclRefExpr need
    // to be resolved via substituteDependentExpr.
    Expr *SubstE = Instantiator.substituteDependentExpr(E, CurrentSubstInst);
    if (SubstE) {
      // Substitution succeeded — use the substituted expression for evaluation.
      E = SubstE;
    } else {
      // Substitution failed in a non-SFINAE context — requirement not satisfied.
      // (In SFINAE context, this would make the constraint unsatisfied, not a
      // hard error. We treat it as unsatisfied here.)
      return false;
    }
  }

  // Expression validity check:
  // A non-null expression that has survived substitution is considered
  // well-formed. Evaluate the expression as a constant boolean.
  // Per C++ [temp.constr.expr]: an expression requirement is satisfied
  // if the expression is valid and, when evaluated, yields true.
  ConstantExprEvaluator Eval(SemaRef.getASTContext());
  auto Result = Eval.Evaluate(E);
  if (Result.isSuccess() && Result.isIntegral()) {
    return Result.getInt().getBoolValue();
  }

  // If we cannot evaluate the expression, the requirement is satisfied
  // as long as the expression is well-formed (non-null, non-dependent).
  // This handles cases like `requires { expr; }` where expr is valid
  // but not a constant expression.
  return true;
}

bool ConstraintSatisfaction::EvaluateCompoundRequirement(
    CompoundRequirement *CR, llvm::ArrayRef<TemplateArgument> Args) {
  if (!CR)
    return false;

  Expr *E = CR->getExpression();
  if (!E)
    return false;

  // Substitute template arguments if any
  if (!Args.empty() && HasSubstContext) {
    // Use the stored TemplateInstantiation for type substitution.
    if (E->getType().getTypePtr() && E->getType()->isDependentType()) {
      QualType SubstType = CurrentSubstInst.substituteType(E->getType());
      if (!SubstType.isNull()) {
        // Apply the substituted type to the expression for downstream checks.
        // Per C++ [temp.constr]: substitution must be applied before evaluation.
        E->setType(SubstType);
      }
    }
  }

  // 1. Check noexcept constraint
  if (CR->isNoexcept() && canThrow(E))
    return false;

  // 2. Check return type constraint
  if (CR->hasReturnTypeRequirement()) {
    QualType ExprType = E->getType();
    QualType ConstraintType = CR->getReturnType();

    // Substitute into constraint type
    if (!Args.empty() && !ConstraintType.isNull() && HasSubstContext) {
      // Use the stored TemplateInstantiation for type substitution.
      QualType SubstType = CurrentSubstInst.substituteType(ConstraintType);
      if (!SubstType.isNull())
        ConstraintType = SubstType;
    }

    if (!checkReturnTypeConstraint(ExprType, ConstraintType))
      return false;
  }

  return true;
}

bool ConstraintSatisfaction::EvaluateNestedRequirement(
    NestedRequirement *NR, llvm::ArrayRef<TemplateArgument> Args) {
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

  // DeclRefExpr — check if it references a concept by name lookup.
  // NOTE: ConceptDecl inherits from TypeDecl (not ValueDecl), so
  // dyn_cast<ConceptDecl>(DRE->getDecl()) will never succeed since
  // DeclRefExpr stores ValueDecl*. Instead, we do a name-based lookup.
  // A bare concept name without template args is not a valid constraint
  // (concepts require template arguments), so we just try constant eval.
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(E)) {
    // Try constant evaluation for enum constants, constexpr vars, etc.
    ConstantExprEvaluator Eval(SemaRef.getASTContext());
    auto Result = Eval.Evaluate(E);
    if (Result.isSuccess() && Result.isIntegral())
      return Result.getInt().getBoolValue();
    return std::nullopt;
  }

  // CallExpr — check if callee name resolves to a concept.
  // Concept usage like ConceptName(args...) is unusual but possible.
  // The typical form ConceptName<Args> is a TemplateSpecializationExpr.
  if (auto *CE = llvm::dyn_cast<CallExpr>(E)) {
    Expr *Callee = CE->getCallee();
    if (Callee) {
      // Check if the callee is a DeclRefExpr whose name matches a concept
      if (auto *CalleeDRE = llvm::dyn_cast<DeclRefExpr>(Callee)) {
        if (CalleeDRE->getDecl()) {
          llvm::StringRef Name = CalleeDRE->getDecl()->getName();
          if (ConceptDecl *CD =
                  SemaRef.getSymbolTable().lookupConcept(Name)) {
            // Build type args from call expression arguments
            llvm::SmallVector<TemplateArgument, 4> ConceptArgs;
            for (unsigned I = 0; I < CE->getNumArgs(); ++I) {
              QualType ArgType = CE->getArgs()[I]->getType();
              if (!ArgType.isNull())
                ConceptArgs.push_back(TemplateArgument(ArgType));
            }
            return CheckConceptSatisfaction(CD, ConceptArgs);
          }
        }
      }
    }

    // Not a concept call — try constant evaluation
    ConstantExprEvaluator Eval(SemaRef.getASTContext());
    auto Result = Eval.Evaluate(E);
    if (Result.isSuccess() && Result.isIntegral())
      return Result.getInt().getBoolValue();
    return std::nullopt;
  }

  // TemplateSpecializationExpr — concept-id like Integral<int>
  // This is the primary form of concept usage in constraints.
  // The template name (e.g., "Integral") is looked up in the symbol table
  // to find the corresponding ConceptDecl.
  // NOTE: TemplateSpecializationExpr::getTemplateName() returns StringRef
  // (not TemplateDecl*), and getTemplateDecl() returns ValueDecl* which
  // can never be a ConceptDecl (ConceptDecl : TypeDecl, not ValueDecl).
  if (auto *TSE = llvm::dyn_cast<TemplateSpecializationExpr>(E)) {
    llvm::StringRef Name = TSE->getTemplateName();
    if (ConceptDecl *CD = SemaRef.getSymbolTable().lookupConcept(Name)) {
      llvm::SmallVector<TemplateArgument, 4> Args;
      for (const auto &Arg : TSE->getTemplateArgs())
        Args.push_back(Arg);
      return CheckConceptSatisfaction(CD, Args);
    }
    return std::nullopt;
  }

  // Fallback: try general constant evaluation
  ConstantExprEvaluator Eval(SemaRef.getASTContext());
  auto BoolResult = Eval.EvaluateAsBooleanCondition(E);
  if (BoolResult.has_value())
    return BoolResult.value();

  auto Result = Eval.Evaluate(E);
  if (Result.isSuccess() && Result.isIntegral())
    return Result.getInt().getBoolValue();

  // Could not evaluate → not satisfied
  return std::nullopt;
}

std::optional<bool> ConstraintSatisfaction::SubstituteAndEvaluate(
    Expr *E, llvm::ArrayRef<TemplateArgument> Args) {
  if (!E)
    return std::nullopt;

  Expr *SubstE = E;

  // Substitute template arguments
  if (!Args.empty()) {
    auto &Instantiator = SemaRef.getTemplateInstantiator();
    // Use the stored TemplateInstantiation context if available
    // (set by CheckConceptSatisfaction), otherwise substitute types
    // using a fresh TemplateInstantiation.
    if (HasSubstContext) {
      // Substitute the expression's type if it is dependent.
      if (SubstE->getType().getTypePtr() && SubstE->getType()->isDependentType()) {
        QualType SubstType = CurrentSubstInst.substituteType(SubstE->getType());
        if (!SubstType.isNull() && SubstType.getTypePtr() != SubstE->getType().getTypePtr()) {
          // Type was substituted — apply it to the expression so downstream
          // evaluation sees the concrete (non-dependent) type.
          // Per C++ [temp.constr]: substitution must be applied before evaluation.
          SubstE->setType(SubstType);
        }
      }
    }
    // If no substitution context is available, the expression is
    // evaluated as-is. This is correct for non-dependent constraints
    // and for constraints where the template arguments are already
    // resolved (e.g., TemplateSpecializationExpr).
  }

  return EvaluateConstraintExpr(SubstE);
}

//===----------------------------------------------------------------------===//
// Constraint Partial Ordering (C++20 [temp.constr.order])
//===----------------------------------------------------------------------===//

/// Collect all atomic constraints from a constraint expression tree.
/// Per C++ [temp.constr.atomic]: an atomic constraint is a leaf expression
/// that is not a logical AND or OR. We normalize && and || into a set
/// of leaf expressions.
static void collectAtomicConstraints(
    Expr *E, llvm::SmallVectorImpl<Expr *> &Atoms) {
  if (!E)
    return;

  // BinaryOperator && → decompose both sides (conjunction)
  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BinaryOpKind::LAnd) {
      collectAtomicConstraints(BO->getLHS(), Atoms);
      collectAtomicConstraints(BO->getRHS(), Atoms);
      return;
    }
    // || is an atomic constraint (we don't decompose disjunctions for
    // subsumption — C++ says A || B subsumes C only if both A and B
    // individually subsume C, but the standard treats || as atomic)
  }

  // UnaryOperator ! → decompose operand and wrap
  // For subsumption, we treat !C as an atomic constraint
  // (negation changes meaning, can't decompose further)

  // Anything else is an atomic constraint
  Atoms.push_back(E);
}

/// Check if two atomic constraint expressions are "identical" for the
/// purpose of subsumption. Per C++ [temp.constr.order]:
///   Two atomic constraints are identical if they are formed from the
///   same expression (after substitution).
///   For our purposes, we do a structural comparison:
///   - Same expression kind
///   - Same operands (for binary/unary)
///   - Same referenced declaration (for DeclRefExpr)
static bool areAtomicConstraintsIdentical(Expr *A, Expr *B) {
  if (!A || !B)
    return false;

  // Same pointer → identical
  if (A == B)
    return true;

  // BinaryOperator: same operator + recursive check
  auto *BO_A = llvm::dyn_cast<BinaryOperator>(A);
  auto *BO_B = llvm::dyn_cast<BinaryOperator>(B);
  if (BO_A && BO_B) {
    if (BO_A->getOpcode() != BO_B->getOpcode())
      return false;
    return areAtomicConstraintsIdentical(BO_A->getLHS(), BO_B->getLHS()) &&
           areAtomicConstraintsIdentical(BO_A->getRHS(), BO_B->getRHS());
  }

  // UnaryOperator: same operator + recursive check
  auto *UO_A = llvm::dyn_cast<UnaryOperator>(A);
  auto *UO_B = llvm::dyn_cast<UnaryOperator>(B);
  if (UO_A && UO_B) {
    if (UO_A->getOpcode() != UO_B->getOpcode())
      return false;
    return areAtomicConstraintsIdentical(UO_A->getSubExpr(), UO_B->getSubExpr());
  }

  // DeclRefExpr: same referenced declaration
  auto *DRE_A = llvm::dyn_cast<DeclRefExpr>(A);
  auto *DRE_B = llvm::dyn_cast<DeclRefExpr>(B);
  if (DRE_A && DRE_B) {
    return DRE_A->getDecl() == DRE_B->getDecl();
  }

  // RequiresExpr: pointer comparison (same expression)
  if (llvm::isa<RequiresExpr>(A) && llvm::isa<RequiresExpr>(B))
    return A == B;

  // For other expression types, use pointer comparison
  return false;
}

/// Check if constraint C1 subsumes constraint C2.
/// Per C++ [temp.constr.order]: C1 subsumes C2 if every atomic constraint
/// in C2's normalized form appears in C1's normalized form.
static bool constraintSubsumes(Expr *C1, Expr *C2) {
  llvm::SmallVector<Expr *, 8> Atoms1, Atoms2;
  collectAtomicConstraints(C1, Atoms1);
  collectAtomicConstraints(C2, Atoms2);

  // C1 subsumes C2 if every atom in C2 appears in C1
  // (C1's atom set is a superset of C2's)
  for (Expr *Atom2 : Atoms2) {
    bool Found = false;
    for (Expr *Atom1 : Atoms1) {
      if (areAtomicConstraintsIdentical(Atom1, Atom2)) {
        Found = true;
        break;
      }
    }
    if (!Found)
      return false;
  }
  return true;
}

int ConstraintSatisfaction::CompareConstraints(Expr *C1, Expr *C2) {
  if (!C1 && !C2)
    return 0;
  if (!C1)
    return 1;  // No constraint is less constrained
  if (!C2)
    return -1; // Having a constraint is more constrained

  bool C1_subsumes_C2 = constraintSubsumes(C1, C2);
  bool C2_subsumes_C1 = constraintSubsumes(C2, C1);

  if (C1_subsumes_C2 && !C2_subsumes_C1)
    return -1; // C1 is more constrained
  if (C2_subsumes_C1 && !C1_subsumes_C2)
    return 1;  // C2 is more constrained
  return 0;    // Equivalent or incomparable
}

int ConstraintSatisfaction::CompareFunctionTemplateConstraints(
    FunctionTemplateDecl *T1, FunctionTemplateDecl *T2) {
  if (!T1 || !T2)
    return 0;

  Expr *RC1 = T1->hasRequiresClause() ? T1->getRequiresClause() : nullptr;
  Expr *RC2 = T2->hasRequiresClause() ? T2->getRequiresClause() : nullptr;

  return CompareConstraints(RC1, RC2);
}

bool ConstraintSatisfaction::IsMoreConstrained(
    FunctionTemplateDecl *T1, FunctionTemplateDecl *T2) {
  return CompareFunctionTemplateConstraints(T1, T2) < 0;
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

bool ConstraintSatisfaction::canThrow(Expr *E) const {
  if (!E)
    return false;

  // CXXThrowExpr → can throw
  if (llvm::isa<CXXThrowExpr>(E))
    return true;

  // BinaryOperator → can throw if either operand can throw
  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E))
    return canThrow(BO->getLHS()) || canThrow(BO->getRHS());

  // UnaryOperator → can throw if operand can throw
  if (auto *UO = llvm::dyn_cast<UnaryOperator>(E))
    return canThrow(UO->getSubExpr());

  // CallExpr → can throw (conservative: assume any call can throw unless
  // the callee is known noexcept). For now, assume calls can throw.
  if (llvm::isa<CallExpr>(E))
    return true;

  // CXXNewExpr → allocation can throw (conservative)
  if (E->getKind() == ASTNode::NodeKind::CXXNewExprKind)
    return true;

  // MemberExpr → can throw if the base can throw
  if (auto *ME = llvm::dyn_cast<MemberExpr>(E))
    return canThrow(ME->getBase());

  // ArraySubscriptExpr → can throw if base can throw
  if (auto *ASE = llvm::dyn_cast<ArraySubscriptExpr>(E))
    return canThrow(ASE->getBase());

  // CastExpr → can throw if sub-expression can throw
  if (auto *CE = llvm::dyn_cast<CastExpr>(E))
    return canThrow(CE->getSubExpr());

  // ConditionalOperator → can throw if any branch can throw
  if (auto *CO = llvm::dyn_cast<ConditionalOperator>(E))
    return canThrow(CO->getCond()) || canThrow(CO->getTrueExpr()) ||
           canThrow(CO->getFalseExpr());

  // Default: literals, DeclRefExpr, etc. cannot throw
  return false;
}

bool ConstraintSatisfaction::checkReturnTypeConstraint(QualType ExprType,
                                                       QualType ConstraintType) {
  if (ExprType.isNull() || ConstraintType.isNull())
    return false;

  // Per C++ [temp.constr]: the deduced return type must satisfy the
  // return-type-requirement. This handles several cases:
  //
  // 1. Direct type match (exact or canonical)
  // 2. cv-qualifier adjustments (deduced type can be more cv-qualified)
  // 3. Reference binding (deduced reference type must match)
  // 4. Base-of check (derived→base conversion)
  // 5. Template argument deduction (constraint is TemplateSpecializationType)

  // 1. Direct pointer match
  if (ExprType.getTypePtr() == ConstraintType.getTypePtr())
    return true;

  // 2. Canonical type comparison
  if (ExprType.getCanonicalType() == ConstraintType.getCanonicalType())
    return true;

  // 3. cv-qualifier stripping: check if types match ignoring top-level cv
  QualType ExprInner = ExprType.getTypePtr()->isReferenceType()
                            ? ExprType : QualType(ExprType.getTypePtr(), Qualifier::None);
  QualType ConstInner = ConstraintType.getTypePtr()->isReferenceType()
                             ? ConstraintType : QualType(ConstraintType.getTypePtr(), Qualifier::None);
  if (ExprInner.getCanonicalType() == ConstInner.getCanonicalType())
    return true;

  // 4. Reference compatibility: if constraint is a reference type, check
  //    if the expression type can bind to it.
  if (ConstraintType->isReferenceType()) {
    // For lvalue references: expr must be an lvalue of matching type
    // For rvalue references: expr must be convertible
    // Simplified: check canonical types match
    QualType RefTarget = ConstraintType->isReferenceType()
                             ? static_cast<const ReferenceType *>(ConstraintType.getTypePtr())
                                   ->getReferencedType()
                             : ConstraintType;
    QualType ExprTarget = ExprType->isReferenceType()
                              ? static_cast<const ReferenceType *>(ExprType.getTypePtr())
                                    ->getReferencedType()
                              : ExprType;
    if (RefTarget.getCanonicalType() == ExprTarget.getCanonicalType())
      return true;
  }

  // 5. Pointer compatibility: if both are pointers, check pointee types
  if (ExprType->isPointerType() && ConstraintType->isPointerType()) {
    auto *EP = static_cast<const PointerType *>(ExprType.getTypePtr());
    auto *CP = static_cast<const PointerType *>(ConstraintType.getTypePtr());
    QualType EPointee = QualType(EP->getPointeeType(), Qualifier::None);
    QualType CPointee = QualType(CP->getPointeeType(), Qualifier::None);
    if (EPointee.getCanonicalType() == CPointee.getCanonicalType())
      return true;
  }

  // 6. Conversion check: can ExprType be converted to ConstraintType?
  // Use ConversionChecker for standard conversions.
  auto ICS = ConversionChecker::GetConversion(ExprType, ConstraintType);
  if (!ICS.isBad())
    return true;

  return false;
}

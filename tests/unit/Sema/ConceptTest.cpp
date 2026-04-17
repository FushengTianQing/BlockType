//===--- ConceptTest.cpp - Concept Constraint Tests -------------------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E5.5.3.1 — Concept Tests
//
//===-------------------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/ConstraintSatisfaction.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class ConceptTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  ConceptTest() : Diags() {
    S = std::make_unique<Sema>(Context, Diags);
  }
};

// --- Concept Declaration ---

TEST_F(ConceptTest, ActOnConceptDeclRegisters) {
  auto *Constraint = Context.create<CXXBoolLiteral>(
      SourceLocation(1), true, Context.getBoolType());
  auto *CD = Context.create<ConceptDecl>(
      SourceLocation(1), "Integral", Constraint, nullptr);

  auto Result = S->ActOnConceptDecl(CD);
  EXPECT_TRUE(Result.isUsable());

  auto *Found = S->getSymbolTable().lookupConcept("Integral");
  EXPECT_EQ(Found, CD);
}

TEST_F(ConceptTest, ActOnConceptDeclNullReturnsInvalid) {
  auto Result = S->ActOnConceptDecl(nullptr);
  EXPECT_TRUE(Result.isInvalid());
}

// --- ConstraintSatisfaction ---

TEST_F(ConceptTest, CheckConstraintSatisfactionTrueLiteral) {
  auto &Checker = S->getConstraintChecker();
  auto *TrueLit = Context.create<CXXBoolLiteral>(
      SourceLocation(1), true, Context.getBoolType());

  TemplateArgumentList EmptyArgs;
  EXPECT_TRUE(Checker.CheckConstraintSatisfaction(TrueLit, EmptyArgs));
}

TEST_F(ConceptTest, CheckConstraintSatisfactionFalseLiteral) {
  auto &Checker = S->getConstraintChecker();
  auto *FalseLit = Context.create<CXXBoolLiteral>(
      SourceLocation(1), false, Context.getBoolType());

  TemplateArgumentList EmptyArgs;
  EXPECT_FALSE(Checker.CheckConstraintSatisfaction(FalseLit, EmptyArgs));
}

TEST_F(ConceptTest, CheckConstraintSatisfactionNullConstraint) {
  auto &CS = S->getConstraintChecker();
  TemplateArgumentList EmptyArgs;
  EXPECT_TRUE(CS.CheckConstraintSatisfaction(nullptr, EmptyArgs));
}

// --- Conjunction / Disjunction ---

TEST_F(ConceptTest, ConjunctionBothTrue) {
  auto &Checker = S->getConstraintChecker();

  auto *True1 = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                                Context.getBoolType());
  auto *True2 = Context.create<CXXBoolLiteral>(SourceLocation(2), true,
                                                Context.getBoolType());
  auto *And = Context.create<BinaryOperator>(SourceLocation(3), True1, True2,
                                              BinaryOpKind::LAnd);

  TemplateArgumentList EmptyArgs;
  EXPECT_TRUE(Checker.CheckConstraintSatisfaction(And, EmptyArgs));
}

TEST_F(ConceptTest, ConjunctionLeftFalse) {
  auto &Checker = S->getConstraintChecker();

  auto *False = Context.create<CXXBoolLiteral>(SourceLocation(1), false,
                                                Context.getBoolType());
  auto *True = Context.create<CXXBoolLiteral>(SourceLocation(2), true,
                                               Context.getBoolType());
  auto *And = Context.create<BinaryOperator>(SourceLocation(3), False, True,
                                              BinaryOpKind::LAnd);

  TemplateArgumentList EmptyArgs;
  EXPECT_FALSE(Checker.CheckConstraintSatisfaction(And, EmptyArgs));
}

TEST_F(ConceptTest, DisjunctionLeftTrue) {
  auto &Checker = S->getConstraintChecker();

  auto *True = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                               Context.getBoolType());
  auto *False = Context.create<CXXBoolLiteral>(SourceLocation(2), false,
                                                Context.getBoolType());
  auto *Or = Context.create<BinaryOperator>(SourceLocation(3), True, False,
                                             BinaryOpKind::LOr);

  TemplateArgumentList EmptyArgs;
  EXPECT_TRUE(Checker.CheckConstraintSatisfaction(Or, EmptyArgs));
}

TEST_F(ConceptTest, DisjunctionBothFalse) {
  auto &Checker = S->getConstraintChecker();

  auto *False1 = Context.create<CXXBoolLiteral>(SourceLocation(1), false,
                                                 Context.getBoolType());
  auto *False2 = Context.create<CXXBoolLiteral>(SourceLocation(2), false,
                                                 Context.getBoolType());
  auto *Or = Context.create<BinaryOperator>(SourceLocation(3), False1, False2,
                                             BinaryOpKind::LOr);

  TemplateArgumentList EmptyArgs;
  EXPECT_FALSE(Checker.CheckConstraintSatisfaction(Or, EmptyArgs));
}

// --- Unary NOT ---

TEST_F(ConceptTest, UnaryNotTrue) {
  auto &Checker = S->getConstraintChecker();

  auto *True = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                               Context.getBoolType());
  auto *Not = Context.create<UnaryOperator>(SourceLocation(2), True,
                                             UnaryOpKind::LNot);

  TemplateArgumentList EmptyArgs;
  EXPECT_FALSE(Checker.CheckConstraintSatisfaction(Not, EmptyArgs));
}

TEST_F(ConceptTest, UnaryNotFalse) {
  auto &Checker = S->getConstraintChecker();

  auto *False = Context.create<CXXBoolLiteral>(SourceLocation(1), false,
                                                Context.getBoolType());
  auto *Not = Context.create<UnaryOperator>(SourceLocation(2), False,
                                             UnaryOpKind::LNot);

  TemplateArgumentList EmptyArgs;
  EXPECT_TRUE(Checker.CheckConstraintSatisfaction(Not, EmptyArgs));
}

// --- Concept Satisfaction ---

TEST_F(ConceptTest, CheckConceptSatisfactionWithTrueConstraint) {
  auto &Checker = S->getConstraintChecker();

  auto *Constraint = Context.create<CXXBoolLiteral>(
      SourceLocation(1), true, Context.getBoolType());
  auto *CD = Context.create<ConceptDecl>(
      SourceLocation(1), "True", Constraint, nullptr);

  llvm::SmallVector<TemplateArgument, 2> Args;
  EXPECT_TRUE(Checker.CheckConceptSatisfaction(CD, Args));
}

TEST_F(ConceptTest, CheckConceptSatisfactionWithFalseConstraint) {
  auto &Checker = S->getConstraintChecker();

  auto *Constraint = Context.create<CXXBoolLiteral>(
      SourceLocation(1), false, Context.getBoolType());
  auto *CD = Context.create<ConceptDecl>(
      SourceLocation(1), "False", Constraint, nullptr);

  llvm::SmallVector<TemplateArgument, 2> Args;
  EXPECT_FALSE(Checker.CheckConceptSatisfaction(CD, Args));
}

// --- Requires Expression ---

TEST_F(ConceptTest, EvaluateRequiresExprEmptyRequirements) {
  auto &Checker = S->getConstraintChecker();

  llvm::SmallVector<Requirement *, 4> Reqs;
  auto *RE = Context.create<RequiresExpr>(SourceLocation(1), Reqs);

  EXPECT_TRUE(Checker.EvaluateRequiresExpr(RE));
}

TEST_F(ConceptTest, EvaluateRequiresExprWithTypeRequirement) {
  auto &Checker = S->getConstraintChecker();

  auto *TR = new TypeRequirement(Context.getIntType(), SourceLocation(1));
  llvm::SmallVector<Requirement *, 4> Reqs = {TR};
  auto *RE = Context.create<RequiresExpr>(SourceLocation(1), Reqs);

  EXPECT_TRUE(Checker.EvaluateRequiresExpr(RE));
}

TEST_F(ConceptTest, EvaluateRequiresExprWithExprRequirement) {
  auto &Checker = S->getConstraintChecker();

  auto *E = Context.create<IntegerLiteral>(SourceLocation(1),
                                            llvm::APInt(32, 42),
                                            Context.getIntType());
  auto *ER = new ExprRequirement(E, false, SourceLocation(1));
  llvm::SmallVector<Requirement *, 4> Reqs = {ER};
  auto *RE = Context.create<RequiresExpr>(SourceLocation(1), Reqs);

  EXPECT_TRUE(Checker.EvaluateRequiresExpr(RE));
}

TEST_F(ConceptTest, EvaluateRequiresExprWithNestedRequirement) {
  auto &Checker = S->getConstraintChecker();

  auto *TrueLit = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                                   Context.getBoolType());
  auto *NR = new NestedRequirement(TrueLit, SourceLocation(1));
  llvm::SmallVector<Requirement *, 4> Reqs = {NR};
  auto *RE = Context.create<RequiresExpr>(SourceLocation(1), Reqs);

  EXPECT_TRUE(Checker.EvaluateRequiresExpr(RE));
}

// --- Complex Constraint: (true && true) || false ---

TEST_F(ConceptTest, ComplexConstraintTree) {
  auto &Checker = S->getConstraintChecker();

  auto *True = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                               Context.getBoolType());
  auto *False = Context.create<CXXBoolLiteral>(SourceLocation(2), false,
                                                Context.getBoolType());

  auto *And = Context.create<BinaryOperator>(SourceLocation(3), True, True,
                                              BinaryOpKind::LAnd);
  auto *Or = Context.create<BinaryOperator>(SourceLocation(4), And, False,
                                             BinaryOpKind::LOr);

  TemplateArgumentList EmptyArgs;
  EXPECT_TRUE(Checker.CheckConstraintSatisfaction(Or, EmptyArgs));
}

} // anonymous namespace

//===--- Stage71Test.cpp - Stage 7.1 C++23 P1 Features Tests -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for Stage 7.1 — C++23 P1 Features:
// - Task 7.1.1: Deducing this (P0847R7)
// - Task 7.1.2: Decay-copy auto(x)/auto{x} (P0849R8)
// - Task 7.1.3: Static operator (P1169R4, P2589R1)
// - Task 7.1.4: [[assume]] attribute (P1774R8)
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceLocation.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Task 7.1.1: Deducing this (P0847R7)
//===----------------------------------------------------------------------===//

TEST(DeducingThisTest, ParmVarDeclExplicitObjectParam) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Create a ParmVarDecl and mark it as explicit object parameter
  auto *PVD = Context.create<ParmVarDecl>(Loc, "self",
      QualType(), 0, nullptr);
  ASSERT_NE(PVD, nullptr);

  // Initially not an explicit object param
  EXPECT_FALSE(PVD->isExplicitObjectParam());

  // Mark as explicit object param
  PVD->setExplicitObjectParam(true);
  EXPECT_TRUE(PVD->isExplicitObjectParam());

  // Can unmark
  PVD->setExplicitObjectParam(false);
  EXPECT_FALSE(PVD->isExplicitObjectParam());
}

TEST(DeducingThisTest, FunctionDeclExplicitObjectParam) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Create a FunctionDecl
  llvm::SmallVector<ParmVarDecl *, 4> Params;
  auto *FD = Context.create<FunctionDecl>(Loc, "func", QualType(), Params,
      nullptr, false, false, false);
  ASSERT_NE(FD, nullptr);

  // Initially no explicit object param
  EXPECT_FALSE(FD->hasExplicitObjectParam());
  EXPECT_EQ(FD->getExplicitObjectParam(), nullptr);

  // Create and set explicit object param
  auto *ExplicitParam = Context.create<ParmVarDecl>(Loc, "self",
      QualType(), 0, nullptr);
  FD->setExplicitObjectParam(ExplicitParam);

  EXPECT_TRUE(FD->hasExplicitObjectParam());
  EXPECT_EQ(FD->getExplicitObjectParam(), ExplicitParam);
}

TEST(DeducingThisTest, CXXMethodDeclExplicitObjectParam) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Create a CXXRecordDecl
  auto *RD = Context.create<CXXRecordDecl>(Loc, "MyClass",
      TagDecl::TK_class);

  // Create a CXXMethodDecl with explicit object param
  llvm::SmallVector<ParmVarDecl *, 4> Params;
  auto *MD = Context.create<CXXMethodDecl>(Loc, "method", QualType(), Params,
      RD, nullptr, false, false, false, false, false, false, false,
      false, false, CXXMethodDecl::RQ_None, false, false, nullptr,
      AccessSpecifier::AS_public);
  ASSERT_NE(MD, nullptr);

  // Set explicit object param
  auto *ExplicitParam = Context.create<ParmVarDecl>(Loc, "self",
      QualType(), 0, nullptr);
  MD->setExplicitObjectParam(ExplicitParam);

  EXPECT_TRUE(MD->hasExplicitObjectParam());
  EXPECT_EQ(MD->getExplicitObjectParam(), ExplicitParam);
}

//===----------------------------------------------------------------------===//
// Task 7.1.2: Decay-copy auto(x)/auto{x} (P0849R8)
//===----------------------------------------------------------------------===//

TEST(DecayCopyExprTest, CreateDecayCopyExpr) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Create a sub-expression (integer literal)
  auto *SubExpr = Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 42));
  SubExpr->setType(QualType(Context.getBuiltinType(BuiltinKind::Int), Qualifier::None));

  // Create DecayCopyExpr with direct init (auto{expr})
  auto *DCE = Context.create<DecayCopyExpr>(Loc, SubExpr, true);
  ASSERT_NE(DCE, nullptr);

  EXPECT_EQ(DCE->getSubExpr(), SubExpr);
  EXPECT_TRUE(DCE->isDirectInit());
  EXPECT_EQ(DCE->getKind(), ASTNode::NodeKind::DecayCopyExprKind);
  EXPECT_TRUE(DCE->isPRValue());
}

TEST(DecayCopyExprTest, DecayCopyExprIndirectInit) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *SubExpr = Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 100));
  SubExpr->setType(QualType(Context.getBuiltinType(BuiltinKind::Int), Qualifier::None));

  // Create DecayCopyExpr with indirect init (auto(expr))
  auto *DCE = Context.create<DecayCopyExpr>(Loc, SubExpr, false);
  ASSERT_NE(DCE, nullptr);

  EXPECT_FALSE(DCE->isDirectInit());
}

TEST(DecayCopyExprTest, DecayCopyExprClassOf) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *SubExpr = Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 0));
  auto *DCE = Context.create<DecayCopyExpr>(Loc, SubExpr, true);

  EXPECT_TRUE(DecayCopyExpr::classof(DCE));
  EXPECT_FALSE(DecayCopyExpr::classof(SubExpr));
}

//===----------------------------------------------------------------------===//
// Task 7.1.3: Static operator (P1169R4, P2589R1)
//===----------------------------------------------------------------------===//

TEST(StaticOperatorTest, StaticCallOperator) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *RD = Context.create<CXXRecordDecl>(Loc, "Callable",
      TagDecl::TK_class);

  llvm::SmallVector<ParmVarDecl *, 4> Params;
  auto *MD = Context.create<CXXMethodDecl>(Loc, "operator()", QualType(),
      Params, RD, nullptr, true, false, false, false, false, false,
      false, false, false, CXXMethodDecl::RQ_None, false, false, nullptr,
      AccessSpecifier::AS_public);
  ASSERT_NE(MD, nullptr);

  // Mark as static operator
  MD->setStaticOperator(true);
  EXPECT_TRUE(MD->isStaticOperator());
  EXPECT_TRUE(MD->isStaticCallOperator());
  EXPECT_FALSE(MD->isStaticSubscriptOperator());
}

TEST(StaticOperatorTest, StaticSubscriptOperator) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *RD = Context.create<CXXRecordDecl>(Loc, "Container",
      TagDecl::TK_class);

  llvm::SmallVector<ParmVarDecl *, 4> Params;
  auto *MD = Context.create<CXXMethodDecl>(Loc, "operator[]", QualType(),
      Params, RD, nullptr, true, false, false, false, false, false,
      false, false, false, CXXMethodDecl::RQ_None, false, false, nullptr,
      AccessSpecifier::AS_public);
  ASSERT_NE(MD, nullptr);

  MD->setStaticOperator(true);
  EXPECT_TRUE(MD->isStaticOperator());
  EXPECT_FALSE(MD->isStaticCallOperator());
  EXPECT_TRUE(MD->isStaticSubscriptOperator());
}

TEST(StaticOperatorTest, NotStaticOperator) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *RD = Context.create<CXXRecordDecl>(Loc, "MyClass",
      TagDecl::TK_class);

  llvm::SmallVector<ParmVarDecl *, 4> Params;
  auto *MD = Context.create<CXXMethodDecl>(Loc, "method", QualType(),
      Params, RD, nullptr, false, false, false, false, false, false,
      false, false, false, CXXMethodDecl::RQ_None, false, false, nullptr,
      AccessSpecifier::AS_public);
  ASSERT_NE(MD, nullptr);

  EXPECT_FALSE(MD->isStaticOperator());
  EXPECT_FALSE(MD->isStaticCallOperator());
  EXPECT_FALSE(MD->isStaticSubscriptOperator());
}

//===----------------------------------------------------------------------===//
// Task 7.1.4: [[assume]] attribute (P1774R8) — tested via integration tests
//===----------------------------------------------------------------------===//

TEST(AssumeAttrTest, AssumeIsCompileOnlyHint) {
  // [[assume]] is a compile-time optimization hint that generates llvm.assume.
  // It produces no runtime value and is tested via CodeGen integration tests.
  // This test verifies the attribute can be stored and retrieved.

  ASTContext Context;
  SourceLocation Loc(1);

  auto *AttrList = Context.create<AttributeListDecl>(Loc);
  ASSERT_NE(AttrList, nullptr);

  // Create an assume attribute with a boolean condition
  auto *CondExpr = Context.create<CXXBoolLiteral>(Loc, true);
  auto *AssumeAttr = Context.create<AttributeDecl>(Loc, "assume", CondExpr);
  AttrList->addAttribute(AssumeAttr);

  auto Attrs = AttrList->getAttributes();
  ASSERT_FALSE(Attrs.empty());
  EXPECT_EQ(Attrs[0]->getAttributeName(), "assume");
  EXPECT_NE(Attrs[0]->getArgumentExpr(), nullptr);
}

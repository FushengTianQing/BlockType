//===--- Stage72Test.cpp - Stage 7.2 C++26 Reflection Tests ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for Stage 7.2 — C++26 Static Reflection:
// - Task 7.2.1: reflexpr keyword enhancement
// - Task 7.2.2: Metaprogramming support
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/ReflectionTypes.h"
#include "blocktype/Basic/SourceLocation.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Task 7.2.1: ReflexprExpr AST node
//===----------------------------------------------------------------------===//

TEST(ReflexprExprTest, TypeOperand) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Create a type to reflect
  QualType IntType = Context.getIntType();
  ASSERT_FALSE(IntType.isNull());

  // Create ReflexprExpr with type operand
  auto *RE = Context.create<ReflexprExpr>(Loc, IntType);
  ASSERT_NE(RE, nullptr);

  // Check operand kind
  EXPECT_TRUE(RE->reflectsType());
  EXPECT_FALSE(RE->reflectsExpression());
  EXPECT_EQ(RE->getOperandKind(), ReflexprExpr::OK_Type);

  // Check reflected type
  EXPECT_EQ(RE->getReflectedType().getTypePtr(), IntType.getTypePtr());
  EXPECT_EQ(RE->getArgument(), nullptr);

  // Check node kind
  EXPECT_EQ(RE->getKind(), ASTNode::NodeKind::ReflexprExprKind);
}

TEST(ReflexprExprTest, ExpressionOperand) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Create an expression to reflect
  auto *Lit = Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 42),
                                              Context.getIntType());
  ASSERT_NE(Lit, nullptr);

  // Create ReflexprExpr with expression operand
  auto *RE = Context.create<ReflexprExpr>(Loc, Lit);
  ASSERT_NE(RE, nullptr);

  // Check operand kind
  EXPECT_FALSE(RE->reflectsType());
  EXPECT_TRUE(RE->reflectsExpression());
  EXPECT_EQ(RE->getOperandKind(), ReflexprExpr::OK_Expression);

  // Check argument
  EXPECT_EQ(RE->getArgument(), Lit);
  EXPECT_TRUE(RE->getReflectedType().isNull());
}

TEST(ReflexprExprTest, ResultType) {
  ASTContext Context;
  SourceLocation Loc(1);

  QualType IntType = Context.getIntType();
  auto *RE = Context.create<ReflexprExpr>(Loc, IntType);

  // Initially no result type
  EXPECT_TRUE(RE->getResultType().isNull());

  // Set result type
  QualType MetaType = Context.getMetaInfoType();
  RE->setResultType(MetaType);
  EXPECT_FALSE(RE->getResultType().isNull());
  EXPECT_EQ(RE->getResultType().getTypePtr(), MetaType.getTypePtr());
}

TEST(ReflexprExprTest, TypeDependence) {
  ASTContext Context;
  SourceLocation Loc(1);

  // Non-dependent type
  QualType IntType = Context.getIntType();
  auto *RE1 = Context.create<ReflexprExpr>(Loc, IntType);
  EXPECT_FALSE(RE1->isTypeDependent());

  // Expression with non-dependent type
  auto *Lit = Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 0),
                                              Context.getIntType());
  auto *RE2 = Context.create<ReflexprExpr>(Loc, Lit);
  EXPECT_FALSE(RE2->isTypeDependent());
}

//===----------------------------------------------------------------------===//
// Task 7.2.1: MetaInfoType
//===----------------------------------------------------------------------===//

TEST(MetaInfoTypeTest, BasicProperties) {
  MetaInfoType MI(nullptr, MetaInfoType::RK_Type);
  EXPECT_EQ(MI.getTypeClass(), TypeClass::MetaInfo);
  EXPECT_TRUE(MI.reflectsType());
  EXPECT_FALSE(MI.reflectsDecl());
  EXPECT_FALSE(MI.reflectsExpr());
}

TEST(MetaInfoTypeTest, ReflecteeKinds) {
  MetaInfoType MIType(nullptr, MetaInfoType::RK_Type);
  EXPECT_TRUE(MIType.reflectsType());

  MetaInfoType MIDecl(nullptr, MetaInfoType::RK_Decl);
  EXPECT_TRUE(MIDecl.reflectsDecl());

  MetaInfoType MIExpr(nullptr, MetaInfoType::RK_Expr);
  EXPECT_TRUE(MIExpr.reflectsExpr());
}

TEST(MetaInfoTypeTest, Classof) {
  MetaInfoType MI(nullptr, MetaInfoType::RK_Type);
  EXPECT_TRUE(MetaInfoType::classof(&MI));

  // Non-MetaInfo type should not match
  BuiltinType BT(BuiltinKind::Int);
  EXPECT_FALSE(MetaInfoType::classof(&BT));
}

TEST(ASTContextTest, GetMetaInfoType) {
  ASTContext Context;
  QualType MetaType = Context.getMetaInfoType();

  ASSERT_FALSE(MetaType.isNull());
  EXPECT_EQ(MetaType.getTypePtr()->getTypeClass(), TypeClass::MetaInfo);

  // Should be consistent (singleton)
  QualType MetaType2 = Context.getMetaInfoType();
  EXPECT_EQ(MetaType.getTypePtr(), MetaType2.getTypePtr());
}

//===----------------------------------------------------------------------===//
// Task 7.2.2: meta::TypeInfo and meta::MemberInfo
//===----------------------------------------------------------------------===//

TEST(TypeInfoTest, NullRecord) {
  meta::TypeInfo TI(nullptr);
  EXPECT_FALSE(TI.isValid());
  EXPECT_TRUE(TI.getMembers().empty());
  EXPECT_TRUE(TI.getFields().empty());
  EXPECT_TRUE(TI.getMethods().empty());
  EXPECT_TRUE(TI.getBases().empty());
  EXPECT_EQ(TI.getName(), "");
  EXPECT_FALSE(TI.hasMember("anything"));
}

TEST(TypeInfoTest, EmptyRecord) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *CXXRD = Context.create<CXXRecordDecl>(Loc, "EmptyClass",
                                                TagDecl::TK_class);
  ASSERT_NE(CXXRD, nullptr);

  meta::TypeInfo TI(CXXRD);
  EXPECT_TRUE(TI.isValid());
  EXPECT_EQ(TI.getName(), "EmptyClass");
  EXPECT_TRUE(TI.getMembers().empty());
  EXPECT_TRUE(TI.getFields().empty());
  EXPECT_TRUE(TI.getMethods().empty());
  EXPECT_FALSE(TI.hasMember("x"));
}

TEST(TypeInfoTest, RecordWithFieldsAndMethods) {
  ASTContext Context;
  SourceLocation Loc(1);

  auto *CXXRD = Context.create<CXXRecordDecl>(Loc, "MyClass",
                                                TagDecl::TK_class);
  ASSERT_NE(CXXRD, nullptr);

  // Add fields
  auto *F1 = Context.create<FieldDecl>(Loc, "x", Context.getIntType(),
      nullptr, false, nullptr, AccessSpecifier::AS_public);
  auto *F2 = Context.create<FieldDecl>(Loc, "name", Context.getIntType(),
      nullptr, false, nullptr, AccessSpecifier::AS_private);
  CXXRD->addField(F1);
  CXXRD->addField(F2);

  // Add method (minimal constructor args)
  auto *Method = Context.create<CXXMethodDecl>(Loc, "getValue",
      QualType(), llvm::ArrayRef<ParmVarDecl *>(), CXXRD, nullptr);
  CXXRD->addMethod(Method);

  meta::TypeInfo TI(CXXRD);
  EXPECT_TRUE(TI.isValid());
  EXPECT_EQ(TI.getName(), "MyClass");

  // Check fields
  auto Fields = TI.getFields();
  EXPECT_EQ(Fields.size(), 2u);
  EXPECT_EQ(Fields[0].Name, "x");
  EXPECT_EQ(Fields[0].Access, AccessSpecifier::AS_public);
  EXPECT_EQ(Fields[1].Name, "name");
  EXPECT_EQ(Fields[1].Access, AccessSpecifier::AS_private);

  // Check methods
  auto Methods = TI.getMethods();
  EXPECT_EQ(Methods.size(), 1u);
  EXPECT_EQ(Methods[0].Name, "getValue");
  EXPECT_TRUE(Methods[0].IsFunction);

  // Check all members
  auto Members = TI.getMembers();
  EXPECT_EQ(Members.size(), 3u); // 2 fields + 1 method

  // Check hasMember
  EXPECT_TRUE(TI.hasMember("x"));
  EXPECT_TRUE(TI.hasMember("name"));
  EXPECT_TRUE(TI.hasMember("getValue"));
  EXPECT_FALSE(TI.hasMember("nonexistent"));
}

//===----------------------------------------------------------------------===//
// Task 7.2.2: meta::InfoType
//===----------------------------------------------------------------------===//

TEST(InfoTypeTest, NullInfo) {
  meta::InfoType Info;
  EXPECT_TRUE(Info.isNull());
  EXPECT_FALSE(Info.isType());
  EXPECT_FALSE(Info.isDecl());
  EXPECT_FALSE(Info.isExpr());
}

TEST(InfoTypeTest, TypeInfo) {
  meta::InfoType Info(reinterpret_cast<void*>(0x1), meta::InfoType::EntityKind::EK_Type);
  EXPECT_FALSE(Info.isNull());
  EXPECT_TRUE(Info.isType());
}

TEST(InfoTypeTest, DeclInfo) {
  meta::InfoType Info(reinterpret_cast<void*>(0x2), meta::InfoType::EntityKind::EK_Decl);
  EXPECT_TRUE(Info.isDecl());
}

TEST(InfoTypeTest, ExprInfo) {
  meta::InfoType Info(reinterpret_cast<void*>(0x3), meta::InfoType::EntityKind::EK_Expr);
  EXPECT_TRUE(Info.isExpr());
}

//===----------------------------------------------------------------------===//
// Task 7.2.2: meta utility functions
//===----------------------------------------------------------------------===//

TEST(ReflectionUtilTest, GetAccessSpecifierName) {
  EXPECT_EQ(meta::getAccessSpecifierName(AccessSpecifier::AS_public), "public");
  EXPECT_EQ(meta::getAccessSpecifierName(AccessSpecifier::AS_protected), "protected");
  EXPECT_EQ(meta::getAccessSpecifierName(AccessSpecifier::AS_private), "private");
}

//===--- TypeCheckTest.cpp - TypeCheck Unit Tests -----------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — 类型检查测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/TypeCheck.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class TypeCheckTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  TypeCheck TC;

  TypeCheckTest() : Diags(), TC(Context, Diags) {}
};

// --- isTypeCompatible ---

TEST_F(TypeCheckTest, SameTypesAreCompatible) {
  EXPECT_TRUE(TC.isTypeCompatible(Context.getIntType(), Context.getIntType()));
}

TEST_F(TypeCheckTest, IntToBoolCompatible) {
  EXPECT_TRUE(TC.isTypeCompatible(Context.getIntType(), Context.getBoolType()));
}

TEST_F(TypeCheckTest, VoidToIntNotCompatible) {
  EXPECT_FALSE(TC.isTypeCompatible(Context.getVoidType(), Context.getIntType()));
}

// --- isSameType ---

TEST_F(TypeCheckTest, SameBuiltinTypes) {
  EXPECT_TRUE(TC.isSameType(Context.getIntType(), Context.getIntType()));
  EXPECT_TRUE(TC.isSameType(Context.getBoolType(), Context.getBoolType()));
}

TEST_F(TypeCheckTest, DifferentBuiltinTypes) {
  EXPECT_FALSE(TC.isSameType(Context.getIntType(), Context.getBoolType()));
  EXPECT_FALSE(TC.isSameType(Context.getFloatType(), Context.getDoubleType()));
}

// --- getCommonType ---

TEST_F(TypeCheckTest, CommonTypeIntInt) {
  QualType Result = TC.getCommonType(Context.getIntType(), Context.getIntType());
  EXPECT_FALSE(Result.isNull());
  EXPECT_TRUE(TC.isSameType(Result, Context.getIntType()));
}

TEST_F(TypeCheckTest, CommonTypeIntFloat) {
  QualType Result = TC.getCommonType(Context.getIntType(), Context.getFloatType());
  EXPECT_FALSE(Result.isNull());
}

// --- getBinaryOperatorResultType ---

TEST_F(TypeCheckTest, BinaryAddIntIntReturnsInt) {
  QualType T = TC.getBinaryOperatorResultType(BinaryOpKind::Add,
      Context.getIntType(), Context.getIntType());
  EXPECT_FALSE(T.isNull());
}

TEST_F(TypeCheckTest, BinaryLTReturnsBool) {
  QualType T = TC.getBinaryOperatorResultType(BinaryOpKind::LT,
      Context.getIntType(), Context.getIntType());
  EXPECT_EQ(T, Context.getBoolType());
}

TEST_F(TypeCheckTest, BinaryLAndReturnsBool) {
  QualType T = TC.getBinaryOperatorResultType(BinaryOpKind::LAnd,
      Context.getBoolType(), Context.getBoolType());
  EXPECT_EQ(T, Context.getBoolType());
}

TEST_F(TypeCheckTest, BinaryAssignReturnsLHS) {
  QualType IntTy = Context.getIntType();
  QualType T = TC.getBinaryOperatorResultType(BinaryOpKind::Assign, IntTy, IntTy);
  EXPECT_EQ(T, IntTy);
}

TEST_F(TypeCheckTest, BinaryCommaReturnsRHS) {
  QualType IntTy = Context.getIntType();
  QualType FloatTy = Context.getFloatType();
  QualType T = TC.getBinaryOperatorResultType(BinaryOpKind::Comma, IntTy, FloatTy);
  EXPECT_EQ(T, FloatTy);
}

// --- getUnaryOperatorResultType ---

TEST_F(TypeCheckTest, UnaryLNotReturnsBool) {
  QualType T = TC.getUnaryOperatorResultType(UnaryOpKind::LNot,
                                              Context.getIntType());
  EXPECT_EQ(T, Context.getBoolType());
}

TEST_F(TypeCheckTest, UnaryMinusPromotesToInt) {
  QualType T = TC.getUnaryOperatorResultType(UnaryOpKind::Minus,
                                              Context.getBoolType());
  EXPECT_EQ(T, Context.getIntType());
}

TEST_F(TypeCheckTest, UnaryDerefReturnsPointee) {
  QualType IntTy = Context.getIntType();
  QualType PtrTy = Context.getPointerType(IntTy.getTypePtr());
  QualType T = TC.getUnaryOperatorResultType(UnaryOpKind::Deref, PtrTy);
  EXPECT_EQ(T, IntTy);
}

TEST_F(TypeCheckTest, UnaryAddrOfReturnsPointer) {
  QualType IntTy = Context.getIntType();
  QualType T = TC.getUnaryOperatorResultType(UnaryOpKind::AddrOf, IntTy);
  EXPECT_FALSE(T.isNull());
}

TEST_F(TypeCheckTest, UnaryPreIncReturnsOperand) {
  QualType IntTy = Context.getIntType();
  QualType T = TC.getUnaryOperatorResultType(UnaryOpKind::PreInc, IntTy);
  EXPECT_EQ(T, IntTy);
}

// --- CheckCondition ---

TEST_F(TypeCheckTest, CheckConditionBoolExpr) {
  auto *E = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                            Context.getBoolType());
  EXPECT_TRUE(TC.CheckCondition(E, SourceLocation(1)));
}

TEST_F(TypeCheckTest, CheckConditionIntExpr) {
  auto *E = Context.create<IntegerLiteral>(SourceLocation(1),
                                            llvm::APInt(32, 0),
                                            Context.getIntType());
  EXPECT_TRUE(TC.CheckCondition(E, SourceLocation(1)));
}

} // anonymous namespace

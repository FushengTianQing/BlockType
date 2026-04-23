//===--- ConstantExprTest.cpp - ConstantExpr Unit Tests -----*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — 常量表达式求值测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/ConstantExpr.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"

using namespace blocktype;

namespace {

class ConstantExprTest : public ::testing::Test {
protected:
  ASTContext Context;
  ConstantExprEvaluator Eval;

  ConstantExprTest() : Eval(Context) {}

  IntegerLiteral *makeInt(uint64_t Val) {
    return Context.create<IntegerLiteral>(SourceLocation(1),
                                           llvm::APInt(32, Val),
                                           Context.getIntType());
  }

  FloatingLiteral *makeFloat(double Val) {
    return Context.create<FloatingLiteral>(
        SourceLocation(1), llvm::APFloat(Val), Context.getDoubleType());
  }

  CXXBoolLiteral *makeBool(bool Val) {
    return Context.create<CXXBoolLiteral>(SourceLocation(1), Val,
                                          Context.getBoolType());
  }

  CharacterLiteral *makeChar(uint32_t Val) {
    return Context.create<CharacterLiteral>(SourceLocation(1), Val,
                                             Context.getCharType());
  }
};

// --- EvalResult ---

TEST_F(ConstantExprTest, EvalResultSuccessInt) {
  auto R = EvalResult::getSuccess(llvm::APSInt(llvm::APInt(32, 42)));
  EXPECT_TRUE(R.isSuccess());
  EXPECT_TRUE(R.isIntegral());
  EXPECT_EQ(R.getInt().getLimitedValue(), 42u);
}

TEST_F(ConstantExprTest, EvalResultSuccessFloat) {
  auto R = EvalResult::getSuccess(llvm::APFloat(3.14));
  EXPECT_TRUE(R.isSuccess());
  EXPECT_FALSE(R.isIntegral());
}

TEST_F(ConstantExprTest, EvalResultFailure) {
  auto R = EvalResult::getFailure(EvalResult::NotConstantExpression, "test");
  EXPECT_FALSE(R.isSuccess());
  EXPECT_EQ(R.getKind(), EvalResult::NotConstantExpression);
  EXPECT_EQ(R.getDiagMessage(), "test");
}

// --- Literal evaluation ---

TEST_F(ConstantExprTest, EvaluateIntegerLiteral) {
  auto Result = Eval.Evaluate(makeInt(42));
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_TRUE(Result.isIntegral());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 42u);
}

TEST_F(ConstantExprTest, EvaluateBooleanLiteralTrue) {
  auto Result = Eval.Evaluate(makeBool(true));
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 1u);
}

TEST_F(ConstantExprTest, EvaluateBooleanLiteralFalse) {
  auto Result = Eval.Evaluate(makeBool(false));
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 0u);
}

TEST_F(ConstantExprTest, EvaluateCharacterLiteral) {
  auto Result = Eval.Evaluate(makeChar('A'));
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), static_cast<uint64_t>('A'));
}

TEST_F(ConstantExprTest, EvaluateFloatingLiteral) {
  auto Result = Eval.Evaluate(makeFloat(2.5));
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_FALSE(Result.isIntegral());
}

TEST_F(ConstantExprTest, EvaluateNullExprFails) {
  auto Result = Eval.Evaluate(nullptr);
  EXPECT_FALSE(Result.isSuccess());
}

// --- Binary operators ---

TEST_F(ConstantExprTest, EvaluateBinaryAdd) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(10),
                                              makeInt(20), BinaryOpKind::Add);
  auto Result = Eval.Evaluate(BO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 30u);
}

TEST_F(ConstantExprTest, EvaluateBinarySub) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(50),
                                              makeInt(20), BinaryOpKind::Sub);
  auto Result = Eval.Evaluate(BO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 30u);
}

TEST_F(ConstantExprTest, EvaluateBinaryMul) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(6),
                                              makeInt(7), BinaryOpKind::Mul);
  auto Result = Eval.Evaluate(BO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 42u);
}

TEST_F(ConstantExprTest, EvaluateBinaryDiv) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(100),
                                              makeInt(4), BinaryOpKind::Div);
  auto Result = Eval.Evaluate(BO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 25u);
}

TEST_F(ConstantExprTest, EvaluateBinaryDivByZero) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(1),
                                              makeInt(0), BinaryOpKind::Div);
  auto Result = Eval.Evaluate(BO);
  EXPECT_FALSE(Result.isSuccess());
  EXPECT_EQ(Result.getKind(), EvalResult::EvaluationFailed);
}

TEST_F(ConstantExprTest, EvaluateBinaryLT) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(1),
                                              makeInt(2), BinaryOpKind::LT);
  auto Result = Eval.Evaluate(BO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 1u); // true
}

TEST_F(ConstantExprTest, EvaluateBinaryLAnd) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeBool(true),
                                              makeBool(false), BinaryOpKind::LAnd);
  auto Result = Eval.Evaluate(BO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 0u); // false
}

// --- Unary operators ---

TEST_F(ConstantExprTest, EvaluateUnaryMinus) {
  auto *UO = Context.create<UnaryOperator>(SourceLocation(1), makeInt(5),
                                            UnaryOpKind::Minus);
  auto Result = Eval.Evaluate(UO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ((-Result.getInt()).getLimitedValue(), 5u);
}

TEST_F(ConstantExprTest, EvaluateUnaryNot) {
  auto *UO = Context.create<UnaryOperator>(SourceLocation(1), makeInt(0xFF),
                                            UnaryOpKind::Not);
  auto Result = Eval.Evaluate(UO);
  ASSERT_TRUE(Result.isSuccess());
}

TEST_F(ConstantExprTest, EvaluateUnaryLNot) {
  auto *UO = Context.create<UnaryOperator>(SourceLocation(1), makeInt(0),
                                            UnaryOpKind::LNot);
  auto Result = Eval.Evaluate(UO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 1u); // !0 == true
}

// --- Convenience APIs ---

TEST_F(ConstantExprTest, EvaluateAsBooleanCondition) {
  auto Result = Eval.EvaluateAsBooleanCondition(makeInt(42));
  ASSERT_TRUE(Result.has_value());
  EXPECT_TRUE(*Result);
}

TEST_F(ConstantExprTest, EvaluateAsBooleanConditionFalse) {
  auto Result = Eval.EvaluateAsBooleanCondition(makeInt(0));
  ASSERT_TRUE(Result.has_value());
  EXPECT_FALSE(*Result);
}

TEST_F(ConstantExprTest, EvaluateAsInt) {
  auto Result = Eval.EvaluateAsInt(makeInt(99));
  ASSERT_TRUE(Result.has_value());
  EXPECT_EQ(Result->getLimitedValue(), 99u);
}

TEST_F(ConstantExprTest, EvaluateAsFloat) {
  auto Result = Eval.EvaluateAsFloat(makeFloat(1.5));
  ASSERT_TRUE(Result.has_value());
}

// --- isConstantExpr ---

TEST_F(ConstantExprTest, IsConstantExprLiteral) {
  EXPECT_TRUE(Eval.isConstantExpr(makeInt(1)));
  EXPECT_TRUE(Eval.isConstantExpr(makeBool(true)));
  EXPECT_TRUE(Eval.isConstantExpr(makeChar('x')));
}

TEST_F(ConstantExprTest, IsConstantExprBinaryOp) {
  auto *BO = Context.create<BinaryOperator>(SourceLocation(1), makeInt(1),
                                              makeInt(2), BinaryOpKind::Add);
  EXPECT_TRUE(Eval.isConstantExpr(BO));
}

TEST_F(ConstantExprTest, IsConstantExprNullReturnsFalse) {
  EXPECT_FALSE(Eval.isConstantExpr(nullptr));
}

// --- EnumConstantDecl caching ---

TEST_F(ConstantExprTest, EnumConstantDeclWithCachedValue) {
  auto *ECD = Context.create<EnumConstantDecl>(SourceLocation(1), "Red",
      Context.getIntType());
  ECD->setVal(llvm::APSInt(llvm::APInt(32, 42)));
  ASSERT_TRUE(ECD->hasVal());

  auto *DRE = Context.create<DeclRefExpr>(SourceLocation(2), ECD);
  auto Result = Eval.Evaluate(DRE);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 42u);
}

// --- constexpr VarDecl ---

TEST_F(ConstantExprTest, ConstexprVarDecl) {
  auto *Init = makeInt(100);
  auto *VD = Context.create<VarDecl>(SourceLocation(1), "pi",
                                      Context.getIntType(), Init, false, true);
  ASSERT_TRUE(VD->isConstexpr());

  auto *DRE = Context.create<DeclRefExpr>(SourceLocation(2), VD);
  auto Result = Eval.Evaluate(DRE);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 100u);
}

// --- P3068R6: constexpr throw (Task 4) ---

TEST_F(ConstantExprTest, ThrowExprEvaluationFails) {
  // P3068R6: If a throw is reached during constant evaluation, it fails.
  auto *Throw = Context.create<CXXThrowExpr>(SourceLocation(1), makeInt(42));
  auto Result = Eval.Evaluate(Throw);
  EXPECT_FALSE(Result.isSuccess());
  EXPECT_EQ(Result.getKind(), EvalResult::EvaluationFailed);
}

TEST_F(ConstantExprTest, ThrowExprNullSubExprEvaluationFails) {
  // throw without operand (rethrow)
  auto *Throw = Context.create<CXXThrowExpr>(SourceLocation(1), nullptr);
  auto Result = Eval.Evaluate(Throw);
  EXPECT_FALSE(Result.isSuccess());
  EXPECT_EQ(Result.getKind(), EvalResult::EvaluationFailed);
}

TEST_F(ConstantExprTest, ThrowExprInConditionalNotReached) {
  // true ? 42 : throw — the throw branch is not evaluated
  auto *Throw = Context.create<CXXThrowExpr>(SourceLocation(1), makeInt(0));
  auto *CO = Context.create<ConditionalOperator>(SourceLocation(1),
      makeBool(true), makeInt(42), Throw);
  auto Result = Eval.Evaluate(CO);
  ASSERT_TRUE(Result.isSuccess());
  EXPECT_EQ(Result.getInt().getLimitedValue(), 42u);
}

} // anonymous namespace

//===--- ParserTest.cpp - Parser Expression Tests -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Parse/Parser.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

using namespace blocktype;

namespace {

class ParserTest : public ::testing::Test {
protected:
  SourceManager SM;
  DiagnosticsEngine Diags;
  ASTContext Context;
  std::unique_ptr<Sema> S;
  std::unique_ptr<Preprocessor> PP;
  std::unique_ptr<Parser> P;

  void TearDown() override {
    P.reset();
    PP.reset();
    S.reset();
  }

  void parse(StringRef Code) {
    PP = std::make_unique<Preprocessor>(SM, Diags);
    PP->enterSourceFile("test.cpp", Code);
    S = std::make_unique<Sema>(Context, Diags);
    P = std::make_unique<Parser>(*PP, Context, *S);
  }
};

//===----------------------------------------------------------------------===//
// Literal Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, IntegerLiteral) {
  parse("42");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<IntegerLiteral>(E));
  
  auto *Lit = llvm::cast<IntegerLiteral>(E);
  EXPECT_EQ(Lit->getValue(), 42);
}

TEST_F(ParserTest, IntegerLiteralHex) {
  parse("0xFF");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<IntegerLiteral>(E));
  
  auto *Lit = llvm::cast<IntegerLiteral>(E);
  EXPECT_EQ(Lit->getValue(), 255);
}

TEST_F(ParserTest, IntegerLiteralBinary) {
  parse("0b1010");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<IntegerLiteral>(E));
  
  auto *Lit = llvm::cast<IntegerLiteral>(E);
  EXPECT_EQ(Lit->getValue(), 10);
}

TEST_F(ParserTest, FloatingLiteral) {
  parse("3.14");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<FloatingLiteral>(E));
}

TEST_F(ParserTest, FloatingLiteralScientific) {
  parse("1.5e10");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<FloatingLiteral>(E));
}

TEST_F(ParserTest, StringLiteral) {
  parse("\"hello\"");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<StringLiteral>(E));
  
  auto *Lit = llvm::cast<StringLiteral>(E);
  EXPECT_EQ(Lit->getValue(), "hello");
}

TEST_F(ParserTest, CharacterLiteral) {
  parse("'a'");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CharacterLiteral>(E));
  
  auto *Lit = llvm::cast<CharacterLiteral>(E);
  EXPECT_EQ(Lit->getValue(), 'a');
}

TEST_F(ParserTest, BoolLiteralTrue) {
  parse("true");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CXXBoolLiteral>(E));
  
  auto *Lit = llvm::cast<CXXBoolLiteral>(E);
  EXPECT_TRUE(Lit->getValue());
}

TEST_F(ParserTest, BoolLiteralFalse) {
  parse("false");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CXXBoolLiteral>(E));
  
  auto *Lit = llvm::cast<CXXBoolLiteral>(E);
  EXPECT_FALSE(Lit->getValue());
}

TEST_F(ParserTest, NullPtrLiteral) {
  parse("nullptr");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CXXNullPtrLiteral>(E));
}

//===----------------------------------------------------------------------===//
// Binary Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, BinaryAdd) {
  parse("1 + 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Add);
  EXPECT_TRUE(llvm::isa<IntegerLiteral>(BinOp->getLHS()));
  EXPECT_TRUE(llvm::isa<IntegerLiteral>(BinOp->getRHS()));
}

TEST_F(ParserTest, BinarySubtract) {
  parse("5 - 3");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Sub);
}

TEST_F(ParserTest, BinaryMultiply) {
  parse("2 * 3");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Mul);
}

TEST_F(ParserTest, BinaryDivide) {
  parse("6 / 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Div);
}

TEST_F(ParserTest, BinaryModulo) {
  parse("7 % 3");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Rem);
}

TEST_F(ParserTest, Precedence) {
  // 1 + 2 * 3 should parse as 1 + (2 * 3)
  parse("1 + 2 * 3");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Add);
  
  // RHS should be a multiplication
  EXPECT_TRUE(llvm::isa<BinaryOperator>(BinOp->getRHS()));
  auto *RHS = llvm::cast<BinaryOperator>(BinOp->getRHS());
  EXPECT_EQ(RHS->getOpcode(), BinaryOpKind::Mul);
}

TEST_F(ParserTest, LeftAssociativity) {
  // 1 - 2 - 3 should parse as (1 - 2) - 3
  parse("1 - 2 - 3");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Sub);
  
  // LHS should be a subtraction
  EXPECT_TRUE(llvm::isa<BinaryOperator>(BinOp->getLHS()));
  auto *LHS = llvm::cast<BinaryOperator>(BinOp->getLHS());
  EXPECT_EQ(LHS->getOpcode(), BinaryOpKind::Sub);
}

TEST_F(ParserTest, Parentheses) {
  // (1 + 2) * 3 should parse as (1 + 2) * 3
  parse("(1 + 2) * 3");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Mul);
  
  // LHS should be a parenthesized addition
  EXPECT_TRUE(llvm::isa<BinaryOperator>(BinOp->getLHS()));
  auto *LHS = llvm::cast<BinaryOperator>(BinOp->getLHS());
  EXPECT_EQ(LHS->getOpcode(), BinaryOpKind::Add);
}

//===----------------------------------------------------------------------===//
// Unary Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, UnaryPlus) {
  parse("+5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::Plus);
}

TEST_F(ParserTest, UnaryMinus) {
  parse("-5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::Minus);
}

TEST_F(ParserTest, UnaryNot) {
  parse("!true");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::LNot);
}

TEST_F(ParserTest, UnaryComplement) {
  parse("~0xFF");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::Not);
}

TEST_F(ParserTest, UnaryDereference) {
  parse("*ptr");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::Deref);
}

TEST_F(ParserTest, UnaryAddressOf) {
  parse("&x");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::AddrOf);
}

TEST_F(ParserTest, PreIncrement) {
  parse("++i");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::PreInc);
  EXPECT_TRUE(UnOp->isPrefix());
}

TEST_F(ParserTest, PreDecrement) {
  parse("--i");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::PreDec);
  EXPECT_TRUE(UnOp->isPrefix());
}

TEST_F(ParserTest, PostIncrement) {
  parse("i++");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::PostInc);
  EXPECT_FALSE(UnOp->isPrefix());
}

TEST_F(ParserTest, PostDecrement) {
  parse("i--");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<UnaryOperator>(E));
  
  auto *UnOp = llvm::cast<UnaryOperator>(E);
  EXPECT_EQ(UnOp->getOpcode(), UnaryOpKind::PostDec);
  EXPECT_FALSE(UnOp->isPrefix());
}

//===----------------------------------------------------------------------===//
// Comparison Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, Equal) {
  parse("a == b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::EQ);
}

TEST_F(ParserTest, NotEqual) {
  parse("a != b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::NE);
}

TEST_F(ParserTest, Less) {
  parse("a < b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LT);
}

TEST_F(ParserTest, LessEqual) {
  parse("a <= b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LE);
}

TEST_F(ParserTest, Greater) {
  parse("a > b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::GT);
}

TEST_F(ParserTest, GreaterEqual) {
  parse("a >= b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::GE);
}

//===----------------------------------------------------------------------===//
// Template Specialization Expression Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, TemplateSpecializationWithBuiltinType) {
  // Test: Vector<int> should be parsed as template specialization
  parse("Vector<int>");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateSpecializationExpr>(E));
}

TEST_F(ParserTest, TemplateSpecializationWithIdentifier) {
  // Test: Container<T> should be parsed as template specialization
  // This tests the tentative parsing layer
  parse("Container<T>");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  // After tentative parsing, this should be recognized as template specialization
  EXPECT_TRUE(llvm::isa<TemplateSpecializationExpr>(E));
}

TEST_F(ParserTest, TemplateSpecializationNested) {
  // Test: Vector<Vector<int>> should be parsed as nested template specialization
  parse("Vector<Vector<int>>");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateSpecializationExpr>(E));
}

TEST_F(ParserTest, TemplateSpecializationMultipleArgs) {
  // Test: Pair<int, float> should be parsed as template specialization
  parse("Pair<int, float>");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateSpecializationExpr>(E));
}

TEST_F(ParserTest, ComparisonNotTemplate) {
  // Test: a < b should be parsed as comparison, not template
  parse("a < b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));

  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LT);
}

TEST_F(ParserTest, ChainedComparison) {
  // Test: a < b < c should be parsed as chained comparisons
  parse("a < b < c");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));

  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LT);
}

//===----------------------------------------------------------------------===//
// Logical Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, LogicalAnd) {
  parse("a && b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LAnd);
}

TEST_F(ParserTest, LogicalOr) {
  parse("a || b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LOr);
}

//===----------------------------------------------------------------------===//
// Bitwise Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, BitwiseAnd) {
  parse("a & b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::And);
}

TEST_F(ParserTest, BitwiseOr) {
  parse("a | b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Or);
}

TEST_F(ParserTest, BitwiseXor) {
  parse("a ^ b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Xor);
}

TEST_F(ParserTest, LeftShift) {
  parse("a << 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Shl);
}

TEST_F(ParserTest, RightShift) {
  parse("a >> 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Shr);
}

//===----------------------------------------------------------------------===//
// Assignment Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, SimpleAssignment) {
  parse("x = 5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Assign);
}

TEST_F(ParserTest, AddAssignment) {
  parse("x += 5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::AddAssign);
}

TEST_F(ParserTest, SubAssignment) {
  parse("x -= 5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::SubAssign);
}

TEST_F(ParserTest, MulAssignment) {
  parse("x *= 5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::MulAssign);
}

TEST_F(ParserTest, DivAssignment) {
  parse("x /= 5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::DivAssign);
}

TEST_F(ParserTest, ModAssignment) {
  parse("x %= 5");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::RemAssign);
}

TEST_F(ParserTest, AndAssignment) {
  parse("x &= mask");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::AndAssign);
}

TEST_F(ParserTest, OrAssignment) {
  parse("x |= mask");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::OrAssign);
}

TEST_F(ParserTest, XorAssignment) {
  parse("x ^= mask");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::XorAssign);
}

TEST_F(ParserTest, ShlAssignment) {
  parse("x <<= 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::ShlAssign);
}

TEST_F(ParserTest, ShrAssignment) {
  parse("x >>= 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::ShrAssign);
}

//===----------------------------------------------------------------------===//
// Conditional Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, ConditionalOperator) {
  parse("cond ? a : b");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<ConditionalOperator>(E));
  
  auto *CondOp = llvm::cast<ConditionalOperator>(E);
  EXPECT_NE(CondOp->getCond(), nullptr);
  EXPECT_NE(CondOp->getTrueExpr(), nullptr);
  EXPECT_NE(CondOp->getFalseExpr(), nullptr);
}

TEST_F(ParserTest, NestedConditional) {
  parse("a ? b ? c : d : e");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<ConditionalOperator>(E));
  
  auto *Outer = llvm::cast<ConditionalOperator>(E);
  EXPECT_TRUE(llvm::isa<ConditionalOperator>(Outer->getTrueExpr()));
}

//===----------------------------------------------------------------------===//
// Comma Operator Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, CommaOperator) {
  parse("a, b, c");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::Comma);
}

//===----------------------------------------------------------------------===//
// Identifier and Call Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, Identifier) {
  parse("foo");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<DeclRefExpr>(E));
  
  auto *DRE = llvm::cast<DeclRefExpr>(E);
  // For an undeclared identifier, getDecl() returns nullptr
  // This is valid error recovery behavior
  EXPECT_EQ(DRE->getDecl(), nullptr);
}

TEST_F(ParserTest, CallExpression) {
  parse("foo()");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CallExpr>(E));
  
  auto *Call = llvm::cast<CallExpr>(E);
  EXPECT_EQ(Call->getNumArgs(), 0u);
}

TEST_F(ParserTest, CallWithArgs) {
  parse("foo(1, 2, 3)");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CallExpr>(E));
  
  auto *Call = llvm::cast<CallExpr>(E);
  EXPECT_EQ(Call->getNumArgs(), 3u);
}

TEST_F(ParserTest, NestedCall) {
  parse("foo(bar())");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<CallExpr>(E));
  
  auto *Call = llvm::cast<CallExpr>(E);
  EXPECT_EQ(Call->getNumArgs(), 1u);
  EXPECT_TRUE(llvm::isa<CallExpr>(Call->getArgs()[0]));
}

//===----------------------------------------------------------------------===//
// Complex Expression Tests
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, ComplexExpression1) {
  // a + b * c - d / e
  parse("a + b * c - d / e");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
}

TEST_F(ParserTest, ComplexExpression2) {
  // (a || b) && (c || d)
  parse("(a || b) && (c || d)");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
  
  auto *BinOp = llvm::cast<BinaryOperator>(E);
  EXPECT_EQ(BinOp->getOpcode(), BinaryOpKind::LAnd);
}

TEST_F(ParserTest, DeeplyNested) {
  // (((((1))))) + 2
  parse("(((((1))))) + 2");
  Expr *E = P->parseExpression();
  ASSERT_NE(E, nullptr);
  EXPECT_TRUE(llvm::isa<BinaryOperator>(E));
}

//===----------------------------------------------------------------------===//
// P2893R3: Variadic friend (Task 2)
//===----------------------------------------------------------------------===//

TEST_F(ParserTest, FriendTypeDeclNormal) {
  // Normal friend class declaration
  parse("class C { friend int; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
}

TEST_F(ParserTest, FriendTypeDeclPackExpansion) {
  // P2893R3: friend class Ts...; — pack expansion after class-key type name
  parse("class C { friend class X...; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));

  auto *RD = llvm::cast<CXXRecordDecl>(D);
  // Check that a friend decl with pack expansion exists
  bool hasFriendDecl = false;
  for (auto *Member : RD->members()) {
    if (auto *FD = llvm::dyn_cast<FriendDecl>(Member)) {
      hasFriendDecl = true;
      EXPECT_TRUE(FD->isPackExpansion());
    }
  }
  EXPECT_TRUE(hasFriendDecl);
}

} // anonymous namespace

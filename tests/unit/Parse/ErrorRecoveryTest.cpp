//===--- ErrorRecoveryTest.cpp - Error Recovery Tests -------------*- C++ -*-===//
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
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Decl.h"

using namespace blocktype;

namespace {

class ErrorRecoveryTest : public ::testing::Test {
protected:
  SourceManager SM;
  DiagnosticsEngine Diags;
  ASTContext Context;
  std::unique_ptr<Preprocessor> PP;
  std::unique_ptr<Parser> P;

  void TearDown() override {
    P.reset();
    PP.reset();
  }

  void parse(StringRef Code) {
    PP = std::make_unique<Preprocessor>(SM, Diags);
    PP->enterSourceFile("test.cpp", Code);
    P = std::make_unique<Parser>(*PP, Context);
  }
  
  bool hasErrors() {
    return P->hasErrors();
  }
};

//===----------------------------------------------------------------------===//
// Expression Error Recovery Tests
//===----------------------------------------------------------------------===//

TEST_F(ErrorRecoveryTest, MissingOperand) {
  parse("1 +");
  (void)P->parseExpression();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingClosingParen) {
  parse("(1 + 2");
  (void)P->parseExpression();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingOperandInTernary) {
  parse("a ? b :");
  (void)P->parseExpression();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingOperandInCall) {
  parse("foo(1,)");
  (void)P->parseExpression();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, ExtraOperator) {
  parse("1 + + 2");
  Expr *E = P->parseExpression();
  // May or may not have errors depending on how + is parsed
  // But should not crash
  EXPECT_NE(E, nullptr);
}

TEST_F(ErrorRecoveryTest, InvalidToken) {
  parse("1 @ 2");
  (void)P->parseExpression();
  EXPECT_TRUE(hasErrors());
}

//===----------------------------------------------------------------------===//
// Statement Error Recovery Tests
//===----------------------------------------------------------------------===//

TEST_F(ErrorRecoveryTest, MissingSemicolon) {
  parse("return 42");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingClosingBrace) {
  parse("{ return; ");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingIfCondition) {
  parse("if () ;");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingWhileCondition) {
  parse("while () ;");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingForParts) {
  parse("for ( ) ;");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingSwitchCondition) {
  parse("switch () { }");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, MissingCaseValue) {
  parse("case: ;");
  (void)P->parseStatement();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, BreakOutsideLoop) {
  // Note: Semantic analysis should catch this, not parsing
  // Parser should still parse it
  parse("break;");
  Stmt *S = P->parseStatement();
  EXPECT_NE(S, nullptr);
}

TEST_F(ErrorRecoveryTest, ContinueOutsideLoop) {
  // Note: Semantic analysis should catch this, not parsing
  parse("continue;");
  Stmt *S = P->parseStatement();
  EXPECT_NE(S, nullptr);
}

//===----------------------------------------------------------------------===//
// Recovery After Error Tests
//===----------------------------------------------------------------------===//

TEST_F(ErrorRecoveryTest, RecoveryAfterMissingSemicolon) {
  parse("{ x = 1 y = 2; }");
  Stmt *S = P->parseStatement();
  // Parser should recover and continue parsing
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(S, nullptr);
}

TEST_F(ErrorRecoveryTest, RecoveryAfterMissingBrace) {
  parse("{ if (true) { x = 1 } y = 2; }");
  Stmt *S = P->parseStatement();
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(S, nullptr);
}

TEST_F(ErrorRecoveryTest, MultipleErrors) {
  parse("{ x = @ y = # z = $ }");
  Stmt *S = P->parseStatement();
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(S, nullptr);
}

//===----------------------------------------------------------------------===//
// Edge Case Tests
//===----------------------------------------------------------------------===//

TEST_F(ErrorRecoveryTest, EmptyInput) {
  parse("");
  // Should handle gracefully
  EXPECT_NE(P.get(), nullptr);
}

TEST_F(ErrorRecoveryTest, OnlyWhitespace) {
  parse("   \t\n   ");
  EXPECT_NE(P.get(), nullptr);
}

TEST_F(ErrorRecoveryTest, DeeplyNestedErrors) {
  parse("if (a) if (b) if (c) if (d) { @ }");
  Stmt *S = P->parseStatement();
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(S, nullptr);
}

TEST_F(ErrorRecoveryTest, UnmatchedBrackets) {
  parse("(((1 + 2)");
  (void)P->parseExpression();
  EXPECT_TRUE(hasErrors());
}

TEST_F(ErrorRecoveryTest, ExtraClosingBrackets) {
  parse("(1 + 2)))");
  (void)P->parseExpression();
  // parseExpression() only parses one expression and doesn't check for
  // unconsumed tokens. The extra closing brackets are valid tokens that
  // just weren't consumed. This is expected behavior - the caller should
  // check if there are remaining tokens.
  EXPECT_FALSE(hasErrors());
}

//===----------------------------------------------------------------------===//
// Declaration-Level Recovery Tests
//===----------------------------------------------------------------------===//

TEST_F(ErrorRecoveryTest, SkipToNextDeclaration) {
  parse("int x @@@ int y;");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  // Should recover and parse "int y;" successfully
  ASSERT_NE(TU, nullptr);
  EXPECT_GE(TU->decls().size(), 1u);
}

TEST_F(ErrorRecoveryTest, SkipNestedBrackets) {
  parse("int a = (1 + @@@); int b;");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
  // Should recover past the broken expression and parse "int b;"
  EXPECT_GE(TU->decls().size(), 1u);
}

TEST_F(ErrorRecoveryTest, RecoveryAcrossMultipleErrors) {
  parse("int x @@@ int y ### int z;");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
}

TEST_F(ErrorRecoveryTest, MissingSemicolonBetweenDeclarations) {
  parse("int x int y;");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
  // "int x" then "int y;" — should detect missing ';' and recover
}

TEST_F(ErrorRecoveryTest, GarbageBetweenValidDeclarations) {
  parse("int a; @@@@ int b;");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
  // Should parse "int a;", skip "@@@@", and parse "int b;"
  EXPECT_GE(TU->decls().size(), 2u);
}

TEST_F(ErrorRecoveryTest, MissingSemicolonBeforeClass) {
  parse("int x class Foo {};");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
}

TEST_F(ErrorRecoveryTest, MissingSemicolonBeforeNamespace) {
  parse("int x namespace N {}");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
}

TEST_F(ErrorRecoveryTest, RecoveryInNamespaceBody) {
  parse("namespace N { int x @@@ int y; }");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  ASSERT_NE(TU, nullptr);
}

//===----------------------------------------------------------------------===//
// skipUntilBalanced Tests
//===----------------------------------------------------------------------===//

TEST_F(ErrorRecoveryTest, BalancedParenSkip) {
  parse("void f() { foo(1, @@@, 3); }");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(TU, nullptr);
}

TEST_F(ErrorRecoveryTest, BalancedBraceSkip) {
  parse("int x = { 1, @@@, 3 }; int y;");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(TU, nullptr);
}

TEST_F(ErrorRecoveryTest, NestedBracketsSkip) {
  parse("void f() { foo(bar(@@@), 2); }");
  auto *TU = P->parseTranslationUnit();
  EXPECT_TRUE(hasErrors());
  EXPECT_NE(TU, nullptr);
}

} // anonymous namespace

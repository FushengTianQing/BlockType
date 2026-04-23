//===--- StatementTest.cpp - Statement Parsing Tests --------------*- C++ -*-===//
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
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Sema/Sema.h"

using namespace blocktype;

namespace {

class StatementTest : public ::testing::Test {
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
// Basic Statement Tests
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, NullStatement) {
  parse(";");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<NullStmt>(S));
}

TEST_F(StatementTest, CompoundStatement) {
  parse("{ }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CompoundStmt>(S));
  
  auto *CS = llvm::cast<CompoundStmt>(S);
  EXPECT_EQ(CS->getBody().size(), 0u);
}

TEST_F(StatementTest, CompoundStatementWithStatements) {
  parse("{ ; ; }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CompoundStmt>(S));
  
  auto *CS = llvm::cast<CompoundStmt>(S);
  EXPECT_EQ(CS->getBody().size(), 2u);
}

TEST_F(StatementTest, ReturnStatement) {
  parse("return;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ReturnStmt>(S));
  
  auto *RS = llvm::cast<ReturnStmt>(S);
  EXPECT_EQ(RS->getRetValue(), nullptr);
}

TEST_F(StatementTest, ReturnStatementWithValue) {
  parse("return 42;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ReturnStmt>(S));
  
  auto *RS = llvm::cast<ReturnStmt>(S);
  EXPECT_NE(RS->getRetValue(), nullptr);
  EXPECT_TRUE(llvm::isa<IntegerLiteral>(RS->getRetValue()));
}

TEST_F(StatementTest, ExpressionStatement) {
  parse("42;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ExprStmt>(S));
  
  auto *ES = llvm::cast<ExprStmt>(S);
  EXPECT_NE(ES->getExpr(), nullptr);
}

//===----------------------------------------------------------------------===//
// Control Flow Tests
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, IfStatement) {
  parse("if (true) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<IfStmt>(S));
  
  auto *IS = llvm::cast<IfStmt>(S);
  EXPECT_NE(IS->getCond(), nullptr);
  EXPECT_NE(IS->getThen(), nullptr);
  EXPECT_EQ(IS->getElse(), nullptr);
}

TEST_F(StatementTest, IfElseStatement) {
  parse("if (true) ; else ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<IfStmt>(S));
  
  auto *IS = llvm::cast<IfStmt>(S);
  EXPECT_NE(IS->getCond(), nullptr);
  EXPECT_NE(IS->getThen(), nullptr);
  EXPECT_NE(IS->getElse(), nullptr);
}

TEST_F(StatementTest, IfWithCompoundBody) {
  parse("if (true) { }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<IfStmt>(S));
  
  auto *IS = llvm::cast<IfStmt>(S);
  EXPECT_TRUE(llvm::isa<CompoundStmt>(IS->getThen()));
}

TEST_F(StatementTest, NestedIf) {
  parse("if (a) if (b) ; else ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<IfStmt>(S));
  
  auto *Outer = llvm::cast<IfStmt>(S);
  EXPECT_TRUE(llvm::isa<IfStmt>(Outer->getThen()));
  
  auto *Inner = llvm::cast<IfStmt>(Outer->getThen());
  EXPECT_NE(Inner->getElse(), nullptr);
}

TEST_F(StatementTest, SwitchStatement) {
  parse("switch (x) { }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<SwitchStmt>(S));
  
  auto *SS = llvm::cast<SwitchStmt>(S);
  EXPECT_NE(SS->getCond(), nullptr);
}

TEST_F(StatementTest, CaseStatement) {
  parse("case 1: ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CaseStmt>(S));
  
  auto *CS = llvm::cast<CaseStmt>(S);
  EXPECT_NE(CS->getLHS(), nullptr);
}

TEST_F(StatementTest, DefaultStatement) {
  parse("default: ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<DefaultStmt>(S));
}

//===----------------------------------------------------------------------===//
// Loop Statement Tests
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, WhileStatement) {
  parse("while (true) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<WhileStmt>(S));
  
  auto *WS = llvm::cast<WhileStmt>(S);
  EXPECT_NE(WS->getCond(), nullptr);
  EXPECT_NE(WS->getBody(), nullptr);
}

TEST_F(StatementTest, DoWhileStatement) {
  parse("do ; while (true);");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<DoStmt>(S));
  
  auto *DS = llvm::cast<DoStmt>(S);
  EXPECT_NE(DS->getCond(), nullptr);
  EXPECT_NE(DS->getBody(), nullptr);
}

TEST_F(StatementTest, ForStatement) {
  parse("for (;;) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ForStmt>(S));
  
  auto *FS = llvm::cast<ForStmt>(S);
  EXPECT_NE(FS->getBody(), nullptr);
}

TEST_F(StatementTest, ForStatementWithInit) {
  parse("for (int i = 0; i < 10; ++i) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ForStmt>(S));
}

TEST_F(StatementTest, ForRangeStatement) {
  parse("for (auto x : arr) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CXXForRangeStmt>(S));
}

//===----------------------------------------------------------------------===//
// Jump Statement Tests
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, BreakStatement) {
  parse("break;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<BreakStmt>(S));
}

TEST_F(StatementTest, ContinueStatement) {
  parse("continue;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ContinueStmt>(S));
}

TEST_F(StatementTest, GotoStatement) {
  parse("goto label;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<GotoStmt>(S));
}

TEST_F(StatementTest, LabelStatement) {
  parse("label: ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<LabelStmt>(S));
  
  auto *LS = llvm::cast<LabelStmt>(S);
  EXPECT_NE(LS->getSubStmt(), nullptr);
}

//===----------------------------------------------------------------------===//
// C++ Statement Tests
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, TryStatement) {
  parse("try { } catch (...) { }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CXXTryStmt>(S));
  
  auto *TS = llvm::cast<CXXTryStmt>(S);
  EXPECT_NE(TS->getTryBlock(), nullptr);
  EXPECT_GT(TS->getCatchBlocks().size(), 0u);
}

TEST_F(StatementTest, CatchStatement) {
  parse("try { } catch (int e) { }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CXXTryStmt>(S));
  
  auto *TS = llvm::cast<CXXTryStmt>(S);
  EXPECT_GT(TS->getCatchBlocks().size(), 0u);
}

TEST_F(StatementTest, CoreturnStatement) {
  parse("co_return;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CoreturnStmt>(S));
}

TEST_F(StatementTest, CoreturnWithValue) {
  parse("co_return 42;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CoreturnStmt>(S));
  
  auto *CRS = llvm::cast<CoreturnStmt>(S);
  EXPECT_NE(CRS->getOperand(), nullptr);
}

TEST_F(StatementTest, CoyieldStatement) {
  parse("co_yield value;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CoyieldStmt>(S));
  
  auto *CYS = llvm::cast<CoyieldStmt>(S);
  EXPECT_NE(CYS->getOperand(), nullptr);
}

//===----------------------------------------------------------------------===//
// Nested Statement Tests
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, NestedCompoundStatements) {
  parse("{ { } { } }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<CompoundStmt>(S));
  
  auto *Outer = llvm::cast<CompoundStmt>(S);
  EXPECT_EQ(Outer->getBody().size(), 2u);
  
  for (auto *Inner : Outer->getBody()) {
    EXPECT_TRUE(llvm::isa<CompoundStmt>(Inner));
  }
}

TEST_F(StatementTest, ComplexControlFlow) {
  parse("if (a) { while (b) { if (c) break; } } else { for (;;) continue; }");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<IfStmt>(S));
}

//===----------------------------------------------------------------------===//
// P2360R0: for init-statement using (Task 1)
//===----------------------------------------------------------------------===//

TEST_F(StatementTest, ForWithUsingInit) {
  // P2360R0: for (using T = int; ...)
  parse("for (using T = int; ; ) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ForStmt>(S));
}

TEST_F(StatementTest, ForWithUsingInitAndCondition) {
  // P2360R0: for (using T = int; T x; ) ;
  parse("for (using T = int; true; ) ;");
  Stmt *S = P->parseStatement();
  ASSERT_NE(S, nullptr);
  EXPECT_TRUE(llvm::isa<ForStmt>(S));
}

} // anonymous namespace

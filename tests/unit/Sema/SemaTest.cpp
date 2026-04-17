//===--- SemaTest.cpp - Sema Unit Tests ---------------------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — Sema 主类测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class SemaTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  SemaTest() : Diags() {
    S = std::make_unique<Sema>(Context, Diags);
  }
};

// --- Construction / Scope ---

TEST_F(SemaTest, ConstructionCreatesTUScope) {
  EXPECT_NE(S->getCurrentScope(), nullptr);
}

TEST_F(SemaTest, PushPopScope) {
  Scope *Outer = S->getCurrentScope();
  S->PushScope(ScopeFlags::BlockScope);
  EXPECT_NE(S->getCurrentScope(), Outer);
  S->PopScope();
  EXPECT_EQ(S->getCurrentScope(), Outer);
}

// --- VarDecl ---

TEST_F(SemaTest, ActOnVarDeclBasic) {
  QualType IntTy = Context.getIntType();
  auto Result = S->ActOnVarDecl(SourceLocation(1), "x", IntTy, nullptr);
  EXPECT_TRUE(Result.isUsable());
  auto *VD = llvm::dyn_cast<VarDecl>(Result.get());
  ASSERT_NE(VD, nullptr);
  EXPECT_EQ(VD->getName(), "x");
  EXPECT_FALSE(VD->isConstexpr());
  EXPECT_FALSE(VD->isStatic());
}

TEST_F(SemaTest, ActOnVarDeclWithInit) {
  QualType IntTy = Context.getIntType();
  auto *Init = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 42), IntTy);
  auto Result = S->ActOnVarDecl(SourceLocation(2), "y", IntTy, Init);
  EXPECT_TRUE(Result.isUsable());
  auto *VD = llvm::cast<VarDecl>(Result.get());
  EXPECT_EQ(VD->getInit(), Init);
}

TEST_F(SemaTest, ActOnVarDeclVoidTypeStillCreates) {
  // void is a builtin type, isCompleteType treats it as complete.
  // An invalid var-decl type would be an incomplete class type.
  QualType VoidTy = Context.getVoidType();
  auto Result = S->ActOnVarDecl(SourceLocation(1), "v", VoidTy, nullptr);
  // The result is usable because void passes isCompleteType.
  EXPECT_TRUE(Result.isUsable());
}

// --- FunctionDecl ---

TEST_F(SemaTest, ActOnFunctionDecl) {
  QualType IntTy = Context.getIntType();
  SmallVector<ParmVarDecl *, 4> Params;
  auto Result = S->ActOnFunctionDecl(SourceLocation(1), "foo",
                                      IntTy, Params, nullptr);
  EXPECT_TRUE(Result.isUsable());
  auto *FD = llvm::dyn_cast<FunctionDecl>(Result.get());
  ASSERT_NE(FD, nullptr);
  EXPECT_EQ(FD->getName(), "foo");
  EXPECT_EQ(FD->getBody(), nullptr);
}

TEST_F(SemaTest, ActOnFunctionDef) {
  QualType IntTy = Context.getIntType();
  SmallVector<ParmVarDecl *, 4> Params;
  auto *FD = Context.create<FunctionDecl>(SourceLocation(1), "bar",
                                           IntTy, Params);
  S->ActOnStartOfFunctionDef(FD);
  EXPECT_NE(S->getCurrentScope(), nullptr);
  S->ActOnFinishOfFunctionDef(FD);
}

// --- BinaryOperator ---

TEST_F(SemaTest, ActOnBinaryOperatorIntAdd) {
  QualType IntTy = Context.getIntType();
  auto *LHS = Context.create<IntegerLiteral>(SourceLocation(1),
                                              llvm::APInt(32, 1), IntTy);
  auto *RHS = Context.create<IntegerLiteral>(SourceLocation(2),
                                              llvm::APInt(32, 2), IntTy);
  auto Result = S->ActOnBinaryOperator(BinaryOpKind::Add, LHS, RHS,
                                        SourceLocation(3));
  EXPECT_TRUE(Result.isUsable());
  auto *BO = llvm::dyn_cast<BinaryOperator>(Result.get());
  ASSERT_NE(BO, nullptr);
  EXPECT_EQ(BO->getOpcode(), BinaryOpKind::Add);
  EXPECT_FALSE(BO->getType().isNull());
}

TEST_F(SemaTest, ActOnBinaryOperatorComparisonReturnsBool) {
  QualType IntTy = Context.getIntType();
  auto *LHS = Context.create<IntegerLiteral>(SourceLocation(1),
                                              llvm::APInt(32, 1), IntTy);
  auto *RHS = Context.create<IntegerLiteral>(SourceLocation(2),
                                              llvm::APInt(32, 2), IntTy);
  auto Result = S->ActOnBinaryOperator(BinaryOpKind::LT, LHS, RHS,
                                        SourceLocation(3));
  ASSERT_TRUE(Result.isUsable());
  auto *BO = llvm::cast<BinaryOperator>(Result.get());
  EXPECT_EQ(BO->getType(), Context.getBoolType());
}

// --- UnaryOperator ---

TEST_F(SemaTest, ActOnUnaryOperatorLNotReturnsBool) {
  QualType BoolTy = Context.getBoolType();
  auto *Operand = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                                  BoolTy);
  auto Result = S->ActOnUnaryOperator(UnaryOpKind::LNot, Operand,
                                       SourceLocation(2));
  ASSERT_TRUE(Result.isUsable());
  auto *UO = llvm::cast<UnaryOperator>(Result.get());
  EXPECT_EQ(UO->getOpcode(), UnaryOpKind::LNot);
  EXPECT_EQ(UO->getType(), Context.getBoolType());
}

// --- Statements ---

TEST_F(SemaTest, ActOnReturnStmt) {
  QualType IntTy = Context.getIntType();
  SmallVector<ParmVarDecl *, 4> Params;
  auto *FD = Context.create<FunctionDecl>(SourceLocation(1), "fn", IntTy,
                                           Params);
  S->ActOnStartOfFunctionDef(FD);

  auto *RetVal = Context.create<IntegerLiteral>(SourceLocation(2),
                                                 llvm::APInt(32, 0), IntTy);
  auto Result = S->ActOnReturnStmt(RetVal, SourceLocation(3));
  EXPECT_TRUE(Result.isUsable());

  S->ActOnFinishOfFunctionDef(FD);
}

TEST_F(SemaTest, ActOnCompoundStmt) {
  SmallVector<Stmt *, 4> Stmts;
  Stmts.push_back(Context.create<NullStmt>(SourceLocation(1)));
  auto Result = S->ActOnCompoundStmt(Stmts, SourceLocation(2),
                                      SourceLocation(3));
  EXPECT_TRUE(Result.isUsable());
  auto *CS = llvm::dyn_cast<CompoundStmt>(Result.get());
  ASSERT_NE(CS, nullptr);
  EXPECT_EQ(CS->getBody().size(), 1u);
}

// --- Type completeness ---

TEST_F(SemaTest, IsCompleteType) {
  EXPECT_TRUE(S->isCompleteType(Context.getIntType()));
  EXPECT_TRUE(S->isCompleteType(Context.getBoolType()));
  EXPECT_TRUE(S->isCompleteType(Context.getFloatType()));
  // void is a builtin type — isCompleteType returns true for all builtins.
  // Incomplete types would be forward-declared classes.
  EXPECT_TRUE(S->isCompleteType(Context.getVoidType()));
}

TEST_F(SemaTest, DiagnosticsHelper) {
  EXPECT_FALSE(S->hasErrorOccurred());
}

} // anonymous namespace

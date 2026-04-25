//===--- IREmitStmtTest.cpp - IREmitStmt Unit Tests -----------------------===//

#include <gtest/gtest.h>

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitStmt.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

class IREmitStmtTest : public ::testing::Test {
protected:
  ir::IRContext IRCtx;
  ir::IRTypeContext& TypeCtx;
  std::unique_ptr<ir::TargetLayout> Layout;
  DiagnosticsEngine Diags;
  std::unique_ptr<ir::IRModule> TheModule;

  IREmitStmtTest()
    : TypeCtx(IRCtx.getTypeContext()),
      Layout(ir::TargetLayout::Create("x86_64-unknown-linux-gnu")),
      Diags() {}

  /// Build a function with given body, convert, store the module, return the IRFunction*.
  ir::IRFunction* buildFunction(llvm::StringRef FnName, Stmt* Body) {
    SourceLocation Loc;
    auto* FD = new FunctionDecl(Loc, FnName, QualType(),
                                 llvm::ArrayRef<ParmVarDecl*>{}, Body);
    auto* TU = new TranslationUnitDecl(Loc);
    TU->addDecl(FD);

    ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
    auto Result = Converter.convert(TU);
    if (!Result.isUsable()) return nullptr;

    TheModule = Result.takeModule();
    if (!TheModule) return nullptr;
    return TheModule->getFunction(ir::StringRef(FnName.data(), FnName.size()));
  }
};

} // anonymous namespace

// === Test 1: If statement ===
TEST_F(IREmitStmtTest, EmitIfStmt) {
  SourceLocation Loc;
  llvm::APInt TrueVal(32, 1);
  auto* Cond = new IntegerLiteral(Loc, TrueVal, QualType());
  auto* Then = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* Else = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* IfS = new IfStmt(Loc, Cond, Then, Else);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{IfS});
  ir::IRFunction* Fn = buildFunction("test_if", FnBody);
  ASSERT_NE(Fn, nullptr);

  // At least 4 BBs: entry, if.then, if.else, if.end
  EXPECT_GE(Fn->getNumBasicBlocks(), 4u);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator(), nullptr) << "BB has no terminator";
  }
}

// === Test 2: For loop ===
TEST_F(IREmitStmtTest, EmitForStmt) {
  SourceLocation Loc;
  llvm::APInt ZeroVal(32, 0);
  auto* Cond = new IntegerLiteral(Loc, ZeroVal, QualType());
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* ForS = new ForStmt(Loc, nullptr, Cond, nullptr, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ForS});
  ir::IRFunction* Fn = buildFunction("test_for", FnBody);
  ASSERT_NE(Fn, nullptr);

  EXPECT_GE(Fn->getNumBasicBlocks(), 4u);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator(), nullptr) << "BB has no terminator";
  }
}

// === Test 3: Return statement ===
TEST_F(IREmitStmtTest, EmitReturnStmt) {
  SourceLocation Loc;
  llvm::APInt RetVal(32, 42);
  auto* RetExpr = new IntegerLiteral(Loc, RetVal, QualType());
  auto* RetS = new ReturnStmt(Loc, RetExpr);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{RetS});
  ir::IRFunction* Fn = buildFunction("test_ret", FnBody);
  ASSERT_NE(Fn, nullptr);

  ir::IRBasicBlock* EntryBB = Fn->getEntryBlock();
  ASSERT_NE(EntryBB, nullptr);
  ir::IRInstruction* Term = EntryBB->getTerminator();
  ASSERT_NE(Term, nullptr);
  EXPECT_EQ(Term->getOpcode(), ir::Opcode::Ret);
}

// === Test 4: While loop ===
TEST_F(IREmitStmtTest, EmitWhileStmt) {
  SourceLocation Loc;
  llvm::APInt FalseVal(32, 0);
  auto* Cond = new IntegerLiteral(Loc, FalseVal, QualType());
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* WhileS = new WhileStmt(Loc, Cond, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{WhileS});
  ir::IRFunction* Fn = buildFunction("test_while", FnBody);
  ASSERT_NE(Fn, nullptr);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator(), nullptr) << "BB has no terminator";
  }
}

// === Test 5: Break in for loop ===
TEST_F(IREmitStmtTest, EmitBreakInForLoop) {
  SourceLocation Loc;
  auto* BreakS = new BreakStmt(Loc);
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{BreakS});
  auto* ForS = new ForStmt(Loc, nullptr, nullptr, nullptr, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ForS});
  ir::IRFunction* Fn = buildFunction("test_break", FnBody);
  ASSERT_NE(Fn, nullptr);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator(), nullptr) << "BB has no terminator";
  }
}

// === Test 6: Continue in for loop ===
TEST_F(IREmitStmtTest, EmitContinueInForLoop) {
  SourceLocation Loc;
  auto* ContinueS = new ContinueStmt(Loc);
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ContinueS});
  auto* ForS = new ForStmt(Loc, nullptr, nullptr, nullptr, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ForS});
  ir::IRFunction* Fn = buildFunction("test_continue", FnBody);
  ASSERT_NE(Fn, nullptr);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator(), nullptr) << "BB has no terminator";
  }
}

// === Test 7: NullStmt ===
TEST_F(IREmitStmtTest, EmitNullStmt) {
  SourceLocation Loc;
  auto* NullS = new NullStmt(Loc);
  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{NullS});
  ir::IRFunction* Fn = buildFunction("test_null", FnBody);
  ASSERT_NE(Fn, nullptr);
  EXPECT_EQ(Fn->getNumBasicBlocks(), 1u);
}

// === Test 8: DeclStmt with initializer ===
TEST_F(IREmitStmtTest, EmitDeclStmtWithInit) {
  SourceLocation Loc;
  llvm::APInt Val(32, 42);
  auto* Init = new IntegerLiteral(Loc, Val, QualType());
  auto* VD = new VarDecl(Loc, "x", QualType(), Init);
  auto* DeclS = new DeclStmt(Loc, llvm::ArrayRef<Decl*>{VD});

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{DeclS});
  ir::IRFunction* Fn = buildFunction("test_decl", FnBody);
  ASSERT_NE(Fn, nullptr);

  ir::IRBasicBlock* EntryBB = Fn->getEntryBlock();
  EXPECT_GE(EntryBB->size(), 3u); // alloca + store + ret
}

// === Test 9: Nested if ===
TEST_F(IREmitStmtTest, EmitNestedIf) {
  SourceLocation Loc;
  llvm::APInt One(32, 1);
  auto* Cond1 = new IntegerLiteral(Loc, One, QualType());
  auto* Cond2 = new IntegerLiteral(Loc, One, QualType());
  auto* InnerThen = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* InnerIf = new IfStmt(Loc, Cond2, InnerThen, nullptr);
  auto* OuterThen = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{InnerIf});
  auto* OuterIf = new IfStmt(Loc, Cond1, OuterThen, nullptr);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{OuterIf});
  ir::IRFunction* Fn = buildFunction("test_nested_if", FnBody);
  ASSERT_NE(Fn, nullptr);

  EXPECT_GE(Fn->getNumBasicBlocks(), 5u);

  // Debug: list all BBs and their terminators
  for (auto& BB : Fn->getBasicBlocks()) {
    auto* Term = BB->getTerminator();
    if (!Term) {
      ADD_FAILURE() << "BB '" << BB->getName().str() << "' has no terminator";
    }
  }
}

// === Test 10: Do-while ===
TEST_F(IREmitStmtTest, EmitDoWhileStmt) {
  SourceLocation Loc;
  llvm::APInt ZeroVal(32, 0);
  auto* Cond = new IntegerLiteral(Loc, ZeroVal, QualType());
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* DoS = new DoStmt(Loc, Body, Cond);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{DoS});
  ir::IRFunction* Fn = buildFunction("test_dowhile", FnBody);
  ASSERT_NE(Fn, nullptr);

  EXPECT_GE(Fn->getNumBasicBlocks(), 3u);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator(), nullptr) << "BB has no terminator";
  }
}

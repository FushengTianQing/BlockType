//===--- CodeGenStmtTest.cpp - Control Flow CodeGen Tests ----*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"

using namespace blocktype;

namespace {

class CodeGenStmtTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  SourceManager SM;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenStmtTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, SM, "test", "x86_64-apple-darwin");
  }
};

TEST_F(CodeGenStmtTest, CompoundStmt) {
  auto *Ret = Ctx.create<ReturnStmt>(SourceLocation(1), nullptr);
  llvm::SmallVector<Stmt *, 1> Stmts = {Ret};
  auto *CS = Ctx.create<CompoundStmt>(SourceLocation(2), Stmts);

  CodeGenFunction CGF(*CGM);
  auto *Fn = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(LLVMCtx), false),
      llvm::Function::ExternalLinkage, "test", CGM->getModule());
  auto *BB = llvm::BasicBlock::Create(LLVMCtx, "entry", Fn);
  CGF.getBuilder().SetInsertPoint(BB);
  CGF.setCurrentFunction(Fn);

  CGF.EmitStmt(CS);
}

TEST_F(CodeGenStmtTest, IfStmt) {
  auto *Cond = Ctx.create<CXXBoolLiteral>(SourceLocation(1), true);
  auto *Then = Ctx.create<ReturnStmt>(SourceLocation(2), nullptr);
  auto *IS = Ctx.create<IfStmt>(SourceLocation(3), Cond, Then, nullptr);

  CodeGenFunction CGF(*CGM);
  auto *Fn = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(LLVMCtx), false),
      llvm::Function::ExternalLinkage, "test", CGM->getModule());
  auto *BB = llvm::BasicBlock::Create(LLVMCtx, "entry", Fn);
  CGF.getBuilder().SetInsertPoint(BB);
  CGF.setCurrentFunction(Fn);

  CGF.EmitStmt(IS);
}

TEST_F(CodeGenStmtTest, ReturnStmtWithFunctionBody) {
  auto *RetVal = Ctx.create<IntegerLiteral>(SourceLocation(1), llvm::APInt(32, 42));
  auto *RetStmt = Ctx.create<ReturnStmt>(SourceLocation(2), RetVal);
  llvm::SmallVector<Stmt *, 1> Stmts = {RetStmt};
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(3), Stmts);

  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "test",
      QualType(), llvm::SmallVector<ParmVarDecl *, 0>(), Body);

  auto *Fn = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(LLVMCtx), false),
      llvm::Function::ExternalLinkage, "test", CGM->getModule());

  CodeGenFunction CGF(*CGM);
  CGF.EmitFunctionBody(FD, Fn);

  EXPECT_FALSE(Fn->empty());
}

} // anonymous namespace

//===--- CodeGenExprTest.cpp - Expression CodeGen Tests ------*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"

using namespace blocktype;

namespace {

class CodeGenExprTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  SourceManager SM;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenExprTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, SM, "test", "x86_64-apple-darwin");
  }
};

TEST_F(CodeGenExprTest, IntegerLiteralExpr) {
  auto *Lit = Ctx.create<IntegerLiteral>(SourceLocation(1), llvm::APInt(32, 10));
  CodeGenFunction CGF(*CGM);
  auto *Fn = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(LLVMCtx), false),
      llvm::Function::ExternalLinkage, "test", CGM->getModule());
  auto *BB = llvm::BasicBlock::Create(LLVMCtx, "entry", Fn);
  CGF.getBuilder().SetInsertPoint(BB);
  CGF.setCurrentFunction(Fn);

  llvm::Value *V = CGF.EmitExpr(Lit);
  ASSERT_NE(V, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::ConstantInt>(V));
}

TEST_F(CodeGenExprTest, FloatingLiteralExpr) {
  auto *Lit = Ctx.create<FloatingLiteral>(SourceLocation(1), llvm::APFloat(2.5));
  CodeGenFunction CGF(*CGM);
  auto *Fn = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getDoubleTy(LLVMCtx), false),
      llvm::Function::ExternalLinkage, "test", CGM->getModule());
  auto *BB = llvm::BasicBlock::Create(LLVMCtx, "entry", Fn);
  CGF.getBuilder().SetInsertPoint(BB);
  CGF.setCurrentFunction(Fn);

  llvm::Value *V = CGF.EmitExpr(Lit);
  ASSERT_NE(V, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::ConstantFP>(V));
}

} // anonymous namespace

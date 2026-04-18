//===--- CodeGenFunctionTest.cpp - Function CodeGen Tests ----*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

using namespace blocktype;

namespace {

class CodeGenFunctionTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenFunctionTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, "test", "x86_64-apple-darwin");
  }
};

TEST_F(CodeGenFunctionTest, VoidFunction) {
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(1), llvm::SmallVector<Stmt*, 0>());
  // 创建 void() 函数类型
  auto *FnTy = Ctx.getFunctionType(Ctx.getVoidType().getTypePtr(), {});
  QualType FT(FnTy, Qualifier::None);
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "void_func",
      FT, llvm::SmallVector<ParmVarDecl *, 0>(), Body);

  llvm::Function *Fn = CGM->EmitFunction(FD);
  ASSERT_NE(Fn, nullptr);
  EXPECT_FALSE(Fn->empty());
  EXPECT_EQ(Fn->getName(), "void_func");
}

TEST_F(CodeGenFunctionTest, FunctionWithReturn) {
  auto *RetVal = Ctx.create<IntegerLiteral>(SourceLocation(2), llvm::APInt(32, 0));
  auto *RetStmt = Ctx.create<ReturnStmt>(SourceLocation(3), RetVal);
  llvm::SmallVector<Stmt *, 1> Stmts = {RetStmt};
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(4), Stmts);

  // 创建 int() 函数类型
  auto *FnTy = Ctx.getFunctionType(Ctx.getIntType().getTypePtr(), {});
  QualType FT(FnTy, Qualifier::None);
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "ret_func",
      FT, llvm::SmallVector<ParmVarDecl *, 0>(), Body);

  llvm::Function *Fn = CGM->EmitFunction(FD);
  ASSERT_NE(Fn, nullptr);
  EXPECT_FALSE(Fn->empty());
}

TEST_F(CodeGenFunctionTest, FunctionWithParams) {
  auto *P1 = Ctx.create<ParmVarDecl>(SourceLocation(1), "a", Ctx.getIntType(), 0);
  auto *P2 = Ctx.create<ParmVarDecl>(SourceLocation(2), "b", Ctx.getIntType(), 1);
  llvm::SmallVector<ParmVarDecl *, 2> Params = {P1, P2};

  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(3), llvm::SmallVector<Stmt*, 0>());
  // 创建 int(int, int) 函数类型
  auto *FnTy = Ctx.getFunctionType(Ctx.getIntType().getTypePtr(),
      {Ctx.getIntType().getTypePtr(), Ctx.getIntType().getTypePtr()});
  QualType FT(FnTy, Qualifier::None);
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "add",
      FT, Params, Body);

  llvm::Function *Fn = CGM->EmitFunction(FD);
  ASSERT_NE(Fn, nullptr);
  EXPECT_EQ(Fn->arg_size(), 2u);
}

} // anonymous namespace

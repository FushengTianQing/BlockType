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
#include "llvm/IR/GlobalVariable.h"

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
  EXPECT_EQ(Fn->getName(), "_Z9void_funcv");
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

//===----------------------------------------------------------------------===//
// Linkage tests
//===----------------------------------------------------------------------===//

TEST_F(CodeGenFunctionTest, InlineFunctionLinkOnceODR) {
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(1), llvm::SmallVector<Stmt*, 0>());
  auto *FnTy = Ctx.getFunctionType(Ctx.getVoidType().getTypePtr(), {});
  QualType FT(FnTy, Qualifier::None);

  llvm::SmallVector<ParmVarDecl *, 0> NoParams;
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "inline_func",
      FT, NoParams, Body, true /*inline*/);

  llvm::Function *Fn = CGM->GetOrCreateFunctionDecl(FD);
  ASSERT_NE(Fn, nullptr);
  EXPECT_EQ(Fn->getLinkage(), llvm::Function::LinkOnceODRLinkage);
  EXPECT_TRUE(Fn->hasFnAttribute(llvm::Attribute::AlwaysInline));
}

TEST_F(CodeGenFunctionTest, RegularFunctionExternalLinkage) {
  auto *FnTy = Ctx.getFunctionType(Ctx.getVoidType().getTypePtr(), {});
  QualType FT(FnTy, Qualifier::None);

  llvm::SmallVector<ParmVarDecl *, 0> NoParams;
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "regular_func",
      FT, NoParams);

  llvm::Function *Fn = CGM->GetOrCreateFunctionDecl(FD);
  ASSERT_NE(Fn, nullptr);
  EXPECT_EQ(Fn->getLinkage(), llvm::Function::ExternalLinkage);
}

TEST_F(CodeGenFunctionTest, NoexceptDoesNotThrow) {
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(1), llvm::SmallVector<Stmt*, 0>());
  auto *FnTy = Ctx.getFunctionType(Ctx.getVoidType().getTypePtr(), {});
  QualType FT(FnTy, Qualifier::None);

  llvm::SmallVector<ParmVarDecl *, 0> NoParams;
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(0), "safe_func",
      FT, NoParams, Body, false, false,
      true /*hasNoexceptSpec*/, true /*noexceptValue*/);

  llvm::Function *Fn = CGM->GetOrCreateFunctionDecl(FD);
  ASSERT_NE(Fn, nullptr);
  EXPECT_TRUE(Fn->doesNotThrow());
}

//===----------------------------------------------------------------------===//
// Global variable linkage & init classification
//===----------------------------------------------------------------------===//

TEST_F(CodeGenFunctionTest, StaticGlobalVarInternalLinkage) {
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "s_var",
      Ctx.getIntType(), nullptr, true /*static*/);

  llvm::GlobalVariable *GV = CGM->EmitGlobalVar(VD);
  ASSERT_NE(GV, nullptr);
  EXPECT_EQ(GV->getLinkage(), llvm::GlobalValue::InternalLinkage);
}

TEST_F(CodeGenFunctionTest, ConstexprVarLinkOnceODR) {
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "constexpr_var",
      Ctx.getIntType(), nullptr, false, true /*constexpr*/);

  llvm::GlobalVariable *GV = CGM->EmitGlobalVar(VD);
  ASSERT_NE(GV, nullptr);
  EXPECT_EQ(GV->getLinkage(), llvm::GlobalValue::LinkOnceODRLinkage);
}

TEST_F(CodeGenFunctionTest, RegularGlobalVarExternalLinkage) {
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "g_var",
      Ctx.getIntType(), nullptr);

  llvm::GlobalVariable *GV = CGM->EmitGlobalVar(VD);
  ASSERT_NE(GV, nullptr);
  EXPECT_EQ(GV->getLinkage(), llvm::GlobalValue::ExternalLinkage);
}

TEST_F(CodeGenFunctionTest, ConstGlobalVarIsConstant) {
  QualType ConstIntTy = Ctx.getIntType().withConst();
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "const_var",
      ConstIntTy, nullptr);

  llvm::GlobalVariable *GV = CGM->EmitGlobalVar(VD);
  ASSERT_NE(GV, nullptr);
  EXPECT_TRUE(GV->isConstant());
}

TEST_F(CodeGenFunctionTest, GlobalVarWithConstantInit) {
  auto *Init = Ctx.create<IntegerLiteral>(SourceLocation(1), llvm::APInt(32, 42));
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "init_var",
      Ctx.getIntType(), Init);

  llvm::GlobalVariable *GV = CGM->EmitGlobalVar(VD);
  ASSERT_NE(GV, nullptr);
  // 常量初始化应该直接设置初始值
  ASSERT_NE(GV->getInitializer(), nullptr);
}

TEST_F(CodeGenFunctionTest, InitClassificationZero) {
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "zero_var",
      Ctx.getIntType(), nullptr);
  EXPECT_EQ(CGM->ClassifyGlobalInit(VD), InitKind::ZeroInitialization);
}

TEST_F(CodeGenFunctionTest, InitClassificationConstexpr) {
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "ce_var",
      Ctx.getIntType(), nullptr, false, true /*constexpr*/);
  EXPECT_EQ(CGM->ClassifyGlobalInit(VD), InitKind::ConstantInitialization);
}

TEST_F(CodeGenFunctionTest, InitClassificationConstant) {
  auto *Init = Ctx.create<IntegerLiteral>(SourceLocation(1), llvm::APInt(32, 10));
  auto *VD = Ctx.create<VarDecl>(SourceLocation(0), "const_init_var",
      Ctx.getIntType(), Init);
  EXPECT_EQ(CGM->ClassifyGlobalInit(VD), InitKind::ConstantInitialization);
}

TEST_F(CodeGenFunctionTest, AttributeFramework) {
  GlobalDeclAttributes Attrs;
  EXPECT_FALSE(Attrs.IsWeak);
  EXPECT_FALSE(Attrs.IsDLLImport);
  EXPECT_FALSE(Attrs.IsDLLExport);
  EXPECT_FALSE(Attrs.IsUsed);
  EXPECT_FALSE(Attrs.IsDeprecated);
  EXPECT_TRUE(Attrs.IsDefaultVisibility);
  EXPECT_FALSE(Attrs.IsHiddenVisibility);
}

} // anonymous namespace

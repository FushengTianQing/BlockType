//===--- CodeGenConstantTest.cpp - Constant Gen Tests --------*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenConstant.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Constants.h"

using namespace blocktype;

namespace {

class CodeGenConstantTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  SourceManager SM;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenConstantTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, SM, "test", "x86_64-apple-darwin");
  }
};

TEST_F(CodeGenConstantTest, IntegerLiteral) {
  auto *Lit = Ctx.create<IntegerLiteral>(SourceLocation(1), llvm::APInt(32, 42));
  llvm::Constant *C = CGM->getConstants().EmitConstantForType(Lit, Ctx.getIntType());
  ASSERT_NE(C, nullptr);
  auto *CI = llvm::dyn_cast<llvm::ConstantInt>(C);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 42u);
}

TEST_F(CodeGenConstantTest, FloatingLiteral) {
  auto *Lit = Ctx.create<FloatingLiteral>(SourceLocation(1), llvm::APFloat(3.14));
  llvm::Constant *C = CGM->getConstants().EmitConstantForType(Lit, Ctx.getDoubleType());
  ASSERT_NE(C, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::ConstantFP>(C));
}

TEST_F(CodeGenConstantTest, BooleanLiteral) {
  auto *Lit = Ctx.create<CXXBoolLiteral>(SourceLocation(1), true);
  llvm::Constant *C = CGM->getConstants().EmitConstantForType(Lit, Ctx.getBoolType());
  ASSERT_NE(C, nullptr);
  auto *CI = llvm::dyn_cast<llvm::ConstantInt>(C);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 1u);
}

TEST_F(CodeGenConstantTest, ZeroValue) {
  llvm::Constant *C = CGM->getConstants().EmitZeroValue(Ctx.getIntType());
  ASSERT_NE(C, nullptr);
  auto *CI = llvm::dyn_cast<llvm::ConstantInt>(C);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 0u);
}

TEST_F(CodeGenConstantTest, NullPointer) {
  QualType IntPtrTy(Ctx.getPointerType(Ctx.getIntType().getTypePtr()), Qualifier::None);
  llvm::Constant *C = CGM->getConstants().EmitZeroValue(IntPtrTy);
  ASSERT_NE(C, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::ConstantPointerNull>(C));
}

} // anonymous namespace

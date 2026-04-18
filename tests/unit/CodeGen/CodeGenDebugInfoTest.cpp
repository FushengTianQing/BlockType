//===--- CodeGenDebugInfoTest.cpp - Debug Info Tests ---------*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CGDebugInfo.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace blocktype;

namespace {

class CodeGenDebugInfoTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenDebugInfoTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, "test", "x86_64-apple-darwin");
  }
};

TEST_F(CodeGenDebugInfoTest, InitializeAndFinalize) {
  auto &DI = CGM->getDebugInfo();
  EXPECT_FALSE(DI.isInitialized());

  DI.Initialize("test.cpp", "/tmp");
  EXPECT_TRUE(DI.isInitialized());
  EXPECT_NE(DI.getCompileUnit(), nullptr);
  EXPECT_NE(DI.getFile(), nullptr);

  DI.Finalize();
}

TEST_F(CodeGenDebugInfoTest, BuiltinDIType) {
  auto &DI = CGM->getDebugInfo();
  DI.Initialize("test.cpp", "/tmp");

  llvm::DIType *IntDI = DI.GetDIType(Ctx.getIntType());
  ASSERT_NE(IntDI, nullptr);
  EXPECT_EQ(IntDI->getSizeInBits(), 32u);

  llvm::DIType *DblDI = DI.GetDIType(Ctx.getDoubleType());
  ASSERT_NE(DblDI, nullptr);
  EXPECT_EQ(DblDI->getSizeInBits(), 64u);

  DI.Finalize();
}

TEST_F(CodeGenDebugInfoTest, PointerDIType) {
  auto &DI = CGM->getDebugInfo();
  DI.Initialize("test.cpp", "/tmp");

  QualType IntPtrTy(Ctx.getPointerType(Ctx.getIntType().getTypePtr()), Qualifier::None);
  llvm::DIType *DITy = DI.GetDIType(IntPtrTy);
  ASSERT_NE(DITy, nullptr);
  EXPECT_EQ(DITy->getSizeInBits(), 64u);

  DI.Finalize();
}

TEST_F(CodeGenDebugInfoTest, RecordDIType) {
  auto &DI = CGM->getDebugInfo();
  DI.Initialize("test.cpp", "/tmp");

  auto *RD = Ctx.create<RecordDecl>(SourceLocation(1), "Point", RecordDecl::TK_struct);
  auto *F1 = Ctx.create<FieldDecl>(SourceLocation(2), "x", Ctx.getIntType());
  auto *F2 = Ctx.create<FieldDecl>(SourceLocation(3), "y", Ctx.getIntType());
  RD->addField(F1);
  RD->addField(F2);

  QualType RecordTy = Ctx.getRecordType(RD);
  llvm::DIType *DITy = DI.GetDIType(RecordTy);
  ASSERT_NE(DITy, nullptr);

  DI.Finalize();
}

TEST_F(CodeGenDebugInfoTest, FunctionDI) {
  auto &DI = CGM->getDebugInfo();
  DI.Initialize("test.cpp", "/tmp");

  // 创建 int() 函数类型
  auto *FnTy = Ctx.getFunctionType(Ctx.getIntType().getTypePtr(), {});
  QualType FT(FnTy, Qualifier::None);
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(10), "test_func",
      FT, llvm::SmallVector<ParmVarDecl *, 0>());

  auto *SP = DI.GetFunctionDI(FD);
  ASSERT_NE(SP, nullptr);
  EXPECT_EQ(SP->getName(), "test_func");

  DI.Finalize();
}

TEST_F(CodeGenDebugInfoTest, TypeCacheWorks) {
  auto &DI = CGM->getDebugInfo();
  DI.Initialize("test.cpp", "/tmp");

  llvm::DIType *First = DI.GetDIType(Ctx.getIntType());
  llvm::DIType *Second = DI.GetDIType(Ctx.getIntType());
  EXPECT_EQ(First, Second);

  DI.Finalize();
}

} // anonymous namespace

//===--- CodeGenTypesTest.cpp - Type Mapping Tests -----------*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

using namespace blocktype;

namespace {

class CodeGenTypesTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  SourceManager SM;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenTypesTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, SM, "test", "x86_64-apple-darwin");
  }
};

// --- Builtin Types ---

TEST_F(CodeGenTypesTest, VoidType) {
  llvm::Type *T = CGM->getTypes().ConvertType(Ctx.getVoidType());
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isVoidTy());
}

TEST_F(CodeGenTypesTest, BoolType) {
  llvm::Type *T = CGM->getTypes().ConvertType(Ctx.getBoolType());
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isIntegerTy(1));
}

TEST_F(CodeGenTypesTest, IntType) {
  llvm::Type *T = CGM->getTypes().ConvertType(Ctx.getIntType());
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isIntegerTy(32));
}

TEST_F(CodeGenTypesTest, LongType) {
  llvm::Type *T = CGM->getTypes().ConvertType(Ctx.getLongType());
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isIntegerTy(64));
}

TEST_F(CodeGenTypesTest, FloatType) {
  llvm::Type *T = CGM->getTypes().ConvertType(Ctx.getFloatType());
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isFloatTy());
}

TEST_F(CodeGenTypesTest, DoubleType) {
  llvm::Type *T = CGM->getTypes().ConvertType(Ctx.getDoubleType());
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isDoubleTy());
}

// --- Pointer Type ---

TEST_F(CodeGenTypesTest, PointerType) {
  QualType IntPtrTy(Ctx.getPointerType(Ctx.getIntType().getTypePtr()), Qualifier::None);
  llvm::Type *T = CGM->getTypes().ConvertType(IntPtrTy);
  ASSERT_NE(T, nullptr);
  EXPECT_TRUE(T->isPointerTy());
}

// --- Record Type ---

TEST_F(CodeGenTypesTest, RecordType) {
  auto *RD = Ctx.create<RecordDecl>(SourceLocation(1), "Point", RecordDecl::TK_struct);
  auto *F1 = Ctx.create<FieldDecl>(SourceLocation(2), "x", Ctx.getIntType());
  auto *F2 = Ctx.create<FieldDecl>(SourceLocation(3), "y", Ctx.getIntType());
  RD->addField(F1);
  RD->addField(F2);

  llvm::StructType *ST = CGM->getTypes().GetRecordType(RD);
  ASSERT_NE(ST, nullptr);
  EXPECT_EQ(ST->getNumElements(), 2u);
}

// --- CXX Record Type with VPtr ---

TEST_F(CodeGenTypesTest, CXXRecordWithVirtual) {
  auto *RD = Ctx.create<CXXRecordDecl>(SourceLocation(1), "MyClass");
  auto *FD = Ctx.create<FieldDecl>(SourceLocation(2), "value", Ctx.getIntType());
  RD->addField(FD);

  auto *MD = Ctx.create<CXXMethodDecl>(SourceLocation(3), "foo",
      QualType(), llvm::SmallVector<ParmVarDecl *, 0>(), RD,
      nullptr, false, false, false, true);
  RD->addMethod(MD);

  llvm::StructType *ST = CGM->getTypes().GetRecordType(RD);
  ASSERT_NE(ST, nullptr);
  EXPECT_GE(ST->getNumElements(), 2u); // vptr + at least 1 field
}

// --- Function Type ---

TEST_F(CodeGenTypesTest, FunctionTypeForDecl) {
  // 创建一个带函数类型的 FunctionDecl
  auto *FD = Ctx.create<FunctionDecl>(SourceLocation(1), "add",
      QualType(), llvm::SmallVector<ParmVarDecl *, 0>());

  // GetFunctionTypeForDecl 需要 FunctionDecl 有合法的类型
  // 简单测试：确保不会崩溃（返回 fallback 类型）
  llvm::FunctionType *FTy = CGM->getTypes().GetFunctionTypeForDecl(FD);
  ASSERT_NE(FTy, nullptr);
}

TEST_F(CodeGenTypesTest, TypeCaching) {
  QualType IntTy = Ctx.getIntType();
  llvm::Type *T1 = CGM->getTypes().ConvertType(IntTy);
  llvm::Type *T2 = CGM->getTypes().ConvertType(IntTy);
  EXPECT_EQ(T1, T2);
}

} // anonymous namespace

//===--- CodeGenClassTest.cpp - Class CodeGen Tests ----------*- C++ -*-===//

#include "gtest/gtest.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CGCXX.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

using namespace blocktype;

namespace {

class CodeGenClassTest : public ::testing::Test {
protected:
  llvm::LLVMContext LLVMCtx;
  ASTContext Ctx;
  SourceManager SM;
  std::unique_ptr<CodeGenModule> CGM;

  CodeGenClassTest() {
    CGM = std::make_unique<CodeGenModule>(Ctx, LLVMCtx, SM, "test", "x86_64-apple-darwin");
  }
};

TEST_F(CodeGenClassTest, SimpleClassLayout) {
  auto *RD = Ctx.create<CXXRecordDecl>(SourceLocation(1), "Simple");
  auto *F1 = Ctx.create<FieldDecl>(SourceLocation(2), "x", Ctx.getIntType());
  auto *F2 = Ctx.create<FieldDecl>(SourceLocation(3), "y", Ctx.getIntType());
  RD->addField(F1);
  RD->addField(F2);

  auto Offsets = CGM->getCXX().ComputeClassLayout(RD);
  EXPECT_EQ(Offsets.size(), 2u);
  EXPECT_EQ(Offsets[0], 0u);
  EXPECT_EQ(Offsets[1], 4u);

  EXPECT_GE(CGM->getCXX().GetClassSize(RD), 8u);
}

TEST_F(CodeGenClassTest, ClassWithVirtual) {
  auto *RD = Ctx.create<CXXRecordDecl>(SourceLocation(1), "Poly");
  auto *FD = Ctx.create<FieldDecl>(SourceLocation(2), "val", Ctx.getIntType());
  RD->addField(FD);

  auto *MD = Ctx.create<CXXMethodDecl>(SourceLocation(3), "virt",
      QualType(), llvm::SmallVector<ParmVarDecl *, 0>(), RD,
      nullptr, false, false, false, true);
  RD->addMethod(MD);

  auto Offsets = CGM->getCXX().ComputeClassLayout(RD);
  EXPECT_EQ(Offsets.size(), 1u);
  EXPECT_EQ(Offsets[0], 8u); // After vptr

  EXPECT_GE(CGM->getCXX().GetClassSize(RD), 12u);
}

TEST_F(CodeGenClassTest, InheritanceLayout) {
  auto *Base = Ctx.create<CXXRecordDecl>(SourceLocation(1), "Base");
  auto *BF = Ctx.create<FieldDecl>(SourceLocation(2), "base_val", Ctx.getIntType());
  Base->addField(BF);
  CGM->getCXX().ComputeClassLayout(Base);

  auto *Derived = Ctx.create<CXXRecordDecl>(SourceLocation(3), "Derived");
  auto *BaseSpec = new CXXRecordDecl::BaseSpecifier(
      Ctx.getRecordType(Base), SourceLocation(4), false, false, 2);
  Derived->addBase(*BaseSpec);

  auto *DF = Ctx.create<FieldDecl>(SourceLocation(5), "derived_val", Ctx.getDoubleType());
  Derived->addField(DF);

  auto Offsets = CGM->getCXX().ComputeClassLayout(Derived);
  EXPECT_EQ(Offsets.size(), 1u);
  EXPECT_GE(Offsets[0], 4u);

  EXPECT_EQ(CGM->getCXX().GetBaseOffset(Derived, Base), 0u);
}

TEST_F(CodeGenClassTest, VTableGeneration) {
  auto *RD = Ctx.create<CXXRecordDecl>(SourceLocation(1), "VClass");
  auto *MD = Ctx.create<CXXMethodDecl>(SourceLocation(2), "foo",
      QualType(), llvm::SmallVector<ParmVarDecl *, 0>(), RD,
      nullptr, false, false, false, true);
  RD->addMethod(MD);

  CGM->getCXX().ComputeClassLayout(RD);
  auto *VT = CGM->getCXX().EmitVTable(RD);
  ASSERT_NE(VT, nullptr);
  EXPECT_TRUE(VT->getName().starts_with("_ZTV"));
}

TEST_F(CodeGenClassTest, ConstructorGeneration) {
  auto *RD = Ctx.create<CXXRecordDecl>(SourceLocation(1), "MyClass");
  auto *F1 = Ctx.create<FieldDecl>(SourceLocation(2), "x", Ctx.getIntType());
  RD->addField(F1);
  CGM->getCXX().ComputeClassLayout(RD);

  // 提供空的函数体
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(4), llvm::SmallVector<Stmt*, 0>());

  // 创建 void(MyClass*) 函数类型 (this 指针作为第一个参数)
  auto *ThisPtrTy = Ctx.getPointerType(Ctx.getRecordType(RD).getTypePtr());
  auto *FnTy = Ctx.getFunctionType(Ctx.getVoidType().getTypePtr(), {ThisPtrTy});
  QualType FT(FnTy, Qualifier::None);

  auto *Ctor = Ctx.create<CXXConstructorDecl>(SourceLocation(3), RD,
      llvm::SmallVector<ParmVarDecl *, 0>(), Body);
  // 手动设置函数类型（CXXConstructorDecl 构造函数不自动设置）
  Ctor->setType(FT);
  RD->addMethod(Ctor);

  llvm::Function *Fn = CGM->EmitFunction(Ctor);
  ASSERT_NE(Fn, nullptr);
  EXPECT_FALSE(Fn->empty());
}

TEST_F(CodeGenClassTest, DestructorGeneration) {
  auto *RD = Ctx.create<CXXRecordDecl>(SourceLocation(1), "MyClass");
  CGM->getCXX().ComputeClassLayout(RD);

  // 提供空的函数体
  auto *Body = Ctx.create<CompoundStmt>(SourceLocation(3), llvm::SmallVector<Stmt*, 0>());
  auto *Dtor = Ctx.create<CXXDestructorDecl>(SourceLocation(2), RD, Body);

  // 创建 void(MyClass*) 函数类型 (this 指针作为第一个参数)
  auto *ThisPtrTy = Ctx.getPointerType(Ctx.getRecordType(RD).getTypePtr());
  auto *FnTy = Ctx.getFunctionType(Ctx.getVoidType().getTypePtr(), {ThisPtrTy});
  QualType FT(FnTy, Qualifier::None);
  Dtor->setType(FT);

  RD->addMethod(Dtor);

  llvm::Function *Fn = CGM->EmitFunction(Dtor);
  ASSERT_NE(Fn, nullptr);
  EXPECT_FALSE(Fn->empty());
}

} // anonymous namespace

//===--- IRTypeMapperTest.cpp - IRTypeMapper unit tests ------*- C++ -*-===//

#include <memory>

#include <gtest/gtest.h>

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TypeCasting.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/IRTypeMapper.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;
using namespace blocktype::ir;

// ============================================================
// Test cases (each self-contained for gtest_discover_tests)
// ============================================================

/// Map void type.
TEST(IRTypeMapperTest, BuiltinVoid) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType VoidTy = ASTCtx.getVoidType();
  auto* Result = Mapper.mapType(VoidTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isVoid());
}

/// Map bool type → IRIntegerType(1).
TEST(IRTypeMapperTest, BuiltinBool) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType BoolTy = ASTCtx.getBoolType();
  auto* Result = Mapper.mapType(BoolTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isInteger());
  auto* BoolIR = static_cast<IRIntegerType*>(Result);
  EXPECT_EQ(BoolIR->getBitWidth(), 1u);
}

/// Map int type → IRIntegerType(32).
TEST(IRTypeMapperTest, BuiltinInt) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  auto* Result = Mapper.mapType(IntTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isInteger());
  auto* IntIR = static_cast<IRIntegerType*>(Result);
  EXPECT_EQ(IntIR->getBitWidth(), 32u);
}

/// Map long type → platform-dependent (x86_64 Linux = 64-bit).
TEST(IRTypeMapperTest, BuiltinLong) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType LongTy = ASTCtx.getLongType();
  auto* Result = Mapper.mapType(LongTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isInteger());
  auto* LongIR = static_cast<IRIntegerType*>(Result);
  EXPECT_EQ(LongIR->getBitWidth(), 64u);
}

/// Map float type → IRFloatType(32).
TEST(IRTypeMapperTest, BuiltinFloat) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType FloatTy = ASTCtx.getFloatType();
  auto* Result = Mapper.mapType(FloatTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isFloat());
  auto* FloatIR = static_cast<IRFloatType*>(Result);
  EXPECT_EQ(FloatIR->getBitWidth(), 32u);
}

/// Map double type → IRFloatType(64).
TEST(IRTypeMapperTest, BuiltinDouble) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType DoubleTy = ASTCtx.getDoubleType();
  auto* Result = Mapper.mapType(DoubleTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isFloat());
  auto* DoubleIR = static_cast<IRFloatType*>(Result);
  EXPECT_EQ(DoubleIR->getBitWidth(), 64u);
}

/// Map pointer type → IRPointerType.
TEST(IRTypeMapperTest, PointerType) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  QualType PtrIntTy(ASTCtx.getPointerType(IntTy.getTypePtr()), Qualifier::None);
  auto* Result = Mapper.mapType(PtrIntTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isPointer());
  auto* PtrIR = static_cast<IRPointerType*>(Result);
  EXPECT_TRUE(PtrIR->getPointeeType()->isInteger());
}

/// Map reference type → IRPointerType (references are pointers in IR).
TEST(IRTypeMapperTest, ReferenceType) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  QualType RefIntTy(
    ASTCtx.getLValueReferenceType(IntTy.getTypePtr()), Qualifier::None);
  auto* Result = Mapper.mapType(RefIntTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isPointer());
}

/// Cache consistency: same type returns same pointer.
TEST(IRTypeMapperTest, CacheConsistency) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();

  auto* First = Mapper.mapType(IntTy);
  auto* Second = Mapper.mapType(IntTy);
  EXPECT_EQ(First, Second);
}

/// Null QualType → error opaque type.
TEST(IRTypeMapperTest, NullQualType) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  QualType NullTy;
  auto* Result = Mapper.mapType(NullTy);
  ASSERT_NE(Result, nullptr);
  EXPECT_TRUE(Result->isOpaque());
}

/// const int and int map to the same IR type.
TEST(IRTypeMapperTest, ConstQualified) {
  DiagnosticsEngine Diags;
  IRTypeContext TC;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  IRTypeMapper Mapper(TC, *Layout, Diags);

  ASTContext ASTCtx;
  QualType IntTy = ASTCtx.getIntType();
  QualType ConstIntTy = IntTy.withConst();

  auto* Plain = Mapper.mapType(IntTy);
  auto* Const = Mapper.mapType(ConstIntTy);
  EXPECT_EQ(Plain, Const);
}

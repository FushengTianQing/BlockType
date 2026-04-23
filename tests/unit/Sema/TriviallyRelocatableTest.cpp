//===--- TriviallyRelocatableTest.cpp - __is_trivially_relocatable tests -===//
//
// Part of the BlockType Project.
// E7.5.2.4: Trivially relocatable (P2786R13)
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class TriviallyRelocatableTest : public ::testing::Test {
protected:
  ASTContext Context;
};

// Test 1: Builtin types are trivially relocatable
TEST_F(TriviallyRelocatableTest, BuiltinTypes) {
  QualType IntTy = Context.getIntType();
  EXPECT_TRUE(IntTy->isTriviallyRelocatable());

  QualType FloatTy = Context.getDoubleType();
  EXPECT_TRUE(FloatTy->isTriviallyRelocatable());

  QualType BoolTy = Context.getBoolType();
  EXPECT_TRUE(BoolTy->isTriviallyRelocatable());
}

// Test 2: Pointer types are trivially relocatable
TEST_F(TriviallyRelocatableTest, PointerTypes) {
  QualType IntTy = Context.getIntType();
  PointerType *PtrTy = Context.getPointerType(IntTy.getTypePtr());
  QualType PtrQTy(PtrTy, Qualifier::None);
  EXPECT_TRUE(PtrQTy->isTriviallyRelocatable());
}

// Test 3: CXXRecordDecl without user-declared special members is trivially relocatable
TEST_F(TriviallyRelocatableTest, SimpleRecord) {
  auto *RD = Context.create<CXXRecordDecl>(SourceLocation(1), "Simple", TagDecl::TK_class);
  EXPECT_TRUE(RD->isTriviallyRelocatable());
}

// Test 4: CXXRecordDecl with user-declared move constructor is NOT trivially relocatable
TEST_F(TriviallyRelocatableTest, WithUserMoveCtor) {
  auto *RD = Context.create<CXXRecordDecl>(SourceLocation(1), "WithMove", TagDecl::TK_class);
  RD->setUserDeclaredMoveConstructor(true);
  EXPECT_FALSE(RD->isTriviallyRelocatable());
}

// Test 5: CXXRecordDecl with user-declared destructor is NOT trivially relocatable
TEST_F(TriviallyRelocatableTest, WithUserDestructor) {
  auto *RD = Context.create<CXXRecordDecl>(SourceLocation(1), "WithDtor", TagDecl::TK_class);
  RD->setUserDeclaredDestructor(true);
  EXPECT_FALSE(RD->isTriviallyRelocatable());
}

// Test 6: Record type via getRecordType is trivially relocatable when no special members
TEST_F(TriviallyRelocatableTest, RecordTypeTriviallyRelocatable) {
  auto *RD = Context.create<CXXRecordDecl>(SourceLocation(1), "Simple", TagDecl::TK_struct);
  QualType QTy = Context.getRecordType(RD);
  EXPECT_TRUE(QTy->isTriviallyRelocatable());
}

// Test 7: Record with user-declared destructor is not trivially relocatable via type
TEST_F(TriviallyRelocatableTest, RecordTypeNotTriviallyRelocatable) {
  auto *RD = Context.create<CXXRecordDecl>(SourceLocation(1), "NonTrivial", TagDecl::TK_class);
  RD->setUserDeclaredDestructor(true);
  
  QualType QTy = Context.getRecordType(RD);
  EXPECT_FALSE(QTy->isTriviallyRelocatable());
}

// Test 8: Setting both flags makes it non-trivially-relocatable
TEST_F(TriviallyRelocatableTest, BothFlagsSet) {
  auto *RD = Context.create<CXXRecordDecl>(SourceLocation(1), "Both", TagDecl::TK_class);
  RD->setUserDeclaredMoveConstructor(true);
  RD->setUserDeclaredDestructor(true);
  EXPECT_FALSE(RD->isTriviallyRelocatable());
}

} // anonymous namespace

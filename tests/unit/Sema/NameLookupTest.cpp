//===--- NameLookupTest.cpp - NameLookup Unit Tests ---------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — 名字查找测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Lookup.h"
#include "blocktype/Sema/Scope.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"

using namespace blocktype;

namespace {

class NameLookupTest : public ::testing::Test {
protected:
  ASTContext Context;
};

// --- LookupResult ---

TEST_F(NameLookupTest, LookupResultDefaultEmpty) {
  LookupResult LR;
  EXPECT_TRUE(LR.empty());
  EXPECT_EQ(LR.getNumDecls(), 0u);
  EXPECT_FALSE(LR.isSingleResult());
}

TEST_F(NameLookupTest, LookupResultAddDecl) {
  auto *VD = Context.create<VarDecl>(SourceLocation(1), "x",
                                      Context.getIntType());
  LookupResult LR(VD);
  EXPECT_FALSE(LR.empty());
  EXPECT_EQ(LR.getNumDecls(), 1u);
  EXPECT_TRUE(LR.isSingleResult());
  EXPECT_EQ(LR.getFoundDecl(), VD);
}

TEST_F(NameLookupTest, LookupResultMultipleDecls) {
  auto *V1 = Context.create<VarDecl>(SourceLocation(1), "f",
                                      Context.getIntType());
  SmallVector<ParmVarDecl *, 4> Params;
  auto *F1 = Context.create<FunctionDecl>(SourceLocation(2), "f",
                                           Context.getIntType(), Params);
  LookupResult LR;
  LR.addDecl(V1);
  LR.addDecl(F1);
  EXPECT_EQ(LR.getNumDecls(), 2u);
  EXPECT_FALSE(LR.isSingleResult());
  // Overloaded flag must be set explicitly; it's not auto-detected.
  EXPECT_FALSE(LR.isOverloaded());
  LR.setOverloaded();
  EXPECT_TRUE(LR.isOverloaded());
}

TEST_F(NameLookupTest, LookupResultAmbiguous) {
  LookupResult LR;
  auto *V1 = Context.create<VarDecl>(SourceLocation(1), "a",
                                      Context.getIntType());
  auto *V2 = Context.create<VarDecl>(SourceLocation(2), "a",
                                      Context.getFloatType());
  LR.addDecl(V1);
  LR.addDecl(V2);
  LR.setAmbiguous();
  EXPECT_TRUE(LR.isAmbiguous());
}

TEST_F(NameLookupTest, LookupResultTypeName) {
  auto *TD = Context.create<TypedefDecl>(SourceLocation(1), "MyInt",
                                          Context.getIntType());
  LookupResult LR(TD);
  LR.setTypeName();
  EXPECT_TRUE(LR.isTypeName());
}

TEST_F(NameLookupTest, LookupResultClear) {
  auto *VD = Context.create<VarDecl>(SourceLocation(1), "x",
                                      Context.getIntType());
  LookupResult LR(VD);
  EXPECT_FALSE(LR.empty());
  LR.clear();
  EXPECT_TRUE(LR.empty());
}

TEST_F(NameLookupTest, LookupResultGetAsFunction) {
  SmallVector<ParmVarDecl *, 4> Params;
  auto *FD = Context.create<FunctionDecl>(SourceLocation(1), "func",
                                           Context.getIntType(), Params);
  LookupResult LR(FD);
  EXPECT_NE(LR.getAsFunction(), nullptr);
  EXPECT_EQ(LR.getAsFunction(), FD);
}

TEST_F(NameLookupTest, LookupResultGetAsTagDecl) {
  auto *ED = Context.create<EnumDecl>(SourceLocation(1), "Color");
  LookupResult LR(ED);
  EXPECT_NE(LR.getAsTagDecl(), nullptr);
}

// --- Scope lookup ---

class ScopeLookupTest : public ::testing::Test {
protected:
  ASTContext Context;
};

TEST_F(ScopeLookupTest, ScopeLookupInSameScope) {
  Scope TU(nullptr, ScopeFlags::TranslationUnitScope);
  auto *VD = Context.create<VarDecl>(SourceLocation(1), "x",
                                      Context.getIntType());
  TU.addDecl(VD);

  auto *Found = TU.lookupInScope("x");
  EXPECT_EQ(Found, VD);
}

TEST_F(ScopeLookupTest, ScopeLookupInParentScope) {
  Scope Parent(nullptr, ScopeFlags::TranslationUnitScope);
  auto *VD = Context.create<VarDecl>(SourceLocation(1), "x",
                                      Context.getIntType());
  Parent.addDecl(VD);

  Scope Child(&Parent, ScopeFlags::BlockScope);
  auto *Found = Child.lookup("x");
  EXPECT_EQ(Found, VD);
}

TEST_F(ScopeLookupTest, ScopeLookupShadowsParent) {
  Scope Parent(nullptr, ScopeFlags::TranslationUnitScope);
  auto *Outer = Context.create<VarDecl>(SourceLocation(1), "x",
                                         Context.getIntType());
  Parent.addDecl(Outer);

  Scope Child(&Parent, ScopeFlags::BlockScope);
  auto *Inner = Context.create<VarDecl>(SourceLocation(2), "x",
                                         Context.getFloatType());
  Child.addDecl(Inner);

  // lookup finds the inner declaration first
  auto *Found = Child.lookup("x");
  EXPECT_EQ(Found, Inner);
}

TEST_F(ScopeLookupTest, ScopeLookupNonexistentReturnsNull) {
  Scope S(nullptr, ScopeFlags::TranslationUnitScope);
  EXPECT_EQ(S.lookup("nope"), nullptr);
}

TEST_F(ScopeLookupTest, NestedNameSpecifierGlobal) {
  auto *NNS = NestedNameSpecifier::CreateGlobalSpecifier();
  ASSERT_NE(NNS, nullptr);
  EXPECT_EQ(NNS->getKind(), NestedNameSpecifier::Global);
}

} // anonymous namespace

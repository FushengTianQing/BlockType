//===--- SymbolTableTest.cpp - SymbolTable Unit Tests -------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — 符号表测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/SymbolTable.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"

using namespace blocktype;

namespace {

class SymbolTableTest : public ::testing::Test {
protected:
  ASTContext Context;
  SymbolTable SymTab;

  SymbolTableTest() : SymTab(Context) {}

  VarDecl *makeVar(StringRef Name) {
    return Context.create<VarDecl>(SourceLocation(1), Name,
                                   Context.getIntType());
  }

  FunctionDecl *makeFunc(StringRef Name) {
    SmallVector<ParmVarDecl *, 4> Params;
    return Context.create<FunctionDecl>(SourceLocation(1), Name,
                                        Context.getIntType(), Params);
  }

  NamespaceDecl *makeNS(StringRef Name) {
    return Context.create<NamespaceDecl>(SourceLocation(1), Name);
  }
};

// --- Basic add/lookup ---

TEST_F(SymbolTableTest, AddAndLookupOrdinary) {
  auto *VD = makeVar("x");
  SymTab.addDecl(VD);
  auto Results = SymTab.lookup("x");
  ASSERT_FALSE(Results.empty());
  EXPECT_EQ(Results[0], VD);
}

TEST_F(SymbolTableTest, LookupNonexistentReturnsEmpty) {
  auto Results = SymTab.lookup("nonexistent");
  EXPECT_TRUE(Results.empty());
}

TEST_F(SymbolTableTest, AddMultipleDeclsSameName) {
  auto *V1 = makeVar("f");
  auto *F1 = makeFunc("f");
  SymTab.addDecl(V1);
  SymTab.addDecl(F1);
  auto Results = SymTab.lookup("f");
  EXPECT_EQ(Results.size(), 2u);
}

TEST_F(SymbolTableTest, ContainsCheck) {
  EXPECT_FALSE(SymTab.contains("x"));
  SymTab.addDecl(makeVar("x"));
  EXPECT_TRUE(SymTab.contains("x"));
}

// --- Typed lookups ---

TEST_F(SymbolTableTest, TagLookup) {
  auto *ED = Context.create<EnumDecl>(SourceLocation(1), "Color");
  SymTab.addDecl(ED);
  // Tags are stored in a separate map; use lookupTag() for tag lookup.
  auto *Found = SymTab.lookupTag("Color");
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found, ED);
}

TEST_F(SymbolTableTest, NamespaceLookup) {
  auto *NS = makeNS("std");
  // Explicitly call addNamespaceDecl to verify namespace storage.
  SymTab.addNamespaceDecl(NS);
  auto *Found = SymTab.lookupNamespace("std");
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found, NS);
  // Also verify contains() works for namespaces
  EXPECT_TRUE(SymTab.contains("std"));
}

// --- Size / empty ---

TEST_F(SymbolTableTest, InitialSizeIsZero) {
  EXPECT_EQ(SymTab.size(), 0u);
}

TEST_F(SymbolTableTest, SizeAfterAdds) {
  SymTab.addDecl(makeVar("a"));
  SymTab.addDecl(makeVar("b"));
  SymTab.addDecl(makeVar("c"));
  EXPECT_GE(SymTab.size(), 3u);
}

} // anonymous namespace

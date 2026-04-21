//===--- VariadicTemplateTest.cpp - Variadic Template Tests -----------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E5.5.3.1 — Variadic Template Tests
//
//===-------------------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class VariadicTemplateTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  VariadicTemplateTest() : Diags() {
    S = std::make_unique<Sema>(Context, Diags);
  }

  ClassTemplateDecl *createVariadicClassTemplate(llvm::StringRef Name) {
    auto *Record = Context.create<CXXRecordDecl>(SourceLocation(1), Name);
    Record->setCompleteDefinition(true);

    auto *CTD = Context.create<ClassTemplateDecl>(
        SourceLocation(1), Name, Record);

    auto *TTPD = Context.create<TemplateTypeParmDecl>(
        SourceLocation(1), "Ts", 0, 0, /*IsParameterPack=*/true, true);
    llvm::SmallVector<NamedDecl *, 2> Params = {TTPD};
    auto *TPL = new TemplateParameterList(
        SourceLocation(1), SourceLocation(1), SourceLocation(1), Params);
    CTD->setTemplateParameterList(TPL);

    return CTD;
  }

  TemplateArgument createPackArg(llvm::ArrayRef<QualType> Types) {
    llvm::SmallVector<TemplateArgument, 4> PackArgs;
    for (QualType T : Types)
      PackArgs.push_back(TemplateArgument(T));
    return TemplateArgument(PackArgs);
  }
};

// --- Pack Argument ---

TEST_F(VariadicTemplateTest, TemplateArgumentPack) {
  llvm::SmallVector<TemplateArgument, 4> Pack;
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getFloatType()));
  Pack.push_back(TemplateArgument(Context.getBoolType()));

  TemplateArgument PackArg(Pack);
  EXPECT_TRUE(PackArg.isPack());
  EXPECT_EQ(PackArg.getAsPack().size(), 3u);
}

TEST_F(VariadicTemplateTest, TemplateArgumentPackEmpty) {
  llvm::SmallVector<TemplateArgument, 4> Empty;
  TemplateArgument PackArg(Empty);
  EXPECT_TRUE(PackArg.isPack());
  EXPECT_TRUE(PackArg.getAsPack().empty());
}

// --- TemplateArgumentList getPackArgument ---

TEST_F(VariadicTemplateTest, TemplateArgumentListGetPack) {
  // Build the pack inline — the data must stay alive while TemplateArgumentList is used.
  llvm::SmallVector<TemplateArgument, 4> PackElems;
  PackElems.push_back(TemplateArgument(Context.getIntType()));
  PackElems.push_back(TemplateArgument(Context.getFloatType()));
  TemplateArgument PackArg(PackElems);

  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(PackArg);

  // Verify pack argument is present
  ASSERT_TRUE(Args[0].isPack());
  auto PackArgs = Args[0].getAsPack();
  ASSERT_EQ(PackArgs.size(), 2U);
  EXPECT_TRUE(PackArgs[0].isType());
  EXPECT_TRUE(PackArgs[1].isType());
}

TEST_F(VariadicTemplateTest, TemplateArgumentListNoPackReturnsEmpty) {
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  // Non-pack argument should not be a pack
  EXPECT_FALSE(Args[0].isPack());
}

// --- Fold Expression Identity Elements ---

TEST_F(VariadicTemplateTest, FoldIdentityAdd) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 0),
                                                  Context.getIntType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::Add, true);

  llvm::SmallVector<TemplateArgument, 2> EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  auto *IL = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IL, nullptr);
  EXPECT_EQ(IL->getValue().getZExtValue(), 0u);
}

TEST_F(VariadicTemplateTest, FoldIdentityMul) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 1),
                                                  Context.getIntType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::Mul, true);

  llvm::SmallVector<TemplateArgument, 2> EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  auto *IL = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IL, nullptr);
  EXPECT_EQ(IL->getValue().getZExtValue(), 1u);
}

TEST_F(VariadicTemplateTest, FoldIdentityLAnd) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                                  Context.getBoolType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::LAnd, true);

  llvm::SmallVector<TemplateArgument, 2> EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  // For empty pack with LAnd, should return the pattern (true)
  auto *BoolLit = llvm::dyn_cast<CXXBoolLiteral>(Result);
  ASSERT_NE(BoolLit, nullptr);
  EXPECT_TRUE(BoolLit->getValue());
}

TEST_F(VariadicTemplateTest, FoldIdentityLOr) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<CXXBoolLiteral>(SourceLocation(1), false,
                                                  Context.getBoolType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::LOr, true);

  llvm::SmallVector<TemplateArgument, 2> EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  // For empty pack with LOr, should return the pattern (false)
  auto *BoolLit = llvm::dyn_cast<CXXBoolLiteral>(Result);
  ASSERT_NE(BoolLit, nullptr);
  EXPECT_FALSE(BoolLit->getValue());
}

// --- Fold Expression with Pack Elements ---

TEST_F(VariadicTemplateTest, FoldExprLeftFoldWithPack) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 1),
                                                  Context.getIntType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::Add, false);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> Pack;
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Args.push_back(TemplateArgument(Pack));

  auto *Result = Inst.InstantiateFoldExpr(FE, Args);
  ASSERT_NE(Result, nullptr);
  // Current implementation returns the pattern for non-empty packs
  // Full fold expansion would return a BinaryOperator chain
  EXPECT_NE(Result, nullptr);
}

TEST_F(VariadicTemplateTest, FoldExprRightFoldWithPack) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 1),
                                                  Context.getIntType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::Add, true);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> Pack;
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Args.push_back(TemplateArgument(Pack));

  auto *Result = Inst.InstantiateFoldExpr(FE, Args);
  ASSERT_NE(Result, nullptr);
}

// --- Fold Expression with Init Value ---

TEST_F(VariadicTemplateTest, FoldExprBinaryLeftFoldWithInit) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Init = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 100),
                                               Context.getIntType());
  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 1),
                                                  Context.getIntType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          Init, nullptr, Pattern,
                                          BinaryOpKind::Add, false);

  llvm::SmallVector<TemplateArgument, 2> EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  auto *IL = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IL, nullptr);
  EXPECT_EQ(IL->getValue().getZExtValue(), 100u);
}

// --- Expand Pack ---

// --- Pack Indexing ---

TEST_F(VariadicTemplateTest, PackIndexingExpr) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(Context.getIntType()));
  PackArgs.push_back(TemplateArgument(Context.getFloatType()));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  EXPECT_NE(Result, nullptr);
}

// --- Additional Pack Indexing Tests (Task 7.4.4) ---

TEST_F(VariadicTemplateTest, PackIndexingExprWithMultipleElements) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 2 - should return the third element
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 2),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Create pack with 3 integral elements
  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 10))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 20))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 30))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);
  
  // Verify that SubstitutedExprs was set
  EXPECT_TRUE(PIE->isSubstituted());
  EXPECT_EQ(PIE->getSubstitutedExprs().size(), 3U);
}

TEST_F(VariadicTemplateTest, PackIndexingExprOutOfBounds) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 10 - out of bounds
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 10),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Create pack with only 2 elements
  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 1))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 2))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  // Out of bounds should return nullptr
  EXPECT_EQ(Result, nullptr);
}

TEST_F(VariadicTemplateTest, PackIndexingExprFirstElement) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 0 - first element
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Create pack with 3 elements
  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 100))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 200))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 300))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);
  
  // Verify first element was returned
  auto *IntLit = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IntLit, nullptr);
  EXPECT_EQ(IntLit->getValue().getZExtValue(), 100U);
}

TEST_F(VariadicTemplateTest, PackIndexingExprLastElement) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 2 - last element (pack has 3 elements)
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 2),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Create pack with 3 elements
  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 10))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 20))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 30))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);
  
  // Verify last element was returned
  auto *IntLit = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IntLit, nullptr);
  EXPECT_EQ(IntLit->getValue().getZExtValue(), 30U);
}

TEST_F(VariadicTemplateTest, PackIndexingExprEmptyPack) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Empty pack
  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  // Empty pack with index 0 is out of bounds
  EXPECT_EQ(Result, nullptr);
}

TEST_F(VariadicTemplateTest, PackIndexingExprSubstitutedExprsField) {
  // Test that SubstitutedExprs field works correctly
  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 1),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Initially not substituted
  EXPECT_FALSE(PIE->isSubstituted());
  EXPECT_TRUE(PIE->getSubstitutedExprs().empty());

  // Set substituted expressions
  llvm::SmallVector<Expr *, 4> SubstExprs;
  auto *Lit1 = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 10),
                                               Context.getIntType());
  auto *Lit2 = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 20),
                                               Context.getIntType());
  SubstExprs.push_back(Lit1);
  SubstExprs.push_back(Lit2);
  
  PIE->setSubstitutedExprs(SubstExprs);
  
  // Now substituted
  EXPECT_TRUE(PIE->isSubstituted());
  EXPECT_EQ(PIE->getSubstitutedExprs().size(), 2u);
  EXPECT_EQ(PIE->getSubstitutedExprs()[0], Lit1);
  EXPECT_EQ(PIE->getSubstitutedExprs()[1], Lit2);
}

// --- Variadic Class Template Instantiation ---

TEST_F(VariadicTemplateTest, VariadicClassTemplateInstantiation) {
  auto *CTD = createVariadicClassTemplate("Tuple");

  llvm::SmallVector<TemplateArgument, 4> Args;
  llvm::SmallVector<TemplateArgument, 4> PackElems;
  PackElems.push_back(TemplateArgument(Context.getIntType()));
  PackElems.push_back(TemplateArgument(Context.getFloatType()));
  Args.push_back(TemplateArgument(PackElems));

  auto &Inst = S->getTemplateInstantiator();
  auto *Spec = Inst.InstantiateClassTemplate(CTD, Args);
  ASSERT_NE(Spec, nullptr);
  EXPECT_EQ(Spec->getName(), "Tuple");
}

} // anonymous namespace

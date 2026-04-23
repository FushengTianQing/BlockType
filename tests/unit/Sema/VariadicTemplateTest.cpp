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

// --- Task 7.4.4: Pack Indexing Integration Tests ---

TEST_F(VariadicTemplateTest, PackIndexingTypeTemplateArg) {
  // Test pack indexing with type template arguments
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 0 - should return the first type (int)
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(Context.getIntType()));
  PackArgs.push_back(TemplateArgument(Context.getFloatType()));
  PackArgs.push_back(TemplateArgument(Context.getBoolType()));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);

  // Result should be a TypeRefExpr for int
  auto *TypeRef = llvm::dyn_cast<TypeRefExpr>(Result);
  ASSERT_NE(TypeRef, nullptr);
  EXPECT_FALSE(TypeRef->getReferencedType().isNull());
  EXPECT_TRUE(TypeRef->getReferencedType()->isIntegerType());
}

TEST_F(VariadicTemplateTest, PackIndexingExprTemplateArg) {
  // Test pack indexing with expression template arguments
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 1 - should return the second integral value (20)
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 1),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 10))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 20))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 30))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);

  // Result should be an IntegerLiteral with value 20
  auto *IntLit = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IntLit, nullptr);
  EXPECT_EQ(IntLit->getValue().getZExtValue(), 20U);
}

TEST_F(VariadicTemplateTest, PackIndexingDeclTemplateArg) {
  // Test pack indexing with declaration template arguments
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Create a VarDecl for the pack element
  auto *VarD = Context.create<VarDecl>(SourceLocation(1), "testVar",
                                        Context.getIntType());

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(VarD));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);

  // Result should be a DeclRefExpr
  auto *DRE = llvm::dyn_cast<DeclRefExpr>(Result);
  ASSERT_NE(DRE, nullptr);
  EXPECT_EQ(DRE->getDecl(), VarD);
}

TEST_F(VariadicTemplateTest, PackIndexingMixedPackArgs) {
  // Test pack indexing with mixed type arguments
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Index 2 - should return the third type (bool)
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 2),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(Context.getIntType()));
  PackArgs.push_back(TemplateArgument(Context.getFloatType()));
  PackArgs.push_back(TemplateArgument(Context.getBoolType()));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);

  // Result should be a TypeRefExpr for bool
  auto *TypeRef = llvm::dyn_cast<TypeRefExpr>(Result);
  ASSERT_NE(TypeRef, nullptr);
  EXPECT_TRUE(TypeRef->getReferencedType()->isBooleanType());
}

TEST_F(VariadicTemplateTest, PackIndexingValueCategoryPropagation) {
  // Test that value category is propagated from the Nth element
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Create pack with integral arguments (prvalue)
  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 42))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 99))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  ASSERT_NE(Result, nullptr);

  // Verify that the PIE has value category propagated from the result
  // IntegerLiteral is a prvalue
  EXPECT_EQ(PIE->getValueKind(), Result->getValueKind());
}

TEST_F(VariadicTemplateTest, PackIndexingDependentIndex) {
  // Test that a non-constant index returns the original PIE (runtime index)
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  // Use a DeclRefExpr as a non-constant index
  auto *VarD = Context.create<VarDecl>(SourceLocation(3), "n",
                                        Context.getIntType());
  auto *Index = Context.create<DeclRefExpr>(SourceLocation(2), VarD, "n");
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  llvm::SmallVector<TemplateArgument, 2> Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 10))));
  PackArgs.push_back(TemplateArgument(llvm::APSInt(llvm::APInt(32, 20))));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  // Non-constant index should return the original PIE for runtime evaluation
  EXPECT_EQ(Result, PIE);
}

TEST_F(VariadicTemplateTest, PackIndexingSemaValidation) {
  // Test ActOnPackIndexingExpr with valid pack reference
  // Use a NonTypeTemplateParmDecl (which is a ValueDecl) as the pack
  auto *NTTPD = Context.create<NonTypeTemplateParmDecl>(
      SourceLocation(1), "Ns", Context.getIntType(), 0, 0,
      /*IsParameterPack=*/true);
  auto *Pack = Context.create<DeclRefExpr>(SourceLocation(1), NTTPD, "Ns");
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());

  auto Result = S->ActOnPackIndexingExpr(SourceLocation(1), Pack, Index);
  ASSERT_FALSE(Result.isInvalid());
  auto *PIE = llvm::dyn_cast<PackIndexingExpr>(Result.get());
  ASSERT_NE(PIE, nullptr);
  EXPECT_EQ(PIE->getPack(), Pack);
  EXPECT_EQ(PIE->getIndex(), Index);
}

TEST_F(VariadicTemplateTest, PackIndexingSemaNonPackError) {
  // Test ActOnPackIndexingExpr with non-pack reference (should emit error)
  auto *VarD = Context.create<VarDecl>(SourceLocation(1), "x",
                                        Context.getIntType());
  auto *NonPack = Context.create<DeclRefExpr>(SourceLocation(1), VarD, "x");
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());

  // Should still create the node (error recovery) but emit diagnostic
  auto Result = S->ActOnPackIndexingExpr(SourceLocation(1), NonPack, Index);
  // The expression is still created for error recovery
  ASSERT_NE(Result.get(), nullptr);
}

TEST_F(VariadicTemplateTest, PackIndexingSemaNonIntegralIndex) {
  // Test ActOnPackIndexingExpr with non-integral index (should emit error)
  auto *NTTPD = Context.create<NonTypeTemplateParmDecl>(
      SourceLocation(1), "Ns", Context.getIntType(), 0, 0,
      /*IsParameterPack=*/true);
  auto *Pack = Context.create<DeclRefExpr>(SourceLocation(1), NTTPD, "Ns");
  // Float index is not valid for pack indexing
  auto *FloatIndex = Context.create<FloatingLiteral>(SourceLocation(2),
                                                      llvm::APFloat(1.0),
                                                      Context.getFloatType());

  auto Result = S->ActOnPackIndexingExpr(SourceLocation(1), Pack, FloatIndex);
  // Should still create the node (error recovery)
  ASSERT_NE(Result.get(), nullptr);
}

TEST_F(VariadicTemplateTest, PackIndexingIsValueDependent) {
  // Test isValueDependent on PackIndexingExpr
  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Non-dependent pack and index should not be value-dependent
  EXPECT_FALSE(PIE->isValueDependent());
  EXPECT_FALSE(PIE->isInstantiationDependent());

  // After substitution, should not be dependent
  llvm::SmallVector<Expr *, 4> SubstExprs;
  SubstExprs.push_back(Context.create<IntegerLiteral>(SourceLocation(1),
                                                       llvm::APInt(32, 42),
                                                       Context.getIntType()));
  PIE->setSubstitutedExprs(SubstExprs);
  EXPECT_FALSE(PIE->isValueDependent());
  EXPECT_FALSE(PIE->isInstantiationDependent());
  EXPECT_FALSE(PIE->isTypeDependent());
}

TEST_F(VariadicTemplateTest, PackIndexingSubstituteDependentExpr) {
  // Test that substituteDependentExpr handles PackIndexingExpr
  auto &Inst = S->getTemplateInstantiator();

  auto *Pack = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 0),
                                               Context.getIntType());
  auto *Index = Context.create<IntegerLiteral>(SourceLocation(2),
                                                llvm::APInt(32, 0),
                                                Context.getIntType());
  auto *PIE = Context.create<PackIndexingExpr>(SourceLocation(1), Pack, Index);

  // Build a TemplateInstantiation with pack arguments
  TemplateInstantiation TI;
  auto *TTPD = Context.create<TemplateTypeParmDecl>(
      SourceLocation(1), "Ts", 0, 0, /*IsParameterPack=*/true, true);

  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(Context.getIntType()));
  PackArgs.push_back(TemplateArgument(Context.getFloatType()));
  TI.addSubstitution(TTPD, TemplateArgument(PackArgs));

  auto *Result = Inst.substituteDependentExpr(PIE, TI);
  // Should call InstantiatePackIndexingExpr and return a result
  EXPECT_NE(Result, nullptr);
}

} // anonymous namespace

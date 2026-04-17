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

  TemplateArgumentList Args;
  Args.push_back(PackArg);

  auto PackArgs = Args.getPackArgument();
  ASSERT_EQ(PackArgs.size(), 2u);
  EXPECT_TRUE(PackArgs[0].isType());
  EXPECT_TRUE(PackArgs[1].isType());
}

TEST_F(VariadicTemplateTest, TemplateArgumentListNoPackReturnsEmpty) {
  TemplateArgumentList Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto PackArgs = Args.getPackArgument();
  EXPECT_TRUE(PackArgs.empty());
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

  TemplateArgumentList EmptyPackArgs;
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

  TemplateArgumentList EmptyPackArgs;
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

  TemplateArgumentList EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  auto *IL = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IL, nullptr);
  EXPECT_TRUE(IL->getValue().getBoolValue());
}

TEST_F(VariadicTemplateTest, FoldIdentityLOr) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<CXXBoolLiteral>(SourceLocation(1), false,
                                                  Context.getBoolType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::LOr, true);

  TemplateArgumentList EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  auto *IL = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IL, nullptr);
  EXPECT_FALSE(IL->getValue().getBoolValue());
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

  TemplateArgumentList Args;
  llvm::SmallVector<TemplateArgument, 4> Pack;
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Args.push_back(TemplateArgument(Pack));

  auto *Result = Inst.InstantiateFoldExpr(FE, Args);
  ASSERT_NE(Result, nullptr);
  auto *BO = llvm::dyn_cast<BinaryOperator>(Result);
  EXPECT_NE(BO, nullptr);
}

TEST_F(VariadicTemplateTest, FoldExprRightFoldWithPack) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 1),
                                                  Context.getIntType());
  auto *FE = Context.create<CXXFoldExpr>(SourceLocation(1),
                                          nullptr, nullptr, Pattern,
                                          BinaryOpKind::Add, true);

  TemplateArgumentList Args;
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

  TemplateArgumentList EmptyPackArgs;
  auto *Result = Inst.InstantiateFoldExpr(FE, EmptyPackArgs);
  ASSERT_NE(Result, nullptr);
  auto *IL = llvm::dyn_cast<IntegerLiteral>(Result);
  ASSERT_NE(IL, nullptr);
  EXPECT_EQ(IL->getValue().getZExtValue(), 100u);
}

// --- Expand Pack ---

TEST_F(VariadicTemplateTest, ExpandPackWithSubstitution) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 42),
                                                  Context.getIntType());

  TemplateArgumentList Args;
  llvm::SmallVector<TemplateArgument, 4> Pack;
  Pack.push_back(TemplateArgument(Context.getIntType()));
  Pack.push_back(TemplateArgument(Context.getFloatType()));
  Args.push_back(TemplateArgument(Pack));

  auto Results = Inst.ExpandPack(Pattern, Args);
  EXPECT_EQ(Results.size(), 2u);
}

TEST_F(VariadicTemplateTest, ExpandPackEmptyReturnsEmpty) {
  auto &Inst = S->getTemplateInstantiator();

  auto *Pattern = Context.create<IntegerLiteral>(SourceLocation(1),
                                                  llvm::APInt(32, 0),
                                                  Context.getIntType());

  TemplateArgumentList EmptyArgs;
  auto Results = Inst.ExpandPack(Pattern, EmptyArgs);
  EXPECT_TRUE(Results.empty());
}

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

  TemplateArgumentList Args;
  llvm::SmallVector<TemplateArgument, 4> PackArgs;
  PackArgs.push_back(TemplateArgument(Context.getIntType()));
  PackArgs.push_back(TemplateArgument(Context.getFloatType()));
  Args.push_back(TemplateArgument(PackArgs));

  auto *Result = Inst.InstantiatePackIndexingExpr(PIE, Args);
  EXPECT_NE(Result, nullptr);
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

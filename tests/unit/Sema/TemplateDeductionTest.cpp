//===--- TemplateDeductionTest.cpp - Template Deduction Tests ----------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E5.5.3.1 — Template Deduction Tests
//
//===-------------------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class TemplateDeductionTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  TemplateDeductionTest() : Diags() {
    S = std::make_unique<Sema>(Context, Diags);
  }

  FunctionTemplateDecl *createUnaryTemplate(llvm::StringRef Name) {
    auto *TTPD = Context.create<TemplateTypeParmDecl>(
        SourceLocation(1), "T", 0, 0, false, true);
    QualType TType = QualType(
        Context.getTemplateTypeParmType(TTPD, 0, 0), Qualifier::None);

    auto *Param = Context.create<ParmVarDecl>(
        SourceLocation(1), "a", TType, 0);
    llvm::SmallVector<ParmVarDecl *, 2> Params = {Param};

    QualType VoidTy = Context.getVoidType();
    auto *FD = Context.create<FunctionDecl>(
        SourceLocation(1), Name, VoidTy, Params);

    auto *FTD = Context.create<FunctionTemplateDecl>(
        SourceLocation(1), Name, FD);

    llvm::SmallVector<NamedDecl *, 2> TParams = {TTPD};
    auto *TPL = new TemplateParameterList(
        SourceLocation(1), SourceLocation(1), SourceLocation(1), TParams);
    FTD->setTemplateParameterList(TPL);

    return FTD;
  }

  FunctionTemplateDecl *createBinaryTemplate(llvm::StringRef Name) {
    auto *TTPD = Context.create<TemplateTypeParmDecl>(
        SourceLocation(1), "T", 0, 0, false, true);
    QualType TType = QualType(
        Context.getTemplateTypeParmType(TTPD, 0, 0), Qualifier::None);

    auto *P1 = Context.create<ParmVarDecl>(SourceLocation(1), "a", TType, 0);
    auto *P2 = Context.create<ParmVarDecl>(SourceLocation(1), "b", TType, 1);
    llvm::SmallVector<ParmVarDecl *, 2> Params = {P1, P2};

    QualType VoidTy = Context.getVoidType();
    auto *FD = Context.create<FunctionDecl>(
        SourceLocation(1), Name, VoidTy, Params);

    auto *FTD = Context.create<FunctionTemplateDecl>(
        SourceLocation(1), Name, FD);

    llvm::SmallVector<NamedDecl *, 2> TParams = {TTPD};
    auto *TPL = new TemplateParameterList(
        SourceLocation(1), SourceLocation(1), SourceLocation(1), TParams);
    FTD->setTemplateParameterList(TPL);

    return FTD;
  }
};

// --- Basic Deduction ---

TEST_F(TemplateDeductionTest, DeduceSingleIntArg) {
  auto *FTD = createUnaryTemplate("foo");

  auto *Arg = Context.create<IntegerLiteral>(SourceLocation(1),
                                              llvm::APInt(32, 42),
                                              Context.getIntType());
  llvm::SmallVector<Expr *, 2> Args = {Arg};

  TemplateDeductionInfo Info;
  auto &Deduction = S->getTemplateDeduction();
  auto Result = Deduction.DeduceFunctionTemplateArguments(FTD, Args, Info);

  EXPECT_EQ(Result, TemplateDeductionResult::Success);
  auto DeducedArgs = Info.getDeducedArgs(1);
  ASSERT_EQ(DeducedArgs.size(), 1u);
  EXPECT_TRUE(DeducedArgs[0].isType());
}

TEST_F(TemplateDeductionTest, DeduceTwoSameTypeArgs) {
  auto *FTD = createBinaryTemplate("bar");

  auto *Arg1 = Context.create<IntegerLiteral>(SourceLocation(1),
                                               llvm::APInt(32, 1),
                                               Context.getIntType());
  auto *Arg2 = Context.create<IntegerLiteral>(SourceLocation(2),
                                               llvm::APInt(32, 2),
                                               Context.getIntType());
  llvm::SmallVector<Expr *, 2> Args = {Arg1, Arg2};

  TemplateDeductionInfo Info;
  auto &Deduction = S->getTemplateDeduction();
  auto Result = Deduction.DeduceFunctionTemplateArguments(FTD, Args, Info);

  EXPECT_EQ(Result, TemplateDeductionResult::Success);
}

TEST_F(TemplateDeductionTest, DeduceNoArgsForNonEmptyParams) {
  auto *FTD = createUnaryTemplate("baz");

  llvm::SmallVector<Expr *, 2> Args;

  TemplateDeductionInfo Info;
  auto &Deduction = S->getTemplateDeduction();
  auto Result = Deduction.DeduceFunctionTemplateArguments(FTD, Args, Info);

  EXPECT_NE(Result, TemplateDeductionResult::Success);
}

// --- TemplateDeductionInfo ---

TEST_F(TemplateDeductionTest, DeductionInfoInitiallyNoArgs) {
  TemplateDeductionInfo Info;
  EXPECT_FALSE(Info.hasDeducedArg(0));
}

TEST_F(TemplateDeductionTest, DeductionInfoAddArg) {
  TemplateDeductionInfo Info;
  Info.addDeducedArg(0, TemplateArgument(Context.getIntType()));
  EXPECT_TRUE(Info.hasDeducedArg(0));

  auto Arg = Info.getDeducedArg(0);
  EXPECT_TRUE(Arg.isType());
  EXPECT_EQ(Arg.getAsType().getTypePtr(), Context.getIntType().getTypePtr());
}

TEST_F(TemplateDeductionTest, DeductionInfoMultipleArgs) {
  TemplateDeductionInfo Info;
  Info.addDeducedArg(0, TemplateArgument(Context.getIntType()));
  Info.addDeducedArg(1, TemplateArgument(Context.getFloatType()));

  auto Args = Info.getDeducedArgs(2);
  ASSERT_EQ(Args.size(), 2u);
  EXPECT_TRUE(Args[0].isType());
  EXPECT_TRUE(Args[1].isType());
}

TEST_F(TemplateDeductionTest, DeductionInfoGetDeducedArgsReturnsCorrectCount) {
  TemplateDeductionInfo Info;
  Info.addDeducedArg(0, TemplateArgument(Context.getIntType()));
  Info.addDeducedArg(1, TemplateArgument(Context.getFloatType()));

  // Request more args than deduced → fills with empty
  auto Args = Info.getDeducedArgs(3);
  EXPECT_EQ(Args.size(), 3u);
}

// --- Sema Integration ---

TEST_F(TemplateDeductionTest, SemaDeduceAndInstantiateSuccess) {
  auto *FTD = createUnaryTemplate("identity");
  S->ActOnFunctionTemplateDecl(FTD);

  auto *Arg = Context.create<IntegerLiteral>(SourceLocation(1),
                                              llvm::APInt(32, 42),
                                              Context.getIntType());
  llvm::SmallVector<Expr *, 2> Args = {Arg};

  auto *InstFD = S->DeduceAndInstantiateFunctionTemplate(
      FTD, Args, SourceLocation(1));

  EXPECT_NE(InstFD, nullptr);
  EXPECT_EQ(InstFD->getName(), "identity");
}

TEST_F(TemplateDeductionTest, SemaDeduceAndInstantiateNoArgsFails) {
  auto *FTD = createUnaryTemplate("single");
  S->ActOnFunctionTemplateDecl(FTD);

  llvm::SmallVector<Expr *, 2> Args;
  auto *InstFD = S->DeduceAndInstantiateFunctionTemplate(
      FTD, Args, SourceLocation(1));

  EXPECT_EQ(InstFD, nullptr);
  EXPECT_TRUE(S->hasErrorOccurred());
}

} // anonymous namespace

//===--- OverloadResolutionTest.cpp - Overload Unit Tests ---*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — 重载决议测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Overload.h"
#include "blocktype/Sema/Lookup.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"

using namespace blocktype;

namespace {

class OverloadTest : public ::testing::Test {
protected:
  ASTContext Context;

  FunctionDecl *makeFunc(StringRef Name, unsigned NParams) {
    // Create a function type: int(int, int, ...)
    SmallVector<const Type *, 4> ParamTypes;
    for (unsigned I = 0; I < NParams; ++I)
      ParamTypes.push_back(Context.getIntType().getTypePtr());
    QualType FnType(Context.getFunctionType(
        Context.getIntType().getTypePtr(), ParamTypes), Qualifier::None);

    SmallVector<ParmVarDecl *, 4> Params;
    for (unsigned I = 0; I < NParams; ++I)
      Params.push_back(Context.create<ParmVarDecl>(
          SourceLocation(1), "p" + std::to_string(I),
          Context.getIntType(), I));
    return Context.create<FunctionDecl>(SourceLocation(1), Name,
                                        FnType, Params);
  }

  Expr *makeIntExpr() {
    return Context.create<IntegerLiteral>(SourceLocation(1),
                                          llvm::APInt(32, 42),
                                          Context.getIntType());
  }

  Expr *makeBoolExpr() {
    return Context.create<CXXBoolLiteral>(SourceLocation(1), true,
                                          Context.getBoolType());
  }

  Expr *makeFloatExpr() {
    return Context.create<FloatingLiteral>(
        SourceLocation(1), llvm::APFloat(1.0), Context.getDoubleType());
  }
};

// --- OverloadCandidate basics ---

TEST_F(OverloadTest, CandidateConstruction) {
  auto *FD = makeFunc("f", 1);
  OverloadCandidate C(FD);
  EXPECT_EQ(C.getFunction(), FD);
  EXPECT_FALSE(C.isViable()); // not yet checked
}

TEST_F(OverloadTest, CandidateCheckViability) {
  auto *FD = makeFunc("f", 1);
  OverloadCandidate C(FD);
  SmallVector<Expr *, 2> Args = {makeIntExpr()};
  C.checkViability(Args);
  EXPECT_TRUE(C.isViable());
}

TEST_F(OverloadTest, CandidateNotViableWhenArgCountMismatch) {
  auto *FD = makeFunc("f", 2);
  OverloadCandidate C(FD);
  SmallVector<Expr *, 2> Args = {makeIntExpr()};
  C.checkViability(Args);
  EXPECT_FALSE(C.isViable());
}

// --- OverloadCandidateSet ---

TEST_F(OverloadTest, CandidateSetEmpty) {
  OverloadCandidateSet OCS(SourceLocation(1));
  EXPECT_TRUE(OCS.empty());
  EXPECT_EQ(OCS.size(), 0u);
}

TEST_F(OverloadTest, CandidateSetAddAndSize) {
  OverloadCandidateSet OCS(SourceLocation(1));
  OCS.addCandidate(makeFunc("f", 1));
  OCS.addCandidate(makeFunc("f", 2));
  EXPECT_EQ(OCS.size(), 2u);
  EXPECT_FALSE(OCS.empty());
}

TEST_F(OverloadTest, ResolveExactMatch) {
  OverloadCandidateSet OCS(SourceLocation(1));
  OCS.addCandidate(makeFunc("f", 1));
  OCS.addCandidate(makeFunc("f", 2));

  SmallVector<Expr *, 2> Args = {makeIntExpr()};
  auto [Result, Best] = OCS.resolve(Args);
  EXPECT_EQ(Result, OverloadResult::Success);
  ASSERT_NE(Best, nullptr);
  EXPECT_EQ(Best->getNumParams(), 1u);
}

TEST_F(OverloadTest, ResolveTwoArgsMatch) {
  OverloadCandidateSet OCS(SourceLocation(1));
  OCS.addCandidate(makeFunc("f", 1));
  OCS.addCandidate(makeFunc("f", 2));

  SmallVector<Expr *, 2> Args = {makeIntExpr(), makeIntExpr()};
  auto [Result, Best] = OCS.resolve(Args);
  EXPECT_EQ(Result, OverloadResult::Success);
  ASSERT_NE(Best, nullptr);
  EXPECT_EQ(Best->getNumParams(), 2u);
}

TEST_F(OverloadTest, ResolveNoViable) {
  OverloadCandidateSet OCS(SourceLocation(1));
  OCS.addCandidate(makeFunc("f", 3));

  SmallVector<Expr *, 2> Args = {makeIntExpr()};
  auto [Result, Best] = OCS.resolve(Args);
  EXPECT_EQ(Result, OverloadResult::NoViable);
}

TEST_F(OverloadTest, ResolveFromLookupResult) {
  LookupResult LR;
  LR.addDecl(makeFunc("g", 1));
  LR.addDecl(makeFunc("g", 2));

  OverloadCandidateSet OCS(SourceLocation(1));
  OCS.addCandidates(LR);
  EXPECT_EQ(OCS.size(), 2u);
}

// --- Deleted function detection ---

TEST_F(OverloadTest, DeletedFunctionDetected) {
  auto *Parent = Context.create<CXXRecordDecl>(SourceLocation(1), "Cls");
  SmallVector<ParmVarDecl *, 4> Params;
  Params.push_back(Context.create<ParmVarDecl>(SourceLocation(2), "p",
                                                Context.getIntType(), 0));
  // CXXMethodDecl needs a FunctionType for checkViability to pass.
  SmallVector<const Type *, 1> ParamTypes = {Context.getIntType().getTypePtr()};
  QualType FnType(Context.getFunctionType(
      Context.getIntType().getTypePtr(), ParamTypes), Qualifier::None);

  auto *Deleted = Context.create<CXXMethodDecl>(
      SourceLocation(1), "deleted", FnType, Params, Parent,
      nullptr, false, false, false, false, false, false, false,
      false, true, CXXMethodDecl::RQ_None, false, false, nullptr,
      AccessSpecifier::AS_public);
  ASSERT_TRUE(Deleted->isDeleted());

  OverloadCandidateSet OCS(SourceLocation(1));
  OCS.addCandidate(Deleted);

  SmallVector<Expr *, 2> Args = {makeIntExpr()};
  auto [Result, Best] = OCS.resolve(Args);
  EXPECT_EQ(Result, OverloadResult::Deleted);
}

} // anonymous namespace

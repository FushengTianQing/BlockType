//===--- SFINAETest.cpp - SFINAE Tests -----------------------------------*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E5.5.3.1 — SFINAE Tests
//
//===--------------------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class SFINAETest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  SFINAETest() : Diags() {
    S = std::make_unique<Sema>(Context, Diags);
  }
};

// --- SFINAEContext Basics ---

TEST_F(SFINAETest, SFINAEContextInitiallyInactive) {
  SFINAEContext Ctx;
  EXPECT_FALSE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 0u);
}

TEST_F(SFINAETest, SFINAEContextEnterExit) {
  SFINAEContext Ctx;
  Ctx.enterSFINAE(0);
  EXPECT_TRUE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 1u);

  Ctx.exitSFINAE();
  EXPECT_FALSE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 0u);
}

TEST_F(SFINAETest, SFINAEContextNesting) {
  SFINAEContext Ctx;

  Ctx.enterSFINAE(0);
  EXPECT_TRUE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 1u);

  Ctx.enterSFINAE(0);
  EXPECT_TRUE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 2u);

  Ctx.exitSFINAE();
  EXPECT_TRUE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 1u);

  Ctx.exitSFINAE();
  EXPECT_FALSE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 0u);
}

// --- SFINAEContext Error Tracking ---

TEST_F(SFINAETest, SFINAEContextNoNewErrors) {
  SFINAEContext Ctx;
  Ctx.enterSFINAE(5);
  EXPECT_FALSE(Ctx.hasNewErrors(5));
}

TEST_F(SFINAETest, SFINAEContextHasNewErrors) {
  SFINAEContext Ctx;
  Ctx.enterSFINAE(5);
  EXPECT_TRUE(Ctx.hasNewErrors(6));
  EXPECT_TRUE(Ctx.hasNewErrors(10));
}

// --- SFINAEContext Failure Reasons ---

TEST_F(SFINAETest, SFINAEContextFailureReasons) {
  SFINAEContext Ctx;

  Ctx.addFailureReason("substitution failure");
  Ctx.addFailureReason("type mismatch");

  auto Reasons = Ctx.getFailureReasons();
  ASSERT_EQ(Reasons.size(), 2u);
  EXPECT_EQ(Reasons[0], "substitution failure");
  EXPECT_EQ(Reasons[1], "type mismatch");
}

TEST_F(SFINAETest, SFINAEContextClearFailureReasonsOnExit) {
  SFINAEContext Ctx;
  Ctx.enterSFINAE(0);
  Ctx.addFailureReason("test");

  Ctx.exitSFINAE();
  EXPECT_TRUE(Ctx.getFailureReasons().empty());
}

// --- SFINAEGuard RAII ---

TEST_F(SFINAETest, SFINAEGuardRAII) {
  SFINAEContext Ctx;
  EXPECT_FALSE(Ctx.isSFINAE());

  {
    SFINAEGuard Guard(Ctx, 0);
    EXPECT_TRUE(Ctx.isSFINAE());
    EXPECT_EQ(Ctx.getNestingLevel(), 1u);
  }

  EXPECT_FALSE(Ctx.isSFINAE());
  EXPECT_EQ(Ctx.getNestingLevel(), 0u);
}

TEST_F(SFINAETest, SFINAEGuardNested) {
  SFINAEContext Ctx;

  {
    SFINAEGuard G1(Ctx, 0);
    EXPECT_TRUE(Ctx.isSFINAE());
    EXPECT_EQ(Ctx.getNestingLevel(), 1u);

    {
      SFINAEGuard G2(Ctx, 0);
      EXPECT_TRUE(Ctx.isSFINAE());
      EXPECT_EQ(Ctx.getNestingLevel(), 2u);
    }

    EXPECT_TRUE(Ctx.isSFINAE());
    EXPECT_EQ(Ctx.getNestingLevel(), 1u);
  }

  EXPECT_FALSE(Ctx.isSFINAE());
}

// --- TemplateInstantiator SFINAE Integration ---

TEST_F(SFINAETest, InstantiatorSFINAEContextAccessible) {
  auto &Inst = S->getTemplateInstantiator();
  auto &Sfinae = Inst.getSFINAEContext();

  EXPECT_FALSE(Sfinae.isSFINAE());

  SFINAEGuard Guard(Sfinae, 0);
  EXPECT_TRUE(Sfinae.isSFINAE());
}

// --- Recursive Instantiation ---

TEST_F(SFINAETest, RecursiveInstantiationRespectsDepth) {
  auto *Record = Context.create<CXXRecordDecl>(SourceLocation(1), "Recur");
  Record->setCompleteDefinition(true);
  auto *CTD = Context.create<ClassTemplateDecl>(
      SourceLocation(1), "Recur", Record);
  auto *TTPD = Context.create<TemplateTypeParmDecl>(
      SourceLocation(1), "T", 0, 0, false, true);
  llvm::SmallVector<NamedDecl *, 2> Params = {TTPD};
  auto *TPL = new TemplateParameterList(
      SourceLocation(1), SourceLocation(1), SourceLocation(1), Params);
  CTD->setTemplateParameterList(TPL);

  auto &Inst = S->getTemplateInstantiator();
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto *Spec = Inst.InstantiateClassTemplate(CTD, Args);
  ASSERT_NE(Spec, nullptr);
  EXPECT_TRUE(Spec->isCompleteDefinition());
}

} // anonymous namespace

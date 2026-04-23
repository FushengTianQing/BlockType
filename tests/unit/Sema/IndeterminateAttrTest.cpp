//===--- IndeterminateAttrTest.cpp - [[indeterminate]] attribute tests ---===//
//
// Part of the BlockType Project.
// E7.5.2.3: [[indeterminate]] attribute (P2795R5)
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace blocktype;

namespace {

class IndeterminateAttrTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  IndeterminateAttrTest() {
    S = std::make_unique<Sema>(Context, Diags);
  }
};

// Test that [[indeterminate]] attribute can be set on a VarDecl
TEST_F(IndeterminateAttrTest, AttributeOnVarDecl) {
  QualType IntTy = Context.getIntType();
  
  // Create attribute list with [[indeterminate]]
  auto *AttrList = Context.create<AttributeListDecl>(SourceLocation(1));
  auto *Attr = Context.create<AttributeDecl>(SourceLocation(1), "indeterminate");
  AttrList->addAttribute(Attr);
  
  auto Result = S->ActOnVarDecl(SourceLocation(1), "x", IntTy, nullptr, AttrList);
  ASSERT_TRUE(Result.isUsable());
  
  auto *VD = llvm::cast<VarDecl>(Result.get());
  ASSERT_NE(VD->getAttrs(), nullptr);
  
  bool Found = false;
  for (auto *A : VD->getAttrs()->getAttributes()) {
    if (A->getAttributeName() == "indeterminate") {
      Found = true;
      break;
    }
  }
  EXPECT_TRUE(Found);
}

// Test that [[indeterminate]] on a const variable produces a warning
TEST_F(IndeterminateAttrTest, WarningOnConstVar) {
  QualType ConstIntTy(Context.getIntType().getTypePtr(),
                      Qualifier::Const);
  
  auto *AttrList = Context.create<AttributeListDecl>(SourceLocation(1));
  auto *Attr = Context.create<AttributeDecl>(SourceLocation(1), "indeterminate");
  AttrList->addAttribute(Attr);
  
  unsigned WarningsBefore = Diags.getNumWarnings();
  
  S->ActOnVarDecl(SourceLocation(1), "x", ConstIntTy, nullptr, AttrList);
  
  // Should have produced a warning
  EXPECT_GT(Diags.getNumWarnings(), WarningsBefore);
}

// Test that [[indeterminate]] on a non-const variable produces no warning
TEST_F(IndeterminateAttrTest, NoWarningOnNonConstVar) {
  QualType IntTy = Context.getIntType();
  
  auto *AttrList = Context.create<AttributeListDecl>(SourceLocation(1));
  auto *Attr = Context.create<AttributeDecl>(SourceLocation(1), "indeterminate");
  AttrList->addAttribute(Attr);
  
  unsigned WarningsBefore = Diags.getNumWarnings();
  
  S->ActOnVarDecl(SourceLocation(1), "x", IntTy, nullptr, AttrList);
  
  // Should NOT have produced a warning
  EXPECT_EQ(Diags.getNumWarnings(), WarningsBefore);
}

// Test that [[indeterminate]] is properly recognized by name
TEST_F(IndeterminateAttrTest, AttributeName) {
  auto *Attr = Context.create<AttributeDecl>(SourceLocation(1), "indeterminate");
  EXPECT_EQ(Attr->getAttributeName(), "indeterminate");
  EXPECT_EQ(Attr->getFullName(), "indeterminate");
  EXPECT_FALSE(Attr->hasNamespace());
}

} // anonymous namespace

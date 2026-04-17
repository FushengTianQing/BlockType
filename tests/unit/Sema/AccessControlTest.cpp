//===--- AccessControlTest.cpp - AccessControl Unit Tests ---*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E4.5.5.1 — 访问控制测试
//
//===--------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/AccessControl.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class AccessControlTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;

  AccessControlTest() : Diags() {}

  CXXRecordDecl *makeClass(StringRef Name) {
    return Context.create<CXXRecordDecl>(SourceLocation(1), Name);
  }

  CXXMethodDecl *makeMethod(StringRef Name, CXXRecordDecl *Parent,
                            AccessSpecifier Access) {
    SmallVector<ParmVarDecl *, 4> Params;
    return Context.create<CXXMethodDecl>(
        SourceLocation(1), Name, Context.getIntType(), Params, Parent,
        nullptr, false, false, false, false, false, false, false,
        false, false, CXXMethodDecl::RQ_None, false, false, nullptr,
        Access);
  }

  FieldDecl *makeField(StringRef Name, CXXRecordDecl *Parent,
                       AccessSpecifier Access) {
    return Context.create<FieldDecl>(SourceLocation(1), Name,
                                     Context.getIntType(), nullptr, false,
                                     nullptr, Access);
  }
};

// --- getEffectiveAccess ---

TEST_F(AccessControlTest, PublicAccessIsPublic) {
  auto *Cls = makeClass("A");
  auto *M = makeMethod("pub", Cls, AccessSpecifier::AS_public);
  EXPECT_EQ(AccessControl::getEffectiveAccess(M),
            AccessSpecifier::AS_public);
}

TEST_F(AccessControlTest, PrivateAccessIsPrivate) {
  auto *Cls = makeClass("A");
  auto *M = makeMethod("priv", Cls, AccessSpecifier::AS_private);
  EXPECT_EQ(AccessControl::getEffectiveAccess(M),
            AccessSpecifier::AS_private);
}

TEST_F(AccessControlTest, ProtectedAccessIsProtected) {
  auto *Cls = makeClass("A");
  auto *M = makeMethod("prot", Cls, AccessSpecifier::AS_protected);
  EXPECT_EQ(AccessControl::getEffectiveAccess(M),
            AccessSpecifier::AS_protected);
}

// --- CheckMemberAccess ---

TEST_F(AccessControlTest, PublicMemberAccessFromOutside) {
  auto *Cls = makeClass("A");
  auto *F = makeField("x", Cls, AccessSpecifier::AS_public);
  EXPECT_TRUE(AccessControl::CheckMemberAccess(
      F, AccessSpecifier::AS_public, Cls, nullptr,
      SourceLocation(1), Diags));
}

TEST_F(AccessControlTest, PrivateMemberAccessFromOutsideFails) {
  auto *Cls = makeClass("A");
  auto *F = makeField("secret", Cls, AccessSpecifier::AS_private);
  EXPECT_FALSE(AccessControl::CheckMemberAccess(
      F, AccessSpecifier::AS_private, Cls, nullptr,
      SourceLocation(1), Diags));
}

TEST_F(AccessControlTest, ProtectedMemberAccessFromOutsideFails) {
  auto *Cls = makeClass("A");
  auto *F = makeField("prot_member", Cls, AccessSpecifier::AS_protected);
  EXPECT_FALSE(AccessControl::CheckMemberAccess(
      F, AccessSpecifier::AS_protected, Cls, nullptr,
      SourceLocation(1), Diags));
}

// --- Inheritance ---

TEST_F(AccessControlTest, IsDerivedFromSelfReturnsFalse) {
  // isDerivedFrom checks the inheritance hierarchy, not identity.
  // A class is not "derived from" itself unless there's an actual base path.
  auto *Cls = makeClass("A");
  EXPECT_FALSE(AccessControl::isDerivedFrom(Cls, Cls));
}

TEST_F(AccessControlTest, IsDerivedFromUnrelated) {
  auto *A = makeClass("A");
  auto *B = makeClass("B");
  EXPECT_FALSE(AccessControl::isDerivedFrom(B, A));
}

} // anonymous namespace

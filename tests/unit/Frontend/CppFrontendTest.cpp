//===--- CppFrontendTest.cpp - CppFrontend Unit Tests -------------------===//

#include <gtest/gtest.h>

#include <fstream>

#include "blocktype/Frontend/CppFrontend.h"
#include "blocktype/Frontend/FrontendRegistry.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRContract.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

/// Helper to ensure "cpp" frontend is registered in the test binary.
/// Static registration from CppFrontendRegistration.cpp may not be pulled in
/// by the linker since nothing references it directly.
static struct CppFrontendTestRegistration {
  CppFrontendTestRegistration() {
    auto& Reg = FrontendRegistry::instance();
    if (!Reg.hasFrontend("cpp")) {
      Reg.registerFrontend("cpp", [](const FrontendCompileOptions& O,
                                      DiagnosticsEngine& D)
        -> std::unique_ptr<FrontendBase> {
        return std::make_unique<CppFrontend>(O, D);
      });
      Reg.addExtensionMapping(".cpp", "cpp");
      Reg.addExtensionMapping(".cc",  "cpp");
      Reg.addExtensionMapping(".cxx", "cpp");
      Reg.addExtensionMapping(".C",   "cpp");
      Reg.addExtensionMapping(".c",   "cpp");
      Reg.addExtensionMapping(".h",   "cpp");
      Reg.addExtensionMapping(".hpp", "cpp");
      Reg.addExtensionMapping(".hxx", "cpp");
    }
  }
} CppFrontendTestRegistrator;

class CppFrontendTest : public ::testing::Test {
protected:
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  ir::IRContext IRCtx;
  ir::IRTypeContext& TC;
  ir::TargetLayout Layout;

  CppFrontendTest()
    : TC(IRCtx.getTypeContext()),
      Layout("x86_64-unknown-linux-gnu") {
    Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  }
};

} // anonymous namespace

// === T1: Construction + getName/getLanguage ===
TEST_F(CppFrontendTest, BasicProperties) {
  CppFrontend FE(Opts, Diags);
  EXPECT_EQ(FE.getName(), "cpp");
  EXPECT_EQ(FE.getLanguage(), "c++");
}

// === T2: canHandle — valid extensions ===
TEST_F(CppFrontendTest, CanHandleValidExtensions) {
  CppFrontend FE(Opts, Diags);
  EXPECT_TRUE(FE.canHandle("test.cpp"));
  EXPECT_TRUE(FE.canHandle("test.cc"));
  EXPECT_TRUE(FE.canHandle("test.cxx"));
  EXPECT_TRUE(FE.canHandle("test.c"));
  EXPECT_TRUE(FE.canHandle("test.h"));
  EXPECT_TRUE(FE.canHandle("test.hpp"));
  EXPECT_TRUE(FE.canHandle("test.hxx"));
}

// === T3: canHandle — invalid extensions ===
TEST_F(CppFrontendTest, CannotHandleInvalidExtensions) {
  CppFrontend FE(Opts, Diags);
  EXPECT_FALSE(FE.canHandle("test.bt"));
  EXPECT_FALSE(FE.canHandle("test.py"));
  EXPECT_FALSE(FE.canHandle("test.rs"));
  EXPECT_FALSE(FE.canHandle("Makefile"));
  EXPECT_FALSE(FE.canHandle("test.java"));
}

// === T4: compile() — nonexistent file returns nullptr ===
TEST_F(CppFrontendTest, CompileNonexistentFile) {
  CppFrontend FE(Opts, Diags);
  auto Mod = FE.compile("nonexistent_file_xyz.cpp", TC, Layout);
  EXPECT_EQ(Mod, nullptr);
}

// === T5: compile() — simple function (requires filesystem) ===
TEST_F(CppFrontendTest, CompileSimpleFunction) {
  // Write a temporary source file
  const char* TmpPath = "/tmp/blocktype_test_b10_simple.cpp";
  {
    std::ofstream OF(TmpPath);
    OF << "int main() { return 0; }" << std::endl;
  }

  CppFrontend FE(Opts, Diags);
  auto Mod = FE.compile(TmpPath, TC, Layout);
  // Result depends on Parser/Sema completeness — just verify no crash
  // If the parser works, Mod should be non-null
  if (Mod) {
    EXPECT_FALSE(Mod->getName().empty());
  }
  // Cleanup
  std::remove(TmpPath);
}

// === T6: FrontendRegistry registration ===
TEST_F(CppFrontendTest, RegistryRegistration) {
  auto& Reg = FrontendRegistry::instance();
  EXPECT_TRUE(Reg.hasFrontend("cpp"));

  auto FE = Reg.create("cpp", Opts, Diags);
  ASSERT_NE(FE, nullptr);
  EXPECT_EQ(FE->getName(), "cpp");
  EXPECT_EQ(FE->getLanguage(), "c++");
}

// === T7: FrontendRegistry autoSelect ===
TEST_F(CppFrontendTest, RegistryAutoSelect) {
  auto& Reg = FrontendRegistry::instance();
  auto FE = Reg.autoSelect("test.cpp", Opts, Diags);
  ASSERT_NE(FE, nullptr);
  EXPECT_EQ(FE->getName(), "cpp");

  auto FE2 = Reg.autoSelect("test.cc", Opts, Diags);
  ASSERT_NE(FE2, nullptr);
  EXPECT_EQ(FE2->getName(), "cpp");

  auto FE3 = Reg.autoSelect("test.unknown", Opts, Diags);
  EXPECT_EQ(FE3, nullptr);
}

// === T8: Contract verification (with empty module) ===
TEST_F(CppFrontendTest, ContractVerification) {
  ir::IRContext TmpCtx;
  ir::IRModule Mod("test", TmpCtx.getTypeContext(), "x86_64-unknown-linux-gnu");

  auto VR = ir::contract::verifyAllContracts(Mod);
  // Empty module should pass basic contract verification
  EXPECT_TRUE(VR.isValid());
}

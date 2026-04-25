//===--- FrontendBaseTest.cpp - FrontendBase unit tests --------*- C++ -*-===//

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/FrontendBase.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;
using namespace blocktype::ir;

// ============================================================
// Test helpers
// ============================================================

/// A concrete frontend subclass for testing purposes.
class TestFrontend : public FrontendBase {
public:
  using FrontendBase::FrontendBase;

  StringRef getName() const override { return "test"; }
  StringRef getLanguage() const override { return "test"; }

  std::unique_ptr<IRModule> compile(
      StringRef Filename,
      IRTypeContext& TypeCtx,
      const TargetLayout& Layout) override {
    assert(!Opts_.TargetTriple.empty() &&
           "TargetTriple must be set before compile()");
    // Return an empty module for testing.
    return std::make_unique<IRModule>(Filename, TypeCtx,
                                       Opts_.TargetTriple);
  }

  bool canHandle(StringRef Filename) const override {
    return Filename.endswith(".test");
  }
};

// ============================================================
// Test cases
// ============================================================

/// V3: FrontendCompileOptions default values.
TEST(FrontendBaseTest, DefaultOptions) {
  FrontendCompileOptions Opts;
  EXPECT_FALSE(Opts.EmitIR);
  EXPECT_FALSE(Opts.EmitIRBitcode);
  EXPECT_FALSE(Opts.BTIROnly);
  EXPECT_FALSE(Opts.VerifyIR);
  EXPECT_EQ(Opts.OptimizationLevel, 0u);
  EXPECT_TRUE(Opts.InputFile.empty());
  EXPECT_TRUE(Opts.OutputFile.empty());
  EXPECT_TRUE(Opts.TargetTriple.empty());
  EXPECT_TRUE(Opts.Language.empty());
  EXPECT_TRUE(Opts.IncludePaths.empty());
  EXPECT_TRUE(Opts.MacroDefinitions.empty());
}

/// V2: Subclass is instantiable.
TEST(FrontendBaseTest, SubclassInstantiation) {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.InputFile = "hello.test";
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);

  EXPECT_EQ(TF.getName(), "test");
  EXPECT_EQ(TF.getLanguage(), "test");
  EXPECT_TRUE(TF.canHandle("hello.test"));
  EXPECT_FALSE(TF.canHandle("hello.cpp"));
}

/// compile() returns a valid IRModule.
TEST(FrontendBaseTest, CompileProducesModule) {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.InputFile = "hello.test";
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);

  IRTypeContext TypeCtx;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");

  auto Mod = TF.compile("hello.test", TypeCtx, *Layout);
  ASSERT_NE(Mod, nullptr);
  EXPECT_EQ(Mod->getName(), "hello.test");
  EXPECT_EQ(Mod->getTargetTriple(), "x86_64-unknown-linux-gnu");
}

/// compile() with valid triple works correctly.
TEST(FrontendBaseTest, CompileWithValidTriple) {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);
  IRTypeContext TypeCtx;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");

  auto Mod = TF.compile("test.test", TypeCtx, *Layout);
  ASSERT_NE(Mod, nullptr);
}

/// Options accessor returns correct values.
TEST(FrontendBaseTest, OptionsAccess) {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.InputFile = "input.bt";
  Opts.OutputFile = "output.btir";
  Opts.TargetTriple = "aarch64-unknown-linux-gnu";
  Opts.Language = "bt";
  Opts.EmitIR = true;
  Opts.OptimizationLevel = 2;
  Opts.IncludePaths.push_back("/usr/include");
  Opts.MacroDefinitions.push_back("DEBUG=1");

  TestFrontend TF(Opts, Diags);

  const auto& RetOpts = TF.getOptions();
  EXPECT_EQ(RetOpts.InputFile, "input.bt");
  EXPECT_EQ(RetOpts.OutputFile, "output.btir");
  EXPECT_EQ(RetOpts.TargetTriple, "aarch64-unknown-linux-gnu");
  EXPECT_EQ(RetOpts.Language, "bt");
  EXPECT_TRUE(RetOpts.EmitIR);
  EXPECT_EQ(RetOpts.OptimizationLevel, 2u);
  ASSERT_EQ(RetOpts.IncludePaths.size(), 1u);
  EXPECT_EQ(RetOpts.IncludePaths[0], "/usr/include");
  ASSERT_EQ(RetOpts.MacroDefinitions.size(), 1u);
  EXPECT_EQ(RetOpts.MacroDefinitions[0], "DEBUG=1");
}

/// Diagnostics accessor returns correct reference.
TEST(FrontendBaseTest, DiagnosticsAccess) {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;

  TestFrontend TF(Opts, Diags);
  EXPECT_EQ(&TF.getDiagnostics(), &Diags);
}

/// FrontendBase is non-copyable but movable.
TEST(FrontendBaseTest, NonCopyableButMovable) {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);
  TestFrontend TF2 = std::move(TF);
  EXPECT_EQ(TF2.getName(), "test");
}

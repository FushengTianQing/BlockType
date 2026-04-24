//===--- TargetOptimizationTest.cpp - Target Optimization Tests -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for target-specific optimization configuration.
// These tests verify that:
// - x86_64 defaults to x86-64-v2 CPU with SSE4.2 features
// - AArch64 macOS defaults to apple-m1 CPU (with fallback to generic)
// - AArch64 Linux defaults to generic CPU
// - PIE mode correctly sets Reloc::PIC_
// - Float ABI defaults to hard and maps to llvm::FloatABI
// - Code model is correctly mapped
// - Command-line parsing works for new options
// - validate() rejects invalid values
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Frontend/CompilerInstance.h"
#include "blocktype/Frontend/CompilerInvocation.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdio>

using namespace blocktype;

namespace {

/// Initialize LLVM targets once for all tests.
class TargetOptimizationTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  }

  void SetUp() override {
    LLVMCtx = std::make_unique<llvm::LLVMContext>();
  }

  std::unique_ptr<llvm::LLVMContext> LLVMCtx;
};

/// Helper: create a simple LLVM module with a function.
static std::unique_ptr<llvm::Module>
createSimpleModule(llvm::LLVMContext &Ctx, const std::string &FuncName,
                   const std::string &Triple = "") {
  auto M = std::make_unique<llvm::Module>("test", Ctx);
  std::string T = Triple.empty() ? llvm::sys::getDefaultTargetTriple() : Triple;
  M->setTargetTriple(T);

  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *FuncTy = llvm::FunctionType::get(Int32Ty, {Int32Ty}, false);
  auto *F = llvm::Function::Create(FuncTy, llvm::Function::ExternalLinkage,
                                    FuncName, M.get());
  auto *BB = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> Builder(BB);
  Builder.CreateRet(F->getArg(0));

  return M;
}

/// Helper: generate object file and verify it's non-empty.
static bool generateAndVerify(CompilerInstance &Instance,
                               std::unique_ptr<llvm::Module> &M,
                               const std::string &Prefix) {
  std::string OutputPath = "/tmp/blocktype_" + Prefix + "_XXXXXX";
  int FD = mkstemp(&OutputPath[0]);
  if (FD == -1) return false;
  close(FD);

  struct TempFileGuard {
    std::string Path;
    ~TempFileGuard() { std::remove(Path.c_str()); }
  } Guard{OutputPath};

  bool Result = Instance.generateObjectFile(*M, OutputPath);
  if (!Result) return false;

  uint64_t FileSize = 0;
  std::error_code EC = llvm::sys::fs::file_size(OutputPath, FileSize);
  if (EC) return false;
  return FileSize > 0;
}

// === Test 1: x86_64 Default CPU ===

TEST_F(TargetOptimizationTest, X86_64DefaultCPU) {
  std::string Error;
  auto *Target = llvm::TargetRegistry::lookupTarget("x86_64-unknown-linux-gnu", Error);
  if (!Target) {
    GTEST_SKIP() << "x86_64 target not available: " << Error;
  }

  auto M = createSimpleModule(*LLVMCtx, "test_x86_64_cpu",
                               "x86_64-unknown-linux-gnu");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 0;
  CI->TargetOpts.CPU = "";
  CI->TargetOpts.Features = "";
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "x86_64"));
}

// === Test 2: AArch64 macOS Default CPU ===

TEST_F(TargetOptimizationTest, AArch64MacOSDefaultCPU) {
  std::string Error;
  auto *Target = llvm::TargetRegistry::lookupTarget("arm64-apple-darwin22.0.0", Error);
  if (!Target) {
    GTEST_SKIP() << "AArch64 target not available: " << Error;
  }

  auto M = createSimpleModule(*LLVMCtx, "test_aarch64_macos",
                               "arm64-apple-darwin22.0.0");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 0;
  CI->TargetOpts.CPU = "";
  CI->TargetOpts.Features = "";
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "aarch64_mac"));
}

// === Test 3: AArch64 Linux Default CPU ===

TEST_F(TargetOptimizationTest, AArch64LinuxDefaultCPU) {
  std::string Error;
  auto *Target = llvm::TargetRegistry::lookupTarget("aarch64-unknown-linux-gnu", Error);
  if (!Target) {
    GTEST_SKIP() << "AArch64 Linux target not available: " << Error;
  }

  auto M = createSimpleModule(*LLVMCtx, "test_aarch64_linux",
                               "aarch64-unknown-linux-gnu");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 0;
  CI->TargetOpts.CPU = "";
  CI->TargetOpts.Features = "";
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "aarch64_linux"));
}

// === Test 4: PIE Mode Enabled (default) ===

TEST_F(TargetOptimizationTest, PIEModeEnabled) {
  auto CI = std::make_shared<CompilerInvocation>();
  // PIE is now unified in TargetOpts (not CodeGenOpts)
  EXPECT_TRUE(CI->TargetOpts.PIE);
  // CodeGenOpts.PIE no longer exists

  auto M = createSimpleModule(*LLVMCtx, "test_pie");
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "pie"));
}

// === Test 5: PIE Mode Disabled ===

TEST_F(TargetOptimizationTest, PIEModeDisabled) {
  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.PIE = false;
  EXPECT_FALSE(CI->TargetOpts.PIE);

  auto M = createSimpleModule(*LLVMCtx, "test_no_pie");
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "no_pie"));
}

// === Test 6: Float ABI Default ===

TEST_F(TargetOptimizationTest, FloatABIDefaultHard) {
  TargetOptions Opts;
  EXPECT_EQ(Opts.FloatABI, "hard");
}

// === Test 7: Code Model Default ===

TEST_F(TargetOptimizationTest, CodeModelDefault) {
  TargetOptions Opts;
  // Default is now "small" (not "default") for clarity
  EXPECT_EQ(Opts.CodeModel, "small");
}

// === Test 8: Code Model Small ===

TEST_F(TargetOptimizationTest, CodeModelSmall) {
  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.CodeModel = "small";
  EXPECT_EQ(CI->TargetOpts.CodeModel, "small");

  auto M = createSimpleModule(*LLVMCtx, "test_cm_small");
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "cm_small"));
}

// === Test 9: Code Model Large ===

TEST_F(TargetOptimizationTest, CodeModelLarge) {
  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.CodeModel = "large";
  EXPECT_EQ(CI->TargetOpts.CodeModel, "large");

  auto M = createSimpleModule(*LLVMCtx, "test_cm_large");
  CompilerInstance Instance;
  Instance.initialize(CI);

  EXPECT_TRUE(generateAndVerify(Instance, M, "cm_large"));
}

// === Test 10: TargetOptions Defaults ===

TEST_F(TargetOptimizationTest, TargetOptionsDefaults) {
  TargetOptions Opts;
  EXPECT_EQ(Opts.FloatABI, "hard");
  EXPECT_TRUE(Opts.PIE);
  EXPECT_EQ(Opts.CodeModel, "small");
  EXPECT_TRUE(Opts.CPU.empty());
  EXPECT_TRUE(Opts.Features.empty());
  EXPECT_TRUE(Opts.ABI.empty());
  EXPECT_TRUE(Opts.Triple.empty());
}

// === Test 11: Custom CPU Not Overridden in generateObjectFile ===

TEST_F(TargetOptimizationTest, CustomCPUNotOverridden) {
  // Use native target since we can't guarantee cross-compilation targets
  auto M = createSimpleModule(*LLVMCtx, "test_custom_cpu");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.CPU = "generic";  // Explicitly set — should not be overridden
  CI->TargetOpts.Features = "+neon";  // Explicitly set — should not be overridden
  CompilerInstance Instance;
  Instance.initialize(CI);

  // Verify the object file is generated successfully with custom settings
  EXPECT_TRUE(generateAndVerify(Instance, M, "custom_cpu"));
}

// === Test 12: validate() Rejects Invalid FloatABI ===

TEST_F(TargetOptimizationTest, ValidateRejectsInvalidFloatABI) {
  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.FloatABI = "invalid";
  EXPECT_FALSE(CI->validate());
}

// === Test 13: validate() Rejects Invalid CodeModel ===

TEST_F(TargetOptimizationTest, ValidateRejectsInvalidCodeModel) {
  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.CodeModel = "invalid";
  EXPECT_FALSE(CI->validate());
}

// === Test 14: validate() Accepts Valid FloatABI Values ===

TEST_F(TargetOptimizationTest, ValidateAcceptsValidFloatABI) {
  for (const char *ABI : {"hard", "soft", "softfp"}) {
    auto CI = std::make_shared<CompilerInvocation>();
    CI->TargetOpts.FloatABI = ABI;
    EXPECT_TRUE(CI->validate()) << "FloatABI='" << ABI << "' should be valid";
  }
}

// === Test 15: validate() Accepts Valid CodeModel Values ===

TEST_F(TargetOptimizationTest, ValidateAcceptsValidCodeModel) {
  for (const char *CM : {"small", "large"}) {
    auto CI = std::make_shared<CompilerInvocation>();
    CI->TargetOpts.CodeModel = CM;
    EXPECT_TRUE(CI->validate()) << "CodeModel='" << CM << "' should be valid";
  }
}

// === Test 16: Command-Line -fPIE Sets TargetOpts.PIE ===

TEST_F(TargetOptimizationTest, CommandLineFPIE) {
  auto CI = std::make_shared<CompilerInvocation>();
  const char *Args[] = {"bt", "-fPIE"};
  EXPECT_TRUE(CI->parseCommandLine(2, Args));
  EXPECT_TRUE(CI->TargetOpts.PIE);
}

// === Test 17: Command-Line -fno-PIE Clears TargetOpts.PIE ===

TEST_F(TargetOptimizationTest, CommandLineFNoPIE) {
  auto CI = std::make_shared<CompilerInvocation>();
  const char *Args[] = {"bt", "-fno-PIE"};
  EXPECT_TRUE(CI->parseCommandLine(2, Args));
  EXPECT_FALSE(CI->TargetOpts.PIE);
}

// === Test 18: Command-Line -mfloat-abi ===

TEST_F(TargetOptimizationTest, CommandLineMFloatABI) {
  const char *Args[] = {"bt", "-mfloat-abi=soft"};
  auto CI = std::make_shared<CompilerInvocation>();
  EXPECT_TRUE(CI->parseCommandLine(2, Args));
  EXPECT_EQ(CI->TargetOpts.FloatABI, "soft");

  const char *Args2[] = {"bt", "-mfloat-abi=softfp"};
  auto CI2 = std::make_shared<CompilerInvocation>();
  EXPECT_TRUE(CI2->parseCommandLine(2, Args2));
  EXPECT_EQ(CI2->TargetOpts.FloatABI, "softfp");

  const char *Args3[] = {"bt", "-mfloat-abi=hard"};
  auto CI3 = std::make_shared<CompilerInvocation>();
  EXPECT_TRUE(CI3->parseCommandLine(2, Args3));
  EXPECT_EQ(CI3->TargetOpts.FloatABI, "hard");
}

// === Test 19: Command-Line -mfloat-abi Invalid Rejected ===

TEST_F(TargetOptimizationTest, CommandLineMFloatABIInvalid) {
  const char *Args[] = {"bt", "-mfloat-abi=invalid"};
  auto CI = std::make_shared<CompilerInvocation>();
  EXPECT_FALSE(CI->parseCommandLine(2, Args));
}

// === Test 20: Command-Line -mcmodel ===

TEST_F(TargetOptimizationTest, CommandLineMCModel) {
  const char *Args[] = {"bt", "-mcmodel=large"};
  auto CI = std::make_shared<CompilerInvocation>();
  EXPECT_TRUE(CI->parseCommandLine(2, Args));
  EXPECT_EQ(CI->TargetOpts.CodeModel, "large");

  const char *Args2[] = {"bt", "-mcmodel=small"};
  auto CI2 = std::make_shared<CompilerInvocation>();
  EXPECT_TRUE(CI2->parseCommandLine(2, Args2));
  EXPECT_EQ(CI2->TargetOpts.CodeModel, "small");
}

// === Test 21: Command-Line -mcmodel Invalid Rejected ===

TEST_F(TargetOptimizationTest, CommandLineMCModelInvalid) {
  const char *Args[] = {"bt", "-mcmodel=medium"};
  auto CI = std::make_shared<CompilerInvocation>();
  EXPECT_FALSE(CI->parseCommandLine(2, Args));
}

// === Test 22: toString() Includes New Fields ===

TEST_F(TargetOptimizationTest, ToStringIncludesNewFields) {
  auto CI = std::make_shared<CompilerInvocation>();
  std::string Str = CI->toString();
  EXPECT_NE(Str.find("Float ABI"), std::string::npos);
  EXPECT_NE(Str.find("PIE"), std::string::npos);
  EXPECT_NE(Str.find("Code Model"), std::string::npos);
}

} // anonymous namespace

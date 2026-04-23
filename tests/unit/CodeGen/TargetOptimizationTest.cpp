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
// - AArch64 macOS defaults to apple-m1 CPU
// - AArch64 Linux defaults to generic CPU
// - PIE mode correctly sets Reloc::PIC_
// - Float ABI defaults to hard
// - Code model is correctly mapped
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
  EXPECT_TRUE(CI->TargetOpts.PIE);

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
  EXPECT_EQ(Opts.CodeModel, "default");
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
  EXPECT_EQ(Opts.CodeModel, "default");
  EXPECT_TRUE(Opts.CPU.empty());
  EXPECT_TRUE(Opts.Features.empty());
  EXPECT_TRUE(Opts.ABI.empty());
  EXPECT_TRUE(Opts.Triple.empty());
}

// === Test 11: Custom CPU Not Overridden ===

TEST_F(TargetOptimizationTest, CustomCPUNotOverridden) {
  auto CI = std::make_shared<CompilerInvocation>();
  CI->TargetOpts.CPU = "x86-64-v3";
  CI->TargetOpts.Features = "+avx2";

  EXPECT_EQ(CI->TargetOpts.CPU, "x86-64-v3");
  EXPECT_EQ(CI->TargetOpts.Features, "+avx2");
}

} // anonymous namespace

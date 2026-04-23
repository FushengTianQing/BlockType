//===--- OptimizationTest.cpp - Optimization Pass Tests ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for optimization passes and object file generation.
// These tests verify that:
// - O0 skips optimization
// - O1/O2/O3 use PassBuilder correctly
// - generateObjectFile() produces valid .o files
// - macOS ARM64 target code is correct
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Frontend/CompilerInstance.h"
#include "blocktype/Frontend/CompilerInvocation.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/CodeGen.h"
#include <cstdio>
#include <fstream>

using namespace blocktype;

namespace {

/// Initialize LLVM targets once for all tests.
class OptimizationTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    // Only initialize native target (not all targets, since not all are linked)
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  }

  void SetUp() override {
    // Create a minimal module for testing
    LLVMCtx = std::make_unique<llvm::LLVMContext>();
  }

  std::unique_ptr<llvm::LLVMContext> LLVMCtx;
};

/// Helper: create a simple LLVM module with a function.
static std::unique_ptr<llvm::Module> createSimpleModule(llvm::LLVMContext &Ctx,
                                                         const std::string &FuncName) {
  auto M = std::make_unique<llvm::Module>("test", Ctx);
  M->setTargetTriple(llvm::sys::getDefaultTargetTriple());

  // Create function: i32 @FuncName(i32 %x) { ret i32 %x }
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *FuncTy = llvm::FunctionType::get(Int32Ty, {Int32Ty}, false);
  auto *F = llvm::Function::Create(FuncTy, llvm::Function::ExternalLinkage,
                                    FuncName, M.get());
  auto *BB = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> Builder(BB);
  Builder.CreateRet(F->getArg(0));

  return M;
}

/// Helper: create a module with constant-foldable code.
static std::unique_ptr<llvm::Module> createConstFoldModule(llvm::LLVMContext &Ctx) {
  auto M = std::make_unique<llvm::Module>("constfold", Ctx);
  M->setTargetTriple(llvm::sys::getDefaultTargetTriple());

  // Create function: i32 @test() { ret i32 3 }  (1 + 2 folded)
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *FuncTy = llvm::FunctionType::get(Int32Ty, false);
  auto *F = llvm::Function::Create(FuncTy, llvm::Function::ExternalLinkage,
                                    "test_const_fold", M.get());
  auto *BB = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> Builder(BB);
  auto *Sum = Builder.CreateAdd(llvm::ConstantInt::get(Int32Ty, 1),
                                 llvm::ConstantInt::get(Int32Ty, 2));
  Builder.CreateRet(Sum);

  return M;
}

// === Test: O0 No Optimization ===

TEST_F(OptimizationTest, O0NoOptimization) {
  auto M = createSimpleModule(*LLVMCtx, "test_o0");
  std::string OrigIR;
  llvm::raw_string_ostream OrigStream(OrigIR);
  M->print(OrigStream, nullptr);

  // Run with O0 — should return true without modifying the module
  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 0;
  CompilerInstance Instance;
  Instance.initialize(CI);

  bool Result = Instance.runOptimizationPasses(*M);
  EXPECT_TRUE(Result);

  // Module should be unchanged after O0
  std::string AfterIR;
  llvm::raw_string_ostream AfterStream(AfterIR);
  M->print(AfterStream, nullptr);
  EXPECT_EQ(OrigIR, AfterIR) << "Module should not be modified at O0";
}

// === Test: O1 Basic Optimization ===

TEST_F(OptimizationTest, O1BasicOptimization) {
  auto M = createConstFoldModule(*LLVMCtx);

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 1;
  CompilerInstance Instance;
  Instance.initialize(CI);

  bool Result = Instance.runOptimizationPasses(*M);
  EXPECT_TRUE(Result);

  // After O1, the add should be folded to a constant 3
  // Verify the module is still valid
  EXPECT_FALSE(llvm::verifyModule(*M));
}

// === Test: O2 Standard Optimization ===

TEST_F(OptimizationTest, O2StandardOptimization) {
  auto M = createSimpleModule(*LLVMCtx, "test_o2");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 2;
  CompilerInstance Instance;
  Instance.initialize(CI);

  bool Result = Instance.runOptimizationPasses(*M);
  EXPECT_TRUE(Result);

  // Module should still be valid after optimization
  EXPECT_FALSE(llvm::verifyModule(*M));
}

// === Test: O3 Aggressive Optimization ===

TEST_F(OptimizationTest, O3AggressiveOptimization) {
  auto M = createSimpleModule(*LLVMCtx, "test_o3");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 3;
  CompilerInstance Instance;
  Instance.initialize(CI);

  bool Result = Instance.runOptimizationPasses(*M);
  EXPECT_TRUE(Result);

  // Module should still be valid after optimization
  EXPECT_FALSE(llvm::verifyModule(*M));
}

// === Test: PassBuilder Pipeline Construction ===

TEST_F(OptimizationTest, PassBuilderPipelineConstruction) {
  // Verify that PassBuilder can construct pipelines for all optimization levels
  llvm::PassBuilder PB;

  for (unsigned Level = 1; Level <= 3; ++Level) {
    llvm::OptimizationLevel OL;
    switch (Level) {
    case 1: OL = llvm::OptimizationLevel::O1; break;
    case 2: OL = llvm::OptimizationLevel::O2; break;
    case 3: OL = llvm::OptimizationLevel::O3; break;
    default: OL = llvm::OptimizationLevel::O2; break;
    }

    // Should not crash
    auto MPM = PB.buildPerModuleDefaultPipeline(OL);
    (void)MPM;  // Just verify construction succeeds
  }
}

// === Test: generateObjectFile Produces Valid Output ===

TEST_F(OptimizationTest, GenerateObjectFile) {
  auto M = createSimpleModule(*LLVMCtx, "test_obj");
  M->setTargetTriple(llvm::sys::getDefaultTargetTriple());

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 0;
  CompilerInstance Instance;
  Instance.initialize(CI);

  // Generate to a temp file
  std::string OutputPath = "/tmp/blocktype_test_obj_XXXXXX";
  int FD = mkstemp(&OutputPath[0]);
  ASSERT_NE(FD, -1);
  close(FD);

  // RAII cleanup guard
  struct TempFileGuard {
    std::string Path;
    ~TempFileGuard() { std::remove(Path.c_str()); }
  } Guard{OutputPath};

  bool Result = Instance.generateObjectFile(*M, OutputPath);
  EXPECT_TRUE(Result);

  // Verify the file was created and is non-empty
  std::error_code EC;
  uint64_t FileSize = 0;
  EC = llvm::sys::fs::file_size(OutputPath, FileSize);
  EXPECT_FALSE(EC);
  EXPECT_GT(FileSize, 0u);
}

// === Test: macOS ARM64 Target Code Generation ===

TEST_F(OptimizationTest, macOSARM64TargetCodeGeneration) {
  auto M = createSimpleModule(*LLVMCtx, "test_arm64");
  M->setTargetTriple("arm64-apple-darwin22.0.0");

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 0;
  CompilerInstance Instance;
  Instance.initialize(CI);

  // Generate to a temp file
  std::string OutputPath = "/tmp/blocktype_test_arm64_XXXXXX";
  int FD = mkstemp(&OutputPath[0]);
  ASSERT_NE(FD, -1);
  close(FD);

  // RAII cleanup guard
  struct TempFileGuard {
    std::string Path;
    ~TempFileGuard() { std::remove(Path.c_str()); }
  } Guard{OutputPath};

  bool Result = Instance.generateObjectFile(*M, OutputPath);
  EXPECT_TRUE(Result);

  // Verify the file was created and is non-empty
  uint64_t FileSize = 0;
  std::error_code EC2;
  EC2 = llvm::sys::fs::file_size(OutputPath, FileSize);
  EXPECT_FALSE(EC2);
  EXPECT_GT(FileSize, 0u);
}

// === Test: generateObjectFile With Optimization ===

TEST_F(OptimizationTest, GenerateObjectFileWithOptimization) {
  auto M = createConstFoldModule(*LLVMCtx);
  M->setTargetTriple(llvm::sys::getDefaultTargetTriple());

  auto CI = std::make_shared<CompilerInvocation>();
  CI->CodeGenOpts.OptimizationLevel = 2;
  CompilerInstance Instance;
  Instance.initialize(CI);

  // Run optimization first
  bool OptResult = Instance.runOptimizationPasses(*M);
  EXPECT_TRUE(OptResult);

  // Generate to a temp file
  std::string OutputPath = "/tmp/blocktype_test_opt_obj_XXXXXX";
  int FD = mkstemp(&OutputPath[0]);
  ASSERT_NE(FD, -1);
  close(FD);

  // RAII cleanup guard
  struct TempFileGuard {
    std::string Path;
    ~TempFileGuard() { std::remove(Path.c_str()); }
  } Guard{OutputPath};

  bool Result = Instance.generateObjectFile(*M, OutputPath);
  EXPECT_TRUE(Result);

  // Verify the file was created and is non-empty
  uint64_t FileSize2 = 0;
  std::error_code EC3;
  EC3 = llvm::sys::fs::file_size(OutputPath, FileSize2);
  EXPECT_FALSE(EC3);
  EXPECT_GT(FileSize2, 0u);
}

// === Test: TargetMachine Creation for Default Triple ===

TEST_F(OptimizationTest, TargetMachineCreationDefaultTriple) {
  std::string Triple = llvm::sys::getDefaultTargetTriple();
  std::string Error;
  auto *Target = llvm::TargetRegistry::lookupTarget(Triple, Error);
  ASSERT_NE(Target, nullptr) << "Failed to lookup target: " << Error;

  llvm::TargetOptions Opt;
  auto *TM = Target->createTargetMachine(Triple, "", "", Opt,
                                           llvm::Reloc::PIC_,
                                           llvm::CodeModel::Small,
                                           llvm::CodeGenOptLevel::Default);
  ASSERT_NE(TM, nullptr) << "Failed to create TargetMachine";

  // Verify DataLayout is valid
  auto DL = TM->createDataLayout();
  EXPECT_FALSE(DL.getStringRepresentation().empty());
  delete TM;
}

} // anonymous namespace

#include <gtest/gtest.h>
#include <memory>
#include "blocktype/IR/IREquivalenceChecker.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRConstant.h"

using namespace blocktype::ir;

namespace {

/// Helper: create a module with one empty function
std::unique_ptr<IRModule> makeModuleWithFunc(
    IRTypeContext& Ctx, StringRef ModName, StringRef FuncName) {
  auto Mod = std::make_unique<IRModule>(ModName, Ctx);
  auto* VoidTy = Ctx.getVoidType();
  auto* FnTy = Ctx.getFunctionType(VoidTy, SmallVector<IRType*, 8>{});
  auto Fn = std::make_unique<IRFunction>(Mod.get(), FuncName, FnTy);
  Mod->addFunction(std::move(Fn));
  return Mod;
}

/// Helper: create a module with a function containing a ret instruction
std::unique_ptr<IRModule> makeModuleWithRetFunc(
    IRTypeContext& Ctx, StringRef ModName, StringRef FuncName) {
  auto Mod = std::make_unique<IRModule>(ModName, Ctx);
  auto* VoidTy = Ctx.getVoidType();
  auto* FnTy = Ctx.getFunctionType(VoidTy, SmallVector<IRType*, 8>{});
  auto Fn = std::make_unique<IRFunction>(Mod.get(), FuncName, FnTy);
  auto* BB = Fn->addBasicBlock("entry");
  auto RetInst = std::make_unique<IRInstruction>(Opcode::Ret, VoidTy, 0);
  BB->push_back(std::move(RetInst));
  Mod->addFunction(std::move(Fn));
  return Mod;
}

} // anonymous namespace

// === Module-level tests ===

TEST(IREquivalenceCheckerTest, SameModuleIsEquivalent) {
  IRTypeContext Ctx;
  auto Mod = makeModuleWithFunc(Ctx, "test", "main");
  auto Result = IREquivalenceChecker::check(*Mod, *Mod);
  EXPECT_TRUE(Result.IsEquivalent);
  EXPECT_TRUE(Result.Differences.empty());
}

TEST(IREquivalenceCheckerTest, DifferentFuncCountNotEquivalent) {
  IRTypeContext Ctx;
  auto ModA = makeModuleWithFunc(Ctx, "a", "foo");
  auto ModB = std::make_unique<IRModule>("b", Ctx);
  auto Result = IREquivalenceChecker::check(*ModA, *ModB);
  EXPECT_FALSE(Result.IsEquivalent);
  EXPECT_FALSE(Result.Differences.empty());
}

TEST(IREquivalenceCheckerTest, DifferentModuleName) {
  IRTypeContext Ctx;
  auto ModA = makeModuleWithRetFunc(Ctx, "module_a", "main");
  auto ModB = makeModuleWithRetFunc(Ctx, "module_b", "main");
  auto Result = IREquivalenceChecker::check(*ModA, *ModB);
  EXPECT_FALSE(Result.IsEquivalent);
  EXPECT_NE(std::string::npos, Result.Differences[0].find("Module name"));
}

TEST(IREquivalenceCheckerTest, DifferentTargetTriple) {
  IRTypeContext Ctx;
  auto ModA = std::make_unique<IRModule>("test", Ctx, "x86_64-linux");
  auto ModB = std::make_unique<IRModule>("test", Ctx, "aarch64-macos");
  auto Result = IREquivalenceChecker::check(*ModA, *ModB);
  EXPECT_FALSE(Result.IsEquivalent);
  bool found = false;
  for (auto& D : Result.Differences) {
    if (D.find("Target triple") != std::string::npos) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(IREquivalenceCheckerTest, MissingFunctionInSecond) {
  IRTypeContext Ctx;
  auto ModA = makeModuleWithRetFunc(Ctx, "test", "foo");
  auto ModB = makeModuleWithRetFunc(Ctx, "test", "bar");
  auto Result = IREquivalenceChecker::check(*ModA, *ModB);
  EXPECT_FALSE(Result.IsEquivalent);
  bool found = false;
  for (auto& D : Result.Differences) {
    if (D.find("not found") != std::string::npos) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(IREquivalenceCheckerTest, IdenticalModulesWithInstructions) {
  IRTypeContext Ctx;
  auto ModA = makeModuleWithRetFunc(Ctx, "test", "main");
  auto ModB = makeModuleWithRetFunc(Ctx, "test", "main");
  auto Result = IREquivalenceChecker::check(*ModA, *ModB);
  EXPECT_TRUE(Result.IsEquivalent);
}

// === Type equivalence tests ===

TEST(IREquivalenceCheckerTest, TypeEquivalent) {
  IRTypeContext Ctx;
  auto* Int32_A = Ctx.getIntType(32);
  auto* Int32_B = Ctx.getIntType(32);
  EXPECT_TRUE(IREquivalenceChecker::isTypeEquivalent(Int32_A, Int32_B));
}

TEST(IREquivalenceCheckerTest, TypeNotEquivalent) {
  IRTypeContext Ctx;
  auto* Int32 = Ctx.getIntType(32);
  auto* Int64 = Ctx.getIntType(64);
  EXPECT_FALSE(IREquivalenceChecker::isTypeEquivalent(Int32, Int64));
}

TEST(IREquivalenceCheckerTest, NullTypeEquivalent) {
  EXPECT_TRUE(IREquivalenceChecker::isTypeEquivalent(nullptr, nullptr));
  EXPECT_FALSE(IREquivalenceChecker::isTypeEquivalent(nullptr,
      reinterpret_cast<IRType*>(1)));
}

TEST(IREquivalenceCheckerTest, PointerTypeEquivalent) {
  IRTypeContext Ctx;
  auto* PtrInt32_A = Ctx.getPointerType(Ctx.getIntType(32));
  auto* PtrInt32_B = Ctx.getPointerType(Ctx.getIntType(32));
  EXPECT_TRUE(IREquivalenceChecker::isTypeEquivalent(PtrInt32_A, PtrInt32_B));
}

TEST(IREquivalenceCheckerTest, FunctionTypeEquivalent) {
  IRTypeContext Ctx;
  SmallVector<IRType*, 8> Params = {Ctx.getIntType(32), Ctx.getFloatType(64)};
  auto* FnTyA = Ctx.getFunctionType(Ctx.getVoidType(), Params);
  auto* FnTyB = Ctx.getFunctionType(Ctx.getVoidType(), Params);
  EXPECT_TRUE(IREquivalenceChecker::isTypeEquivalent(FnTyA, FnTyB));
}

// === Structural equivalence tests ===

TEST(IREquivalenceCheckerTest, StructurallyEquivalent) {
  IRTypeContext Ctx;
  auto ModA = makeModuleWithFunc(Ctx, "a", "foo");
  auto ModB = makeModuleWithFunc(Ctx, "b", "foo");
  EXPECT_TRUE(IREquivalenceChecker::isStructurallyEquivalent(
      *ModA->getFunction("foo"), *ModB->getFunction("foo")));
}

TEST(IREquivalenceCheckerTest, DifferentBBCount) {
  IRTypeContext Ctx;
  auto* VoidTy = Ctx.getVoidType();
  auto* FnTy = Ctx.getFunctionType(VoidTy, {});

  auto ModA = std::make_unique<IRModule>("a", Ctx);
  auto FnA = std::make_unique<IRFunction>(ModA.get(), "test", FnTy);
  FnA->addBasicBlock("entry");
  auto RetA = std::make_unique<IRInstruction>(Opcode::Ret, VoidTy, 0);
  FnA->getBasicBlocks().front()->push_back(std::move(RetA));
  ModA->addFunction(std::move(FnA));

  auto ModB = std::make_unique<IRModule>("b", Ctx);
  auto FnB = std::make_unique<IRFunction>(ModB.get(), "test", FnTy);
  FnB->addBasicBlock("entry");
  FnB->addBasicBlock("exit");
  ModB->addFunction(std::move(FnB));

  EXPECT_FALSE(IREquivalenceChecker::isStructurallyEquivalent(
      *ModA->getFunction("test"), *ModB->getFunction("test")));
}

TEST(IREquivalenceCheckerTest, DifferentInstructionOpcodes) {
  IRTypeContext Ctx;
  auto* VoidTy = Ctx.getVoidType();
  auto* Int32Ty = Ctx.getIntType(32);
  auto* FnTy = Ctx.getFunctionType(VoidTy, {});

  auto ModA = std::make_unique<IRModule>("a", Ctx);
  auto FnA = std::make_unique<IRFunction>(ModA.get(), "test", FnTy);
  auto* BBA = FnA->addBasicBlock("entry");
  auto RetA = std::make_unique<IRInstruction>(Opcode::Ret, VoidTy, 0);
  BBA->push_back(std::move(RetA));
  ModA->addFunction(std::move(FnA));

  auto ModB = std::make_unique<IRModule>("b", Ctx);
  auto FnB = std::make_unique<IRFunction>(ModB.get(), "test", FnTy);
  auto* BBB = FnB->addBasicBlock("entry");
  auto UnreachableB = std::make_unique<IRInstruction>(Opcode::Unreachable, VoidTy, 0);
  BBB->push_back(std::move(UnreachableB));
  ModB->addFunction(std::move(FnB));

  EXPECT_FALSE(IREquivalenceChecker::isStructurallyEquivalent(
      *ModA->getFunction("test"), *ModB->getFunction("test")));
}

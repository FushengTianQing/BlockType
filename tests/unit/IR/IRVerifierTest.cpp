#include <gtest/gtest.h>

#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRVerifier.h"

using namespace blocktype;
using namespace blocktype::ir;

// ============================================================================
// V1: Legal IRModule passes verification
// ============================================================================

TEST(VerifierTest, LegalModulePasses) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("test", TCtx, "x86_64-unknown-linux-gnu");

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* F = M.getOrInsertFunction("add", FTy);
  auto* Entry = F->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* V1 = Builder.getInt32(10);
  auto* V2 = Builder.getInt32(20);
  auto* Sum = Builder.createAdd(V1, V2, "sum");
  Builder.createRet(Sum);

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_TRUE(OK) << "Legal module should pass verification";
  EXPECT_TRUE(Errors.empty()) << "No errors expected, got " << Errors.size();
}

// ============================================================================
// V2: Module with OpaqueType fails
// ============================================================================

TEST(VerifierTest, OpaqueTypeFails) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("opaque_test", TCtx);

  auto* Opaque = TCtx.getOpaqueType("unresolved");
  auto* FTy = TCtx.getFunctionType(Opaque, {});
  auto* F = M.getOrInsertFunction("bad_func", FTy);
  auto* Entry = F->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  Builder.createRetVoid();

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_FALSE(OK) << "Module with OpaqueType should fail";
  bool hasOpaqueError = false;
  for (auto& E : Errors) {
    if (E.Message.find("opaque") != std::string::npos ||
        E.Message.find("OpaqueType") != std::string::npos) {
      hasOpaqueError = true;
    }
  }
  EXPECT_TRUE(hasOpaqueError) << "Expected OpaqueType error";
}

// ============================================================================
// V3: BB without terminator fails
// ============================================================================

TEST(VerifierTest, NoTerminatorFails) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("no_term", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* F = M.getOrInsertFunction("no_terminator", FTy);
  auto* Entry = F->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  Builder.createAdd(Builder.getInt32(1), Builder.getInt32(2), "dead");
  // No terminator

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_FALSE(OK) << "BB without terminator should fail";
}

// ============================================================================
// V4: Type mismatch in binary op fails
// ============================================================================

TEST(VerifierTest, BinaryOpTypeMismatchFails) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("type_mismatch", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* F = M.getOrInsertFunction("bad_binop", FTy);
  auto* Entry = F->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* V1 = Builder.getInt32(1);
  auto* V2 = Builder.getInt64(2);
  auto* BadAdd = Builder.createAdd(V1, V2, "bad");
  Builder.createRetVoid();

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_FALSE(OK) << "Type mismatch in binary op should fail";
}

// ============================================================================
// V5: Static verify() interface
// ============================================================================

TEST(VerifierTest, StaticVerifyInterface) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("static_test", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* F = M.getOrInsertFunction("add", FTy);
  auto* Entry = F->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* V1 = Builder.getInt32(10);
  auto* V2 = Builder.getInt32(20);
  auto* Sum = Builder.createAdd(V1, V2, "sum");
  Builder.createRet(Sum);

  bool OK = VerifierPass::verify(M);
  EXPECT_TRUE(OK);

  // Also test with Diagnostics parameter
  SmallVector<VerificationDiagnostic, 32> Errors;
  OK = VerifierPass::verify(M, &Errors);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(Errors.empty());
}

// ============================================================================
// V7: Call with wrong argument count fails
// ============================================================================

TEST(VerifierTest, CallArgMismatchFails) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("call_mismatch", TCtx);

  auto* AddTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* Callee = M.getOrInsertFunction("add", AddTy);
  auto* CallerTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* Caller = M.getOrInsertFunction("caller", CallerTy);
  auto* Entry = Caller->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  // Wrong: only 1 arg, but add needs 2
  Builder.createCall(Callee, {Builder.getInt32(1)});
  Builder.createRetVoid();

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_FALSE(OK) << "Call with wrong arg count should fail";
}

// ============================================================================
// Additional: VerifierPass getName
// ============================================================================

TEST(VerifierTest, GetName) {
  VerifierPass VP;
  EXPECT_EQ(VP.getName(), "verifier");
}

// ============================================================================
// Additional: Empty module passes
// ============================================================================

TEST(VerifierTest, EmptyModulePasses) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("empty", TCtx);

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(Errors.empty());
}

// ============================================================================
// Additional: Declaration-only function passes
// ============================================================================

TEST(VerifierTest, FunctionDeclarationPasses) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("decl_test", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty()});
  auto* F = M.getOrInsertFunction("extern_fn", FTy);
  // Don't add any basic blocks — it's a declaration

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_TRUE(OK) << "Declaration-only module should pass";
}

// ============================================================================
// Additional: Branch instruction verification
// ============================================================================

TEST(VerifierTest, BranchInstructionValid) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("br_test", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* F = M.getOrInsertFunction("br_fn", FTy);
  auto* Entry = F->addBasicBlock("entry");
  auto* Exit = F->addBasicBlock("exit");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  Builder.createBr(Exit);
  Builder.setInsertPoint(Exit);
  Builder.createRetVoid();

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_TRUE(OK) << "Valid branch module should pass";
}

// ============================================================================
// Additional: CondBr instruction verification
// ============================================================================

TEST(VerifierTest, CondBrValid) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("condbr_test", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* F = M.getOrInsertFunction("condbr_fn", FTy);
  auto* Entry = F->addBasicBlock("entry");
  auto* TrueBB = F->addBasicBlock("true_bb");
  auto* FalseBB = F->addBasicBlock("false_bb");
  auto* Exit = F->addBasicBlock("exit");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* Cond = Builder.createICmp(ICmpPred::NE,
    Builder.getInt32(1), Builder.getInt32(0), "cond");
  Builder.createCondBr(Cond, TrueBB, FalseBB);

  Builder.setInsertPoint(TrueBB);
  Builder.createBr(Exit);

  Builder.setInsertPoint(FalseBB);
  Builder.createBr(Exit);

  Builder.setInsertPoint(Exit);
  Builder.createRetVoid();

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_TRUE(OK) << "Valid CondBr module should pass";
}

// ============================================================================
// V6: Assert mode (no diagnostics collector) triggers assertion on error
// ============================================================================

TEST(VerifierTest, AssertModeDeath) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("death_test", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* F = M.getOrInsertFunction("no_term_death", FTy);
  auto* Entry = F->addBasicBlock("entry");

  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  Builder.createAdd(Builder.getInt32(1), Builder.getInt32(2), "dead");
  // No terminator → verification error → assert in assert mode

  VerifierPass VP; // No diagnostics collector → assert mode
  EXPECT_DEATH(VP.run(M), "Verifier assertion failure");
}

// ============================================================================
// Invalid ICmp predicate value
// ============================================================================

TEST(VerifierTest, InvalidICmpPredicate) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  IRModule M("bad_pred", TCtx);

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* F = M.getOrInsertFunction("bad_pred", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* ICmp = Builder.createICmp(ICmpPred::EQ, Builder.getInt32(1), Builder.getInt32(2), "cmp");
  // 手动设置非法 predicate 值
  ICmp->setPredicate(200);  // 超出 ICmpPred 范围
  Builder.createRet(ICmp);

  SmallVector<VerificationDiagnostic, 32> Errors;
  VerifierPass VP(&Errors);
  bool OK = VP.run(M);
  EXPECT_FALSE(OK);
  bool hasPredError = false;
  for (auto& E : Errors) {
    if (E.Message.find("predicate") != std::string::npos) hasPredError = true;
  }
  EXPECT_TRUE(hasPredError);
}

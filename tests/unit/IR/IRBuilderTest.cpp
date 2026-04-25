#include <gtest/gtest.h>

#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRModule.h"

using namespace blocktype;
using namespace blocktype::ir;

TEST(IRBuilderTest, CreateAdd) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("test_fn", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* One = Builder.getInt32(1);
  auto* Two = Builder.getInt32(2);
  auto* Add = Builder.createAdd(One, Two, "sum");
  ASSERT_NE(Add, nullptr);
  EXPECT_EQ(Add->getOpcode(), Opcode::Add);
  EXPECT_EQ(Add->getNumOperands(), 2u);
}

TEST(IRBuilderTest, CreateRetVoid) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getVoidType(), {});
  auto* F = Mod.getOrInsertFunction("void_fn", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* Ret = Builder.createRetVoid();
  ASSERT_NE(Ret, nullptr);
  EXPECT_EQ(Ret->getOpcode(), Opcode::Ret);
  EXPECT_TRUE(Ret->isTerminator());
}

TEST(IRBuilderTest, CreateCall) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* Foo = Mod.getOrInsertFunction("foo", FTy);
  auto* Caller = Mod.getOrInsertFunction("caller", FTy);
  auto* Entry = Caller->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* Call = Builder.createCall(Foo, {}, "call_result");
  ASSERT_NE(Call, nullptr);
  EXPECT_EQ(Call->getOpcode(), Opcode::Call);
}

TEST(IRBuilderTest, ConstantFactory) {
  IRContext IRCtx;
  IRBuilder Builder(IRCtx);
  auto* V1 = Builder.getInt1(true);
  ASSERT_NE(V1, nullptr);
  EXPECT_EQ(V1->getZExtValue(), 1u);
  auto* V32 = Builder.getInt32(42);
  ASSERT_NE(V32, nullptr);
  EXPECT_EQ(V32->getZExtValue(), 42u);
  auto* V64 = Builder.getInt64(123456789);
  ASSERT_NE(V64, nullptr);
  EXPECT_EQ(V64->getZExtValue(), 123456789u);
}

TEST(IRBuilderTest, GetNullAndGetUndef) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRBuilder Builder(IRCtx);
  auto* Null = Builder.getNull(Ctx.getInt32Ty());
  ASSERT_NE(Null, nullptr);
  EXPECT_EQ(Null->getValueKind(), ValueKind::ConstantNull);
  auto* Undef = Builder.getUndef(Ctx.getInt32Ty());
  ASSERT_NE(Undef, nullptr);
  EXPECT_EQ(Undef->getValueKind(), ValueKind::ConstantUndef);
}

TEST(IRBuilderTest, CreateBranch) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getVoidType(), {});
  auto* F = Mod.getOrInsertFunction("br_test", FTy);
  auto* Entry = F->addBasicBlock("entry");
  auto* BB1 = F->addBasicBlock("bb1");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* Br = Builder.createBr(BB1);
  ASSERT_NE(Br, nullptr);
  EXPECT_EQ(Br->getOpcode(), Opcode::Br);
  EXPECT_TRUE(Br->isTerminator());
}

TEST(IRBuilderTest, CreateCondBr) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getVoidType(), {});
  auto* F = Mod.getOrInsertFunction("condbr_test", FTy);
  auto* Entry = F->addBasicBlock("entry");
  auto* TrueBB = F->addBasicBlock("true_bb");
  auto* FalseBB = F->addBasicBlock("false_bb");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* Cond = Builder.getInt1(true);
  auto* CBr = Builder.createCondBr(Cond, TrueBB, FalseBB);
  ASSERT_NE(CBr, nullptr);
  EXPECT_EQ(CBr->getOpcode(), Opcode::CondBr);
  EXPECT_TRUE(CBr->isTerminator());
  EXPECT_EQ(CBr->getNumOperands(), 3u);
}

TEST(IRBuilderTest, ArithmeticOps) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("arith", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* A = Builder.getInt32(10);
  auto* B = Builder.getInt32(20);
  auto* Sub = Builder.createSub(A, B, "sub");
  ASSERT_NE(Sub, nullptr);
  EXPECT_EQ(Sub->getOpcode(), Opcode::Sub);
  auto* Mul = Builder.createMul(A, B, "mul");
  ASSERT_NE(Mul, nullptr);
  EXPECT_EQ(Mul->getOpcode(), Opcode::Mul);
  auto* Neg = Builder.createNeg(A, "neg");
  ASSERT_NE(Neg, nullptr);
  EXPECT_EQ(Neg->getOpcode(), Opcode::Sub);
}

TEST(IRBuilderTest, ComparisonOps) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("cmp", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* A = Builder.getInt32(1);
  auto* B = Builder.getInt32(2);
  auto* ICmp = Builder.createICmp(ICmpPred::EQ, A, B, "icmp");
  ASSERT_NE(ICmp, nullptr);
  EXPECT_EQ(ICmp->getOpcode(), Opcode::ICmp);
  EXPECT_EQ(ICmp->getNumOperands(), 2u);
  auto* FCmp = Builder.createFCmp(FCmpPred::OEQ, A, B, "fcmp");
  ASSERT_NE(FCmp, nullptr);
  EXPECT_EQ(FCmp->getOpcode(), Opcode::FCmp);
}

TEST(IRBuilderTest, ICmpPredicateStorage) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("icmp_pred", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* A = Builder.getInt32(1);
  auto* B = Builder.getInt32(2);
  auto* ICmpSGT = Builder.createICmp(ICmpPred::SGT, A, B, "sgt");
  EXPECT_EQ(ICmpSGT->getICmpPredicate(), ICmpPred::SGT);
  EXPECT_EQ(ICmpSGT->getPredicate(), static_cast<uint8_t>(ICmpPred::SGT));

  auto* ICmpULE = Builder.createICmp(ICmpPred::ULE, A, B, "ule");
  EXPECT_EQ(ICmpULE->getICmpPredicate(), ICmpPred::ULE);
}

TEST(IRBuilderTest, FCmpPredicateStorage) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("fcmp_pred", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* A = Builder.getInt32(1);
  auto* B = Builder.getInt32(2);
  auto* FCmpUNO = Builder.createFCmp(FCmpPred::UNO, A, B, "uno");
  EXPECT_EQ(FCmpUNO->getFCmpPredicate(), FCmpPred::UNO);

  auto* FCmpTrue = Builder.createFCmp(FCmpPred::True, A, B, "always");
  EXPECT_EQ(FCmpTrue->getFCmpPredicate(), FCmpPred::True);
}

TEST(IRBuilderTest, MemoryOps) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("mem", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* Alloca = Builder.createAlloca(Ctx.getInt32Ty(), "buf");
  ASSERT_NE(Alloca, nullptr);
  EXPECT_EQ(Alloca->getOpcode(), Opcode::Alloca);
  EXPECT_TRUE(Alloca->getType()->isPointer());
  auto* Load = Builder.createLoad(Ctx.getInt32Ty(), Alloca, "val");
  ASSERT_NE(Load, nullptr);
  EXPECT_EQ(Load->getOpcode(), Opcode::Load);
  auto* V = Builder.getInt32(42);
  auto* Store = Builder.createStore(V, Alloca);
  ASSERT_NE(Store, nullptr);
  EXPECT_EQ(Store->getOpcode(), Opcode::Store);
  auto* Idx = Builder.getInt32(0);
  auto* GEP = Builder.createGEP(Ctx.getInt32Ty(), Alloca, {Idx}, "gep");
  ASSERT_NE(GEP, nullptr);
  EXPECT_EQ(GEP->getOpcode(), Opcode::GEP);
}

TEST(IRBuilderTest, CastOps) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt64Ty(), {});
  auto* F = Mod.getOrInsertFunction("cast", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* V = Builder.getInt32(42);
  auto* ZExt = Builder.createZExt(V, Ctx.getInt64Ty(), "zext");
  ASSERT_NE(ZExt, nullptr);
  EXPECT_EQ(ZExt->getOpcode(), Opcode::ZExt);
  auto* SExt = Builder.createSExt(V, Ctx.getInt64Ty(), "sext");
  ASSERT_NE(SExt, nullptr);
  EXPECT_EQ(SExt->getOpcode(), Opcode::SExt);
  auto* V64 = Builder.getInt64(100);
  auto* Trunc = Builder.createTrunc(V64, Ctx.getInt32Ty(), "trunc");
  ASSERT_NE(Trunc, nullptr);
  EXPECT_EQ(Trunc->getOpcode(), Opcode::Trunc);
  auto* BitCast = Builder.createBitCast(V64, Ctx.getFloatTy(), "bc");
  ASSERT_NE(BitCast, nullptr);
  EXPECT_EQ(BitCast->getOpcode(), Opcode::BitCast);
}

TEST(IRBuilderTest, SelectAndPhi) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("sel_phi", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* Cond = Builder.getInt1(true);
  auto* TV = Builder.getInt32(1);
  auto* FV = Builder.getInt32(0);
  auto* Sel = Builder.createSelect(Cond, TV, FV, "sel");
  ASSERT_NE(Sel, nullptr);
  EXPECT_EQ(Sel->getOpcode(), Opcode::Select);
  auto* Phi = Builder.createPhi(Ctx.getInt32Ty(), 2, "phi");
  ASSERT_NE(Phi, nullptr);
  EXPECT_EQ(Phi->getOpcode(), Opcode::Phi);
}

TEST(IRBuilderTest, ExtractInsertValue) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("agg", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* V = Builder.getInt32(42);
  auto* EV = Builder.createExtractValue(V, {0}, "ev");
  ASSERT_NE(EV, nullptr);
  EXPECT_EQ(EV->getOpcode(), Opcode::ExtractValue);
  auto* IV = Builder.createInsertValue(V, V, {0}, "iv");
  ASSERT_NE(IV, nullptr);
  EXPECT_EQ(IV->getOpcode(), Opcode::InsertValue);
}

TEST(IRBuilderTest, InsertPointMaintenance) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("insert_pt", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  EXPECT_EQ(Builder.getInsertBlock(), nullptr);
  Builder.setInsertPoint(Entry);
  EXPECT_EQ(Builder.getInsertBlock(), Entry);
  auto* One = Builder.getInt32(1);
  Builder.createAdd(One, One, "a");
  Builder.createAdd(One, One, "b");
  EXPECT_EQ(Entry->size(), 2u);
}

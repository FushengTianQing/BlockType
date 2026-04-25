#ifndef BLOCKTYPE_IR_IRBUILDER_H
#define BLOCKTYPE_IR_IRBUILDER_H

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

class IRBuilder {
  IRBasicBlock* InsertBB = nullptr;
  IRInstruction* InsertPt = nullptr;
  IRTypeContext& TypeCtx;
  IRContext& IRCtx;

public:
  explicit IRBuilder(IRContext& Ctx)
    : TypeCtx(Ctx.getTypeContext()), IRCtx(Ctx) {}

  void setInsertPoint(IRBasicBlock* BB) { InsertBB = BB; InsertPt = nullptr; }
  void setInsertPoint(IRBasicBlock* BB, IRInstruction* Before) { InsertBB = BB; InsertPt = Before; }
  IRBasicBlock* getInsertBlock() const { return InsertBB; }

  IRConstantInt* getInt1(bool V);
  IRConstantInt* getInt32(uint32_t V);
  IRConstantInt* getInt64(uint64_t V);
  IRConstantNull* getNull(IRType* T);
  IRConstantUndef* getUndef(IRType* T);

  IRInstruction* createRet(IRValue* V);
  IRInstruction* createRetVoid();
  IRInstruction* createBr(IRBasicBlock* Dest);
  IRInstruction* createCondBr(IRValue* Cond, IRBasicBlock* TrueBB, IRBasicBlock* FalseBB);
  IRInstruction* createInvoke(IRFunction* Callee, ArrayRef<IRValue*> Args,
                               IRBasicBlock* NormalBB, IRBasicBlock* UnwindBB);

  //===--- Switch / Unreachable ---===//

  IRInstruction* createSwitch(IRValue* Cond, IRBasicBlock* DefaultBB,
                               unsigned NumCases = 0);
  IRInstruction* createUnreachable();

  IRInstruction* createAdd(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createSub(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createMul(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createNeg(IRValue* V, StringRef Name = "");

  //===--- Integer Division / Remainder ---===//

  IRInstruction* createSDiv(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createUDiv(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createSRem(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createURem(IRValue* LHS, IRValue* RHS, StringRef Name = "");

  //===--- Bitwise Operations ---===//

  IRInstruction* createAnd(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createOr(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createXor(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createNot(IRValue* V, StringRef Name = "");

  //===--- Shift Operations ---===//

  IRInstruction* createShl(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createLShr(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createAShr(IRValue* LHS, IRValue* RHS, StringRef Name = "");

  //===--- Type Conversions ---===//

  IRInstruction* createSIToFP(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createUIToFP(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createFPToSI(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createFPToUI(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createPtrToInt(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createIntToPtr(IRValue* V, IRType* DestTy, StringRef Name = "");

  IRInstruction* createICmp(ICmpPred Pred, IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createFCmp(FCmpPred Pred, IRValue* LHS, IRValue* RHS, StringRef Name = "");

  IRInstruction* createAlloca(IRType* Ty, StringRef Name = "");
  IRInstruction* createLoad(IRType* Ty, IRValue* Ptr, StringRef Name = "");
  IRInstruction* createStore(IRValue* Val, IRValue* Ptr);
  IRInstruction* createGEP(IRType* SourceTy, IRValue* Ptr, ArrayRef<IRValue*> Indices, StringRef Name = "");

  IRInstruction* createBitCast(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createZExt(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createSExt(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createTrunc(IRValue* V, IRType* DestTy, StringRef Name = "");

  IRInstruction* createCall(IRFunction* Callee, ArrayRef<IRValue*> Args, StringRef Name = "");

  IRInstruction* createExtractValue(IRValue* Agg, ArrayRef<unsigned> Indices, StringRef Name = "");
  IRInstruction* createInsertValue(IRValue* Agg, IRValue* Val, ArrayRef<unsigned> Indices, StringRef Name = "");

  IRInstruction* createPhi(IRType* Ty, unsigned NumIncoming, StringRef Name = "");
  IRInstruction* createSelect(IRValue* Cond, IRValue* TrueVal, IRValue* FalseVal, StringRef Name = "");

private:
  IRInstruction* insertHelper(std::unique_ptr<IRInstruction> I);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRBUILDER_H

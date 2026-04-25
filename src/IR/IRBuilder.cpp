#include "blocktype/IR/IRBuilder.h"

#include <utility>

namespace blocktype {
namespace ir {

IRInstruction* IRBuilder::insertHelper(std::unique_ptr<IRInstruction> I) {
  IRInstruction* Ptr = I.get();
  if (!InsertBB) return Ptr;

  if (!InsertPt) {
    InsertBB->push_back(std::move(I));
    return Ptr;
  }

  auto& InstList = InsertBB->getInstList();
  auto It = InstList.begin();
  IRInstruction* Prev = nullptr;
  for (; It != InstList.end(); ++It) {
    if (It->get() == InsertPt) break;
    Prev = It->get();
  }

  if (Prev) {
    InsertBB->insert(Prev, std::move(I));
  } else {
    InsertBB->push_front(std::move(I));
  }
  return Ptr;
}

IRConstantInt* IRBuilder::getInt1(bool V) {
  return IRCtx.create<IRConstantInt>(TypeCtx.getInt1Ty(), V ? 1 : 0);
}

IRConstantInt* IRBuilder::getInt32(uint32_t V) {
  return IRCtx.create<IRConstantInt>(TypeCtx.getInt32Ty(), static_cast<uint64_t>(V));
}

IRConstantInt* IRBuilder::getInt64(uint64_t V) {
  return IRCtx.create<IRConstantInt>(TypeCtx.getInt64Ty(), V);
}

IRConstantNull* IRBuilder::getNull(IRType* T) {
  return IRCtx.create<IRConstantNull>(T);
}

IRConstantUndef* IRBuilder::getUndef(IRType* T) {
  return IRConstantUndef::get(IRCtx, T);
}

IRInstruction* IRBuilder::createRet(IRValue* V) {
  auto I = std::make_unique<IRInstruction>(Opcode::Ret, V->getType(), 0);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createRetVoid() {
  auto I = std::make_unique<IRInstruction>(Opcode::Ret, TypeCtx.getVoidType(), 0);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createBr(IRBasicBlock* Dest) {
  auto* BBRef = IRCtx.create<IRBasicBlockRef>(Dest);
  auto I = std::make_unique<IRInstruction>(Opcode::Br, TypeCtx.getVoidType(), 0);
  I->addOperand(BBRef);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createCondBr(IRValue* Cond, IRBasicBlock* TrueBB, IRBasicBlock* FalseBB) {
  auto* TrueRef = IRCtx.create<IRBasicBlockRef>(TrueBB);
  auto* FalseRef = IRCtx.create<IRBasicBlockRef>(FalseBB);
  auto I = std::make_unique<IRInstruction>(Opcode::CondBr, TypeCtx.getVoidType(), 0);
  I->addOperand(Cond);
  I->addOperand(TrueRef);
  I->addOperand(FalseRef);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createInvoke(IRFunction* Callee, ArrayRef<IRValue*> Args,
                                        IRBasicBlock* NormalBB, IRBasicBlock* UnwindBB) {
  auto* FTy = Callee->getFunctionType();
  auto* CalleeRef = IRCtx.create<IRConstantFunctionRef>(Callee);
  auto* NormalRef = IRCtx.create<IRBasicBlockRef>(NormalBB);
  auto* UnwindRef = IRCtx.create<IRBasicBlockRef>(UnwindBB);
  auto I = std::make_unique<IRInstruction>(Opcode::Invoke, FTy->getReturnType(), 0);
  I->addOperand(CalleeRef);
  for (auto* A : Args) I->addOperand(A);
  I->addOperand(NormalRef);
  I->addOperand(UnwindRef);
  return insertHelper(std::move(I));
}

//===--- Switch / Unreachable ---===//

IRInstruction* IRBuilder::createSwitch(IRValue* Cond, IRBasicBlock* DefaultBB,
                                        unsigned NumCases) {
  auto* DefaultRef = IRCtx.create<IRBasicBlockRef>(DefaultBB);
  auto I = std::make_unique<IRInstruction>(Opcode::Switch, Cond->getType(), 0,
                                            dialect::DialectID::Core, "switch");
  I->addOperand(Cond);
  I->addOperand(DefaultRef);
  (void)NumCases;
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createUnreachable() {
  auto I = std::make_unique<IRInstruction>(Opcode::Unreachable,
                                            TypeCtx.getVoidType(), 0);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createAdd(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Add, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createSub(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Sub, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createMul(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Mul, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createNeg(IRValue* V, StringRef Name) {
  auto* Zero = getInt32(0);
  return createSub(Zero, V, Name);
}

//===--- Integer Division / Remainder ---===//

IRInstruction* IRBuilder::createSDiv(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::SDiv, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createUDiv(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::UDiv, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createSRem(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::SRem, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createURem(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::URem, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

//===--- Bitwise Operations ---===//

IRInstruction* IRBuilder::createAnd(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::And, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createOr(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Or, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createXor(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Xor, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createNot(IRValue* V, StringRef Name) {
  // Not = XOR with all-ones constant. Use IRCtx to allocate the constant.
  auto* Ty = static_cast<IRIntegerType*>(V->getType());
  auto* AllOnes = IRCtx.create<IRConstantInt>(Ty, static_cast<uint64_t>(-1));
  return createXor(V, AllOnes, Name);
}

//===--- Shift Operations ---===//

IRInstruction* IRBuilder::createShl(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Shl, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createLShr(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::LShr, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createAShr(IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::AShr, LHS->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

//===--- Type Conversions ---===//

IRInstruction* IRBuilder::createSIToFP(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::SIToFP, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createUIToFP(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::UIToFP, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createFPToSI(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::FPToSI, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createFPToUI(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::FPToUI, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createPtrToInt(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::PtrToInt, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createIntToPtr(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::IntToPtr, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createICmp(ICmpPred Pred, IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::ICmp, TypeCtx.getInt1Ty(), 0, dialect::DialectID::Core, Name);
  I->setPredicate(static_cast<uint8_t>(Pred));
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createFCmp(FCmpPred Pred, IRValue* LHS, IRValue* RHS, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::FCmp, TypeCtx.getInt1Ty(), 0, dialect::DialectID::Core, Name);
  I->setPredicate(static_cast<uint8_t>(Pred));
  I->addOperand(LHS);
  I->addOperand(RHS);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createAlloca(IRType* Ty, StringRef Name) {
  auto* PtrTy = TypeCtx.getPointerType(Ty);
  auto I = std::make_unique<IRInstruction>(Opcode::Alloca, PtrTy, 0, dialect::DialectID::Core, Name);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createLoad(IRType* Ty, IRValue* Ptr, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Load, Ty, 0, dialect::DialectID::Core, Name);
  I->addOperand(Ptr);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createStore(IRValue* Val, IRValue* Ptr) {
  auto I = std::make_unique<IRInstruction>(Opcode::Store, TypeCtx.getVoidType(), 0);
  I->addOperand(Val);
  I->addOperand(Ptr);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createGEP(IRType* SourceTy, IRValue* Ptr, ArrayRef<IRValue*> Indices, StringRef Name) {
  auto* PtrTy = TypeCtx.getPointerType(SourceTy);
  auto I = std::make_unique<IRInstruction>(Opcode::GEP, PtrTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(Ptr);
  for (auto* Idx : Indices) I->addOperand(Idx);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createBitCast(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::BitCast, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createZExt(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::ZExt, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createSExt(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::SExt, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createTrunc(IRValue* V, IRType* DestTy, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Trunc, DestTy, 0, dialect::DialectID::Core, Name);
  I->addOperand(V);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createCall(IRFunction* Callee, ArrayRef<IRValue*> Args, StringRef Name) {
  auto* FTy = Callee->getFunctionType();
  auto* CalleeRef = IRCtx.create<IRConstantFunctionRef>(Callee);
  auto I = std::make_unique<IRInstruction>(Opcode::Call, FTy->getReturnType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(CalleeRef);
  for (auto* A : Args) I->addOperand(A);
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createExtractValue(IRValue* Agg, ArrayRef<unsigned> Indices, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::ExtractValue, Agg->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(Agg);
  for (auto Idx : Indices) {
    I->addOperand(getInt32(Idx));
  }
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createInsertValue(IRValue* Agg, IRValue* Val, ArrayRef<unsigned> Indices, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::InsertValue, Agg->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(Agg);
  I->addOperand(Val);
  for (auto Idx : Indices) {
    I->addOperand(getInt32(Idx));
  }
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createPhi(IRType* Ty, unsigned NumIncoming, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Phi, Ty, 0, dialect::DialectID::Core, Name);
  I->addOperand(getInt32(NumIncoming));
  return insertHelper(std::move(I));
}

IRInstruction* IRBuilder::createSelect(IRValue* Cond, IRValue* TrueVal, IRValue* FalseVal, StringRef Name) {
  auto I = std::make_unique<IRInstruction>(Opcode::Select, TrueVal->getType(), 0, dialect::DialectID::Core, Name);
  I->addOperand(Cond);
  I->addOperand(TrueVal);
  I->addOperand(FalseVal);
  return insertHelper(std::move(I));
}

} // namespace ir
} // namespace blocktype

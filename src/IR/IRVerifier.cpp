#include "blocktype/IR/IRVerifier.h"

#include <cassert>
#include <set>
#include <string>

#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

static const char* catStr(VerificationCategory C) {
  switch (C) {
  case VerificationCategory::ModuleLevel:     return "ModuleLevel";
  case VerificationCategory::FunctionLevel:   return "FunctionLevel";
  case VerificationCategory::BasicBlockLevel: return "BasicBlockLevel";
  case VerificationCategory::InstructionLevel:return "InstructionLevel";
  case VerificationCategory::TypeLevel:       return "TypeLevel";
  }
  return "Unknown";
}

VerifierPass::VerifierPass(SmallVector<VerificationDiagnostic, 32>* Diag)
  : Diagnostics(Diag), HasErrors(false) {}

std::string VerifierPass::buildLocationPrefix(VerificationCategory Cat) const {
  std::string Loc;
  switch (Cat) {
  case VerificationCategory::FunctionLevel:
    if (!CurrentFuncName.empty())
      Loc = "in function @" + CurrentFuncName;
    break;
  case VerificationCategory::BasicBlockLevel:
    if (!CurrentFuncName.empty())
      Loc = "in function @" + CurrentFuncName;
    if (!CurrentBBName.empty())
      Loc += ", basic block %" + CurrentBBName;
    break;
  case VerificationCategory::InstructionLevel:
  case VerificationCategory::TypeLevel:
    if (!CurrentFuncName.empty())
      Loc = "in function @" + CurrentFuncName;
    if (!CurrentBBName.empty())
      Loc += ", basic block %" + CurrentBBName;
    if (!CurrentInstInfo.empty())
      Loc += ", " + CurrentInstInfo;
    break;
  default:
    break;
  }
  return Loc;
}

void VerifierPass::reportError(VerificationCategory Cat, const std::string& Msg) {
  HasErrors = true;
  std::string Loc = buildLocationPrefix(Cat);
  std::string FullMsg = Loc.empty() ? Msg : (Loc + ": " + Msg);
  if (Diagnostics) {
    Diagnostics->push_back(VerificationDiagnostic(Cat, FullMsg));
  } else {
    errs() << "[Verifier] " << catStr(Cat) << ": " << FullMsg << "\n";
    assert(false && "Verifier assertion failure");
  }
}

const IRFunction* VerifierPass::getContainingFunction(const IRInstruction& I) const {
  auto* BB = I.getParent();
  return BB ? BB->getParent() : nullptr;
}

bool VerifierPass::hasOperand(const IRInstruction& I, unsigned Idx) const {
  return Idx < I.getNumOperands();
}

IRType* VerifierPass::getOperandType(const IRInstruction& I, unsigned Idx) const {
  if (!hasOperand(I, Idx)) return nullptr;
  auto* Op = I.getOperand(Idx);
  return Op ? Op->getType() : nullptr;
}

bool VerifierPass::verify(IRModule& M, SmallVector<VerificationDiagnostic, 32>* Diag) {
  VerifierPass VP(Diag);
  return VP.run(M);
}

bool VerifierPass::run(IRModule& M) {
  HasErrors = false;
  verifyModule(M);
  return !HasErrors;
}

bool VerifierPass::verifyModule(IRModule& M) {
  CurrentFuncName.clear();
  CurrentBBName.clear();
  CurrentInstInfo.clear();
  if (M.getName().empty())
    reportError(VerificationCategory::ModuleLevel, "Module has empty name");

  { std::set<std::string> S;
    for (auto& F : M.getFunctions())
      if (!S.insert(F->getName().str()).second)
        reportError(VerificationCategory::ModuleLevel, "Duplicate function name: " + F->getName().str());
  }
  { std::set<std::string> S;
    for (auto& GV : M.getGlobals())
      if (!S.insert(GV->getName().str()).second)
        reportError(VerificationCategory::ModuleLevel, "Duplicate global variable name: " + GV->getName().str());
  }
  for (auto& F : M.getFunctions()) verifyFunction(*F);
  for (auto& GV : M.getGlobals()) if (GV->getType()) verifyType(GV->getType());
  for (auto& F : M.getFunctions())
    if (F->isDeclaration() && F->getFunctionType()) verifyType(F->getFunctionType());
  return !HasErrors;
}

bool VerifierPass::verifyType(const IRType* T) {
  if (!T) { reportError(VerificationCategory::TypeLevel, "Type is null"); return false; }
  return true;
}

bool VerifierPass::verifyTypeComplete(const IRType* T) {
  if (!T) { reportError(VerificationCategory::TypeLevel, "Type is null"); return false; }
  if (T->isOpaque()) {
    reportError(VerificationCategory::TypeLevel,
                "OpaqueType '" + static_cast<const IROpaqueType*>(T)->getName().str() + "' found in IR");
    return false;
  }
  if (T->isStruct()) {
    auto* ST = static_cast<const IRStructType*>(T);
    for (unsigned i = 0; i < ST->getNumFields(); ++i)
      if (!ST->getFieldType(i))
        reportError(VerificationCategory::TypeLevel,
                    "StructType '" + ST->getName().str() + "' has null field " + std::to_string(i));
  }
  if (T->isArray() && !static_cast<const IRArrayType*>(T)->getElementType())
    reportError(VerificationCategory::TypeLevel, "ArrayType has null element type");
  if (T->isVector() && !static_cast<const IRVectorType*>(T)->getElementType())
    reportError(VerificationCategory::TypeLevel, "VectorType has null element type");
  if (T->isPointer() && !static_cast<const IRPointerType*>(T)->getPointeeType())
    reportError(VerificationCategory::TypeLevel, "PointerType has null pointee type");
  if (T->isFunction()) {
    auto* FT = static_cast<const IRFunctionType*>(T);
    if (!FT->getReturnType()) reportError(VerificationCategory::TypeLevel, "FunctionType null return type");
    for (unsigned i = 0; i < FT->getNumParams(); ++i)
      if (!FT->getParamType(i))
        reportError(VerificationCategory::TypeLevel, "FunctionType null param " + std::to_string(i));
  }
  return !HasErrors;
}

bool VerifierPass::verifyFunction(IRFunction& F) {
  if (!F.getFunctionType()) {
    reportError(VerificationCategory::FunctionLevel, "Function '" + F.getName().str() + "' has null function type");
  } else {
    if (F.getName().empty()) reportError(VerificationCategory::FunctionLevel, "Function has empty name");
    if (F.getNumArgs() != F.getFunctionType()->getNumParams())
      reportError(VerificationCategory::FunctionLevel,
                  "Function '" + F.getName().str() + "': arg count mismatch");
    unsigned N = std::min(F.getNumArgs(), F.getFunctionType()->getNumParams());
    for (unsigned i = 0; i < N; ++i) {
      auto* A = F.getArgType(i), *P = F.getFunctionType()->getParamType(i);
      if (A && P && !A->equals(P))
        reportError(VerificationCategory::FunctionLevel,
                    "Function '" + F.getName().str() + "': arg " + std::to_string(i) + " type mismatch");
    }
    if (auto* R = F.getFunctionType()->getReturnType()) verifyTypeComplete(R);
  }
  if (F.isDefinition() && F.getNumBasicBlocks() == 0)
    reportError(VerificationCategory::FunctionLevel,
                "Function '" + F.getName().str() + "' definition with no basic blocks");
  if (F.isDefinition() && !F.getEntryBlock())
    reportError(VerificationCategory::FunctionLevel,
                "Function '" + F.getName().str() + "' definition with no entry block");
  for (auto& BB : F.getBasicBlocks()) verifyBasicBlock(*BB);
  return !HasErrors;
}

bool VerifierPass::verifyBasicBlock(IRBasicBlock& BB) {
  CurrentBBName = BB.getName().str();
  CurrentInstInfo.clear();
  if (!BB.getParent())
    reportError(VerificationCategory::BasicBlockLevel,
                "BB '" + BB.getName().str() + "' has no parent function");
  if (!BB.empty() && !BB.getTerminator())
    reportError(VerificationCategory::BasicBlockLevel,
                "BB '" + BB.getName().str() + "' non-empty but no terminator");

  bool seenNonPhi = false, seenTerm = false;
  IRInstruction* last = nullptr;
  for (auto& I : BB.getInstList()) {
    if (I->getOpcode() == Opcode::Phi && seenNonPhi)
      reportError(VerificationCategory::BasicBlockLevel,
                  "Phi in BB '" + BB.getName().str() + "' after non-Phi");
    if (I->getOpcode() != Opcode::Phi) seenNonPhi = true;
    if (I->isTerminator()) {
      if (seenTerm) reportError(VerificationCategory::BasicBlockLevel,
                                 "Multiple terminators in BB '" + BB.getName().str() + "'");
      seenTerm = true;
    }
    last = I.get();
  }
  if (seenTerm && last && !last->isTerminator())
    reportError(VerificationCategory::BasicBlockLevel,
                "Terminator not last in BB '" + BB.getName().str() + "'");
  for (auto& I : BB.getInstList()) verifyInstruction(*I);
  return !HasErrors;
}

bool VerifierPass::verifyInstruction(const IRInstruction& I) {
  {
    CurrentInstInfo = "opcode " + std::to_string(static_cast<uint16_t>(I.getOpcode()));
    auto N = I.getName();
    if (!N.empty()) CurrentInstInfo += " (%" + N.str() + ")";
  }
  for (unsigned i = 0; i < I.getNumOperands(); ++i)
    if (!I.getOperand(i))
      reportError(VerificationCategory::InstructionLevel, "Null operand " + std::to_string(i));
  if (!I.getType()) reportError(VerificationCategory::InstructionLevel, "Null result type");
  if (!I.getParent()) reportError(VerificationCategory::InstructionLevel, "No parent BB");
  if (I.getParent() && !I.getParent()->getParent())
    reportError(VerificationCategory::InstructionLevel, "Parent BB has no parent function");

  auto Op = I.getOpcode();
  auto V = static_cast<uint16_t>(Op);
  if (I.isTerminator()) verifyTerminator(I);
  if (V >= 16 && V <= 22) verifyBinaryOp(I);
  if (V >= 32 && V <= 36) verifyFloatBinaryOp(I);
  if (V >= 48 && V <= 53) verifyBitwiseOp(I);
  if (Op == Opcode::Alloca || Op == Opcode::Load || Op == Opcode::Store || Op == Opcode::GEP)
    verifyMemoryOp(I);
  if (I.isCast()) verifyCastOp(I);
  if (I.isComparison()) verifyCmpOp(I);
  if (Op == Opcode::Call) verifyCallOp(I);
  if (Op == Opcode::Phi) verifyPhiOp(I);
  if (Op == Opcode::Select) verifySelectOp(I);
  if (Op == Opcode::ExtractValue || Op == Opcode::InsertValue) verifyOtherOp(I);
  return !HasErrors;
}

bool VerifierPass::verifyTerminator(const IRInstruction& I) {
  auto Op = I.getOpcode();
  auto* Func = getContainingFunction(I);
  switch (Op) {
  case Opcode::Ret: {
    if (I.getNumOperands() == 0) {
      if (I.getType() && !I.getType()->isVoid())
        reportError(VerificationCategory::InstructionLevel, "Void Ret non-void result");
    } else if (I.getNumOperands() == 1) {
      auto* RT = getOperandType(I, 0);
      if (RT && Func && Func->getFunctionType()) {
        auto* FR = Func->getFunctionType()->getReturnType();
        if (FR && !RT->equals(FR))
          reportError(VerificationCategory::InstructionLevel, "Ret type != function return type");
      }
    } else {
      reportError(VerificationCategory::InstructionLevel,
                  "Ret invalid operand count " + std::to_string(I.getNumOperands()));
    }
    break;
  }
  case Opcode::Br: {
    if (I.getNumOperands() != 1)
      reportError(VerificationCategory::InstructionLevel,
                  "Br must have 1 operand, has " + std::to_string(I.getNumOperands()));
    else if (I.getOperand(0) && I.getOperand(0)->getValueKind() != ValueKind::BasicBlockRef)
      reportError(VerificationCategory::InstructionLevel, "Br operand must be BBRef");
    break;
  }
  case Opcode::CondBr: {
    if (I.getNumOperands() != 3)
      reportError(VerificationCategory::InstructionLevel,
                  "CondBr must have 3 operands, has " + std::to_string(I.getNumOperands()));
    else {
      auto* CT = getOperandType(I, 0);
      if (CT && !(CT->isInteger() && static_cast<IRIntegerType*>(CT)->getBitWidth() == 1))
        reportError(VerificationCategory::InstructionLevel, "CondBr condition must be i1");
      if (I.getOperand(1) && I.getOperand(1)->getValueKind() != ValueKind::BasicBlockRef)
        reportError(VerificationCategory::InstructionLevel, "CondBr op[1] must be BBRef");
      if (I.getOperand(2) && I.getOperand(2)->getValueKind() != ValueKind::BasicBlockRef)
        reportError(VerificationCategory::InstructionLevel, "CondBr op[2] must be BBRef");
    }
    break;
  }
  case Opcode::Unreachable: {
    if (I.getNumOperands() != 0)
      reportError(VerificationCategory::InstructionLevel, "Unreachable must have 0 operands");
    break;
  }
  case Opcode::Invoke: {
    unsigned N = I.getNumOperands();
    if (N < 3) {
      reportError(VerificationCategory::InstructionLevel, "Invoke must have >= 3 operands");
    } else {
      auto* O0 = I.getOperand(0);
      if (!O0 || O0->getValueKind() != ValueKind::ConstantFunctionRef)
        reportError(VerificationCategory::InstructionLevel, "Invoke op[0] must be ConstFuncRef");
      else {
        auto* Callee = static_cast<IRConstantFunctionRef*>(O0)->getFunction();
        if (!Callee) reportError(VerificationCategory::InstructionLevel, "Invoke callee null");
        else if (auto* FT = Callee->getFunctionType()) {
          unsigned AA = N - 3;
          if (AA != FT->getNumParams())
            reportError(VerificationCategory::InstructionLevel, "Invoke arg count mismatch");
          else for (unsigned i = 0; i < AA; ++i) {
            auto* AT = getOperandType(I, 1 + i); auto* PT = FT->getParamType(i);
            if (AT && PT && !AT->equals(PT))
              reportError(VerificationCategory::InstructionLevel,
                          "Invoke arg " + std::to_string(i) + " type mismatch");
          }
        }
      }
      if (I.getOperand(N-2) && I.getOperand(N-2)->getValueKind() != ValueKind::BasicBlockRef)
        reportError(VerificationCategory::InstructionLevel, "Invoke normal dest must be BBRef");
      if (I.getOperand(N-1) && I.getOperand(N-1)->getValueKind() != ValueKind::BasicBlockRef)
        reportError(VerificationCategory::InstructionLevel, "Invoke unwind dest must be BBRef");
    }
    break;
  }
  default: break;
  }
  return !HasErrors;
}

bool VerifierPass::verifyBinaryOp(const IRInstruction& I) {
  if (I.getNumOperands() != 2) {
    reportError(VerificationCategory::InstructionLevel,
                "Int binop must have 2 operands, has " + std::to_string(I.getNumOperands()));
    return !HasErrors;
  }
  auto* L = getOperandType(I, 0), *R = getOperandType(I, 1);
  if (L && R) {
    if (!L->isInteger() || !R->isInteger())
      reportError(VerificationCategory::InstructionLevel, "Int binop operands must be IRIntegerType");
    else if (!L->equals(R))
      reportError(VerificationCategory::InstructionLevel, "Int binop operand types mismatch");
  }
  auto* Res = I.getType();
  if (Res && L && !Res->equals(L))
    reportError(VerificationCategory::InstructionLevel, "Int binop result != operand type");
  return !HasErrors;
}

bool VerifierPass::verifyFloatBinaryOp(const IRInstruction& I) {
  if (I.getNumOperands() != 2) {
    reportError(VerificationCategory::InstructionLevel,
                "Float binop must have 2 operands, has " + std::to_string(I.getNumOperands()));
    return !HasErrors;
  }
  auto* L = getOperandType(I, 0), *R = getOperandType(I, 1);
  if (L && R) {
    if (!L->isFloat() || !R->isFloat())
      reportError(VerificationCategory::InstructionLevel, "Float binop operands must be IRFloatType");
    else if (!L->equals(R))
      reportError(VerificationCategory::InstructionLevel, "Float binop operand types mismatch");
  }
  auto* Res = I.getType();
  if (Res && L && !Res->equals(L))
    reportError(VerificationCategory::InstructionLevel, "Float binop result != operand type");
  return !HasErrors;
}

bool VerifierPass::verifyBitwiseOp(const IRInstruction& I) {
  if (I.getNumOperands() != 2) {
    reportError(VerificationCategory::InstructionLevel,
                "Bitwise op must have 2 operands, has " + std::to_string(I.getNumOperands()));
    return !HasErrors;
  }
  auto* L = getOperandType(I, 0), *R = getOperandType(I, 1);
  if (L && R) {
    if (!L->isInteger() || !R->isInteger())
      reportError(VerificationCategory::InstructionLevel, "Bitwise op operands must be IRIntegerType");
    else if (!L->equals(R))
      reportError(VerificationCategory::InstructionLevel, "Bitwise op operand types mismatch");
  }
  auto* Res = I.getType();
  if (Res && L && !Res->equals(L))
    reportError(VerificationCategory::InstructionLevel, "Bitwise op result != operand type");
  return !HasErrors;
}

bool VerifierPass::verifyMemoryOp(const IRInstruction& I) {
  auto Op = I.getOpcode();
  switch (Op) {
  case Opcode::Alloca:
    if (I.getType() && !I.getType()->isPointer())
      reportError(VerificationCategory::InstructionLevel, "Alloca result must be ptr");
    break;
  case Opcode::Load:
    if (I.getNumOperands() != 1)
      reportError(VerificationCategory::InstructionLevel, "Load must have 1 operand");
    else {
      auto* PT = getOperandType(I, 0);
      if (PT && !PT->isPointer())
        reportError(VerificationCategory::InstructionLevel, "Load operand must be ptr");
      else if (PT) {
        auto* P = static_cast<IRPointerType*>(PT)->getPointeeType();
        if (I.getType() && P && !I.getType()->equals(P))
          reportError(VerificationCategory::InstructionLevel, "Load result != pointee type");
      }
    }
    break;
  case Opcode::Store:
    if (I.getNumOperands() != 2)
      reportError(VerificationCategory::InstructionLevel, "Store must have 2 operands");
    else {
      auto* VT = getOperandType(I, 0), *PT = getOperandType(I, 1);
      if (PT && !PT->isPointer())
        reportError(VerificationCategory::InstructionLevel, "Store op[1] must be ptr");
      else if (PT && VT) {
        auto* P = static_cast<IRPointerType*>(PT)->getPointeeType();
        if (P && !VT->equals(P))
          reportError(VerificationCategory::InstructionLevel, "Store op[0] != pointee type");
      }
      if (I.getType() && !I.getType()->isVoid())
        reportError(VerificationCategory::InstructionLevel, "Store result must be void");
    }
    break;
  case Opcode::GEP:
    if (I.getNumOperands() < 2)
      reportError(VerificationCategory::InstructionLevel, "GEP must have >= 2 operands");
    else {
      if (getOperandType(I, 0) && !getOperandType(I, 0)->isPointer())
        reportError(VerificationCategory::InstructionLevel, "GEP op[0] must be ptr");
      for (unsigned i = 1; i < I.getNumOperands(); ++i)
        if (getOperandType(I, i) && !getOperandType(I, i)->isInteger())
          reportError(VerificationCategory::InstructionLevel, "GEP index must be int");
      if (I.getType() && !I.getType()->isPointer())
        reportError(VerificationCategory::InstructionLevel, "GEP result must be ptr");
    }
    break;
  default: break;
  }
  return !HasErrors;
}

bool VerifierPass::verifyCastOp(const IRInstruction& I) {
  auto Op = I.getOpcode();
  auto* Res = I.getType();
  if (I.getNumOperands() != 1) {
    reportError(VerificationCategory::InstructionLevel,
                "Cast must have 1 operand, has " + std::to_string(I.getNumOperands()));
    return !HasErrors;
  }
  auto* Src = getOperandType(I, 0);
  if (!Src || !Res) return !HasErrors;
  switch (Op) {
  case Opcode::Trunc:
    if (!Src->isInteger() || !Res->isInteger())
      reportError(VerificationCategory::InstructionLevel, "Trunc: src/dest must be int");
    else if (static_cast<IRIntegerType*>(Res)->getBitWidth() >= static_cast<IRIntegerType*>(Src)->getBitWidth())
      reportError(VerificationCategory::InstructionLevel, "Trunc: dest BW must be < src BW");
    break;
  case Opcode::ZExt:
    if (!Src->isInteger() || !Res->isInteger())
      reportError(VerificationCategory::InstructionLevel, "ZExt: src/dest must be int");
    else if (static_cast<IRIntegerType*>(Res)->getBitWidth() <= static_cast<IRIntegerType*>(Src)->getBitWidth())
      reportError(VerificationCategory::InstructionLevel, "ZExt: dest BW must be > src BW");
    break;
  case Opcode::SExt:
    if (!Src->isInteger() || !Res->isInteger())
      reportError(VerificationCategory::InstructionLevel, "SExt: src/dest must be int");
    else if (static_cast<IRIntegerType*>(Res)->getBitWidth() <= static_cast<IRIntegerType*>(Src)->getBitWidth())
      reportError(VerificationCategory::InstructionLevel, "SExt: dest BW must be > src BW");
    break;
  case Opcode::FPTrunc:
    if (!Src->isFloat() || !Res->isFloat())
      reportError(VerificationCategory::InstructionLevel, "FPTrunc: src/dest must be float");
    else if (static_cast<IRFloatType*>(Res)->getBitWidth() >= static_cast<IRFloatType*>(Src)->getBitWidth())
      reportError(VerificationCategory::InstructionLevel, "FPTrunc: dest BW must be < src BW");
    break;
  case Opcode::FPExt:
    if (!Src->isFloat() || !Res->isFloat())
      reportError(VerificationCategory::InstructionLevel, "FPExt: src/dest must be float");
    else if (static_cast<IRFloatType*>(Res)->getBitWidth() <= static_cast<IRFloatType*>(Src)->getBitWidth())
      reportError(VerificationCategory::InstructionLevel, "FPExt: dest BW must be > src BW");
    break;
  case Opcode::FPToSI:
    if (!Src->isFloat() || !Res->isInteger())
      reportError(VerificationCategory::InstructionLevel, "FPToSI: src float, dest int");
    break;
  case Opcode::FPToUI:
    if (!Src->isFloat() || !Res->isInteger())
      reportError(VerificationCategory::InstructionLevel, "FPToUI: src float, dest int");
    break;
  case Opcode::SIToFP:
    if (!Src->isInteger() || !Res->isFloat())
      reportError(VerificationCategory::InstructionLevel, "SIToFP: src int, dest float");
    break;
  case Opcode::UIToFP:
    if (!Src->isInteger() || !Res->isFloat())
      reportError(VerificationCategory::InstructionLevel, "UIToFP: src int, dest float");
    break;
  case Opcode::PtrToInt:
    if (!Src->isPointer() || !Res->isInteger())
      reportError(VerificationCategory::InstructionLevel, "PtrToInt: src ptr, dest int");
    break;
  case Opcode::IntToPtr:
    if (!Src->isInteger() || !Res->isPointer())
      reportError(VerificationCategory::InstructionLevel, "IntToPtr: src int, dest ptr");
    break;
  case Opcode::BitCast:
    if (Src->equals(Res))
      reportError(VerificationCategory::InstructionLevel, "BitCast: src == dest");
    else {
      bool sp = Src->isPointer(), dp = Res->isPointer();
      if (sp && dp) { /* ok */ }
      else if (Src->isInteger() && Res->isInteger()) {
        if (static_cast<IRIntegerType*>(Src)->getBitWidth() != static_cast<IRIntegerType*>(Res)->getBitWidth())
          reportError(VerificationCategory::InstructionLevel, "BitCast: int sizes must match");
      } else if (Src->isFloat() && Res->isFloat()) {
        if (static_cast<IRFloatType*>(Src)->getBitWidth() != static_cast<IRFloatType*>(Res)->getBitWidth())
          reportError(VerificationCategory::InstructionLevel, "BitCast: float sizes must match");
      } else
        reportError(VerificationCategory::InstructionLevel, "BitCast: invalid type combo");
    }
    break;
  default: break;
  }
  return !HasErrors;
}

bool VerifierPass::verifyCmpOp(const IRInstruction& I) {
  auto Op = I.getOpcode();
  if (I.getNumOperands() != 2) {
    reportError(VerificationCategory::InstructionLevel,
                "Cmp must have 2 operands, has " + std::to_string(I.getNumOperands()));
    return !HasErrors;
  }
  auto* L = getOperandType(I, 0), *R = getOperandType(I, 1), *Res = I.getType();
  if (Op == Opcode::ICmp) {
    if (L && R) {
      if (!L->equals(R)) reportError(VerificationCategory::InstructionLevel, "ICmp operand types mismatch");
      if (!L->isInteger() && !L->isPointer())
        reportError(VerificationCategory::InstructionLevel, "ICmp operands must be int or ptr");
    }
    if (Res && (!Res->isInteger() || static_cast<IRIntegerType*>(Res)->getBitWidth() != 1))
      reportError(VerificationCategory::InstructionLevel, "ICmp result must be i1");
  } else {
    if (L && R) {
      if (!L->equals(R)) reportError(VerificationCategory::InstructionLevel, "FCmp operand types mismatch");
      if (!L->isFloat()) reportError(VerificationCategory::InstructionLevel, "FCmp operands must be float");
    }
    if (Res && (!Res->isInteger() || static_cast<IRIntegerType*>(Res)->getBitWidth() != 1))
      reportError(VerificationCategory::InstructionLevel, "FCmp result must be i1");
  }
  return !HasErrors;
}

bool VerifierPass::verifyCallOp(const IRInstruction& I) {
  if (I.getNumOperands() < 1) {
    reportError(VerificationCategory::InstructionLevel, "Call must have >= 1 operand");
    return !HasErrors;
  }
  auto* O0 = I.getOperand(0);
  if (!O0 || O0->getValueKind() != ValueKind::ConstantFunctionRef) {
    reportError(VerificationCategory::InstructionLevel, "Call op[0] must be ConstFuncRef");
    return !HasErrors;
  }
  auto* Callee = static_cast<IRConstantFunctionRef*>(O0)->getFunction();
  if (!Callee) {
    reportError(VerificationCategory::InstructionLevel, "Call callee null");
    return !HasErrors;
  }
  auto* FT = Callee->getFunctionType();
  if (!FT) return !HasErrors;
  unsigned AA = I.getNumOperands() - 1;
  if (AA != FT->getNumParams())
    reportError(VerificationCategory::InstructionLevel,
                "Call arg count (" + std::to_string(AA) + ") != param count (" +
                std::to_string(FT->getNumParams()) + ")");
  else for (unsigned i = 0; i < AA; ++i) {
    auto* AT = getOperandType(I, 1 + i), *PT = FT->getParamType(i);
    if (AT && PT && !AT->equals(PT))
      reportError(VerificationCategory::InstructionLevel, "Call arg " + std::to_string(i) + " type mismatch");
  }
  auto* Res = I.getType(), *FR = FT->getReturnType();
  if (Res && FR && !Res->equals(FR))
    reportError(VerificationCategory::InstructionLevel, "Call result != callee return type");
  return !HasErrors;
}

bool VerifierPass::verifyPhiOp(const IRInstruction& I) {
  auto* T = I.getType();
  if (!T || T->isVoid())
    reportError(VerificationCategory::InstructionLevel, "Phi result must be non-null non-void");
  return !HasErrors;
}

bool VerifierPass::verifySelectOp(const IRInstruction& I) {
  if (I.getNumOperands() != 3) {
    reportError(VerificationCategory::InstructionLevel,
                "Select must have 3 operands, has " + std::to_string(I.getNumOperands()));
    return !HasErrors;
  }
  auto* CT = getOperandType(I, 0), *TT = getOperandType(I, 1),
       *FT = getOperandType(I, 2), *R = I.getType();
  if (CT && !(CT->isInteger() && static_cast<IRIntegerType*>(CT)->getBitWidth() == 1))
    reportError(VerificationCategory::InstructionLevel, "Select condition must be i1");
  if (TT && FT && !TT->equals(FT))
    reportError(VerificationCategory::InstructionLevel, "Select true/false types must match");
  if (R && TT && !R->equals(TT))
    reportError(VerificationCategory::InstructionLevel, "Select result != true value type");
  return !HasErrors;
}

bool VerifierPass::verifyOtherOp(const IRInstruction& I) {
  if (I.getOpcode() == Opcode::ExtractValue && I.getNumOperands() < 2)
    reportError(VerificationCategory::InstructionLevel, "ExtractValue must have >= 2 operands");
  if (I.getOpcode() == Opcode::InsertValue && I.getNumOperands() < 3)
    reportError(VerificationCategory::InstructionLevel, "InsertValue must have >= 3 operands");
  return !HasErrors;
}

} // namespace ir
} // namespace blocktype

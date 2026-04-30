#include "blocktype/IR/IREquivalenceChecker.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRValue.h"
#include "blocktype/IR/IRConstant.h"

namespace blocktype {
namespace ir {

//===----------------------------------------------------------------------===//
// isTypeEquivalent — 完整类型比较
//===----------------------------------------------------------------------===//

bool IREquivalenceChecker::isTypeEquivalent(const IRType* A, const IRType* B) {
  if (!A && !B) return true;
  if (!A || !B) return false;
  if (A->getKind() != B->getKind()) return false;

  switch (A->getKind()) {
  case IRType::Void:
  case IRType::Bool:
    return true;
  case IRType::Integer:
    return static_cast<const IRIntegerType*>(A)->getBitWidth() ==
           static_cast<const IRIntegerType*>(B)->getBitWidth();
  case IRType::Float:
    return static_cast<const IRFloatType*>(A)->getBitWidth() ==
           static_cast<const IRFloatType*>(B)->getBitWidth();
  case IRType::Pointer: {
    auto* PA = static_cast<const IRPointerType*>(A);
    auto* PB = static_cast<const IRPointerType*>(B);
    return isTypeEquivalent(PA->getPointeeType(), PB->getPointeeType());
  }
  case IRType::Array: {
    auto* AA = static_cast<const IRArrayType*>(A);
    auto* AB = static_cast<const IRArrayType*>(B);
    return AA->getNumElements() == AB->getNumElements() &&
           isTypeEquivalent(AA->getElementType(), AB->getElementType());
  }
  case IRType::Vector: {
    auto* VA = static_cast<const IRVectorType*>(A);
    auto* VB = static_cast<const IRVectorType*>(B);
    return VA->getNumElements() == VB->getNumElements() &&
           isTypeEquivalent(VA->getElementType(), VB->getElementType());
  }
  case IRType::Struct: {
    auto* SA = static_cast<const IRStructType*>(A);
    auto* SB = static_cast<const IRStructType*>(B);
    if (SA->getNumFields() != SB->getNumFields()) return false;
    for (unsigned i = 0; i < SA->getNumFields(); ++i) {
      if (!isTypeEquivalent(SA->getFieldType(i), SB->getFieldType(i)))
        return false;
    }
    return true;
  }
  case IRType::Function: {
    auto* FA = static_cast<const IRFunctionType*>(A);
    auto* FB = static_cast<const IRFunctionType*>(B);
    if (FA->getNumParams() != FB->getNumParams()) return false;
    if (FA->isVarArg() != FB->isVarArg()) return false;
    if (!isTypeEquivalent(FA->getReturnType(), FB->getReturnType())) return false;
    for (unsigned i = 0; i < FA->getNumParams(); ++i) {
      if (!isTypeEquivalent(FA->getParamType(i), FB->getParamType(i)))
        return false;
    }
    return true;
  }
  case IRType::Opaque:
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// isStructurallyEquivalent — 完整函数结构比较
//===----------------------------------------------------------------------===//

bool IREquivalenceChecker::isStructurallyEquivalent(
    const IRFunction& A, const IRFunction& B) {
  // Compare name
  if (A.getName() != B.getName()) return false;

  // Compare argument count
  if (A.getNumArgs() != B.getNumArgs()) return false;

  // Compare return type
  if (!isTypeEquivalent(A.getReturnType(), B.getReturnType())) return false;

  // Compare basic block count
  if (A.getNumBasicBlocks() != B.getNumBasicBlocks()) return false;

  // Compare each basic block
  auto& BBsA = const_cast<IRFunction&>(A).getBasicBlocks();
  auto& BBsB = const_cast<IRFunction&>(B).getBasicBlocks();
  auto ItA = BBsA.begin();
  auto ItB = BBsB.begin();
  for (; ItA != BBsA.end() && ItB != BBsB.end(); ++ItA, ++ItB) {
    auto& BBA = *ItA;
    auto& BBB = *ItB;

    // Compare BB name
    if (BBA->getName() != BBB->getName()) return false;

    // Compare instruction count
    if (BBA->getInstList().size() != BBB->getInstList().size()) return false;

    // Compare each instruction's opcode
    auto& InstsA = BBA->getInstList();
    auto& InstsB = BBB->getInstList();
    auto IA = InstsA.begin();
    auto IB = InstsB.begin();
    for (; IA != InstsA.end() && IB != InstsB.end(); ++IA, ++IB) {
      if ((*IA)->getOpcode() != (*IB)->getOpcode()) return false;
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
// check — 完整模块等价检查
//===----------------------------------------------------------------------===//

IREquivalenceChecker::EquivalenceResult
IREquivalenceChecker::check(const IRModule& A, const IRModule& B) {
  EquivalenceResult Result;

  // 1. Compare module name
  if (A.getName() != B.getName()) {
    Result.Differences.push_back(
      "Module name mismatch: '" + A.getName().str() +
      "' vs '" + B.getName().str() + "'");
  }

  // 2. Compare target triple
  if (A.getTargetTriple() != B.getTargetTriple()) {
    Result.Differences.push_back(
      "Target triple mismatch: '" + A.getTargetTriple().str() +
      "' vs '" + B.getTargetTriple().str() + "'");
  }

  // 3. Compare global variable count
  auto& GVsA = const_cast<IRModule&>(A).getGlobals();
  auto& GVsB = const_cast<IRModule&>(B).getGlobals();
  if (GVsA.size() != GVsB.size()) {
    Result.Differences.push_back(
      "Global variable count mismatch: " + std::to_string(GVsA.size()) +
      " vs " + std::to_string(GVsB.size()));
  } else {
    // Compare each global variable
    auto GA = GVsA.begin();
    auto GB = GVsB.begin();
    for (; GA != GVsA.end() && GB != GVsB.end(); ++GA, ++GB) {
      if ((*GA)->getName() != (*GB)->getName()) {
        Result.Differences.push_back(
          "Global name mismatch: '" + (*GA)->getName().str() +
          "' vs '" + (*GB)->getName().str() + "'");
      }
      if (!isTypeEquivalent((*GA)->getType(), (*GB)->getType())) {
        Result.Differences.push_back(
          "Global type mismatch for '" + (*GA)->getName().str() + "'");
      }
    }
  }

  // 4. Compare function count
  auto& FAs = const_cast<IRModule&>(A).getFunctions();
  auto& FBs = const_cast<IRModule&>(B).getFunctions();
  if (FAs.size() != FBs.size()) {
    Result.Differences.push_back(
      "Function count mismatch: " + std::to_string(FAs.size()) +
      " vs " + std::to_string(FBs.size()));
  }

  // 5. Compare each function by name and structure
  // Build a name-to-function map for B
  SmallVector<IRFunction*, 16> FuncsB;
  for (auto& F : FBs) FuncsB.push_back(F.get());

  for (auto& FA : FAs) {
    // Find matching function by name in B
    IRFunction* MatchB = nullptr;
    for (auto* FB : FuncsB) {
      if (FB->getName() == FA->getName()) {
        MatchB = FB;
        break;
      }
    }
    if (!MatchB) {
      Result.Differences.push_back(
        "Function '" + FA->getName().str() + "' not found in second module");
      continue;
    }

    // Compare signatures
    if (!isTypeEquivalent(FA->getFunctionType(), MatchB->getFunctionType())) {
      Result.Differences.push_back(
        "Function signature mismatch for '" + FA->getName().str() + "'");
    }

    // Compare basic block count
    if (FA->getNumBasicBlocks() != MatchB->getNumBasicBlocks()) {
      Result.Differences.push_back(
        "Basic block count mismatch in '" + FA->getName().str() +
        "': " + std::to_string(FA->getNumBasicBlocks()) +
        " vs " + std::to_string(MatchB->getNumBasicBlocks()));
      continue;
    }

    // Compare instruction count and opcodes per basic block
    auto& BBsA = FA->getBasicBlocks();
    auto& BBsB = MatchB->getBasicBlocks();
    auto BBA = BBsA.begin();
    auto BBB = BBsB.begin();
    for (; BBA != BBsA.end() && BBB != BBsB.end(); ++BBA, ++BBB) {
      auto& InstsA = (*BBA)->getInstList();
      auto& InstsB = (*BBB)->getInstList();
      if (InstsA.size() != InstsB.size()) {
        Result.Differences.push_back(
          "Instruction count mismatch in function '" + FA->getName().str() +
          "' basic block '" + (*BBA)->getName().str() +
          "': " + std::to_string(InstsA.size()) +
          " vs " + std::to_string(InstsB.size()));
        continue;
      }
      auto IA = InstsA.begin();
      auto IB = InstsB.begin();
      for (; IA != InstsA.end() && IB != InstsB.end(); ++IA, ++IB) {
        if ((*IA)->getOpcode() != (*IB)->getOpcode()) {
          Result.Differences.push_back(
            "Opcode mismatch in '" + FA->getName().str() +
            "' block '" + (*BBA)->getName().str() + "'");
        }
      }
    }
  }

  Result.IsEquivalent = Result.Differences.empty();
  return Result;
}

} // namespace ir
} // namespace blocktype

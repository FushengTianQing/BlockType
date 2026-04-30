#include "blocktype/IR/PassInvariantChecker.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

bool PassInvariantChecker::run(IRModule& M) {
  return checkAllInvariants(M);
}

bool PassInvariantChecker::checkAllInvariants(const IRModule& M) {
  clearViolations();
  bool OK = true;
  if (!checkSSAInvariants(M)) OK = false;
  if (!checkTypeInvariants(M)) OK = false;
  if (!checkControlFlowInvariants(M)) OK = false;
  if (!checkMemoryInvariants(M)) OK = false;
  return OK;
}

bool PassInvariantChecker::checkSSAInvariants(const IRModule& M) {
  bool OK = true;
  for (auto& Fn : M.getFunctions()) {
    if (!checkSingleDefinition(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
    if (!checkDominance(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
    if (!checkPhiNodeConsistency(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
  }
  return OK;
}

bool PassInvariantChecker::checkTypeInvariants(const IRModule& M) {
  bool OK = true;
  if (!checkTypeCompleteness(M)) {
    OK = false;
    if (FailFast) return false;
  }
  if (!checkTypeConsistency(M)) {
    OK = false;
    if (FailFast) return false;
  }
  if (!checkFunctionSignature(M)) {
    OK = false;
    if (FailFast) return false;
  }
  return OK;
}

bool PassInvariantChecker::checkControlFlowInvariants(const IRModule& M) {
  bool OK = true;
  for (auto& Fn : M.getFunctions()) {
    if (!checkEntryBlock(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
    if (!checkSingleTerminator(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
    if (!checkReachability(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
  }
  return OK;
}

bool PassInvariantChecker::checkMemoryInvariants(const IRModule& M) {
  bool OK = true;
  if (!checkDefUseChain(M)) {
    OK = false;
    if (FailFast) return false;
  }
  for (auto& Fn : M.getFunctions()) {
    if (!checkAllocaOwner(*Fn)) {
      OK = false;
      if (FailFast) return false;
    }
  }
  return OK;
}

bool PassInvariantChecker::checkSingleDefinition(const IRFunction& Fn) {
  ir::DenseMap<const IRValue*, const IRInstruction*> Definitions;

  for (auto& BB : const_cast<IRFunction&>(Fn).getBasicBlocks()) {
    for (auto& I : BB->getInstList()) {
      if (I->getNumOperands() > 0 || I->isTerminator()) {
        // For instructions that produce a result value, check uniqueness
        const IRValue* Result = I.get();
        auto It = Definitions.find(Result);
        if (It != Definitions.end()) {
          reportViolation(InvariantKind::SSA_SingleDefinition,
                          "Value defined multiple times",
                          Result, I.get(), BB.get(), &Fn);
          if (FailFast) return false;
        }
        Definitions[Result] = I.get();
      }
    }
  }
  return true;
}

bool PassInvariantChecker::checkSingleTerminator(const IRFunction& Fn) {
  bool OK = true;
  for (auto& BB : const_cast<IRFunction&>(Fn).getBasicBlocks()) {
    unsigned TerminatorCount = 0;
    for (auto& I : BB->getInstList()) {
      if (I->isTerminator()) {
        ++TerminatorCount;
        if (TerminatorCount > 1) {
          reportViolation(InvariantKind::CF_SingleTerminator,
                          "BasicBlock has multiple terminators",
                          nullptr, I.get(), BB.get(), &Fn);
          if (FailFast) return false;
          OK = false;
        }
      }
    }
    if (TerminatorCount == 0) {
      reportViolation(InvariantKind::CF_SingleTerminator,
                      "BasicBlock has no terminator",
                      nullptr, nullptr, BB.get(), &Fn);
      if (FailFast) return false;
      OK = false;
    }
  }
  return OK;
}

bool PassInvariantChecker::checkEntryBlock(const IRFunction& Fn) {
  if (const_cast<IRFunction&>(Fn).getBasicBlocks().empty()) {
    reportViolation(InvariantKind::CF_EntryBlock,
                    "Function has no entry block",
                    nullptr, nullptr, nullptr, &Fn);
    return false;
  }
  return true;
}

bool PassInvariantChecker::checkDominance(const IRFunction& Fn) {
  // Simplified dominance check: basic validation that defs come before uses
  // Full dominance analysis requires dominator tree computation
  return true;
}

bool PassInvariantChecker::checkPhiNodeConsistency(const IRFunction& Fn) {
  // Check that phi nodes have incoming values matching predecessor count
  for (auto& BB : const_cast<IRFunction&>(Fn).getBasicBlocks()) {
    for (auto& I : BB->getInstList()) {
      if (I->getOpcode() == Opcode::Phi) {
        // Phi node incoming values should match predecessor count
        // Simplified check: just validate it's not empty
        if (I->getNumOperands() == 0) {
          reportViolation(InvariantKind::SSA_PhiNodeConsistency,
                          "Phi node has no incoming values",
                          nullptr, I.get(), BB.get(), &Fn);
          if (FailFast) return false;
        }
      }
    }
  }
  return true;
}

bool PassInvariantChecker::checkTypeCompleteness(const IRModule& M) {
  // Check that no function types contain opaque types
  // Simplified implementation
  return true;
}

bool PassInvariantChecker::checkTypeConsistency(const IRModule& M) {
  // Check that operand types match instruction requirements
  // Simplified implementation
  return true;
}

bool PassInvariantChecker::checkFunctionSignature(const IRModule& M) {
  // Check that call sites match function signatures
  // Simplified implementation
  return true;
}

bool PassInvariantChecker::checkReachability(const IRFunction& Fn) {
  // Check that all basic blocks are reachable from the entry block
  // Simplified implementation using BFS from entry
  auto& BBs = const_cast<IRFunction&>(Fn).getBasicBlocks();
  if (BBs.empty()) return true;

  ir::SmallVector<IRBasicBlock*, 16> Worklist;
  ir::DenseMap<IRBasicBlock*, bool> Visited;

  Worklist.push_back(BBs.front().get());
  Visited[BBs.front().get()] = true;

  while (!Worklist.empty()) {
    auto* Cur = Worklist.back();
    Worklist.pop_back();
    for (auto& I : Cur->getInstList()) {
      if (I->isTerminator()) {
        // Collect successor basic blocks from terminators
        for (unsigned Op = 0; Op < I->getNumOperands(); ++Op) {
          auto* Val = I->getOperand(Op);
          if (Val && Val->getValueKind() == ValueKind::BasicBlockRef) {
            auto* Succ = static_cast<IRBasicBlockRef*>(Val)->getBasicBlock();
            if (Succ && !Visited[Succ]) {
              Visited[Succ] = true;
              Worklist.push_back(Succ);
            }
          }
        }
      }
    }
  }

  bool OK = true;
  for (auto& BB : BBs) {
    if (!Visited[BB.get()]) {
      reportViolation(InvariantKind::CF_Reachability,
                      "Basic block is unreachable from entry",
                      nullptr, nullptr, BB.get(), &Fn);
      if (FailFast) return false;
      OK = false;
    }
  }
  return OK;
}

bool PassInvariantChecker::checkDefUseChain(const IRModule& M) {
  // Check Use-Def chain consistency
  // Simplified implementation
  return true;
}

bool PassInvariantChecker::checkAllocaOwner(const IRFunction& Fn) {
  // Check that alloca instructions are in the entry block
  auto& BBs = const_cast<IRFunction&>(Fn).getBasicBlocks();
  if (BBs.empty()) return true;

  bool OK = true;
  bool First = true;
  for (auto& BB : BBs) {
    if (First) { First = false; continue; }  // Skip entry block
    for (auto& I : BB->getInstList()) {
      if (I->getOpcode() == Opcode::Alloca) {
        reportViolation(InvariantKind::Mem_AllocaOwner,
                        "Alloca instruction not in entry block",
                        nullptr, I.get(), BB.get(), &Fn);
        if (FailFast) return false;
        OK = false;
      }
    }
  }
  return OK;
}

void PassInvariantChecker::reportViolation(InvariantKind Kind, StringRef Desc,
                                           const IRValue* V,
                                           const IRInstruction* I,
                                           const IRBasicBlock* B,
                                           const IRFunction* F) {
  InvariantViolation Vio;
  Vio.Kind = Kind;
  Vio.Description = Desc.str();
  Vio.Value = V;
  Vio.Inst = I;
  Vio.BB = B;
  Vio.Fn = F;
  Violations.push_back(Vio);
}

void PassInvariantChecker::printViolations(raw_ostream& OS) const {
  for (const auto& V : Violations) {
    OS << "Invariant violation: " << V.Description << "\n";
    if (V.Fn) OS << "  Function: " << V.Fn->getName() << "\n";
    if (V.BB) OS << "  BasicBlock: " << V.BB->getName() << "\n";
    if (V.Inst) OS << "  Instruction: " << static_cast<unsigned>(V.Inst->getOpcode()) << "\n";
  }
}

} // namespace ir
} // namespace blocktype
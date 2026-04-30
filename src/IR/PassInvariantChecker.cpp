#include "blocktype/IR/PassInvariantChecker.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"

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
  }
  return OK;
}

bool PassInvariantChecker::checkTypeInvariants(const IRModule& M) {
  return true; // TODO
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
  }
  return OK;
}

bool PassInvariantChecker::checkMemoryInvariants(const IRModule& M) {
  return true; // TODO
}

bool PassInvariantChecker::checkSingleDefinition(const IRFunction& Fn) {
  return true; // TODO
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
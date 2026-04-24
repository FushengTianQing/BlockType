#include "blocktype/IR/IRBasicBlock.h"

#include <algorithm>
#include <iterator>

#include "blocktype/IR/IRFunction.h"

namespace blocktype {
namespace ir {

IRInstruction* IRBasicBlock::getTerminator() {
  for (auto& Inst : InstList) {
    if (Inst->isTerminator())
      return Inst.get();
  }
  return nullptr;
}

IRInstruction* IRBasicBlock::getTerminator() const {
  for (auto& Inst : InstList) {
    if (Inst->isTerminator())
      return Inst.get();
  }
  return nullptr;
}

IRInstruction* IRBasicBlock::getFirstNonPHI() {
  for (auto& Inst : InstList) {
    if (Inst->getOpcode() != Opcode::Phi)
      return Inst.get();
  }
  return nullptr;
}

IRInstruction* IRBasicBlock::getFirstInsertionPt() {
  auto* Term = getTerminator();
  if (Term) return Term;
  return nullptr;
}

IRInstruction* IRBasicBlock::push_back(std::unique_ptr<IRInstruction> I) {
  auto* Ptr = I.get();
  I->setParent(this);
  InstList.push_back(std::move(I));
  return Ptr;
}

IRInstruction* IRBasicBlock::push_front(std::unique_ptr<IRInstruction> I) {
  auto* Ptr = I.get();
  I->setParent(this);
  InstList.push_front(std::move(I));
  return Ptr;
}

void IRBasicBlock::insert(IRInstruction* After, std::unique_ptr<IRInstruction> I) {
  if (!After) {
    push_back(std::move(I));
    return;
  }
  auto* Ptr = I.get();
  I->setParent(this);
  for (auto It = InstList.begin(); It != InstList.end(); ++It) {
    if (It->get() == After) {
      ++It;
      InstList.insert(It, std::move(I));
      return;
    }
  }
  InstList.push_back(std::move(I));
}

std::unique_ptr<IRInstruction> IRBasicBlock::erase(IRInstruction* I) {
  for (auto It = InstList.begin(); It != InstList.end(); ++It) {
    if (It->get() == I) {
      I->setParent(nullptr);
      auto Removed = std::move(*It);
      InstList.erase(It);
      return Removed;
    }
  }
  return nullptr;
}

SmallVector<IRBasicBlock*, 4> IRBasicBlock::getPredecessors() const {
  SmallVector<IRBasicBlock*, 4> Preds;
  if (!Parent) return Preds;
  for (auto& BB : Parent->getBasicBlocks()) {
    if (BB.get() == this) continue;
    auto* Term = BB->getTerminator();
    if (!Term) continue;
    for (unsigned i = 0; i < Term->getNumSuccessors(); ++i) {
      if (Term->getSuccessor(i) == this) {
        Preds.push_back(BB.get());
        break;
      }
    }
  }
  return Preds;
}

SmallVector<IRBasicBlock*, 4> IRBasicBlock::getSuccessors() const {
  SmallVector<IRBasicBlock*, 4> Succs;
  auto* Term = getTerminator();
  if (!Term) return Succs;
  for (unsigned i = 0; i < Term->getNumSuccessors(); ++i) {
    Succs.push_back(Term->getSuccessor(i));
  }
  return Succs;
}

void IRBasicBlock::print(std::ostream& OS) const {
  OS << Name << ":\n";
  for (auto& Inst : InstList) {
    OS << "  ";
    Inst->print(OS);
    OS << "\n";
  }
}

} // namespace ir
} // namespace blocktype

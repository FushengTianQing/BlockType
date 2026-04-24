#ifndef BLOCKTYPE_IR_IRBASICBLOCK_H
#define BLOCKTYPE_IR_IRBASICBLOCK_H

#include <list>
#include <memory>
#include <ostream>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRInstruction.h"

namespace blocktype {
namespace ir {

class IRBasicBlock {
  IRFunction* Parent;
  std::string Name;
  std::list<std::unique_ptr<IRInstruction>> InstList;

public:
  explicit IRBasicBlock(StringRef N, IRFunction* P = nullptr)
    : Parent(P), Name(N.str()) {}

  StringRef getName() const { return Name; }
  IRFunction* getParent() const { return Parent; }
  void setParent(IRFunction* F) { Parent = F; }

  auto& getInstList() { return InstList; }
  const auto& getInstList() const { return InstList; }

  IRInstruction* getTerminator();
  IRInstruction* getTerminator() const;
  IRInstruction* getFirstNonPHI();
  IRInstruction* getFirstInsertionPt();
  IRInstruction* push_back(std::unique_ptr<IRInstruction> I);
  IRInstruction* push_front(std::unique_ptr<IRInstruction> I);
  void insert(IRInstruction* After, std::unique_ptr<IRInstruction> I);
  std::unique_ptr<IRInstruction> erase(IRInstruction* I);
  SmallVector<IRBasicBlock*, 4> getPredecessors() const;
  SmallVector<IRBasicBlock*, 4> getSuccessors() const;

  size_t size() const { return InstList.size(); }
  bool empty() const { return InstList.empty(); }

  void print(std::ostream& OS) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRBASICBLOCK_H

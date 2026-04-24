#ifndef BLOCKTYPE_IR_IRBASICBLOCK_H
#define BLOCKTYPE_IR_IRBASICBLOCK_H

#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "blocktype/IR/IRInstruction.h"

namespace blocktype {
namespace ir {

class IRBasicBlock {
  IRFunction* Parent;
  std::string Name;
  std::list<std::unique_ptr<IRInstruction>> InstList;

public:
  explicit IRBasicBlock(std::string_view N, IRFunction* P = nullptr)
    : Parent(P), Name(N) {}

  std::string_view getName() const { return Name; }
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
  std::vector<IRBasicBlock*> getPredecessors() const;
  std::vector<IRBasicBlock*> getSuccessors() const;

  size_t size() const { return InstList.size(); }
  bool empty() const { return InstList.empty(); }

  void print(std::ostream& OS) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRBASICBLOCK_H

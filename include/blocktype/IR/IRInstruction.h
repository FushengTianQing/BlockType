#ifndef BLOCKTYPE_IR_IRINSTRUCTION_H
#define BLOCKTYPE_IR_IRINSTRUCTION_H

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

class IRInstruction : public User {
  Opcode Op;
  dialect::DialectID DialectID_;
  IRBasicBlock* Parent;
  std::vector<IRBasicBlock*> Successors;

public:
  IRInstruction(Opcode O, IRType* Ty, unsigned ID,
                dialect::DialectID D = dialect::DialectID::Core, std::string_view N = "")
    : User(ValueKind::InstructionResult, Ty, ID, N),
      Op(O), DialectID_(D), Parent(nullptr) {}

  Opcode getOpcode() const { return Op; }
  dialect::DialectID getDialect() const { return DialectID_; }
  IRBasicBlock* getParent() const { return Parent; }
  void setParent(IRBasicBlock* BB) { Parent = BB; }

  unsigned getNumSuccessors() const { return static_cast<unsigned>(Successors.size()); }
  IRBasicBlock* getSuccessor(unsigned i) const { return Successors[i]; }
  void addSuccessor(IRBasicBlock* BB) { Successors.push_back(BB); }
  void removeSuccessor(unsigned i) { Successors.erase(Successors.begin() + static_cast<intptr_t>(i)); }
  bool isTerminator() const;
  bool isBinaryOp() const;
  bool isCast() const;
  bool isMemoryOp() const;
  bool isComparison() const;
  void eraseFromParent();
  void print(std::ostream& OS) const override;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRINSTRUCTION_H

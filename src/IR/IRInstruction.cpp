#include "blocktype/IR/IRInstruction.h"

#include <ostream>
#include <string>

namespace blocktype {
namespace ir {

bool IRInstruction::isTerminator() const {
  switch (Op) {
  case Opcode::Ret:
  case Opcode::Br:
  case Opcode::CondBr:
  case Opcode::Switch:
  case Opcode::Invoke:
  case Opcode::Unreachable:
  case Opcode::Resume:
    return true;
  default:
    return false;
  }
}

bool IRInstruction::isBinaryOp() const {
  auto V = static_cast<uint16_t>(Op);
  return (V >= 16 && V <= 22) || (V >= 32 && V <= 36) ||
         (V >= 48 && V <= 53);
}

bool IRInstruction::isCast() const {
  auto V = static_cast<uint16_t>(Op);
  return V >= 80 && V <= 91;
}

bool IRInstruction::isMemoryOp() const {
  auto V = static_cast<uint16_t>(Op);
  return (V >= 64 && V <= 69) || (V >= 176 && V <= 180);
}

bool IRInstruction::isComparison() const {
  return Op == Opcode::ICmp || Op == Opcode::FCmp;
}

void IRInstruction::eraseFromParent() {
  if (Parent) {
    Parent = nullptr;
  }
}

void IRInstruction::print(raw_ostream& OS) const {
  static const char* OpcodeNames[] = {
    "ret", "br", "condbr", "switch", "invoke", "unreachable", "resume",
    "", "", "", "", "", "", "", "", "",
    "add", "sub", "mul", "udiv", "sdiv", "urem", "srem",
    "", "", "", "", "", "", "", "", "",
    "fadd", "fsub", "fmul", "fdiv", "frem",
    "", "", "", "", "", "", "", "", "", "", "", "",
    "shl", "lshr", "ashr", "and", "or", "xor",
    "", "", "", "", "", "", "", "", "", "", "", "",
    "alloca", "load", "store", "gep", "memcpy", "memset",
    "", "", "", "", "", "", "", "", "", "", "", "",
    "trunc", "zext", "sext", "fptrunc", "fpext",
    "fptosi", "fptoui", "sitofp", "uitofp",
    "ptrtoint", "inttoptr", "bitcast",
    "", "", "", "", "",
    "icmp", "fcmp",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "call",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "phi", "select", "extractvalue", "insertvalue",
    "extractelement", "insertelement", "shufflevector",
    "", "", "", "", "", "", "", "", "",
    "dbg_declare", "dbg_value", "dbg_label",
    "", "", "", "", "", "", "", "", "", "", "", "", "",
    "ffi_call", "ffi_check", "ffi_coerce", "ffi_unwind",
    "", "", "", "", "", "", "", "", "", "", "", "",
    "atomic_load", "atomic_store", "atomic_rmw", "atomic_cmpxchg", "fence"
  };

  auto Idx = static_cast<size_t>(Op);
  const char* Name = (Idx < sizeof(OpcodeNames) / sizeof(OpcodeNames[0]) && OpcodeNames[Idx][0])
                     ? OpcodeNames[Idx] : "unknown";
  OS << "%" << getValueID() << " = " << Name;
  if (getNumOperands() > 0) {
    OS << " ";
    for (unsigned i = 0; i < getNumOperands(); ++i) {
      if (i > 0) OS << ", ";
      OS << "%" << getOperand(i)->getValueID();
    }
  }
}

} // namespace ir
} // namespace blocktype

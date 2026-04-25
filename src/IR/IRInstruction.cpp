#include "blocktype/IR/IRInstruction.h"

#include <ostream>
#include <string>

namespace blocktype {
namespace ir {

static const char* icmpPredToText(ICmpPred P) {
  switch (P) {
  case ICmpPred::EQ:  return "eq";
  case ICmpPred::NE:  return "ne";
  case ICmpPred::UGT: return "ugt";
  case ICmpPred::UGE: return "uge";
  case ICmpPred::ULT: return "ult";
  case ICmpPred::ULE: return "ule";
  case ICmpPred::SGT: return "sgt";
  case ICmpPred::SGE: return "sge";
  case ICmpPred::SLT: return "slt";
  case ICmpPred::SLE: return "sle";
  }
  return "unknown";
}

static const char* fcmpPredToText(FCmpPred P) {
  switch (P) {
  case FCmpPred::False: return "false";
  case FCmpPred::OEQ:   return "oeq";
  case FCmpPred::OGT:   return "ogt";
  case FCmpPred::OGE:   return "oge";
  case FCmpPred::OLT:   return "olt";
  case FCmpPred::OLE:   return "ole";
  case FCmpPred::ONE:   return "one";
  case FCmpPred::ORD:   return "ord";
  case FCmpPred::UNO:   return "uno";
  case FCmpPred::UEQ:   return "ueq";
  case FCmpPred::UGT:   return "ugt";
  case FCmpPred::UGE:   return "uge";
  case FCmpPred::ULT:   return "ult";
  case FCmpPred::ULE:   return "ule";
  case FCmpPred::UNE:   return "une";
  case FCmpPred::True:  return "true";
  }
  return "unknown";
}

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
  // 输出比较谓词
  if (Op == Opcode::ICmp) {
    OS << " " << icmpPredToText(getICmpPredicate());
  } else if (Op == Opcode::FCmp) {
    OS << " " << fcmpPredToText(getFCmpPredicate());
  }
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

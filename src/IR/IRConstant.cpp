#include "blocktype/IR/IRConstant.h"

#include <ostream>

namespace blocktype {
namespace ir {

void IRConstantInt::print(raw_ostream& OS) const {
  OS << Value.toString(10, true);
}

void IRConstantFP::print(raw_ostream& OS) const {
  OS << Value.toString();
}

void IRConstantNull::print(raw_ostream& OS) const {
  OS << "null";
}

void IRConstantUndef::print(raw_ostream& OS) const {
  OS << "undef";
}

void IRConstantAggregateZero::print(raw_ostream& OS) const {
  OS << "zeroinitializer";
}

void IRConstantStruct::print(raw_ostream& OS) const {
  OS << "{ ";
  for (size_t i = 0; i < Elements.size(); ++i) {
    if (i > 0) OS << ", ";
    Elements[i]->print(OS);
  }
  OS << " }";
}

void IRConstantArray::print(raw_ostream& OS) const {
  OS << "[ ";
  for (size_t i = 0; i < Elements.size(); ++i) {
    if (i > 0) OS << ", ";
    Elements[i]->print(OS);
  }
  OS << " ]";
}

IRConstantFunctionRef::IRConstantFunctionRef(IRFunction* F)
  : IRConstant(ValueKind::ConstantFunctionRef, nullptr, 0), Func(F) {}

void IRConstantFunctionRef::print(raw_ostream& OS) const {
  OS << "@func_ref";
}

IRConstantGlobalRef::IRConstantGlobalRef(IRGlobalVariable* G)
  : IRConstant(ValueKind::ConstantGlobalRef, nullptr, 0), Global(G) {}

void IRConstantGlobalRef::print(raw_ostream& OS) const {
  OS << "@global_ref";
}

} // namespace ir
} // namespace blocktype

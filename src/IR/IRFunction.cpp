#include "blocktype/IR/IRFunction.h"

#include <iostream>

#include "blocktype/IR/IRModule.h"

namespace blocktype {
namespace ir {

void IRArgument::print(raw_ostream& OS) const {
  if (!Name.empty()) {
    OS << Name;
  }
  if (ParamType) {
    OS << " : " << ParamType->toString();
  }
}

IRFunction::IRFunction(IRModule* M, StringRef N, IRFunctionType* T,
                       LinkageKind L, CallingConvention CC)
    : Parent(M), Name(N.str()), Ty(T), Linkage(L), CallConv(CC) {
  unsigned NumParams = T->getNumParams();
  Args.reserve(NumParams);
  for (unsigned i = 0; i < NumParams; ++i) {
    Args.push_back(std::make_unique<IRArgument>(T->getParamType(i), i));
  }
}

IRBasicBlock* IRFunction::addBasicBlock(StringRef BBName) {
  auto BB = std::make_unique<IRBasicBlock>(BBName, this);
  auto* Ptr = BB.get();
  BasicBlocks.push_back(std::move(BB));
  return Ptr;
}

IRBasicBlock* IRFunction::getEntryBlock() {
  if (BasicBlocks.empty()) return nullptr;
  return BasicBlocks.front().get();
}

void IRFunction::print(raw_ostream& OS) const {
  OS << "define ";
  OS << Ty->getReturnType()->toString() << " @" << Name << "(";
  for (unsigned i = 0; i < getNumArgs(); ++i) {
    if (i > 0) OS << ", ";
    Args[i]->print(OS);
  }
  OS << ") {\n";
  for (auto& BB : BasicBlocks) {
    BB->print(OS);
  }
  OS << "}\n";
}

} // namespace ir
} // namespace blocktype

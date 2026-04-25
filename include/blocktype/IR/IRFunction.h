#ifndef BLOCKTYPE_IR_IRFUNCTION_H
#define BLOCKTYPE_IR_IRFUNCTION_H

#include <list>
#include <memory>
#include <ostream>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

class IRArgument {
  IRType* ParamType;
  std::string Name;
  unsigned ArgNo;
  unsigned Attrs = 0;

public:
  IRArgument(IRType* T, unsigned No, StringRef N = "")
    : ParamType(T), Name(N.str()), ArgNo(No) {}

  IRType* getType() const { return ParamType; }
  StringRef getName() const { return Name; }
  unsigned getArgNo() const { return ArgNo; }
  bool hasAttr(unsigned A) const { return (Attrs & A) != 0; }
  void addAttr(unsigned A) { Attrs |= A; }

  void print(raw_ostream& OS) const;
};

class IRModule;

class IRFunction {
  friend class IRModule;
  IRModule* Parent;
  std::string Name;
  IRFunctionType* Ty;
  LinkageKind Linkage;
  CallingConvention CallConv;
  SmallVector<std::unique_ptr<IRArgument>, 8> Args;
  std::list<std::unique_ptr<IRBasicBlock>> BasicBlocks;
  FunctionAttrs Attrs = 0;
  unsigned Alignment = 0;
  StringRef Section;

public:
  IRFunction(IRModule* M, StringRef N, IRFunctionType* T,
             LinkageKind L = LinkageKind::External,
             CallingConvention CC = CallingConvention::C);

  StringRef getName() const { return Name; }
  IRModule* getParent() const { return Parent; }
  IRFunctionType* getFunctionType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  CallingConvention getCallingConv() const { return CallConv; }
  FunctionAttrs getAttributes() const { return Attrs; }
  void addAttribute(FunctionAttr A) { Attrs |= static_cast<uint32_t>(A); }

  unsigned getNumArgs() const { return static_cast<unsigned>(Args.size()); }
  IRArgument* getArg(unsigned i) const { return Args[i].get(); }
  IRType* getArgType(unsigned i) const { return Args[i]->getType(); }

  IRBasicBlock* addBasicBlock(StringRef Name);
  IRBasicBlock* getEntryBlock();
  auto& getBasicBlocks() { return BasicBlocks; }
  unsigned getNumBasicBlocks() const { return static_cast<unsigned>(BasicBlocks.size()); }

  IRType* getReturnType() const { return Ty->getReturnType(); }
  bool isDeclaration() const { return BasicBlocks.empty(); }
  bool isDefinition() const { return !BasicBlocks.empty(); }

  void print(raw_ostream& OS) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRFUNCTION_H

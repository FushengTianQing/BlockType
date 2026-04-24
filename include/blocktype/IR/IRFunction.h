#ifndef BLOCKTYPE_IR_IRFUNCTION_H
#define BLOCKTYPE_IR_IRFUNCTION_H

#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

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
  IRArgument(IRType* T, unsigned No, std::string_view N = "")
    : ParamType(T), Name(N), ArgNo(No) {}

  IRType* getType() const { return ParamType; }
  std::string_view getName() const { return Name; }
  unsigned getArgNo() const { return ArgNo; }
  bool hasAttr(unsigned A) const { return (Attrs & A) != 0; }
  void addAttr(unsigned A) { Attrs |= A; }

  void print(std::ostream& OS) const;
};

class IRModule;

class IRFunction {
  friend class IRModule;
  IRModule* Parent;
  std::string Name;
  IRFunctionType* Ty;
  LinkageKind Linkage;
  CallingConvention CallConv;
  std::vector<std::unique_ptr<IRArgument>> Args;
  std::list<std::unique_ptr<IRBasicBlock>> BasicBlocks;
  FunctionAttrs Attrs = 0;
  unsigned Alignment = 0;
  std::string Section;

public:
  IRFunction(IRModule* M, std::string_view N, IRFunctionType* T,
             LinkageKind L = LinkageKind::External,
             CallingConvention CC = CallingConvention::C);

  std::string_view getName() const { return Name; }
  IRModule* getParent() const { return Parent; }
  IRFunctionType* getFunctionType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  CallingConvention getCallingConv() const { return CallConv; }
  FunctionAttrs getAttributes() const { return Attrs; }
  void addAttribute(FunctionAttr A) { Attrs |= static_cast<uint32_t>(A); }

  unsigned getNumArgs() const { return static_cast<unsigned>(Args.size()); }
  IRArgument* getArg(unsigned i) const { return Args[i].get(); }
  IRType* getArgType(unsigned i) const { return Args[i]->getType(); }

  IRBasicBlock* addBasicBlock(std::string_view Name);
  IRBasicBlock* getEntryBlock();
  auto& getBasicBlocks() { return BasicBlocks; }
  unsigned getNumBasicBlocks() const { return static_cast<unsigned>(BasicBlocks.size()); }

  IRType* getReturnType() const { return Ty->getReturnType(); }
  bool isDeclaration() const { return BasicBlocks.empty(); }
  bool isDefinition() const { return !BasicBlocks.empty(); }

  void print(std::ostream& OS) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRFUNCTION_H

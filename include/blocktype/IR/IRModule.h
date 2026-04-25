#ifndef BLOCKTYPE_IR_IRMODULE_H
#define BLOCKTYPE_IR_IRMODULE_H

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace ir {

enum class IRFeature : uint32_t {
  IntegerArithmetic  = 1 << 0,
  FloatArithmetic    = 1 << 1,
  VectorOperations   = 1 << 2,
  AtomicOperations   = 1 << 3,
  ExceptionHandling  = 1 << 4,
  DebugInfo          = 1 << 5,
  VarArg             = 1 << 6,
  SeparateFloatInt   = 1 << 7,
  StructReturn       = 1 << 8,
  DynamicCast        = 1 << 9,
  VirtualDispatch    = 1 << 10,
  Coroutines         = 1 << 11,
};

class IRMetadata {
public:
  virtual ~IRMetadata() = default;
  virtual void print(raw_ostream& OS) const = 0;
};

class IRFunctionDecl {
  std::string Name;
  IRFunctionType* Ty;
  LinkageKind Linkage;
  CallingConvention CallConv;

public:
  IRFunctionDecl(StringRef N, IRFunctionType* T,
                 LinkageKind L = LinkageKind::External,
                 CallingConvention CC = CallingConvention::C);

  StringRef getName() const { return Name; }
  IRFunctionType* getFunctionType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  CallingConvention getCallingConv() const { return CallConv; }
};

class IRGlobalVariable {
  std::string Name;
  IRType* Ty;
  LinkageKind Linkage;
  IRConstant* Initializer = nullptr;
  unsigned Alignment = 0;
  bool IsConstant = false;
  std::string Section;
  unsigned AddressSpace = 0;

public:
  IRGlobalVariable(StringRef N, IRType* T, bool IsConst,
                   LinkageKind L = LinkageKind::External,
                   IRConstant* Init = nullptr, unsigned Align = 0, unsigned AS = 0);

  StringRef getName() const { return Name; }
  IRType* getType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  IRConstant* getInitializer() const { return Initializer; }
  void setInitializer(IRConstant* C) { Initializer = C; }
  bool hasInitializer() const { return Initializer != nullptr; }
  unsigned getAlignment() const { return Alignment; }
  bool isConstant() const { return IsConstant; }
  unsigned getAddressSpace() const { return AddressSpace; }
};

class IRGlobalAlias {
  std::string Name;
  IRType* Ty;
  IRConstant* Aliasee;

public:
  IRGlobalAlias(StringRef N, IRType* T, IRConstant* A);

  StringRef getName() const { return Name; }
  IRType* getType() const { return Ty; }
  IRConstant* getAliasee() const { return Aliasee; }
};

class IRModule {
  IRTypeContext& TypeCtx;
  std::string Name;
  std::string TargetTriple;
  std::string DataLayoutStr;
  std::vector<std::unique_ptr<IRFunction>> Functions;
  std::vector<std::unique_ptr<IRFunctionDecl>> FunctionDecls;
  std::vector<std::unique_ptr<IRGlobalVariable>> Globals;
  std::vector<std::unique_ptr<IRGlobalAlias>> Aliases;
  std::vector<std::unique_ptr<IRMetadata>> Metadata;
  bool IsReproducible = false;
  bool IsSealed = false;
  uint32_t RequiredFeatures = 0;

public:
  IRModule(StringRef N, IRTypeContext& Ctx,
           StringRef Triple = "", StringRef DL = "");

  StringRef getName() const { return Name; }
  StringRef getTargetTriple() const { return TargetTriple; }
  void setTargetTriple(StringRef T) { TargetTriple = T.str(); }
  IRTypeContext& getTypeContext() const { return TypeCtx; }

  bool isSealed() const { return IsSealed; }
  void seal() { IsSealed = true; }

  IRFunction* getFunction(StringRef Name) const;
  IRFunction* getOrInsertFunction(StringRef Name, IRFunctionType* Ty);
  void addFunction(std::unique_ptr<IRFunction> F);
  auto& getFunctions() { return Functions; }
  unsigned getNumFunctions() const { return static_cast<unsigned>(Functions.size()); }

  IRFunctionDecl* getFunctionDecl(StringRef Name) const;
  void addFunctionDecl(std::unique_ptr<IRFunctionDecl> D);

  IRGlobalVariable* getGlobalVariable(StringRef Name) const;
  IRGlobalVariable* getOrInsertGlobal(StringRef Name, IRType* Ty);
  void addGlobal(std::unique_ptr<IRGlobalVariable> GV);
  auto& getGlobals() { return Globals; }

  void addAlias(std::unique_ptr<IRGlobalAlias> A);
  void addMetadata(std::unique_ptr<IRMetadata> M);

  bool isReproducible() const { return IsReproducible; }
  void setReproducible(bool V) { IsReproducible = V; }

  uint32_t getRequiredFeatures() const { return RequiredFeatures; }
  void addRequiredFeature(IRFeature F) { RequiredFeatures |= static_cast<uint32_t>(F); }

  void print(raw_ostream& OS) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRMODULE_H

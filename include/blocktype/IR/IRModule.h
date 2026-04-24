#ifndef BLOCKTYPE_IR_IRMODULE_H
#define BLOCKTYPE_IR_IRMODULE_H

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace ir {

enum class IRFeature : uint32_t {
  None = 0,
  Exceptions = 1 << 0,
  Threads = 1 << 1,
  SIMD = 1 << 2,
};

class IRMetadata {
public:
  virtual ~IRMetadata() = default;
  virtual void print(std::ostream& OS) const = 0;
};

class IRFunctionDecl {
  std::string Name;
  IRFunctionType* Ty;
  LinkageKind Linkage;
  CallingConvention CallConv;

public:
  IRFunctionDecl(std::string_view N, IRFunctionType* T,
                 LinkageKind L = LinkageKind::External,
                 CallingConvention CC = CallingConvention::C);

  std::string_view getName() const { return Name; }
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
  IRGlobalVariable(std::string_view N, IRType* T, bool IsConst,
                   LinkageKind L = LinkageKind::External,
                   IRConstant* Init = nullptr, unsigned Align = 0, unsigned AS = 0);

  std::string_view getName() const { return Name; }
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
  IRGlobalAlias(std::string_view N, IRType* T, IRConstant* A);

  std::string_view getName() const { return Name; }
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
  uint32_t RequiredFeatures = 0;

public:
  IRModule(std::string_view N, IRTypeContext& Ctx,
           std::string_view Triple = "", std::string_view DL = "");

  std::string_view getName() const { return Name; }
  std::string_view getTargetTriple() const { return TargetTriple; }
  void setTargetTriple(std::string_view T) { TargetTriple = T; }
  IRTypeContext& getTypeContext() const { return TypeCtx; }

  IRFunction* getFunction(std::string_view Name) const;
  IRFunction* getOrInsertFunction(std::string_view Name, IRFunctionType* Ty);
  void addFunction(std::unique_ptr<IRFunction> F);
  auto& getFunctions() { return Functions; }
  unsigned getNumFunctions() const { return static_cast<unsigned>(Functions.size()); }

  IRFunctionDecl* getFunctionDecl(std::string_view Name) const;
  void addFunctionDecl(std::unique_ptr<IRFunctionDecl> D);

  IRGlobalVariable* getGlobalVariable(std::string_view Name) const;
  IRGlobalVariable* getOrInsertGlobal(std::string_view Name, IRType* Ty);
  void addGlobal(std::unique_ptr<IRGlobalVariable> GV);
  auto& getGlobals() { return Globals; }

  void addAlias(std::unique_ptr<IRGlobalAlias> A);
  void addMetadata(std::unique_ptr<IRMetadata> M);

  bool isReproducible() const { return IsReproducible; }
  void setReproducible(bool V) { IsReproducible = V; }

  uint32_t getRequiredFeatures() const { return RequiredFeatures; }
  void addRequiredFeature(IRFeature F) { RequiredFeatures |= static_cast<uint32_t>(F); }

  void print(std::ostream& OS) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRMODULE_H

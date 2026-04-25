#include "blocktype/IR/IRModule.h"

#include <iostream>

namespace blocktype {
namespace ir {

IRFunctionDecl::IRFunctionDecl(StringRef N, IRFunctionType* T,
                               LinkageKind L, CallingConvention CC)
    : Name(N.str()), Ty(T), Linkage(L), CallConv(CC) {}

IRGlobalVariable::IRGlobalVariable(StringRef N, IRType* T, bool IsConst,
                                   LinkageKind L, IRConstant* Init,
                                   unsigned Align, unsigned AS)
    : Name(N.str()), Ty(T), Linkage(L), Initializer(Init),
      Alignment(Align), IsConstant(IsConst), AddressSpace(AS) {}

IRGlobalAlias::IRGlobalAlias(StringRef N, IRType* T, IRConstant* A)
    : Name(N.str()), Ty(T), Aliasee(A) {}

IRModule::IRModule(StringRef N, IRTypeContext& Ctx,
                   StringRef Triple, StringRef DL)
    : TypeCtx(Ctx), Name(N.str()), TargetTriple(Triple.str()), DataLayoutStr(DL.str()) {}

IRFunction* IRModule::getFunction(StringRef FuncName) const {
  for (auto& F : Functions) {
    if (StringRef(F->getName()) == FuncName) return F.get();
  }
  return nullptr;
}

IRFunction* IRModule::getOrInsertFunction(StringRef FuncName, IRFunctionType* FTy) {
  assert(!IsSealed && "Cannot modify sealed module");
  for (auto& F : Functions) {
    if (StringRef(F->getName()) == FuncName) {
      if (F->getFunctionType() == FTy) return F.get();
      return nullptr;
    }
  }
  auto F = std::make_unique<IRFunction>(this, FuncName, FTy);
  auto* Ptr = F.get();
  Functions.push_back(std::move(F));
  return Ptr;
}

void IRModule::addFunction(std::unique_ptr<IRFunction> F) {
  assert(!IsSealed && "Cannot modify sealed module");
  F->Parent = this;
  Functions.push_back(std::move(F));
}

IRFunctionDecl* IRModule::getFunctionDecl(StringRef DeclName) const {
  for (auto& D : FunctionDecls) {
    if (StringRef(D->getName()) == DeclName) return D.get();
  }
  return nullptr;
}

void IRModule::addFunctionDecl(std::unique_ptr<IRFunctionDecl> D) {
  assert(!IsSealed && "Cannot modify sealed module");
  FunctionDecls.push_back(std::move(D));
}

IRGlobalVariable* IRModule::getGlobalVariable(StringRef GlobalName) const {
  for (auto& GV : Globals) {
    if (StringRef(GV->getName()) == GlobalName) return GV.get();
  }
  return nullptr;
}

IRGlobalVariable* IRModule::getOrInsertGlobal(StringRef GlobalName, IRType* Ty) {
  assert(!IsSealed && "Cannot modify sealed module");
  for (auto& GV : Globals) {
    if (StringRef(GV->getName()) == GlobalName) {
      if (GV->getType() == Ty) return GV.get();
      return nullptr;
    }
  }
  auto GV = std::make_unique<IRGlobalVariable>(GlobalName, Ty, false);
  auto* Ptr = GV.get();
  Globals.push_back(std::move(GV));
  return Ptr;
}

void IRModule::addGlobal(std::unique_ptr<IRGlobalVariable> GV) {
  assert(!IsSealed && "Cannot modify sealed module");
  Globals.push_back(std::move(GV));
}

void IRModule::addAlias(std::unique_ptr<IRGlobalAlias> A) {
  assert(!IsSealed && "Cannot modify sealed module");
  Aliases.push_back(std::move(A));
}

void IRModule::addMetadata(std::unique_ptr<IRMetadata> M) {
  assert(!IsSealed && "Cannot modify sealed module");
  Metadata.push_back(std::move(M));
}

void IRModule::print(raw_ostream& OS) const {
  if (!TargetTriple.empty()) OS << "target triple = \"" << TargetTriple << "\"\n";
  if (!DataLayoutStr.empty()) OS << "target datalayout = \"" << DataLayoutStr << "\"\n";
  for (auto& GV : Globals) {
    OS << "@" << GV->getName() << " = ";
    if (GV->isConstant()) OS << "constant ";
    else OS << "global ";
    OS << GV->getType()->toString();
    if (GV->hasInitializer()) OS << " <initializer>";
    OS << "\n";
  }
  for (auto& F : Functions) {
    F->print(OS);
    OS << "\n";
  }
  for (auto& D : FunctionDecls) {
    OS << "declare ";
    OS << D->getFunctionType()->getReturnType()->toString();
    OS << " @" << D->getName() << "(";
    for (unsigned i = 0; i < D->getFunctionType()->getNumParams(); ++i) {
      if (i > 0) OS << ", ";
      OS << D->getFunctionType()->getParamType(i)->toString();
    }
    OS << ")\n";
  }
}

} // namespace ir
} // namespace blocktype

#include <cassert>
#include <memory>
#include <string>

#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRValue.h"

using namespace blocktype::ir;

int main() {
  IRTypeContext Ctx;
  IRModule Mod("test", Ctx);

  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {Ctx.getInt32Ty(), Ctx.getInt32Ty()});
  auto* F = Mod.getOrInsertFunction("foo", FTy);
  assert(F != nullptr);
  assert(F->getName() == "foo");

  auto* F2 = Mod.getFunction("foo");
  assert(F2 == F);

  auto* F3 = Mod.getOrInsertFunction("foo", FTy);
  assert(F3 == F);

  assert(F->isDeclaration());

  auto* Entry = F->addBasicBlock("entry");
  assert(Entry != nullptr);
  assert(Entry->getName() == "entry");
  assert(Entry->getParent() == F);

  assert(!F->isDeclaration());
  assert(F->isDefinition());
  assert(F->getNumBasicBlocks() == 1);
  assert(F->getEntryBlock() == Entry);

  assert(Entry->getTerminator() == nullptr);
  assert(Entry->empty());
  assert(Entry->size() == 0);

  auto RetInst = std::make_unique<IRInstruction>(Opcode::Ret, Ctx.getInt32Ty(), 0);
  auto* RetPtr = Entry->push_back(std::move(RetInst));
  assert(RetPtr != nullptr);
  assert(!Entry->empty());
  assert(Entry->size() == 1);
  assert(Entry->getTerminator() == RetPtr);

  auto* Int32Const = new IRConstantInt(static_cast<IRIntegerType*>(Ctx.getInt32Ty()), 42U);
  RetPtr->addOperand(Int32Const);
  assert(RetPtr->getNumOperands() == 1);

  auto* SecondBB = F->addBasicBlock("second");
  assert(F->getNumBasicBlocks() == 2);

  assert(F->getFunctionType() == FTy);
  assert(F->getReturnType() == Ctx.getInt32Ty());
  assert(F->getNumArgs() == 2);
  assert(F->getArg(0) != nullptr);
  assert(F->getArg(1) != nullptr);
  assert(F->getArgType(0) == Ctx.getInt32Ty());
  assert(F->getArgType(1) == Ctx.getInt32Ty());

  assert(Mod.getNumFunctions() == 1);

  auto* GVar = Mod.getOrInsertGlobal("my_global", Ctx.getInt32Ty());
  assert(GVar != nullptr);
  assert(GVar->getName() == "my_global");
  assert(GVar->getType() == Ctx.getInt32Ty());
  assert(!GVar->hasInitializer());

  auto ConstGVar = std::make_unique<IRGlobalVariable>("const_global", Ctx.getInt32Ty(), true);
  Mod.addGlobal(std::move(ConstGVar));
  assert(Mod.getGlobalVariable("const_global") != nullptr);

  auto Decl = std::make_unique<IRFunctionDecl>("bar", FTy);
  Mod.addFunctionDecl(std::move(Decl));
  assert(Mod.getFunctionDecl("bar") != nullptr);

  assert(Mod.getTypeContext().getInt32Ty() == Ctx.getInt32Ty());

  Mod.setTargetTriple("x86_64-unknown-linux-gnu");
  assert(Mod.getTargetTriple() == "x86_64-unknown-linux-gnu");

  Mod.setReproducible(true);
  assert(Mod.isReproducible());

  Mod.addRequiredFeature(IRFeature::Exceptions);
  assert(Mod.getRequiredFeatures() != 0);

  return 0;
}

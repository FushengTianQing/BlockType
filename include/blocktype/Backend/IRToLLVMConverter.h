#pragma once
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRValue.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRDebugInfo.h"
#include "blocktype/IR/IRDebugMetadata.h"
#include "blocktype/Backend/BackendOptions.h"

#include "llvm/IR/IRBuilder.h"

// LLVM 前向声明
namespace llvm {
  class LLVMContext;
  class Module;
  class Type;
  class Value;
  class Function;
  class BasicBlock;
  class Constant;
  class GlobalVariable;
} // namespace llvm

namespace blocktype::backend {

class IRToLLVMConverter {
  ir::IRTypeContext& IRCtx;
  llvm::LLVMContext& LLVMCtx;
  const BackendOptions& Opts;
  DiagnosticsEngine& Diags;
  std::unique_ptr<llvm::Module> TheModule;

  ir::DenseMap<const ir::IRType*, llvm::Type*> TypeMap;
  ir::DenseMap<const ir::IRValue*, llvm::Value*> ValueMap;
  ir::DenseMap<const ir::IRFunction*, llvm::Function*> FunctionMap;
  ir::DenseMap<const ir::IRBasicBlock*, llvm::BasicBlock*> BlockMap;
  ir::DenseMap<const ir::IRGlobalVariable*, llvm::GlobalVariable*> GlobalVarMap;

public:
  IRToLLVMConverter(ir::IRTypeContext& IRCtx,
                    llvm::LLVMContext& LLVMCtx,
                    const BackendOptions& Opts = BackendOptions(),
                    DiagnosticsEngine& Diags = getNullDiags());
  ~IRToLLVMConverter();

  std::unique_ptr<llvm::Module> convert(ir::IRModule& IRModule);
  llvm::Type* mapType(const ir::IRType* T);

private:
  llvm::Value* mapValue(const ir::IRValue* V);
  llvm::Function* mapFunction(const ir::IRFunction* F);
  llvm::BasicBlock* mapBasicBlock(const ir::IRBasicBlock* BB);
  void convertFunction(ir::IRFunction& IRF, llvm::Function* LLVMF);
  void convertInstruction(ir::IRInstruction& I, llvm::IRBuilder<>& Builder);
  void convertGlobalVariable(ir::IRGlobalVariable& IRGV);
  llvm::Constant* convertConstant(const ir::IRConstant* C);
  void convertDebugInfo(ir::IRModule& IRModule, llvm::Module& LLVMMod);

  static DiagnosticsEngine& getNullDiags();
};

} // namespace blocktype::backend

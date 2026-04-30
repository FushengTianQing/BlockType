#pragma once
#include "blocktype/Backend/BackendBase.h"

// LLVM 前向声明
namespace llvm {
  class LLVMContext;
  class Module;
  class TargetMachine;
} // namespace llvm

namespace blocktype::backend {

class IRToLLVMConverter;

class LLVMBackend : public BackendBase {
  std::unique_ptr<llvm::LLVMContext> LLVMCtx;
  std::unique_ptr<IRToLLVMConverter> Converter;

public:
  LLVMBackend(const BackendOptions& Opts, DiagnosticsEngine& Diags);

  ir::StringRef getName() const override { return "llvm"; }
  bool emitObject(ir::IRModule& IRModule, ir::StringRef OutputPath) override;
  bool emitAssembly(ir::IRModule& IRModule, ir::StringRef OutputPath) override;
  bool emitIRText(ir::IRModule& IRModule, ir::raw_ostream& OS) override;
  bool canHandle(ir::StringRef TargetTriple) const override;
  bool optimize(ir::IRModule& IRModule) override;
  ir::BackendCapability getCapability() const override;

private:
  std::unique_ptr<llvm::Module> convertToLLVM(ir::IRModule& IRModule);
  bool optimizeLLVMModule(llvm::Module& M, unsigned OptLevel);
  bool emitCode(llvm::Module& M, ir::StringRef OutputPath,
                unsigned FileType);  // 0=Object, 1=Assembly
  bool checkCapability(ir::IRModule& IRModule);
};

/// 工厂函数：创建 LLVMBackend 实例
std::unique_ptr<BackendBase> createLLVMBackend(
  const BackendOptions& Opts, DiagnosticsEngine& Diags);

} // namespace blocktype::backend

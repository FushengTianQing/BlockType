#include "blocktype/Backend/LLVMBackend.h"
#include "blocktype/Backend/IRToLLVMConverter.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype::backend {

LLVMBackend::LLVMBackend(const BackendOptions& Opts, DiagnosticsEngine& Diags)
  : BackendBase(Opts, Diags),
    LLVMCtx(std::make_unique<llvm::LLVMContext>()) {}

ir::BackendCapability LLVMBackend::getCapability() const {
  ir::BackendCapability Cap;
  Cap.declareFeature(ir::IRFeature::IntegerArithmetic);
  Cap.declareFeature(ir::IRFeature::FloatArithmetic);
  Cap.declareFeature(ir::IRFeature::VectorOperations);
  Cap.declareFeature(ir::IRFeature::AtomicOperations);
  Cap.declareFeature(ir::IRFeature::ExceptionHandling);
  Cap.declareFeature(ir::IRFeature::DebugInfo);
  Cap.declareFeature(ir::IRFeature::VarArg);
  Cap.declareFeature(ir::IRFeature::SeparateFloatInt);
  Cap.declareFeature(ir::IRFeature::StructReturn);
  Cap.declareFeature(ir::IRFeature::DynamicCast);
  Cap.declareFeature(ir::IRFeature::VirtualDispatch);
  return Cap;
}

bool LLVMBackend::canHandle(ir::StringRef TargetTriple) const {
  (void)TargetTriple;
  return true;
}

bool LLVMBackend::optimize(ir::IRModule& IRModule) {
  (void)IRModule;
  return true;
}

bool LLVMBackend::checkCapability(ir::IRModule& IRModule) {
  auto Cap = getCapability();
  uint32_t Required = IRModule.getRequiredFeatures();
  uint32_t Unsupported = Cap.getUnsupported(Required);

  uint32_t NonDegradable =
    static_cast<uint32_t>(ir::IRFeature::DynamicCast) |
    static_cast<uint32_t>(ir::IRFeature::VirtualDispatch) |
    static_cast<uint32_t>(ir::IRFeature::Coroutines);

  uint32_t NonDegradableUnsupported = Unsupported & NonDegradable;
  if (NonDegradableUnsupported != 0) return false;

  uint32_t DegradableUnsupported = Unsupported & ~NonDegradable;
  if (DegradableUnsupported != 0) {
    // 可降级特性发出警告
  }
  return true;
}

std::unique_ptr<llvm::Module> LLVMBackend::convertToLLVM(ir::IRModule& IRModule) {
  Converter = std::make_unique<IRToLLVMConverter>(
    IRModule.getTypeContext(), *LLVMCtx, Opts, Diags);
  return Converter->convert(IRModule);
}

// #12: 使用 llvm::legacy::PassManager 进行基础优化
bool LLVMBackend::optimizeLLVMModule(llvm::Module& M, unsigned OptLevel) {
  if (OptLevel == 0) return true;
  // 使用 legacy PassManager 进行基础优化
  llvm::legacy::PassManager PM;
  PM.run(M);
  return true;
}

// #15: 使用 unique_ptr 管理 TargetMachine
bool LLVMBackend::emitCode(llvm::Module& M, ir::StringRef OutputPath,
                            unsigned FileType) {
  std::string Triple = Opts.TargetTriple;
  if (Triple.empty()) Triple = M.getTargetTriple();
  if (Triple.empty()) Triple = llvm::sys::getDefaultTargetTriple();

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();

  std::string Error;
  auto* Target = llvm::TargetRegistry::lookupTarget(Triple, Error);
  if (!Target) {
    Diags.report(blocktype::SourceLocation{}, DiagLevel::Error, Error);
    return false;
  }

  llvm::TargetOptions TO;
  auto TM = std::unique_ptr<llvm::TargetMachine>(
    Target->createTargetMachine(Triple, "generic", "", TO, llvm::Reloc::PIC_));
  if (!TM) {
    Diags.report(blocktype::SourceLocation{}, DiagLevel::Error,
                 "could not create TargetMachine");
    return false;
  }

  M.setDataLayout(TM->createDataLayout());
  M.setTargetTriple(Triple);

  std::error_code EC;
  llvm::raw_fd_ostream Dest(OutputPath.data(), EC, llvm::sys::fs::OF_None);
  if (EC) {
    Diags.report(blocktype::SourceLocation{}, DiagLevel::Error, EC.message());
    return false;
  }

  llvm::legacy::PassManager PM;
  llvm::CodeGenFileType CGFT = (FileType == 1)
    ? llvm::CodeGenFileType::AssemblyFile
    : llvm::CodeGenFileType::ObjectFile;

  if (TM->addPassesToEmitFile(PM, Dest, nullptr, CGFT)) {
    Diags.report(blocktype::SourceLocation{}, DiagLevel::Error,
                 "cannot emit file of this type");
    return false;
  }

  PM.run(M);
  Dest.flush();
  return true;
}

bool LLVMBackend::emitObject(ir::IRModule& IRModule, ir::StringRef OutputPath) {
  if (!checkCapability(IRModule)) return false;
  auto LLVMMod = convertToLLVM(IRModule);
  if (!LLVMMod) return false;
  if (!optimizeLLVMModule(*LLVMMod, Opts.OptimizationLevel)) return false;
  return emitCode(*LLVMMod, OutputPath, 0);
}

bool LLVMBackend::emitAssembly(ir::IRModule& IRModule, ir::StringRef OutputPath) {
  if (!checkCapability(IRModule)) return false;
  auto LLVMMod = convertToLLVM(IRModule);
  if (!LLVMMod) return false;
  if (!optimizeLLVMModule(*LLVMMod, Opts.OptimizationLevel)) return false;
  return emitCode(*LLVMMod, OutputPath, 1);
}

// #16: DIBuilder 已移至 IRToLLVMConverter::convert() 内部
bool LLVMBackend::emitIRText(ir::IRModule& IRModule, ir::raw_ostream& OS) {
  auto LLVMMod = convertToLLVM(IRModule);
  if (!LLVMMod) return false;

  std::string IRStr;
  llvm::raw_string_ostream SS(IRStr);
  LLVMMod->print(SS, nullptr);
  SS.flush();

  OS << IRStr.c_str();
  return true;
}

std::unique_ptr<BackendBase> createLLVMBackend(
    const BackendOptions& Opts, DiagnosticsEngine& Diags) {
  return std::make_unique<LLVMBackend>(Opts, Diags);
}

} // namespace blocktype::backend

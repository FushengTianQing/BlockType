//===--- CompilerInstance.cpp - Compiler Instance Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CompilerInstance class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/CompilerInstance.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/AST/ASTDumper.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/CodeGen.h"
#include <cstdlib>

using namespace llvm;
using namespace blocktype;

CompilerInstance::CompilerInstance()
    : Invocation(std::make_shared<CompilerInvocation>()) {
}

CompilerInstance::~CompilerInstance() = default;

//===--------------------------------------------------------------------===//
// Component creation
//===--------------------------------------------------------------------===//

void CompilerInstance::createSourceManager() {
  if (!SourceMgr) {
    SourceMgr = std::make_unique<SourceManager>();
  }
}

void CompilerInstance::createDiagnostics() {
  if (!Diags) {
    Diags = std::make_unique<DiagnosticsEngine>();
  }
}

void CompilerInstance::createASTContext() {
  if (!Context) {
    Context = std::make_unique<ASTContext>();
  }
}

void CompilerInstance::createPreprocessor() {
  if (!PP) {
    // Ensure dependencies are created
    if (!SourceMgr) createSourceManager();
    if (!Diags) createDiagnostics();
    
    PP = std::make_unique<Preprocessor>(*SourceMgr, *Diags);
  }
}

void CompilerInstance::createSema() {
  if (!SemaPtr) {
    // Ensure dependencies are created
    if (!Context) createASTContext();
    if (!Diags) createDiagnostics();
    
    SemaPtr = std::make_unique<Sema>(*Context, *Diags);
  }
}

void CompilerInstance::createParser() {
  if (!ParserPtr) {
    // Ensure dependencies are created
    if (!PP) createPreprocessor();
    if (!Context) createASTContext();
    if (!SemaPtr) createSema();
    
    ParserPtr = std::make_unique<Parser>(*PP, *Context, *SemaPtr);
  }
}

void CompilerInstance::createLLVMContext() {
  if (!LLVMCtx) {
    LLVMCtx = std::make_unique<llvm::LLVMContext>();
  }
}

void CompilerInstance::createAllComponents() {
  createSourceManager();
  createDiagnostics();
  createASTContext();
  createPreprocessor();
  createSema();
  createParser();
  createLLVMContext();
}

//===--------------------------------------------------------------------===//
// Compilation actions
//===--------------------------------------------------------------------===//

bool CompilerInstance::initialize(std::shared_ptr<CompilerInvocation> CI) {
  if (!CI) {
    errs() << "Error: CompilerInvocation is null\n";
    return false;
  }

  Invocation = std::move(CI);

  // Validate options
  if (!Invocation->validate()) {
    return false;
  }

  // Set default target triple
  Invocation->setDefaultTargetTriple();

  // Create all components
  createAllComponents();

  Initialized = true;
  return true;
}

bool CompilerInstance::readFileContent(StringRef Filename, std::string &Content) {
  auto BufferOrErr = llvm::MemoryBuffer::getFile(Filename);
  if (!BufferOrErr) {
    errs() << "Error: Cannot read file '" << Filename << "'\n";
    return false;
  }

  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(*BufferOrErr);
  Content = Buffer->getBuffer().str();
  return true;
}

bool CompilerInstance::loadSourceFile(StringRef Filename) {
  if (!Initialized) {
    errs() << "Error: Compiler not initialized\n";
    return false;
  }

  // Read file content
  std::string Content;
  if (!readFileContent(Filename, Content)) {
    return false;
  }

  // Enter source file
  PP->enterSourceFile(Filename.str(), Content);

  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Source size: " << Content.size() << " bytes\n";
  }

  return true;
}

TranslationUnitDecl *CompilerInstance::parseTranslationUnit() {
  if (!Initialized) {
    errs() << "Error: Compiler not initialized\n";
    return nullptr;
  }

  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Parsing...\n";
  }

  CurrentTU = ParserPtr->parseTranslationUnit();

  if (ParserPtr->hasErrors() || Diags->hasErrorOccurred()) {
    setError();
    return nullptr;
  }

  return CurrentTU;
}

bool CompilerInstance::performSemaAnalysis() {
  if (!CurrentTU) {
    errs() << "Error: No translation unit to analyze\n";
    return false;
  }

  // Perform post-parse diagnostics
  SemaPtr->DiagnoseUnusedDecls(CurrentTU);

  return !hasErrors();
}

bool CompilerInstance::performPreprocessing() {
  if (!Initialized || !PP) {
    errs() << "Error: Compiler not initialized\n";
    return false;
  }

  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Preprocessing...\n";
  }

  // Process all tokens from preprocessor
  Token Tok;
  while (!PP->isEOF()) {
    if (!PP->lexToken(Tok)) {
      break;
    }
    
    // For -E mode, output preprocessed tokens
    if (Invocation->CodeGenOpts.PreprocessOnly) {
      outs() << Tok.getText() << " ";
    }
  }

  if (Invocation->CodeGenOpts.PreprocessOnly) {
    outs() << "\n";
  }

  return !hasErrors();
}

std::unique_ptr<llvm::Module> CompilerInstance::generateLLVMIR(StringRef ModuleName) {
  if (!CurrentTU) {
    errs() << "Error: No translation unit to generate code for\n";
    return nullptr;
  }

  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Generating LLVM IR...\n";
  }

  // Create CodeGenModule
  CodeGenModule CGM(*Context, *LLVMCtx, *SourceMgr, ModuleName.str(),
                    Invocation->CodeGenOpts.TargetTriple);

  // P7.3.2.3: Propagate contract mode from invocation to CodeGenModule
  CGM.setDefaultContractMode(Invocation->FrontendOpts.DefaultContractMode);
  CGM.setContractsEnabled(Invocation->FrontendOpts.ContractsEnabled);

  // Emit translation unit
  CGM.EmitTranslationUnit(CurrentTU);

  // Transfer ownership of the Module from CGM to the caller
  return CGM.releaseModule();
}

bool CompilerInstance::runOptimizationPasses(llvm::Module &Module) {
  unsigned OptLevel = Invocation->CodeGenOpts.OptimizationLevel;

  // O0: no optimization
  if (OptLevel == 0) {
    if (Invocation->FrontendOpts.Verbose)
      outs() << "  Skipping optimization (O0)\n";
    return true;
  }

  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Running optimization passes (O" << OptLevel << ")...\n";
  }

  // TODO: 未来需要调试优化 pipeline 时，应创建 StandardInstrumentations
  // 并注册到 PassBuilder（用于 pass timing、printing 等调试功能）
  llvm::PassBuilder PB;

  // Register Analysis Managers
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  // Register standard analyses
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // Build optimization pipeline based on level
  llvm::OptimizationLevel Level;
  switch (OptLevel) {
  case 1:  Level = llvm::OptimizationLevel::O1; break;
  case 2:  Level = llvm::OptimizationLevel::O2; break;
  case 3:  Level = llvm::OptimizationLevel::O3; break;
  default: Level = llvm::OptimizationLevel::O2; break;
  }

  auto MPM = PB.buildPerModuleDefaultPipeline(Level);
  MPM.run(Module, MAM);

  return true;
}

bool CompilerInstance::generateObjectFile(llvm::Module &Module, StringRef OutputPath) {
  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Generating object file: " << OutputPath << "\n";
  }

  // Get target triple
  std::string TargetTripleStr = Module.getTargetTriple();
  if (TargetTripleStr.empty())
    TargetTripleStr = llvm::sys::getDefaultTargetTriple();
  Module.setTargetTriple(TargetTripleStr);

  // Look up target
  std::string Error;
  auto *Target = llvm::TargetRegistry::lookupTarget(TargetTripleStr, Error);
  if (!Target) {
    errs() << "Error: " << Error << "\n";
    return false;
  }

  // Create TargetMachine
  llvm::TargetOptions Opt;
  std::string CPU = Invocation->TargetOpts.CPU;
  std::string Features = Invocation->TargetOpts.Features;

  // Use StringRef for prefix/contains checks
  llvm::StringRef TargetTriple(TargetTripleStr);

  // x86_64 target-specific defaults
  if (TargetTriple.startswith("x86_64")) {
    if (CPU.empty()) CPU = "x86-64-v2";
    if (Features.empty()) Features = "+sse4.2,+cx16,+popcnt";
  }

  // AArch64/ARM64 target-specific defaults
  if (TargetTriple.startswith("aarch64") || TargetTriple.startswith("arm64")) {
    if (CPU.empty()) {
      CPU = TargetTriple.contains("apple") ? "apple-m1" : "generic";
    }
    if (Features.empty()) Features = "+neon,+fp-armv8";
  }

  // Float ABI mapping (LLVM supports Default/Soft/Hard; "softfp" maps to Default)
  if (Invocation->TargetOpts.FloatABI == "soft")
    Opt.FloatABIType = llvm::FloatABI::Soft;
  else if (Invocation->TargetOpts.FloatABI == "softfp")
    Opt.FloatABIType = llvm::FloatABI::Default;  // softfp = target default
  else
    Opt.FloatABIType = llvm::FloatABI::Hard;

  // PIC/PIE relocation model
  // TODO: PIE conflicts with static linking (-static). When the linker
  // is implemented, PIE should be disabled automatically when -static
  // is specified. For now, the user must manually use -fno-PIE with -static.
  auto RM = Invocation->TargetOpts.PIE
                ? llvm::Reloc::PIC_
                : llvm::Reloc::Static;

  // CodeGenOptLevel (implemented in 8.3.1)
  auto CM = [&]() -> llvm::CodeGenOptLevel {
    switch (Invocation->CodeGenOpts.OptimizationLevel) {
    case 0:  return llvm::CodeGenOptLevel::None;
    case 1:  return llvm::CodeGenOptLevel::Less;
    case 2:  return llvm::CodeGenOptLevel::Default;
    case 3:  return llvm::CodeGenOptLevel::Aggressive;
    default: return llvm::CodeGenOptLevel::Default;
    }
  }();

  // Code model (LLVM 18 removed CodeModel::Default, use Small as default)
  auto CMModel = llvm::CodeModel::Small;
  if (Invocation->TargetOpts.CodeModel == "large")
    CMModel = llvm::CodeModel::Large;
  // "small" or any other value → CodeModel::Small (the default)

  // Verify apple-m1 CPU is supported; fall back to "generic" if not
  if (CPU == "apple-m1") {
    // Try creating a TargetMachine with apple-m1; if it fails, fall back
    std::string TestError;
    auto *TestTarget = llvm::TargetRegistry::lookupTarget(TargetTripleStr, TestError);
    if (TestTarget) {
      auto TestTM = TestTarget->createTargetMachine(
          TargetTripleStr, CPU, Features, Opt, RM, CMModel, CM);
      if (!TestTM) {
        errs() << "Warning: CPU 'apple-m1' not supported by this LLVM version, "
               << "falling back to 'generic'\n";
        CPU = "generic";
      } else {
        delete TestTM;
      }
    }
  }

  auto TM = Target->createTargetMachine(TargetTripleStr, CPU, Features, Opt,
                                         RM, CMModel, CM);
  if (!TM) {
    errs() << "Error: Could not create TargetMachine\n";
    return false;
  }

  Module.setDataLayout(TM->createDataLayout());

  // Open output file
  std::error_code EC;
  llvm::raw_fd_ostream Out(OutputPath, EC, llvm::sys::fs::OF_None);
  if (EC) {
    errs() << "Error: Cannot open output file '" << OutputPath
           << "': " << EC.message() << "\n";
    return false;
  }

  // Generate object code using legacy PassManager
  llvm::legacy::PassManager PM;
  if (TM->addPassesToEmitFile(PM, Out, nullptr,
                               llvm::CodeGenFileType::ObjectFile)) {
    errs() << "Error: TargetMachine can't emit object file\n";
    return false;
  }

  PM.run(Module);
  Out.flush();
  return true;
}

bool CompilerInstance::linkExecutable(const std::vector<std::string> &ObjectFiles,
                                      StringRef OutputPath) {
  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Linking executable: " << OutputPath << "\n";
  }

  // TODO: Implement linking by invoking system linker
  // For now, we skip linking
  errs() << "Warning: Linking not yet implemented\n";
  
  // Build linker command
  std::string LinkerCmd = "clang++";  // Use clang++ as linker
  
  // Add output file
  LinkerCmd += " -o " + OutputPath.str();
  
  // Add object files
  for (const auto &ObjFile : ObjectFiles) {
    LinkerCmd += " " + ObjFile;
  }
  
  // Add library paths
  for (const auto &LibPath : Invocation->FrontendOpts.LibraryPaths) {
    LinkerCmd += " -L" + LibPath;
  }
  
  // Add libraries
  for (const auto &Lib : Invocation->FrontendOpts.Libraries) {
    LinkerCmd += " -l" + Lib;
  }
  
  // Add additional linker flags
  for (const auto &Flag : Invocation->FrontendOpts.LinkerFlags) {
    LinkerCmd += " " + Flag;
  }
  
  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Linker command: " << LinkerCmd << "\n";
  }
  
  // Execute linker
  int Result = std::system(LinkerCmd.c_str());
  return Result == 0;
}

bool CompilerInstance::compileFile(StringRef Filename) {
  if (Invocation->FrontendOpts.Verbose) {
    outs() << "Compiling: " << Filename << "\n";
  }

  // Load source file
  if (!loadSourceFile(Filename)) {
    return false;
  }

  // === Stage 1: Preprocessing ===
  if (Invocation->CodeGenOpts.PreprocessOnly) {
    return performPreprocessing();
  }

  // === Stage 2: Parsing ===
  TranslationUnitDecl *TU = parseTranslationUnit();
  if (!TU) {
    errs() << "Error: Parsing failed for '" << Filename << "'\n";
    return false;
  }

  // === Stage 3: Semantic Analysis ===
  if (!performSemaAnalysis()) {
    errs() << "Error: Semantic analysis failed for '" << Filename << "'\n";
    return false;
  }

  // Stop after syntax analysis if requested
  if (Invocation->CodeGenOpts.SyntaxOnly) {
    if (Invocation->FrontendOpts.Verbose) {
      outs() << "  Syntax check passed\n";
    }
    return true;
  }

  // Dump AST if requested
  if (Invocation->FrontendOpts.DumpAST) {
    dumpAST();
  }

  // === Stage 4: LLVM IR Generation ===
  auto Module = generateLLVMIR(Filename);
  if (!Module) {
    errs() << "Error: Code generation failed for '" << Filename << "'\n";
    return false;
  }

  // Stop after LLVM IR generation if requested
  if (Invocation->CodeGenOpts.EmitLLVM || Invocation->CodeGenOpts.EmitLLVMOnly) {
    if (Invocation->CodeGenOpts.EmitLLVMOnly) {
      // Emit LLVM IR to stdout
      Module->print(outs(), nullptr);
      return true;
    }
  }

  // === Stage 5: Optimization ===
  if (Invocation->CodeGenOpts.OptimizationLevel > 0) {
    if (!runOptimizationPasses(*Module)) {
      errs() << "Error: Optimization failed for '" << Filename << "'\n";
      return false;
    }
  }

  // === Stage 6: Object File Generation ===
  if (Invocation->CodeGenOpts.EmitObject || Invocation->CodeGenOpts.LinkExecutable) {
    // Determine output file name
    std::string OutputPath;
    if (!Invocation->CodeGenOpts.OutputFile.empty()) {
      OutputPath = Invocation->CodeGenOpts.OutputFile;
    } else {
      // Generate default output file name
      OutputPath = Filename.str();
      // Replace extension with .o
      size_t DotPos = OutputPath.rfind('.');
      if (DotPos != std::string::npos) {
        OutputPath = OutputPath.substr(0, DotPos) + ".o";
      } else {
        OutputPath += ".o";
      }
    }

    if (!generateObjectFile(*Module, OutputPath)) {
      errs() << "Error: Object file generation failed for '" << Filename << "'\n";
      return false;
    }

    if (Invocation->FrontendOpts.Verbose) {
      outs() << "  Object file generated: " << OutputPath << "\n";
    }
  }

  // Emit LLVM IR if requested (even with object file generation)
  if (Invocation->CodeGenOpts.EmitLLVM) {
    Module->print(outs(), nullptr);
  }

  if (Invocation->FrontendOpts.Verbose) {
    outs() << "  Compilation successful\n";
  }

  return true;
}

bool CompilerInstance::compileAllFiles() {
  bool AllSucceeded = true;

  // P0-2: 在循环外创建共享基础设施
  // 这些组件在多文件编译时应该共享，以支持跨文件符号解析
  
  // 如果只有一个文件，使用简单路径
  if (Invocation->FrontendOpts.InputFiles.size() == 1) {
    return compileFile(Invocation->FrontendOpts.InputFiles[0]);
  }

  // 多文件编译：共享基础设施
  if (Invocation->FrontendOpts.Verbose) {
    outs() << "Compiling " << Invocation->FrontendOpts.InputFiles.size() 
           << " files with shared infrastructure\n";
  }

  // 确保基础设施已创建
  if (!Initialized) {
    if (!initialize(Invocation)) {
      return false;
    }
  }

  // 收集所有翻译单元
  std::vector<TranslationUnitDecl *> AllTUs;
  
  // 收集生成的对象文件（用于链接）
  std::vector<std::string> ObjectFiles;

  // 编译每个文件
  for (const auto &File : Invocation->FrontendOpts.InputFiles) {
    if (Invocation->FrontendOpts.Verbose) {
      outs() << "\nCompiling: " << File << "\n";
    }

    // 读取文件内容
    std::string Content;
    if (!readFileContent(File, Content)) {
      AllSucceeded = false;
      continue;
    }

    if (Invocation->FrontendOpts.Verbose) {
      outs() << "  Source size: " << Content.size() << " bytes\n";
    }

    // 重置 Preprocessor 状态（每个文件需要独立的预处理状态）
    PP->reset();
    
    // 进入源文件
    PP->enterSourceFile(File, Content);

    // 解析
    if (Invocation->FrontendOpts.Verbose) {
      outs() << "  Parsing...\n";
    }

    TranslationUnitDecl *TU = ParserPtr->parseTranslationUnit();
    if (!TU || ParserPtr->hasErrors() || Diags->hasErrorOccurred()) {
      errs() << "Error: Parsing failed for '" << File << "'\n";
      setError();
      AllSucceeded = false;
      continue;
    }

    // 保存翻译单元
    AllTUs.push_back(TU);
    CurrentTU = TU;

    // 执行语义分析
    if (!performSemaAnalysis()) {
      errs() << "Error: Semantic analysis failed for '" << File << "'\n";
      AllSucceeded = false;
      continue;
    }

    // Dump AST if requested
    if (Invocation->FrontendOpts.DumpAST) {
      dumpAST();
    }

    // 生成 LLVM IR
    auto Module = generateLLVMIR(File);
    if (!Module) {
      errs() << "Error: Code generation failed for '" << File << "'\n";
      AllSucceeded = false;
      continue;
    }

    // 优化
    if (Invocation->CodeGenOpts.OptimizationLevel > 0) {
      if (!runOptimizationPasses(*Module)) {
        AllSucceeded = false;
        continue;
      }
    }

    // 生成对象文件
    if (Invocation->CodeGenOpts.EmitObject || Invocation->CodeGenOpts.LinkExecutable) {
      std::string ObjectPath = File;
      size_t DotPos = ObjectPath.rfind('.');
      if (DotPos != std::string::npos) {
        ObjectPath = ObjectPath.substr(0, DotPos) + ".o";
      } else {
        ObjectPath += ".o";
      }

      if (!generateObjectFile(*Module, ObjectPath)) {
        AllSucceeded = false;
        continue;
      }

      ObjectFiles.push_back(ObjectPath);
      
      if (Invocation->FrontendOpts.Verbose) {
        outs() << "  Object file: " << ObjectPath << "\n";
      }
    }

    if (Invocation->FrontendOpts.Verbose) {
      outs() << "  Compilation successful\n";
    }
  }

  // === Stage 7: Linking ===
  if (AllSucceeded && Invocation->CodeGenOpts.LinkExecutable && !ObjectFiles.empty()) {
    // Determine output file name
    std::string OutputPath;
    if (!Invocation->FrontendOpts.OutputFile.empty()) {
      OutputPath = Invocation->FrontendOpts.OutputFile;
    } else {
      OutputPath = "a.out";  // Default executable name
    }

    if (Invocation->FrontendOpts.Verbose) {
      outs() << "\nLinking " << ObjectFiles.size() << " object files...\n";
    }

    if (!linkExecutable(ObjectFiles, OutputPath)) {
      errs() << "Error: Linking failed\n";
      AllSucceeded = false;
    } else {
      if (Invocation->FrontendOpts.Verbose) {
        outs() << "Executable generated: " << OutputPath << "\n";
      }
    }
  }

  return AllSucceeded;
}

//===--------------------------------------------------------------------===//
// Utility functions
//===--------------------------------------------------------------------===//

void CompilerInstance::dumpAST() {
  if (!CurrentTU) {
    errs() << "Error: No translation unit to dump\n";
    return;
  }

  outs() << "\n=== AST Dump ===\n";
  CurrentTU->dump(outs());
  outs() << "=== End AST Dump ===\n\n";
}

void CompilerInstance::dumpLLVMIR() {
  // TODO: Implement LLVM IR dumping
  errs() << "Warning: dumpLLVMIR() not yet implemented\n";
}

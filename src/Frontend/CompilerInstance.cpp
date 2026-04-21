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
#include "llvm/IR/Module.h"

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

  if (ParserPtr->hasErrors()) {
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

  // Emit translation unit
  CGM.EmitTranslationUnit(CurrentTU);

  return std::unique_ptr<llvm::Module>(CGM.getModule());
}

bool CompilerInstance::compileFile(StringRef Filename) {
  if (Invocation->FrontendOpts.Verbose) {
    outs() << "Compiling: " << Filename << "\n";
  }

  // Load source file
  if (!loadSourceFile(Filename)) {
    return false;
  }

  // Parse
  TranslationUnitDecl *TU = parseTranslationUnit();
  if (!TU) {
    errs() << "Error: Parsing failed for '" << Filename << "'\n";
    return false;
  }

  // Perform semantic analysis
  if (!performSemaAnalysis()) {
    errs() << "Error: Semantic analysis failed for '" << Filename << "'\n";
    return false;
  }

  // Dump AST if requested
  if (Invocation->FrontendOpts.DumpAST) {
    dumpAST();
  }

  // Generate LLVM IR if requested
  if (Invocation->CodeGenOpts.EmitLLVM) {
    auto Module = generateLLVMIR(Filename);
    if (!Module) {
      errs() << "Error: Code generation failed for '" << Filename << "'\n";
      return false;
    }

    // Output LLVM IR
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
    if (!TU || ParserPtr->hasErrors()) {
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

    if (Invocation->FrontendOpts.Verbose) {
      outs() << "  Compilation successful\n";
    }
  }

  // 生成代码（如果需要）
  if (AllSucceeded && Invocation->CodeGenOpts.EmitLLVM && !AllTUs.empty()) {
    if (Invocation->FrontendOpts.Verbose) {
      outs() << "\nGenerating LLVM IR for all translation units...\n";
    }

    // 为所有 TU 生成一个模块
    auto Module = generateLLVMIR("multi_file_module");
    if (Module) {
      Module->print(outs(), nullptr);
    } else {
      errs() << "Error: Code generation failed\n";
      AllSucceeded = false;
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

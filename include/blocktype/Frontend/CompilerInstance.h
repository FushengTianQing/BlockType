//===--- CompilerInstance.h - Compiler Instance -------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CompilerInstance class which manages compiler state
// and provides access to all compiler components.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Frontend/CompilerInvocation.h"
#include "blocktype/Basic/LLVM.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Parse/Parser.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/Decl.h"
#include "llvm/IR/LLVMContext.h"
#include <memory>
#include <string>

namespace blocktype {

/// CompilerInstance - Manages compiler state and provides access to all
/// compiler components.
///
/// This class is designed to:
/// - Own all compiler components (SourceManager, DiagnosticsEngine, etc.)
/// - Support multiple independent compiler instances
/// - Provide a clean interface for driving compilation
/// - Enable testing without global state
class CompilerInstance {
  /// The invocation (options/configuration).
  std::shared_ptr<CompilerInvocation> Invocation;

  /// The source manager (manages source files and locations).
  std::unique_ptr<SourceManager> SourceMgr;

  /// The diagnostics engine (reports errors and warnings).
  std::unique_ptr<DiagnosticsEngine> Diags;

  /// The AST context (manages AST node memory).
  std::unique_ptr<ASTContext> Context;

  /// The preprocessor (lexes and preprocesses source).
  std::unique_ptr<Preprocessor> PP;

  /// The semantic analyzer (performs semantic analysis).
  std::unique_ptr<Sema> SemaPtr;

  /// The parser (parses source into AST).
  std::unique_ptr<Parser> ParserPtr;

  /// The LLVM context (for code generation).
  std::unique_ptr<llvm::LLVMContext> LLVMCtx;

  /// The current translation unit being compiled.
  TranslationUnitDecl *CurrentTU = nullptr;

  /// Whether the compiler has encountered errors.
  bool HasErrors = false;

  /// Whether the compiler has been initialized.
  bool Initialized = false;

public:
  CompilerInstance();
  ~CompilerInstance();

  // Non-copyable but movable
  CompilerInstance(const CompilerInstance &) = delete;
  CompilerInstance &operator=(const CompilerInstance &) = delete;
  CompilerInstance(CompilerInstance &&) = default;
  CompilerInstance &operator=(CompilerInstance &&) = default;

  //===--------------------------------------------------------------------===//
  // Invocation access
  //===--------------------------------------------------------------------===//

  /// Get the compiler invocation.
  CompilerInvocation &getInvocation() { return *Invocation; }
  const CompilerInvocation &getInvocation() const { return *Invocation; }

  /// Set the compiler invocation.
  void setInvocation(std::shared_ptr<CompilerInvocation> CI) {
    Invocation = std::move(CI);
  }

  //===--------------------------------------------------------------------===//
  // Component access
  //===--------------------------------------------------------------------===//

  /// Get the source manager.
  SourceManager &getSourceManager() { return *SourceMgr; }
  const SourceManager &getSourceManager() const { return *SourceMgr; }

  /// Get the diagnostics engine.
  DiagnosticsEngine &getDiagnostics() { return *Diags; }
  const DiagnosticsEngine &getDiagnostics() const { return *Diags; }

  /// Get the AST context.
  ASTContext &getASTContext() { return *Context; }
  const ASTContext &getASTContext() const { return *Context; }

  /// Get the preprocessor.
  Preprocessor &getPreprocessor() { return *PP; }
  const Preprocessor &getPreprocessor() const { return *PP; }

  /// Get the semantic analyzer.
  Sema &getSema() { return *SemaPtr; }
  const Sema &getSema() const { return *SemaPtr; }

  /// Get the parser.
  Parser &getParser() { return *ParserPtr; }
  const Parser &getParser() const { return *ParserPtr; }

  /// Get the LLVM context.
  llvm::LLVMContext &getLLVMContext() { return *LLVMCtx; }
  const llvm::LLVMContext &getLLVMContext() const { return *LLVMCtx; }

  /// Get the current translation unit.
  TranslationUnitDecl *getCurrentTranslationUnit() { return CurrentTU; }
  const TranslationUnitDecl *getCurrentTranslationUnit() const { return CurrentTU; }

  //===--------------------------------------------------------------------===//
  // Component creation
  //===--------------------------------------------------------------------===//

  /// Create the source manager.
  void createSourceManager();

  /// Create the diagnostics engine.
  void createDiagnostics();

  /// Create the AST context.
  void createASTContext();

  /// Create the preprocessor.
  void createPreprocessor();

  /// Create the semantic analyzer.
  void createSema();

  /// Create the parser.
  void createParser();

  /// Create the LLVM context.
  void createLLVMContext();

  /// Create all components in the correct order.
  void createAllComponents();

  //===--------------------------------------------------------------------===//
  // Compilation actions
  //===--------------------------------------------------------------------===//

  /// Initialize the compiler with the given invocation.
  ///
  /// \param CI The compiler invocation (options/configuration).
  /// \returns true if initialization succeeded, false otherwise.
  bool initialize(std::shared_ptr<CompilerInvocation> CI);

  /// Load a source file.
  ///
  /// \param Filename The source file to load.
  /// \returns true if loading succeeded, false otherwise.
  bool loadSourceFile(StringRef Filename);

  /// Parse the current translation unit.
  ///
  /// \returns The parsed translation unit, or nullptr on error.
  TranslationUnitDecl *parseTranslationUnit();

  /// Perform semantic analysis on the current translation unit.
  ///
  /// \returns true if analysis succeeded, false otherwise.
  bool performSemaAnalysis();

  /// Perform preprocessing on the current file.
  ///
  /// \returns true if preprocessing succeeded, false otherwise.
  bool performPreprocessing();

  /// Generate LLVM IR for the current translation unit.
  ///
  /// \param ModuleName The module name.
  /// \returns The LLVM module, or nullptr on error.
  std::unique_ptr<llvm::Module> generateLLVMIR(StringRef ModuleName);

  /// Run optimization passes on the LLVM module.
  ///
  /// \param Module The LLVM module to optimize.
  /// \returns true if optimization succeeded, false otherwise.
  bool runOptimizationPasses(llvm::Module &Module);

  /// Generate object file (.o) from LLVM module.
  ///
  /// \param Module The LLVM module.
  /// \param OutputPath The output file path.
  /// \returns true if code generation succeeded, false otherwise.
  bool generateObjectFile(llvm::Module &Module, StringRef OutputPath);

  /// Link object files into executable.
  ///
  /// \param ObjectFiles List of object file paths.
  /// \param OutputPath The output executable path.
  /// \returns true if linking succeeded, false otherwise.
  bool linkExecutable(const std::vector<std::string> &ObjectFiles,
                      StringRef OutputPath);

  /// Compile a single file.
  ///
  /// \param Filename The source file to compile.
  /// \returns true if compilation succeeded, false otherwise.
  bool compileFile(StringRef Filename);

  /// Compile all input files.
  ///
  /// \returns true if all compilations succeeded, false otherwise.
  bool compileAllFiles();

  //===--------------------------------------------------------------------===//
  // Status queries
  //===--------------------------------------------------------------------===//

  /// Check if the compiler has encountered errors.
  bool hasErrors() const { return HasErrors; }

  /// Check if the compiler has been initialized.
  bool isInitialized() const { return Initialized; }

  /// Clear error state.
  void clearErrors() { HasErrors = false; }

  //===--------------------------------------------------------------------===//
  // Utility functions
  //===--------------------------------------------------------------------===//

  /// Print the current AST (for debugging).
  void dumpAST();

  /// Print the current LLVM IR (for debugging).
  void dumpLLVMIR();

private:
  /// Set error state.
  void setError() { HasErrors = true; }

  /// Read source file content.
  ///
  /// \param Filename The source file to read.
  /// \param Content Output parameter for file content.
  /// \returns true if reading succeeded, false otherwise.
  bool readFileContent(StringRef Filename, std::string &Content);
};

} // namespace blocktype

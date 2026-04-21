//===--- CompilerInvocation.cpp - Compiler Invocation Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CompilerInvocation class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/CompilerInvocation.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/ADT/StringSwitch.h"

using namespace llvm;
using namespace blocktype;

// Forward declarations of global command-line options (defined in driver.cpp)
namespace {
  // These will be removed after migration to CompilerInvocation
  extern cl::list<std::string> &getInputFiles();
  extern cl::opt<bool> &getAIAssist();
  extern cl::opt<std::string> &getAIProviderName();
  extern cl::opt<bool> &getAICache();
  extern cl::opt<double> &getAICostLimit();
  extern cl::opt<std::string> &getAIModel();
  extern cl::opt<std::string> &getOllamaEndpoint();
  extern cl::opt<bool> &getASTDump();
  extern cl::opt<bool> &getEmitLLVM();
  extern cl::opt<bool> &getVerbose();
}

void CompilerInvocation::setDefaultTargetTriple() {
  if (TargetOpts.Triple.empty()) {
    // Use host triple as default
    TargetOpts.Triple = llvm::sys::getDefaultTargetTriple();
  }
  
  // Set CodeGen target triple if not specified
  if (CodeGenOpts.TargetTriple.empty()) {
    CodeGenOpts.TargetTriple = TargetOpts.Triple;
  }
}

bool CompilerInvocation::validate() const {
  // Check C++ standard version
  if (LangOpts.CXXStandard != 11 && LangOpts.CXXStandard != 14 &&
      LangOpts.CXXStandard != 17 && LangOpts.CXXStandard != 20 &&
      LangOpts.CXXStandard != 23 && LangOpts.CXXStandard != 26) {
    errs() << "Error: Invalid C++ standard version: " << LangOpts.CXXStandard << "\n";
    errs() << "Supported versions: 11, 14, 17, 20, 23, 26\n";
    return false;
  }

  // Check optimization level
  if (CodeGenOpts.OptimizationLevel > 3) {
    errs() << "Error: Invalid optimization level: " << CodeGenOpts.OptimizationLevel << "\n";
    errs() << "Valid levels: 0, 1, 2, 3\n";
    return false;
  }

  // Check AI options
  if (AIOpts.Enable) {
    if (AIOpts.Provider != "openai" && AIOpts.Provider != "claude" &&
        AIOpts.Provider != "qwen" && AIOpts.Provider != "local" &&
        AIOpts.Provider != "all") {
      errs() << "Error: Invalid AI provider: " << AIOpts.Provider << "\n";
      errs() << "Valid providers: openai, claude, qwen, local, all\n";
      return false;
    }

    if (AIOpts.MaxCostPerDay < 0) {
      errs() << "Error: Maximum daily cost cannot be negative\n";
      return false;
    }
  }

  // Check that input files are specified (unless showing help/version)
  if (!FrontendOpts.ShowHelp && !FrontendOpts.ShowVersion &&
      FrontendOpts.InputFiles.empty()) {
    // This is not an error - just no input files
    return true;
  }

  return true;
}

std::string CompilerInvocation::toString() const {
  std::string Result;
  raw_string_ostream OS(Result);

  OS << "=== CompilerInvocation ===\n";
  
  OS << "\n[Language Options]\n";
  OS << "  C++ Standard: " << LangOpts.CXXStandard << "\n";
  OS << "  Modules: " << (LangOpts.Modules ? "enabled" : "disabled") << "\n";
  OS << "  Concepts: " << (LangOpts.Concepts ? "enabled" : "disabled") << "\n";
  OS << "  Static Reflection: " << (LangOpts.StaticReflection ? "enabled" : "disabled") << "\n";
  OS << "  Bilingual Keywords: " << (LangOpts.BilingualKeywords ? "enabled" : "disabled") << "\n";

  OS << "\n[Target Options]\n";
  OS << "  Triple: " << TargetOpts.Triple << "\n";
  OS << "  CPU: " << TargetOpts.CPU << "\n";
  OS << "  Features: " << TargetOpts.Features << "\n";

  OS << "\n[Code Generation Options]\n";
  OS << "  Optimization Level: " << CodeGenOpts.OptimizationLevel << "\n";
  OS << "  Debug Info: " << (CodeGenOpts.DebugInfo ? "enabled" : "disabled") << "\n";
  OS << "  PIC: " << (CodeGenOpts.PIC ? "enabled" : "disabled") << "\n";
  OS << "  Emit LLVM IR: " << (CodeGenOpts.EmitLLVM ? "enabled" : "disabled") << "\n";
  OS << "  Output File: " << CodeGenOpts.OutputFile << "\n";
  OS << "  Target Triple: " << CodeGenOpts.TargetTriple << "\n";

  OS << "\n[Diagnostic Options]\n";
  OS << "  Show Location: " << (DiagOpts.ShowLocation ? "enabled" : "disabled") << "\n";
  OS << "  Show Carets: " << (DiagOpts.ShowCarets ? "enabled" : "disabled") << "\n";
  OS << "  Use Colors: " << (DiagOpts.UseColors ? "enabled" : "disabled") << "\n";
  OS << "  Warnings as Errors: " << (DiagOpts.WarningsAsErrors ? "enabled" : "disabled") << "\n";
  OS << "  Error Limit: " << DiagOpts.ErrorLimit << "\n";

  OS << "\n[AI Options]\n";
  OS << "  Enable: " << (AIOpts.Enable ? "enabled" : "disabled") << "\n";
  OS << "  Provider: " << AIOpts.Provider << "\n";
  OS << "  Model: " << AIOpts.Model << "\n";
  OS << "  Cache: " << (AIOpts.EnableCache ? "enabled" : "disabled") << "\n";
  OS << "  Max Cost Per Day: $" << AIOpts.MaxCostPerDay << "\n";
  OS << "  Ollama Endpoint: " << AIOpts.OllamaEndpoint << "\n";

  OS << "\n[Frontend Options]\n";
  OS << "  Input Files: ";
  for (const auto &File : FrontendOpts.InputFiles) {
    OS << File << " ";
  }
  OS << "\n";
  OS << "  Output File: " << FrontendOpts.OutputFile << "\n";
  OS << "  Dump AST: " << (FrontendOpts.DumpAST ? "enabled" : "disabled") << "\n";
  OS << "  Verbose: " << (FrontendOpts.Verbose ? "enabled" : "disabled") << "\n";

  return OS.str();
}

bool CompilerInvocation::fromString(const std::string &Str) {
  // TODO: Implement deserialization from string
  // For now, this is a placeholder for future implementation
  errs() << "Warning: fromString() not yet implemented\n";
  return false;
}

void CompilerInvocation::parseFromCommandLine() {
  // This method transfers values from global cl::opt variables
  // to this CompilerInvocation object.
  //
  // After we complete the migration, this method will be replaced
  // by parseCommandLine() which parses directly without global state.
  
  // Note: This is a temporary bridge during migration.
  // The actual implementation will be in driver.cpp after we
  // refactor the command-line parsing.
}

bool CompilerInvocation::parseCommandLine(int Argc, const char *const *Argv) {
  // TODO: Implement direct command-line parsing
  // This will replace the global cl::opt variables
  errs() << "Warning: parseCommandLine() not yet fully implemented\n";
  return false;
}

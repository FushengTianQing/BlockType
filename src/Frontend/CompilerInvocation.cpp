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

  // Validate TargetOptions
  if (TargetOpts.FloatABI != "hard" && TargetOpts.FloatABI != "soft" &&
      TargetOpts.FloatABI != "softfp") {
    errs() << "Error: Invalid float ABI: " << TargetOpts.FloatABI << "\n";
    errs() << "Valid values: hard, soft, softfp\n";
    return false;
  }

  if (TargetOpts.CodeModel != "small" && TargetOpts.CodeModel != "large") {
    errs() << "Error: Invalid code model: " << TargetOpts.CodeModel << "\n";
    errs() << "Valid values: small, large\n";
    return false;
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
  OS << "  Float ABI: " << TargetOpts.FloatABI << "\n";
  OS << "  PIE: " << (TargetOpts.PIE ? "enabled" : "disabled") << "\n";
  OS << "  Code Model: " << TargetOpts.CodeModel << "\n";

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
  // Simple command-line parser for BlockType compiler options.
  // This replaces the global cl::opt variables approach.

  for (int i = 1; i < Argc; ++i) {
    llvm::StringRef Arg(Argv[i]);

    // P7.3.2.3: -fcontract-mode=<mode>
    if (Arg.startswith("-fcontract-mode=")) {
      llvm::StringRef ModeStr = Arg.substr(16);
      if (ModeStr == "off") {
        FrontendOpts.DefaultContractMode = ContractMode::Off;
      } else if (ModeStr == "default") {
        FrontendOpts.DefaultContractMode = ContractMode::Default;
      } else if (ModeStr == "enforce") {
        FrontendOpts.DefaultContractMode = ContractMode::Enforce;
      } else if (ModeStr == "observe") {
        FrontendOpts.DefaultContractMode = ContractMode::Observe;
      } else if (ModeStr == "quick-enforce") {
        FrontendOpts.DefaultContractMode = ContractMode::Quick_Enforce;
      } else {
        errs() << "Error: Invalid contract mode '" << ModeStr
               << "'; expected off|default|enforce|observe|quick-enforce\n";
        return false;
      }
      FrontendOpts.ContractsEnabled = true;
      continue;
    }

    // P7.3.2.3: -fcontracts / -fno-contracts
    if (Arg == "-fcontracts") {
      FrontendOpts.ContractsEnabled = true;
      continue;
    }
    if (Arg == "-fno-contracts") {
      FrontendOpts.ContractsEnabled = false;
      FrontendOpts.DefaultContractMode = ContractMode::Off;
      continue;
    }

    // Input files (positional arguments not starting with -)
    if (!Arg.startswith("-")) {
      FrontendOpts.InputFiles.push_back(Arg.str());
      continue;
    }

    // Standard options
    if (Arg == "--help" || Arg == "-h") {
      FrontendOpts.ShowHelp = true;
      continue;
    }
    if (Arg == "--version" || Arg == "-v") {
      FrontendOpts.ShowVersion = true;
      continue;
    }
    if (Arg == "-dump-ast") {
      FrontendOpts.DumpAST = true;
      continue;
    }
    if (Arg == "-verbose") {
      FrontendOpts.Verbose = true;
      continue;
    }
    if (Arg == "-emit-llvm") {
      CodeGenOpts.EmitLLVM = true;
      continue;
    }
    if (Arg == "-S" || Arg == "-emit-assembly") {
      CodeGenOpts.EmitAssembly = true;
      continue;
    }
    if (Arg == "-c") {
      CodeGenOpts.EmitObject = true;
      continue;
    }
    if (Arg == "-fsyntax-only") {
      CodeGenOpts.SyntaxOnly = true;
      continue;
    }
    if (Arg.startswith("-o")) {
      if (Arg.size() > 2) {
        CodeGenOpts.OutputFile = Arg.substr(2).str();
      } else if (i + 1 < Argc) {
        ++i;
        CodeGenOpts.OutputFile = Argv[i];
      }
      continue;
    }
    if (Arg.startswith("-std=")) {
      llvm::StringRef StdStr = Arg.substr(5);
      if (StdStr == "c++11" || StdStr == "c++0x")
        LangOpts.CXXStandard = 11;
      else if (StdStr == "c++14" || StdStr == "c++1y")
        LangOpts.CXXStandard = 14;
      else if (StdStr == "c++17" || StdStr == "c++1z")
        LangOpts.CXXStandard = 17;
      else if (StdStr == "c++20" || StdStr == "c++2a")
        LangOpts.CXXStandard = 20;
      else if (StdStr == "c++23" || StdStr == "c++2b")
        LangOpts.CXXStandard = 23;
      else if (StdStr == "c++26" || StdStr == "c++2c")
        LangOpts.CXXStandard = 26;
      else {
        errs() << "Error: Unknown standard '" << StdStr << "'\n";
        return false;
      }
      continue;
    }
    if (Arg.startswith("-O")) {
      llvm::StringRef OptStr = Arg.substr(2);
      if (OptStr.empty()) {
        CodeGenOpts.OptimizationLevel = 2;
      } else {
        unsigned Level = 0;
        if (!OptStr.getAsInteger(10, Level) && Level <= 3) {
          CodeGenOpts.OptimizationLevel = Level;
        }
      }
      continue;
    }
    if (Arg.startswith("-I")) {
      if (Arg.size() > 2) {
        FrontendOpts.IncludePaths.push_back(Arg.substr(2).str());
      } else if (i + 1 < Argc) {
        ++i;
        FrontendOpts.IncludePaths.push_back(Argv[i]);
      }
      continue;
    }
    if (Arg.startswith("-L")) {
      if (Arg.size() > 2) {
        FrontendOpts.LibraryPaths.push_back(Arg.substr(2).str());
      } else if (i + 1 < Argc) {
        ++i;
        FrontendOpts.LibraryPaths.push_back(Argv[i]);
      }
      continue;
    }
    if (Arg.startswith("-l")) {
      FrontendOpts.Libraries.push_back(Arg.substr(2).str());
      continue;
    }
    if (Arg == "-g" || Arg == "-g1") {
      CodeGenOpts.DebugInfo = true;
      continue;
    }
    if (Arg == "-PIC" || Arg == "-fPIC") {
      CodeGenOpts.PIC = true;
      continue;
    }
    if (Arg == "-fPIE") {
      TargetOpts.PIE = true;
      continue;
    }
    if (Arg == "-fno-PIE" || Arg == "-fno-pie") {
      TargetOpts.PIE = false;
      continue;
    }
    if (Arg == "-static") {
      CodeGenOpts.StaticLink = true;
      continue;
    }
    if (Arg.startswith("-mfloat-abi=")) {
      llvm::StringRef ABIStr = Arg.substr(12);
      if (ABIStr == "hard" || ABIStr == "soft" || ABIStr == "softfp") {
        TargetOpts.FloatABI = ABIStr.str();
      } else {
        errs() << "Error: Invalid float ABI '" << ABIStr
               << "'; expected hard|soft|softfp\n";
        return false;
      }
      continue;
    }
    if (Arg.startswith("-mcmodel=")) {
      llvm::StringRef CMStr = Arg.substr(9);
      if (CMStr == "small" || CMStr == "large") {
        TargetOpts.CodeModel = CMStr.str();
      } else {
        errs() << "Error: Invalid code model '" << CMStr
               << "'; expected small|large\n";
        return false;
      }
      continue;
    }
    if (Arg == "-E") {
      CodeGenOpts.PreprocessOnly = true;
      continue;
    }

    // Unknown option: skip (might be handled by the driver layer)
  }

  return true;
}

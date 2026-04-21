//===--- CompilerInvocation.h - Compiler Invocation --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CompilerInvocation class which encapsulates all
// compiler options and configuration.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include <string>
#include <vector>

namespace blocktype {

/// LangOptions - Language options for the compiler.
struct LangOptions {
  /// C++ standard version (11, 14, 17, 20, 23, 26).
  unsigned CXXStandard = 26;

  /// Enable C++20 modules.
  bool Modules = false;

  /// Enable C++20 concepts.
  bool Concepts = true;

  /// Enable C++26 static reflection.
  bool StaticReflection = true;

  /// Enable C++26 placeholder variables.
  bool PlaceholderVariables = true;

  /// Enable C++26 pack indexing.
  bool PackIndexing = true;

  /// Enable C++26 delete with reason.
  bool DeleteWithReason = true;

  /// Enable strict conformance mode.
  bool StrictConformance = false;

  /// Enable bilingual keywords (中文/English).
  bool BilingualKeywords = true;
};

/// TargetOptions - Target-specific options.
struct TargetOptions {
  /// Target triple (e.g., "x86_64-unknown-linux-gnu").
  std::string Triple;

  /// CPU name (e.g., "x86-64", "armv8-a").
  std::string CPU;

  /// Target features (e.g., "+sse4.2,+avx").
  std::string Features;

  /// ABI name (e.g., "itanium", "ms").
  std::string ABI;
};

/// CodeGenOptions - Code generation options.
struct CodeGenOptions {
  /// Optimization level (0-3).
  unsigned OptimizationLevel = 0;

  /// Enable debug info generation.
  bool DebugInfo = false;

  /// Enable position-independent code.
  bool PIC = false;

  /// Enable PIE (position-independent executable).
  bool PIE = false;

  /// Emit LLVM IR instead of object file.
  bool EmitLLVM = false;

  /// Emit assembly instead of object file.
  bool EmitAssembly = false;

  /// Emit object file (.o).
  bool EmitObject = false;

  /// Output file name.
  std::string OutputFile;

  /// Target triple for code generation.
  std::string TargetTriple;

  /// Stop after preprocessing (-E).
  bool PreprocessOnly = false;

  /// Stop after parsing (-fsyntax-only).
  bool SyntaxOnly = false;

  /// Stop after LLVM IR generation (-emit-llvm).
  bool EmitLLVMOnly = false;

  /// Link into executable (default behavior).
  bool LinkExecutable = true;
};

/// DiagnosticOptions - Diagnostic options.
struct DiagnosticOptions {
  /// Show source location in diagnostics.
  bool ShowLocation = true;

  /// Show caret and source snippet.
  bool ShowCarets = true;

  /// Show fix-it hints.
  bool ShowFixIts = true;

  /// Show column numbers.
  bool ShowColumn = true;

  /// Use colors in diagnostics.
  bool UseColors = true;

  /// Warning as error.
  bool WarningsAsErrors = false;

  /// Maximum number of errors before stopping.
  unsigned ErrorLimit = 20;
};

/// AIOptions - AI-assisted compilation options.
struct AIOptions {
  /// Enable AI-assisted compilation.
  bool Enable = false;

  /// AI provider name ("openai", "claude", "qwen", "local", "all").
  std::string Provider = "local";

  /// AI model name.
  std::string Model;

  /// Enable response caching.
  bool EnableCache = true;

  /// Maximum daily cost in USD.
  double MaxCostPerDay = 10.0;

  /// Ollama endpoint URL.
  std::string OllamaEndpoint = "http://localhost:11434";
};

/// FrontendOptions - Frontend options.
struct FrontendOptions {
  /// Input files.
  std::vector<std::string> InputFiles;

  /// Output file.
  std::string OutputFile;

  /// Dump AST after parsing.
  bool DumpAST = false;

  /// Verbose output.
  bool Verbose = false;

  /// Show help message.
  bool ShowHelp = false;

  /// Show version.
  bool ShowVersion = false;

  /// Include search paths.
  std::vector<std::string> IncludePaths;

  /// Library search paths for linking.
  std::vector<std::string> LibraryPaths;

  /// Libraries to link.
  std::vector<std::string> Libraries;

  /// Additional linker flags.
  std::vector<std::string> LinkerFlags;
};

/// CompilerInvocation - Encapsulates all compiler options.
///
/// This class is designed to:
/// - Store all compiler configuration in one place
/// - Support serialization/deserialization
/// - Be copyable for multi-threaded compilation
/// - Be independent of global state
class CompilerInvocation {
public:
  /// Language options.
  LangOptions LangOpts;

  /// Target options.
  TargetOptions TargetOpts;

  /// Code generation options.
  CodeGenOptions CodeGenOpts;

  /// Diagnostic options.
  DiagnosticOptions DiagOpts;

  /// AI options.
  AIOptions AIOpts;

  /// Frontend options.
  FrontendOptions FrontendOpts;

public:
  CompilerInvocation() = default;
  ~CompilerInvocation() = default;

  // Copyable and movable
  CompilerInvocation(const CompilerInvocation &) = default;
  CompilerInvocation &operator=(const CompilerInvocation &) = default;
  CompilerInvocation(CompilerInvocation &&) = default;
  CompilerInvocation &operator=(CompilerInvocation &&) = default;

  /// Parse command-line arguments and populate options.
  ///
  /// \param Argc Number of arguments.
  /// \param Argv Array of argument strings.
  /// \returns true if parsing succeeded, false otherwise.
  bool parseCommandLine(int Argc, const char *const *Argv);

  /// Parse command-line arguments from LLVM's CommandLine library.
  ///
  /// This is called after cl::ParseCommandLineOptions() to transfer
  /// values from global cl::opt variables to this object.
  void parseFromCommandLine();

  /// Set default target triple based on host platform.
  void setDefaultTargetTriple();

  /// Validate options for consistency.
  ///
  /// \returns true if options are valid, false otherwise.
  bool validate() const;

  /// Serialize options to a string (for debugging/testing).
  std::string toString() const;

  /// Deserialize options from a string.
  ///
  /// \param Str String representation of options.
  /// \returns true if deserialization succeeded, false otherwise.
  bool fromString(const std::string &Str);
};

} // namespace blocktype

//===--- FrontendCompileOptions.h - Frontend Compile Options ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FrontendCompileOptions struct which encapsulates
// per-frontend compilation options (input file, target triple, etc.).
// This is distinct from the existing blocktype::FrontendOptions in
// CompilerInvocation.h which controls compiler frontend behavior (DumpAST,
// Verbose, etc.).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_FRONTENDCOMPILEOPTIONS_H
#define BLOCKTYPE_FRONTEND_FRONTENDCOMPILEOPTIONS_H

#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace frontend {

/// FrontendCompileOptions - Per-frontend compilation options.
///
/// These options describe how a specific frontend should compile a single
/// input file. Each frontend instance receives its own copy of these options.
struct FrontendCompileOptions {
  /// Input source file path.
  std::string InputFile;

  /// Output file path (empty means derive from input).
  std::string OutputFile;

  /// Target triple (e.g., "x86_64-unknown-linux-gnu").
  /// Must be non-empty before calling compile().
  std::string TargetTriple;

  /// Source language identifier (e.g., "bt", "c", "cpp").
  std::string Language;

  /// Emit BTIR textual representation.
  bool EmitIR = false;

  /// Emit BTIR bitcode representation.
  bool EmitIRBitcode = false;

  /// Only emit IR, skip code generation.
  bool BTIROnly = false;

  /// Verify IR after generation.
  bool VerifyIR = false;

  /// Optimization level (0-3).
  unsigned OptimizationLevel = 0;

  /// Include search paths.
  ir::SmallVector<std::string, 4> IncludePaths;

  /// Predefined macro definitions.
  ir::SmallVector<std::string, 4> MacroDefinitions;
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_FRONTENDCOMPILEOPTIONS_H

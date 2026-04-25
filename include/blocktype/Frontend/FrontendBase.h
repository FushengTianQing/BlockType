//===--- FrontendBase.h - Abstract Frontend Base Class --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FrontendBase abstract base class, which provides
// the interface that all frontend implementations must conform to.
// Frontends consume source files and produce IR modules.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_FRONTENDBASE_H
#define BLOCKTYPE_FRONTEND_FRONTENDBASE_H

#include <functional>
#include <memory>

#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/FrontendCompileOptions.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace frontend {

/// FrontendBase - Abstract base class for all frontend implementations.
///
/// Each frontend is responsible for:
/// 1. Parsing source files in a specific language
/// 2. Producing an IRModule (BTIR) as output
/// 3. Reporting diagnostics through the DiagnosticsEngine
///
/// FrontendBase is non-copyable. Ownership of the produced IRModule
/// is transferred to the caller via compile().
class FrontendBase {
protected:
  FrontendCompileOptions Opts_;
  DiagnosticsEngine& Diags_;

public:
  /// Construct a frontend with the given options and diagnostics engine.
  FrontendBase(const FrontendCompileOptions& Opts, DiagnosticsEngine& Diags);

  /// Virtual destructor.
  virtual ~FrontendBase() = default;

  // Non-copyable.
  FrontendBase(const FrontendBase&) = delete;
  FrontendBase& operator=(const FrontendBase&) = delete;

  // Movable (needed for FrontendFactory).
  FrontendBase(FrontendBase&&) = default;
  // Move assignment deleted: reference member Diags_ cannot be re-bound.
  FrontendBase& operator=(FrontendBase&&) = delete;

  /// Get the human-readable name of this frontend (e.g., "BlockType", "C").
  virtual ir::StringRef getName() const = 0;

  /// Get the source language this frontend handles (e.g., "bt", "c").
  virtual ir::StringRef getLanguage() const = 0;

  /// Compile a source file into an IR module.
  ///
  /// \param Filename  Path to the source file.
  /// \param TypeCtx   IR type context for creating types.
  /// \param Layout    Target layout information.
  /// \returns Ownership of the produced IRModule, or nullptr on failure.
  ///
  /// Precondition: Opts_.TargetTriple must be non-empty.
  virtual std::unique_ptr<ir::IRModule> compile(
    ir::StringRef Filename,
    ir::IRTypeContext& TypeCtx,
    const ir::TargetLayout& Layout) = 0;

  /// Check if this frontend can handle the given file.
  ///
  /// Typically checks file extension (e.g., ".bt" for BlockType frontend).
  virtual bool canHandle(ir::StringRef Filename) const = 0;

  /// Access the compile options.
  const FrontendCompileOptions& getOptions() const { return Opts_; }

  /// Access the diagnostics engine.
  DiagnosticsEngine& getDiagnostics() const { return Diags_; }
};

/// FrontendFactory - Factory function type for creating frontend instances.
using FrontendFactory = std::function<std::unique_ptr<FrontendBase>(
  const FrontendCompileOptions&, DiagnosticsEngine&)>;

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_FRONTENDBASE_H

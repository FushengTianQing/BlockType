//===--- SFINAE.h - Substitution Failure Is Not An Error ----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines SFINAEContext for managing SFINAE contexts during
// template argument substitution and overload resolution.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

class DiagnosticsEngine;

/// SFINAEContext - Manages the SFINAE (Substitution Failure Is Not An Error)
/// context during template argument deduction and overload resolution.
///
/// In the "immediate context" of template argument substitution, failures
/// do not produce hard compilation errors. Instead, the failing candidate
/// is simply removed from the overload set.
///
/// Typical usage pattern in overload resolution:
///   1. Enter SFINAE context
///   2. For each candidate, attempt substitution
///   3. If substitution fails → remove candidate (no error emitted)
///   4. Exit SFINAE context
///
/// Nesting: SFINAE contexts can nest (e.g., when deducing template arguments
/// for a template template parameter). The context is active as long as
/// at least one level is open.
class SFINAEContext {
  /// Whether we are currently inside any SFINAE context.
  bool InSFINAE = false;

  /// Nesting depth counter. Supports nested deduction/substitution.
  unsigned NestingLevel = 0;

  /// Collected failure reasons (for diagnostics in ambiguous cases).
  llvm::SmallVector<std::string, 4> FailureReasons;

  /// Diagnostic trap count at the point of SFINAE entry.
  /// Used to determine if any new errors were emitted during substitution.
  unsigned ErrorCountAtEntry = 0;

  /// Pointer to the DiagnosticsEngine for error suppression.
  /// Set by SFINAEGuard when it knows the DiagnosticsEngine.
  DiagnosticsEngine *Diags = nullptr;

public:
  /// Enter a SFINAE context. Increments nesting level.
  void enterSFINAE(unsigned CurrentErrorCount = 0) {
    if (NestingLevel == 0)
      ErrorCountAtEntry = CurrentErrorCount;
    InSFINAE = true;
    ++NestingLevel;
  }

  /// Exit a SFINAE context. Decrements nesting level.
  /// When level reaches 0, the context is fully exited.
  void exitSFINAE() {
    if (NestingLevel > 0)
      --NestingLevel;
    if (NestingLevel == 0) {
      InSFINAE = false;
      FailureReasons.clear();
    }
  }

  /// Whether currently in an active SFINAE context.
  bool isSFINAE() const { return InSFINAE; }

  /// Get the current nesting level.
  unsigned getNestingLevel() const { return NestingLevel; }

  /// Record a failure reason (for diagnostic purposes).
  void addFailureReason(llvm::StringRef Reason) {
    FailureReasons.push_back(Reason.str());
  }

  /// Get all recorded failure reasons.
  llvm::ArrayRef<std::string> getFailureReasons() const {
    return FailureReasons;
  }

  /// Clear all recorded failure reasons.
  void clearFailureReasons() { FailureReasons.clear(); }

  /// Get the error count at the time of SFINAE entry.
  unsigned getErrorCountAtEntry() const { return ErrorCountAtEntry; }

  /// Determine if any errors were emitted since entering SFINAE.
  /// @param CurrentErrorCount  The current total error count
  /// @return true if new errors appeared during SFINAE context
  bool hasNewErrors(unsigned CurrentErrorCount) const {
    return CurrentErrorCount > ErrorCountAtEntry;
  }

  /// Set the DiagnosticsEngine for suppression integration.
  void setDiagnostics(DiagnosticsEngine *D) { Diags = D; }
  DiagnosticsEngine *getDiagnostics() const { return Diags; }
};

/// RAII guard for SFINAE context entry/exit.
/// When constructed with a DiagnosticsEngine, automatically pushes/pops
/// diagnostic suppression so that errors during substitution are silently
/// discarded (the core SFINAE mechanism).
class SFINAEGuard {
  SFINAEContext &Ctx;
  DiagnosticsEngine *Diags;

public:
  /// Construct an SFINAE guard.
  /// @param C              The SFINAEContext to enter/exit
  /// @param CurrentErrorCount  Current error count (for hasNewErrors)
  /// @param Diagnostics    Optional DiagnosticsEngine to suppress during SFINAE
  explicit SFINAEGuard(SFINAEContext &C, unsigned CurrentErrorCount = 0,
                        DiagnosticsEngine *Diagnostics = nullptr)
      : Ctx(C), Diags(Diagnostics) {
    Ctx.enterSFINAE(CurrentErrorCount);
    // Suppress diagnostics while in SFINAE context
    if (Diags)
      Diags->pushSuppression();
  }

  ~SFINAEGuard() {
    // Restore diagnostic emission before exiting SFINAE context
    if (Diags)
      Diags->popSuppression();
    Ctx.exitSFINAE();
  }

  // Non-copyable, non-movable
  SFINAEGuard(const SFINAEGuard &) = delete;
  SFINAEGuard &operator=(const SFINAEGuard &) = delete;
};

} // namespace blocktype

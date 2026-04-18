//===--- Overload.h - Overload Resolution Types -------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines types used for overload resolution: OverloadCandidate
// and OverloadCandidateSet.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Sema/Conversion.h"
#include "llvm/ADT/SmallVector.h"
#include <utility>

// Forward declare — used only for constraint-based tie-breaking.
namespace detail {
class ConstraintTieBreaker;
}

namespace blocktype {

class LookupResult;

/// OverloadCandidate - A single candidate in overload resolution.
class OverloadCandidate {
  FunctionDecl *Function;

  /// If this candidate came from a function template, track the template.
  /// Used for constraint-based partial ordering tie-breaking.
  FunctionTemplateDecl *Template = nullptr;

  /// Computed conversion rank for each argument.
  llvm::SmallVector<ConversionRank, 4> ArgRanks;

  /// Whether this candidate is viable (all conversions possible).
  bool Viable = false;

  /// Failure reason (for error diagnostics).
  llvm::StringRef FailureReason;

public:
  explicit OverloadCandidate(FunctionDecl *F) : Function(F) {}

  FunctionDecl *getFunction() const { return Function; }
  FunctionTemplateDecl *getTemplate() const { return Template; }
  void setTemplate(FunctionTemplateDecl *T) { Template = T; }
  bool isViable() const { return Viable; }
  void setViable(bool V) { Viable = V; }

  llvm::ArrayRef<ConversionRank> getArgRanks() const { return ArgRanks; }
  void addArgRank(ConversionRank R) { ArgRanks.push_back(R); }

  llvm::StringRef getFailureReason() const { return FailureReason; }
  void setFailureReason(llvm::StringRef Reason) { FailureReason = Reason; }

  /// Check argument count and conversions.
  bool checkViability(llvm::ArrayRef<Expr *> Args);

  /// Compare this candidate with another.
  /// Returns:
  ///  - <0 if this is better
  ///  - 0 if indistinguishable
  ///  - >0 if Other is better
  int compare(const OverloadCandidate &Other) const;
};

/// Result of overload resolution.
enum class OverloadResult {
  /// A single best viable function was found.
  Success,
  /// No viable candidates.
  NoViable,
  /// Multiple candidates are equally good (ambiguous).
  Ambiguous,
  /// The best candidate is a deleted function.
  Deleted,
};

/// OverloadCandidateSet - A set of candidates for overload resolution.
class OverloadCandidateSet {
  SourceLocation CallLoc;
  llvm::SmallVector<OverloadCandidate, 16> Candidates;

  /// Constraint checker for tie-breaking via constraint partial ordering.
  class ConstraintSatisfaction *ConstraintChecker = nullptr;

public:
  explicit OverloadCandidateSet(SourceLocation Loc) : CallLoc(Loc) {}

  /// Set the constraint checker for constraint-based tie-breaking.
  void setConstraintChecker(ConstraintSatisfaction *Checker) {
    ConstraintChecker = Checker;
  }

  /// Add a candidate function.
  OverloadCandidate &addCandidate(FunctionDecl *F);

  /// Add a candidate function that came from a function template.
  OverloadCandidate &addTemplateCandidate(FunctionDecl *F,
                                           FunctionTemplateDecl *T);

  /// Add all functions from a LookupResult.
  void addCandidates(const LookupResult &R);

  /// Get all candidates.
  llvm::ArrayRef<OverloadCandidate> getCandidates() const { return Candidates; }

  /// Get viable candidates only.
  llvm::SmallVector<OverloadCandidate *, 4> getViableCandidates() const;

  /// Resolve overload: find the best matching function.
  /// Returns a pair of (result kind, best function).
  /// Function is non-null only when result is Success or Deleted.
  std::pair<OverloadResult, FunctionDecl *>
  resolve(llvm::ArrayRef<Expr *> Args);

  /// Get the number of candidates.
  unsigned size() const { return Candidates.size(); }
  bool empty() const { return Candidates.empty(); }
};

} // namespace blocktype

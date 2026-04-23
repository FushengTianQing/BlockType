//===--- Overload.cpp - Overload Resolution -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements overload resolution: candidate collection,
// viability checking, candidate comparison, and best function selection.
//
// Task 4.4.1 — 重载候选集
// Task 4.4.3 — 最佳函数选择
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Overload.h"
#include "blocktype/Sema/Lookup.h"
#include "blocktype/Sema/ConstraintSatisfaction.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// OverloadCandidate — checkViability
//===----------------------------------------------------------------------===//

bool OverloadCandidate::checkViability(llvm::ArrayRef<Expr *> Args) {
  if (!Function) {
    Viable = false;
    FailureReason = "null function declaration";
    return false;
  }

  // Get the function type
  QualType FnType = Function->getType();
  if (FnType.isNull()) {
    Viable = false;
    FailureReason = "function has no type";
    return false;
  }

  const auto *FT = llvm::dyn_cast<FunctionType>(FnType.getTypePtr());
  if (!FT) {
    Viable = false;
    FailureReason = "not a function type";
    return false;
  }

  unsigned NumParams = Function->getNumParams();
  unsigned NumArgs = Args.size();
  bool IsVariadic = FT->isVariadic();

  // Check parameter count
  // Count parameters with default arguments
  unsigned MinArgs = 0;
  for (unsigned I = 0; I < NumParams; ++I) {
    ParmVarDecl *PVD = Function->getParamDecl(I);
    if (!PVD->getDefaultArg()) {
      MinArgs = I + 1;
    }
  }

  // Not enough arguments
  if (NumArgs < MinArgs) {
    Viable = false;
    FailureReason = "too few arguments";
    return false;
  }

  // Too many arguments (for non-variadic functions)
  if (!IsVariadic && NumArgs > NumParams) {
    Viable = false;
    FailureReason = "too many arguments";
    return false;
  }

  // Check each argument's conversion
  ArgRanks.clear();
  ConversionSequences.clear();
  bool AllConversionsValid = true;

  for (unsigned I = 0; I < NumArgs; ++I) {
    QualType ArgType = Args[I]->getType();
    if (ArgType.isNull()) {
      ArgRanks.push_back(ConversionRank::BadConversion);
      ConversionSequences.push_back(ImplicitConversionSequence::getBad());
      AllConversionsValid = false;
      continue;
    }

    if (I < NumParams) {
      // Regular parameter: check conversion from arg type to param type
      QualType ParamType = Function->getParamDecl(I)->getType();
      ImplicitConversionSequence ICS =
          ConversionChecker::GetConversion(ArgType, ParamType);

      if (ICS.isBad()) {
        ArgRanks.push_back(ConversionRank::BadConversion);
        ConversionSequences.push_back(ICS);
        AllConversionsValid = false;
      } else {
        ArgRanks.push_back(ICS.getRank());
        ConversionSequences.push_back(ICS);
      }
    } else {
      // Variadic argument: always matches via ellipsis
      ArgRanks.push_back(ConversionRank::Ellipsis);
      ConversionSequences.push_back(ImplicitConversionSequence::getEllipsis());
    }
  }

  if (!AllConversionsValid) {
    Viable = false;
    FailureReason = "no viable conversion for one or more arguments";
    return false;
  }

  Viable = true;
  FailureReason = "";
  return true;
}

//===----------------------------------------------------------------------===//
// OverloadCandidate — compare
//===----------------------------------------------------------------------===//

int OverloadCandidate::compare(const OverloadCandidate &Other) const {
  // Per C++ [over.match.best]:
  // A candidate is better if for at least one argument its ICS is better
  // than the Other's ICS for the same argument, and no argument has a
  // worse ICS.

  if (!Function || !Other.Function)
    return 0;

  unsigned NumParams = Function->getNumParams();
  unsigned OtherNumParams = Other.Function->getNumParams();
  unsigned MaxParams = std::max(NumParams, OtherNumParams);
  unsigned NumArgs = ArgRanks.size();

  bool ThisBetter = false;
  bool OtherBetter = false;

  for (unsigned I = 0; I < NumArgs && I < MaxParams; ++I) {
    ConversionRank ThisRank =
        (I < ArgRanks.size()) ? ArgRanks[I] : ConversionRank::BadConversion;
    ConversionRank OtherRank = (I < Other.ArgRanks.size())
                                   ? Other.ArgRanks[I]
                                   : ConversionRank::BadConversion;

    if (static_cast<unsigned>(ThisRank) <
        static_cast<unsigned>(OtherRank)) {
      ThisBetter = true;
    } else if (static_cast<unsigned>(ThisRank) >
               static_cast<unsigned>(OtherRank)) {
      OtherBetter = true;
    } else if (ThisRank == OtherRank &&
               I < ConversionSequences.size() &&
               I < Other.ConversionSequences.size()) {
      // Same rank: use fine-grained ICS comparison for disambiguation
      // Per [over.ics.rank]: compare sub-steps of conversion sequences
      int ICSCompare = ConversionSequences[I].compare(Other.ConversionSequences[I]);
      if (ICSCompare < 0) {
        ThisBetter = true;
      } else if (ICSCompare > 0) {
        OtherBetter = true;
      }
    }
  }

  if (ThisBetter && !OtherBetter)
    return -1; // This is better
  if (OtherBetter && !ThisBetter)
    return 1; // Other is better

  // Tie-breaker: non-variadic is better than variadic
  const auto *FT =
      llvm::dyn_cast<FunctionType>(Function->getType().getTypePtr());
  const auto *OFT =
      llvm::dyn_cast<FunctionType>(Other.Function->getType().getTypePtr());

  if (FT && OFT) {
    if (!FT->isVariadic() && OFT->isVariadic())
      return -1;
    if (FT->isVariadic() && !OFT->isVariadic())
      return 1;
  }

  // Tie-breaker: fewer parameters (with defaults) is preferred
  if (NumParams < OtherNumParams)
    return -1;
  if (NumParams > OtherNumParams)
    return 1;

  // Tie-breaker: non-template is better than template
  // Per C++ [over.match.best]/2a: a non-template function is preferred
  // over a function template specialization.
  if (!Template && Other.Template)
    return -1;
  if (Template && !Other.Template)
    return 1;

  // Tie-breaker: more specialized template is preferred
  // Per C++ [over.match.best]/2b and [temp.func.order]: partial ordering
  // of function templates — a more specialized template is preferred.
  if (Template && Other.Template && Template != Other.Template) {
    // Both are function templates with different constraints.
    // We need a ConstraintSatisfaction to compare — but we don't have
    // access to it here. The constraint-based tie-break is done in resolve()
    // after initial comparison. Return 0 (indistinguishable) here so that
    // resolve() can apply constraint-based disambiguation.
    return 0;
  }

  return 0; // Indistinguishable
}

//===----------------------------------------------------------------------===//
// OverloadCandidateSet — addCandidate
//===----------------------------------------------------------------------===//

OverloadCandidate &OverloadCandidateSet::addCandidate(FunctionDecl *F) {
  Candidates.emplace_back(F);
  return Candidates.back();
}

//===----------------------------------------------------------------------===//
// OverloadCandidateSet — addTemplateCandidate
//===----------------------------------------------------------------------===//

OverloadCandidate &
OverloadCandidateSet::addTemplateCandidate(FunctionDecl *F,
                                             FunctionTemplateDecl *T) {
  Candidates.emplace_back(F);
  Candidates.back().setTemplate(T);
  return Candidates.back();
}

//===----------------------------------------------------------------------===//
// OverloadCandidateSet — addCandidates
//===----------------------------------------------------------------------===//

void OverloadCandidateSet::addCandidates(const LookupResult &R) {
  for (auto *D : R.getDecls()) {
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      addCandidate(FD);
    } else if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
      // Add as a template candidate — the caller is responsible for
      // deducing template arguments and instantiating before viability check.
      if (auto *TemplatedFD = llvm::dyn_cast_or_null<FunctionDecl>(
              FTD->getTemplatedDecl())) {
        addTemplateCandidate(TemplatedFD, FTD);
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// OverloadCandidateSet — getViableCandidates
//===----------------------------------------------------------------------===//

llvm::SmallVector<OverloadCandidate *, 4>
OverloadCandidateSet::getViableCandidates() const {
  llvm::SmallVector<OverloadCandidate *, 4> Result;
  for (auto &C : Candidates) {
    if (C.isViable())
      Result.push_back(const_cast<OverloadCandidate *>(&C));
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// OverloadCandidateSet — resolve (Task 4.4.3)
//===----------------------------------------------------------------------===//

std::pair<OverloadResult, FunctionDecl *>
OverloadCandidateSet::resolve(llvm::ArrayRef<Expr *> Args) {
  // 1. Check viability of all candidates
  for (auto &C : Candidates) {
    C.checkViability(Args);
  }

  // 2. Collect viable candidates
  auto Viable = getViableCandidates();

  // 3. No viable candidates
  if (Viable.empty())
    return {OverloadResult::NoViable, nullptr};

  // 4. Single viable candidate
  if (Viable.size() == 1) {
    FunctionDecl *FD = Viable[0]->getFunction();
    // Check if the selected function is deleted
    if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
      if (MD->isDeleted())
        return {OverloadResult::Deleted, FD};
    }
    return {OverloadResult::Success, FD};
  }

  // 5. Multiple viable candidates: find the best via pairwise comparison
  // Per C++ [over.match.best]:
  //   Best is the candidate that is better than all others.
  OverloadCandidate *Best = Viable[0];
  bool Ambiguous = false;

  for (unsigned I = 1; I < Viable.size(); ++I) {
    int Cmp = Best->compare(*Viable[I]);
    if (Cmp > 0) {
      // Viable[I] is better
      Best = Viable[I];
      Ambiguous = false;
    } else if (Cmp == 0) {
      // Indistinguishable — mark as potentially ambiguous
      Ambiguous = true;
    }
    // Cmp < 0: Best is still better, no change
  }

  // Verify: Best must be better than ALL other candidates
  // (The first pass may miss cases where Best beats some but ties with another)
  if (!Ambiguous) {
    for (auto *C : Viable) {
      if (C == Best)
        continue;
      if (Best->compare(*C) == 0) {
        Ambiguous = true;
        break;
      }
    }
  }

  if (Ambiguous)
    return {OverloadResult::Ambiguous, nullptr};

  // If Best tied with another candidate, attempt constraint-based tie-break.
  // Per C++ [over.match.best]: a more constrained template is preferred.
  if (ConstraintChecker && Best->getTemplate()) {
    for (auto *C : Viable) {
      if (C == Best)
        continue;
      if (Best->compare(*C) == 0 && C->getTemplate()) {
        // Both have templates and are indistinguishable by conversion rank.
        // Check constraint partial ordering.
        int Cmp = ConstraintChecker->CompareFunctionTemplateConstraints(
            Best->getTemplate(), C->getTemplate());
        if (Cmp > 0) {
          // C is more constrained — it becomes the new best
          Best = C;
        } else if (Cmp == 0) {
          // Constraints are equivalent — still ambiguous
          return {OverloadResult::Ambiguous, nullptr};
        }
        // Cmp < 0: Best is more constrained — keep Best
      }
    }
  }

  FunctionDecl *FD = Best->getFunction();

  // Check if the best candidate is a deleted function
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
    if (MD->isDeleted())
      return {OverloadResult::Deleted, FD};
  }

  return {OverloadResult::Success, FD};
}

} // namespace blocktype

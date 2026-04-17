//===--- TemplateDeduction.h - Template Argument Deduction ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines TemplateDeductionResult, TemplateDeductionInfo, and
// TemplateDeduction for template argument deduction.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

class Sema;

/// TemplateDeductionResult - Result code for template argument deduction.
enum class TemplateDeductionResult {
  Success,             // Deduction succeeded
  SubstitutionFailure, // Substitution failure (SFINAE)
  NonDeducedMismatch,  // Non-deduced context mismatch
  Inconsistent,        // Already-deduced argument inconsistent
  TooManyArguments,    // Too many arguments provided
  TooFewArguments,     // Too few arguments provided
};

/// TemplateDeductionInfo - Tracks per-parameter deduction results.
///
/// Maps template parameter indices to their deduced arguments.
/// Also records the first failure position for diagnostics.
class TemplateDeductionInfo {
  /// Deduced arguments: parameter index → deduced argument
  llvm::DenseMap<unsigned, TemplateArgument> DeducedArgs;

  /// First failure position (for diagnostics).
  unsigned FirstFailedIndex = ~0u;

  /// Whether deduction has failed at any point.
  bool HasFailure = false;

public:
  void addDeducedArg(unsigned Index, TemplateArgument Arg) {
    DeducedArgs[Index] = Arg;
  }

  TemplateArgument getDeducedArg(unsigned Index) const {
    auto It = DeducedArgs.find(Index);
    return It != DeducedArgs.end() ? It->second : TemplateArgument();
  }

  bool hasDeducedArg(unsigned Index) const {
    return DeducedArgs.find(Index) != DeducedArgs.end();
  }

  /// Get all deduced arguments ordered by parameter index (0..NumParams-1).
  /// Un-deduced parameters get a null TemplateArgument.
  llvm::SmallVector<TemplateArgument, 4>
  getDeducedArgs(unsigned NumParams) const;

  unsigned getFirstFailedIndex() const { return FirstFailedIndex; }
  void setFirstFailedIndex(unsigned Idx) {
    FirstFailedIndex = Idx;
    HasFailure = true;
  }

  bool hasFailure() const { return HasFailure; }
  void setHasFailure(bool Val = true) { HasFailure = Val; }

  unsigned getNumDeduced() const { return DeducedArgs.size(); }

  void reset() {
    DeducedArgs.clear();
    FirstFailedIndex = ~0u;
    HasFailure = false;
  }
};

/// TemplateDeduction - Template argument deduction engine.
///
/// Implements C++ [temp.deduct] deduction algorithm:
/// - Deduce function template arguments from call arguments
/// - Deduce from type pairs (P, A) recursively
/// - Support partial ordering comparisons
class TemplateDeduction {
  Sema &SemaRef;

public:
  explicit TemplateDeduction(Sema &S) : SemaRef(S) {}

  // === Function Template Argument Deduction ===

  /// Deduce function template arguments from call expressions.
  /// @param FTD        Function template declaration
  /// @param CallArgs   Call argument expressions
  /// @param Info       Output: deduction results
  /// @return           Deduction result code
  TemplateDeductionResult
  DeduceFunctionTemplateArguments(FunctionTemplateDecl *FTD,
                                  llvm::ArrayRef<Expr *> CallArgs,
                                  TemplateDeductionInfo &Info);

  // === Type Deduction ===

  /// Deduce template arguments from a parameter type P and argument type A.
  /// Implements C++ [temp.deduct.type] deduction rules.
  /// @param ParamType  Function parameter type (may contain TemplateTypeParmType)
  /// @param ArgType    Argument expression type
  /// @param Info       Deduction info (accumulates results)
  /// @return           Deduction result code
  TemplateDeductionResult
  DeduceTemplateArguments(QualType ParamType, QualType ArgType,
                          TemplateDeductionInfo &Info);

  // === Array Deduction ===

  /// Deduce template arguments for an array of P/A type pairs.
  /// Fails if any individual deduction fails.
  TemplateDeductionResult
  DeduceTemplateArguments(llvm::ArrayRef<QualType> ParamTypes,
                          llvm::ArrayRef<QualType> ArgTypes,
                          TemplateDeductionInfo &Info);

  // === Partial Ordering ===

  /// Determine if P1 is more specialized than P2.
  /// Implements C++ [temp.deduct.partial] partial ordering algorithm.
  /// @return true if P1 is more specialized than P2
  static bool isMoreSpecialized(TemplateDecl *P1, TemplateDecl *P2);

private:
  // Recursive deduction for various type combinations
  TemplateDeductionResult
  DeduceFromPointerType(const PointerType *Param, const PointerType *Arg,
                        TemplateDeductionInfo &Info);

  TemplateDeductionResult
  DeduceFromReferenceType(const ReferenceType *Param, const ReferenceType *Arg,
                          TemplateDeductionInfo &Info);

  TemplateDeductionResult
  DeduceFromTemplateSpecializationType(
      const TemplateSpecializationType *Param,
      const TemplateSpecializationType *Arg, TemplateDeductionInfo &Info);

  TemplateDeductionResult
  DeduceFromArrayType(const ArrayType *Param, const ArrayType *Arg,
                      TemplateDeductionInfo &Info);

  TemplateDeductionResult
  DeduceFromFunctionType(const FunctionType *Param, const FunctionType *Arg,
                         TemplateDeductionInfo &Info);

  /// Perform reference collapsing.
  /// T& & → T&, T&& & → T&, T& && → T&, T&& && → T&&
  QualType collapseReferences(QualType Inner, bool OuterIsLValue);

  /// Generate a unique synthetic type for partial ordering.
  static QualType generateDeducedType(NamedDecl *Param);
};

} // namespace blocktype

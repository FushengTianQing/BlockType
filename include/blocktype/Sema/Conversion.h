//===--- Conversion.h - Implicit Conversion Types -----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines types used for implicit conversion analysis:
// ConversionRank, StandardConversionSequence, ImplicitConversionSequence,
// and ConversionChecker.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

class FunctionDecl; // Forward declaration

/// ConversionRank - The rank of an implicit conversion sequence.
///
/// Following the C++ standard [over.ics.rank]:
/// - ExactMatch: no conversion needed (or only qualification adjustment)
/// - Promotion: integral promotion (char→int, float→double)
/// - Conversion: standard conversion (int→long, derived→base pointer)
/// - UserDefined: user-defined conversion (constructor or conversion op)
/// - Ellipsis: match via `...`
/// - BadConversion: no valid conversion exists
enum class ConversionRank : unsigned {
  ExactMatch    = 0,
  Promotion     = 1,
  Conversion    = 2,
  UserDefined   = 3,
  Ellipsis      = 4,
  BadConversion = 5,
};

/// StandardConversionKind - The three sub-ranks of a standard conversion.
enum class StandardConversionKind {
  /// No conversion needed.
  Identity,

  /// Lvalue-to-rvalue conversion, function-to-pointer, array-to-pointer.
  LvalueTransformation,

  /// Integral promotion or floating-point promotion.
  Promotion,

  /// Integral conversion, floating-point conversion, floating-integral,
  /// pointer conversion, pointer-to-member conversion, boolean conversion.
  Conversion,

  /// Qualification adjustment (adding const/volatile).
  QualificationAdjustment,
};

/// StandardConversionSequence - A standard conversion sequence.
///
/// A standard conversion sequence consists of up to three conversions:
/// 1. Lvalue transformation
/// 2. Qualification adjustment
/// 3. Promotion or conversion
class StandardConversionSequence {
  StandardConversionKind First = StandardConversionKind::Identity;
  StandardConversionKind Second = StandardConversionKind::Identity;
  StandardConversionKind Third = StandardConversionKind::Identity;
  ConversionRank Rank = ConversionRank::ExactMatch;

public:
  StandardConversionSequence() = default;

  void setFirst(StandardConversionKind K) { First = K; }
  void setSecond(StandardConversionKind K) { Second = K; }
  void setThird(StandardConversionKind K) { Third = K; }
  void setRank(ConversionRank R) { Rank = R; }

  ConversionRank getRank() const { return Rank; }
  StandardConversionKind getFirst() const { return First; }
  StandardConversionKind getSecond() const { return Second; }
  StandardConversionKind getThird() const { return Third; }

  bool isBad() const { return Rank == ConversionRank::BadConversion; }

  /// Compare two standard conversion sequences.
  /// Returns <0 if this is better, 0 if equal, >0 if Other is better.
  int compare(const StandardConversionSequence &Other) const;
};

/// ImplicitConversionSequence - A complete implicit conversion sequence.
///
/// Either a standard conversion sequence, a user-defined conversion,
/// or an ellipsis conversion.
class ImplicitConversionSequence {
public:
  enum ConversionKind {
    /// Standard conversion sequence only.
    StandardConversion,

    /// User-defined conversion (constructor or conversion operator)
    /// followed by a standard conversion.
    UserDefinedConversion,

    /// Ellipsis conversion (...).
    EllipsisConversion,

    /// No valid conversion.
    BadConversion,
  };

private:
  ConversionKind Kind = BadConversion;
  StandardConversionSequence Standard;
  FunctionDecl *UserDefinedConverter = nullptr;
  ConversionRank Rank = ConversionRank::BadConversion;

public:
  ImplicitConversionSequence() = default;

  static ImplicitConversionSequence getStandard(StandardConversionSequence SCS) {
    ImplicitConversionSequence ICS;
    ICS.Kind = StandardConversion;
    ICS.Standard = SCS;
    ICS.Rank = SCS.getRank();
    return ICS;
  }

  static ImplicitConversionSequence getUserDefined(FunctionDecl *Converter,
                                                    StandardConversionSequence SCS) {
    ImplicitConversionSequence ICS;
    ICS.Kind = UserDefinedConversion;
    ICS.UserDefinedConverter = Converter;
    ICS.Standard = SCS;
    ICS.Rank = ConversionRank::UserDefined;
    return ICS;
  }

  static ImplicitConversionSequence getEllipsis() {
    ImplicitConversionSequence ICS;
    ICS.Kind = EllipsisConversion;
    ICS.Rank = ConversionRank::Ellipsis;
    return ICS;
  }

  static ImplicitConversionSequence getBad() {
    return ImplicitConversionSequence();
  }

  ConversionKind getKind() const { return Kind; }
  ConversionRank getRank() const { return Rank; }
  bool isBad() const { return Kind == BadConversion; }
  const StandardConversionSequence &getStandard() const { return Standard; }
  FunctionDecl *getUserDefinedConverter() const { return UserDefinedConverter; }

  /// Compare two implicit conversion sequences.
  int compare(const ImplicitConversionSequence &Other) const;
};

/// ConversionChecker - Static utility for checking implicit conversions.
class ConversionChecker {
public:
  /// Get the implicit conversion sequence from From to To.
  static ImplicitConversionSequence GetConversion(QualType From, QualType To);

  /// Check if a standard conversion exists from From to To.
  static StandardConversionSequence GetStandardConversion(QualType From,
                                                            QualType To);

  /// Check if an integral promotion exists.
  static bool isIntegralPromotion(QualType From, QualType To);

  /// Check if a floating-point promotion exists.
  static bool isFloatingPointPromotion(QualType From, QualType To);

  /// Check if a standard conversion exists (not promotion).
  static bool isStandardConversion(QualType From, QualType To);

  /// Check if a qualification conversion exists.
  static bool isQualificationConversion(QualType From, QualType To);

  /// Check if a derived-to-base pointer conversion exists.
  static bool isDerivedToBaseConversion(QualType From, QualType To);

private:
  /// Try to find a converting constructor in ToClass that accepts From.
  static ImplicitConversionSequence TryConvertingConstructor(
      QualType From, CXXRecordDecl *ToClass);

  /// Try to find a conversion operator in FromClass that converts to To.
  static ImplicitConversionSequence TryConversionOperator(
      CXXRecordDecl *FromClass, QualType To);
};

} // namespace blocktype

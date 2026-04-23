//===--- Conversion.cpp - Implicit Conversion Analysis ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ConversionChecker static utility class and
// the compare() methods for StandardConversionSequence and
// ImplicitConversionSequence.
//
// Task 4.4.2 — 隐式转换与排序
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Conversion.h"
#include "blocktype/AST/Decl.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Helper: BuiltinKind integer rank table
//
// Per C++ [conv.rank]:
//   bool < char < short < int < long < long long
//   signed and unsigned of the same width share the same rank.
//   Float < Double < LongDouble (separate floating-point rank)
//===----------------------------------------------------------------------===//

/// Get the integer conversion rank for a BuiltinKind.
/// Returns -1 if not an integer type.
static int getIntegerRank(BuiltinKind K) {
  switch (K) {
  case BuiltinKind::Bool:
    return 0;
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:
  case BuiltinKind::Char16:
    return 1;
  case BuiltinKind::WChar:
  case BuiltinKind::Char32:
    return 2;
  case BuiltinKind::Short:
  case BuiltinKind::UnsignedShort:
    return 3;
  case BuiltinKind::Int:
  case BuiltinKind::UnsignedInt:
    return 4;
  case BuiltinKind::Long:
  case BuiltinKind::UnsignedLong:
    return 5;
  case BuiltinKind::LongLong:
  case BuiltinKind::UnsignedLongLong:
    return 6;
  case BuiltinKind::Int128:
  case BuiltinKind::UnsignedInt128:
    return 7;
  default:
    return -1;
  }
}

/// Get the floating-point rank for a BuiltinKind.
/// Returns -1 if not a floating-point type.
static int getFloatingRank(BuiltinKind K) {
  switch (K) {
  case BuiltinKind::Float:
    return 0;
  case BuiltinKind::Double:
    return 1;
  case BuiltinKind::LongDouble:
    return 2;
  case BuiltinKind::Float128:
    return 3;
  default:
    return -1;
  }
}

/// Check if two QualTypes are the same ignoring CVR qualifiers.
static bool isSameUnqualifiedType(QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return false;
  // Compare canonical types ignoring qualifiers
  QualType C1 = T1.getCanonicalType();
  QualType C2 = T2.getCanonicalType();
  // Strip qualifiers for comparison — compare only the Type* pointers
  return C1.getTypePtr() == C2.getTypePtr();
}

/// Get the unqualified type pointer from QualType.
static const Type *getUnqualified(QualType T) {
  if (T.isNull())
    return nullptr;
  return T.getTypePtr();
}

//===----------------------------------------------------------------------===//
// StandardConversionSequence::compare
//===----------------------------------------------------------------------===//

int StandardConversionSequence::compare(
    const StandardConversionSequence &Other) const {
  // Compare by rank first
  if (static_cast<unsigned>(Rank) < static_cast<unsigned>(Other.Rank))
    return -1;
  if (static_cast<unsigned>(Rank) > static_cast<unsigned>(Other.Rank))
    return 1;

  // Same rank: compare sub-steps for better disambiguation
  // Per [over.ics.rank]:
  //   - A conversion that is not a qualification conversion is better than
  //     one that is (when both have the same rank).
  //   - SCS with fewer non-Identity sub-steps is better.

  auto subStepRank = [](StandardConversionKind K) -> int {
    switch (K) {
    case StandardConversionKind::Identity:
      return 0;
    case StandardConversionKind::QualificationAdjustment:
      return 1;
    case StandardConversionKind::LvalueTransformation:
      return 2;
    case StandardConversionKind::Promotion:
      return 3;
    case StandardConversionKind::Conversion:
      return 4;
    }
    return 5;
  };

  // Compare First step
  int r1 = subStepRank(First) - subStepRank(Other.First);
  if (r1 != 0)
    return r1;

  // Compare Second step
  int r2 = subStepRank(Second) - subStepRank(Other.Second);
  if (r2 != 0)
    return r2;

  // Compare Third step
  int r3 = subStepRank(Third) - subStepRank(Other.Third);
  if (r3 != 0)
    return r3;

  return 0; // Truly indistinguishable
}

//===----------------------------------------------------------------------===//
// ImplicitConversionSequence::compare
//===----------------------------------------------------------------------===//

int ImplicitConversionSequence::compare(
    const ImplicitConversionSequence &Other) const {
  // Bad conversions are always worst
  if (isBad() && Other.isBad())
    return 0;
  if (isBad())
    return 1;
  if (Other.isBad())
    return -1;

  // Kind priority: Standard > UserDefined > Ellipsis
  auto kindPriority = [](ConversionKind K) -> int {
    switch (K) {
    case StandardConversion:
      return 0;
    case UserDefinedConversion:
      return 1;
    case EllipsisConversion:
      return 2;
    case BadConversion:
      return 3;
    }
    return 4;
  };

  int p1 = kindPriority(Kind);
  int p2 = kindPriority(Other.Kind);
  if (p1 < p2)
    return -1;
  if (p1 > p2)
    return 1;

  // Same kind: compare standard conversion sequences
  if (Kind == StandardConversion) {
    return Standard.compare(Other.Standard);
  }

  // For user-defined or ellipsis: same kind is indistinguishable
  // unless the standard parts differ
  if (Kind == UserDefinedConversion) {
    // Per [over.ics.rank]: compare the second standard conversion
    return Standard.compare(Other.Standard);
  }

  return 0;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isIntegralPromotion
//===----------------------------------------------------------------------===//

bool ConversionChecker::isIntegralPromotion(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  const auto *FromBT = llvm::dyn_cast<BuiltinType>(FromTy);
  const auto *ToBT = llvm::dyn_cast<BuiltinType>(ToTy);

  // --- Enum integral promotion (C++ [conv.prom]) ---
  // An unscoped enumeration whose underlying type is not fixed can promote
  // to int if int can represent all values of the enumeration, otherwise
  // to unsigned int. For simplicity, we treat any enum → int/unsigned int
  // as a promotion, which matches common compiler behavior.
  if (auto *FromEnum = llvm::dyn_cast<EnumType>(FromTy)) {
    if (!ToBT)
      return false;
    // Enum promotes to int (or unsigned int if the enum's values exceed int)
    (void)FromEnum; // suppress unused warning
    if (ToBT->getKind() == BuiltinKind::Int ||
        ToBT->getKind() == BuiltinKind::UnsignedInt) {
      return true;
    }
    return false;
  }

  // Both must be builtin integer types from here
  if (!FromBT || !ToBT)
    return false;

  // Both must be integer types
  if (!FromBT->isInteger() || !ToBT->isInteger())
    return false;

  // int -> int is not a promotion (it's identity)
  if (FromBT->getKind() == ToBT->getKind())
    return false;

  int FromRank = getIntegerRank(FromBT->getKind());
  int ToRank = getIntegerRank(ToBT->getKind());

  // Promotion: From has strictly lower rank, To is at least int
  // Per C++ [conv.prom]:
  //   - bool, char, short → int (or unsigned int) is promotion
  //   - int → long is NOT promotion (it's conversion)
  if (FromRank < 0 || ToRank < 0)
    return false;

  // Promotion only occurs when the source rank is less than int (rank 4)
  // and target is int or unsigned int.
  if (FromRank < 4 && ToRank == 4) {
    return true;
  }

  // Special case: bool → int is always promotion
  if (FromBT->getKind() == BuiltinKind::Bool &&
      (ToBT->getKind() == BuiltinKind::Int ||
       ToBT->getKind() == BuiltinKind::UnsignedInt)) {
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isFloatingPointPromotion
//===----------------------------------------------------------------------===//

bool ConversionChecker::isFloatingPointPromotion(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  const auto *FromBT = llvm::dyn_cast<BuiltinType>(getUnqualified(From));
  const auto *ToBT = llvm::dyn_cast<BuiltinType>(getUnqualified(To));
  if (!FromBT || !ToBT)
    return false;

  if (!FromBT->isFloatingPoint() || !ToBT->isFloatingPoint())
    return false;

  // float → double is the only floating-point promotion
  // (float → long double is also promotion per C++ [conv.fpprom])
  int FromRank = getFloatingRank(FromBT->getKind());
  int ToRank = getFloatingRank(ToBT->getKind());

  if (FromRank < 0 || ToRank < 0)
    return false;

  // Strict increase in floating-point rank is a promotion
  return FromRank < ToRank;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isQualificationConversion
//===----------------------------------------------------------------------===//

bool ConversionChecker::isQualificationConversion(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  // Qualification conversion: adding const/volatile to pointer types
  // e.g., int* → const int*  or  int** → const int* const*
  //
  // Per C++ [conv.qual]: A qualification conversion is a multi-level
  // pointer conversion that adds qualifiers at each level.

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  // Single-level pointer qualification conversion
  const auto *FromPtr = llvm::dyn_cast<PointerType>(FromTy);
  const auto *ToPtr = llvm::dyn_cast<PointerType>(ToTy);

  if (FromPtr && ToPtr) {
    // Recursively check qualification conversion at each pointer level
    return isQualificationConversionRecursive(
        QualType(FromPtr->getPointeeType(), From.getQualifiers()),
        QualType(ToPtr->getPointeeType(), To.getQualifiers()));
  }

  // Non-pointer types: qualification conversion only if same type
  // with added qualifiers
  if (isSameUnqualifiedType(From, To)) {
    auto FromQuals = From.getQualifiers();
    auto ToQuals = To.getQualifiers();
    unsigned cvrMask = static_cast<unsigned>(Qualifier::Const) |
                       static_cast<unsigned>(Qualifier::Volatile) |
                       static_cast<unsigned>(Qualifier::Restrict);
    unsigned fromCV = static_cast<unsigned>(FromQuals) & cvrMask;
    unsigned toCV = static_cast<unsigned>(ToQuals) & cvrMask;
    // To must add at least one qualifier
    return ((toCV & ~fromCV) & cvrMask) != 0;
  }

  return false;
}

/// Recursive helper for multi-level qualification conversion.
/// Per C++ [conv.qual]: checks qualification compatibility at each
/// pointer level, with the rule that inner levels can add qualifiers
/// only if all outer levels have const.
bool ConversionChecker::isQualificationConversionRecursive(
    QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  // Both are pointers: recurse
  auto *FromPtr = llvm::dyn_cast<PointerType>(FromTy);
  auto *ToPtr = llvm::dyn_cast<PointerType>(ToTy);

  if (FromPtr && ToPtr) {
    // Check qualifiers at this level
    auto FromQuals = From.getQualifiers();
    auto ToQuals = To.getQualifiers();
    unsigned cvrMask = static_cast<unsigned>(Qualifier::Const) |
                       static_cast<unsigned>(Qualifier::Volatile);
    unsigned fromCV = static_cast<unsigned>(FromQuals) & cvrMask;
    unsigned toCV = static_cast<unsigned>(ToQuals) & cvrMask;

    // To must have at least the same qualifiers as From at this level
    if ((toCV & fromCV) != fromCV)
      return false;

    // Recurse into pointee types
    return isQualificationConversionRecursive(
        QualType(FromPtr->getPointeeType(), Qualifier::None),
        QualType(ToPtr->getPointeeType(), Qualifier::None));
  }

  // Neither is a pointer: check if same type with To adding qualifiers
  if (!FromPtr && !ToPtr) {
    if (!isSameUnqualifiedType(From, To))
      return false;

    auto FromQuals = From.getQualifiers();
    auto ToQuals = To.getQualifiers();
    unsigned cvrMask = static_cast<unsigned>(Qualifier::Const) |
                       static_cast<unsigned>(Qualifier::Volatile);
    unsigned fromCV = static_cast<unsigned>(FromQuals) & cvrMask;
    unsigned toCV = static_cast<unsigned>(ToQuals) & cvrMask;

    // To must have at least the same qualifiers as From
    return (toCV & fromCV) == fromCV;
  }

  // One is pointer, other is not: not a qualification conversion
  return false;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isDerivedToBaseRefConversion
//===----------------------------------------------------------------------===//

bool ConversionChecker::isDerivedToBaseRefConversion(QualType From,
                                                       QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  const auto *FromRef = llvm::dyn_cast<ReferenceType>(getUnqualified(From));
  const auto *ToRef = llvm::dyn_cast<ReferenceType>(getUnqualified(To));

  if (!FromRef || !ToRef)
    return false;

  QualType FromReferenced(FromRef->getReferencedType(), Qualifier::None);
  QualType ToReferenced(ToRef->getReferencedType(), Qualifier::None);

  const auto *FromRecord = llvm::dyn_cast<RecordType>(getUnqualified(FromReferenced));
  const auto *ToRecord = llvm::dyn_cast<RecordType>(getUnqualified(ToReferenced));

  if (!FromRecord || !ToRecord)
    return false;

  auto *FromCXX = llvm::dyn_cast<CXXRecordDecl>(FromRecord->getDecl());
  auto *ToCXX = llvm::dyn_cast<CXXRecordDecl>(ToRecord->getDecl());

  if (!FromCXX || !ToCXX)
    return false;

  return FromCXX->isDerivedFrom(ToCXX);
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isArrayToPointerDecay
//===----------------------------------------------------------------------===//

bool ConversionChecker::isArrayToPointerDecay(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  if (!FromTy->isArrayType() || !ToTy->isPointerType())
    return false;

  const auto *FromArray = llvm::cast<ArrayType>(FromTy);
  const auto *ToPtr = llvm::cast<PointerType>(ToTy);

  // The element type must match the pointee type (possibly with
  // qualification adjustment)
  QualType ElementType(FromArray->getElementType(), Qualifier::None);
  QualType PointeeType(ToPtr->getPointeeType(), Qualifier::None);

  return isSameUnqualifiedType(ElementType, PointeeType);
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isFunctionToPointerDecay
//===----------------------------------------------------------------------===//

bool ConversionChecker::isFunctionToPointerDecay(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  if (!FromTy->isFunctionType() || !ToTy->isPointerType())
    return false;

  const auto *ToPtr = llvm::cast<PointerType>(ToTy);
  return isSameUnqualifiedType(QualType(FromTy, Qualifier::None),
                                QualType(ToPtr->getPointeeType(), Qualifier::None));
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isDerivedToBaseConversion
//===----------------------------------------------------------------------===//

bool ConversionChecker::isDerivedToBaseConversion(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  // Pointer to derived → pointer to base
  const auto *FromPtr = llvm::dyn_cast<PointerType>(getUnqualified(From));
  const auto *ToPtr = llvm::dyn_cast<PointerType>(getUnqualified(To));

  if (FromPtr && ToPtr) {
    const auto *FromRecord =
        llvm::dyn_cast<RecordType>(FromPtr->getPointeeType());
    const auto *ToRecord =
        llvm::dyn_cast<RecordType>(ToPtr->getPointeeType());

    if (FromRecord && ToRecord) {
      auto *FromCXX = llvm::dyn_cast<CXXRecordDecl>(FromRecord->getDecl());
      auto *ToCXX = llvm::dyn_cast<CXXRecordDecl>(ToRecord->getDecl());

      if (FromCXX && ToCXX) {
        return FromCXX->isDerivedFrom(ToCXX);
      }
    }
    return false;
  }

  // Reference to derived → reference to base
  // e.g., Derived& → Base& or const Base&
  const auto *FromRef = llvm::dyn_cast<ReferenceType>(getUnqualified(From));
  const auto *ToRef = llvm::dyn_cast<ReferenceType>(getUnqualified(To));

  if (FromRef && ToRef) {
    QualType FromReferenced(FromRef->getReferencedType(), Qualifier::None);
    QualType ToReferenced(ToRef->getReferencedType(), Qualifier::None);

    // Both must reference record types
    const auto *FromRecord = llvm::dyn_cast<RecordType>(getUnqualified(FromReferenced));
    const auto *ToRecord = llvm::dyn_cast<RecordType>(getUnqualified(ToReferenced));

    if (FromRecord && ToRecord) {
      auto *FromCXX = llvm::dyn_cast<CXXRecordDecl>(FromRecord->getDecl());
      auto *ToCXX = llvm::dyn_cast<CXXRecordDecl>(ToRecord->getDecl());

      if (FromCXX && ToCXX) {
        return FromCXX->isDerivedFrom(ToCXX);
      }
    }
    return false;
  }

  // Direct class type: Derived → Base (for copy initialization)
  // This occurs when catching exceptions: catch(Base) with throw(Derived)
  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  if (FromTy->isRecordType() && ToTy->isRecordType()) {
    auto *FromRecord = llvm::cast<RecordType>(FromTy);
    auto *ToRecord = llvm::cast<RecordType>(ToTy);

    auto *FromCXX = llvm::dyn_cast<CXXRecordDecl>(FromRecord->getDecl());
    auto *ToCXX = llvm::dyn_cast<CXXRecordDecl>(ToRecord->getDecl());

    if (FromCXX && ToCXX) {
      return FromCXX->isDerivedFrom(ToCXX);
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — isStandardConversion
//===----------------------------------------------------------------------===//

bool ConversionChecker::isStandardConversion(QualType From, QualType To) {
  if (From.isNull() || To.isNull())
    return false;

  // Identity: same type (exact match)
  if (isSameUnqualifiedType(From, To)) {
    // Even if qualifiers differ, it could be a qualification adjustment
    // which is still a standard conversion (ExactMatch)
    return true;
  }

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  // Integer conversions: any integer to any integer (including bool)
  if (FromTy->isIntegerType() && ToTy->isIntegerType()) {
    // Already handled as promotion? Still a conversion if not promotion.
    return true;
  }

  // Floating-point conversions: any float to any float
  if (FromTy->isFloatingType() && ToTy->isFloatingType()) {
    return true;
  }

  // Integer ↔ Floating conversions
  if ((FromTy->isIntegerType() && ToTy->isFloatingType()) ||
      (FromTy->isFloatingType() && ToTy->isIntegerType())) {
    return true;
  }

  // Boolean conversion: any arithmetic/pointer/enum/nullptr_t → bool
  if (ToTy->isBooleanType()) {
    if (FromTy->isIntegerType() || FromTy->isFloatingType() ||
        FromTy->isPointerType()) {
      return true;
    }
    // nullptr_t → bool (C++ [conv.bool])
    if (FromTy->isBuiltinType()) {
      auto *FromBT = llvm::cast<BuiltinType>(FromTy);
      if (FromBT->getKind() == BuiltinKind::NullPtr)
        return true;
    }
  }

  // Null pointer conversion: nullptr_t → any pointer type
  if (FromTy->isBuiltinType()) {
    auto *FromBT = llvm::cast<BuiltinType>(FromTy);
    if (FromBT->getKind() == BuiltinKind::NullPtr && ToTy->isPointerType()) {
      return true;
    }
  }

  // Pointer conversions
  if (FromTy->isPointerType() && ToTy->isPointerType()) {
    // Qualification conversion
    if (isQualificationConversion(From, To))
      return true;

    // Derived-to-base pointer conversion
    if (isDerivedToBaseConversion(From, To))
      return true;

    // void* conversions: any pointer → void*
    const auto *ToPtr = llvm::cast<PointerType>(ToTy);
    if (ToPtr->getPointeeType()->isVoidType()) {
      return true;
    }
  }

  // Reference conversions
  if (FromTy->isReferenceType() && ToTy->isReferenceType()) {
    // Derived-to-base reference conversion
    if (isDerivedToBaseRefConversion(From, To))
      return true;

    // Qualification adjustment on references
    // e.g., int& → const int&
    auto *FromRef = llvm::cast<ReferenceType>(FromTy);
    auto *ToRef = llvm::cast<ReferenceType>(ToTy);
    QualType FromRefd(FromRef->getReferencedType(), From.getQualifiers());
    QualType ToRefd(ToRef->getReferencedType(), To.getQualifiers());
    if (isSameUnqualifiedType(FromRefd, ToRefd)) {
      // Same type, possibly with added qualifiers on To
      return true;
    }
  }

  // Lvalue-to-rvalue: reference to object → value
  // (This is implicit in how we handle argument types — handled in
  // GetStandardConversion below)

  // Array-to-pointer decay
  if (FromTy->isArrayType() && ToTy->isPointerType()) {
    const auto *ToArray = llvm::cast<ArrayType>(FromTy);
    const auto *ToPtr = llvm::cast<PointerType>(ToTy);
    if (isSameUnqualifiedType(QualType(ToArray->getElementType(), Qualifier::None),
                               QualType(ToPtr->getPointeeType(), Qualifier::None))) {
      return true;
    }
  }

  // Function-to-pointer decay
  if (FromTy->isFunctionType() && ToTy->isPointerType()) {
    const auto *ToPtr = llvm::cast<PointerType>(ToTy);
    if (ToPtr->getPointeeType() == FromTy) {
      return true;
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — GetStandardConversion
//===----------------------------------------------------------------------===//

StandardConversionSequence
ConversionChecker::GetStandardConversion(QualType From, QualType To) {
  StandardConversionSequence SCS;

  if (From.isNull() || To.isNull()) {
    SCS.setRank(ConversionRank::BadConversion);
    return SCS;
  }

  // 1. Identity / exact match (same unqualified type, possibly different
  // qualifiers)
  if (isSameUnqualifiedType(From, To)) {
    // Check for qualification adjustment
    if (From.getQualifiers() != To.getQualifiers()) {
      SCS.setFirst(StandardConversionKind::QualificationAdjustment);
    }
    // else: pure identity — all defaults are correct
    SCS.setRank(ConversionRank::ExactMatch);
    return SCS;
  }

  // 2. Check for integral promotion
  if (isIntegralPromotion(From, To)) {
    SCS.setFirst(StandardConversionKind::Identity);
    SCS.setThird(StandardConversionKind::Promotion);
    SCS.setRank(ConversionRank::Promotion);
    return SCS;
  }

  // 3. Check for floating-point promotion
  if (isFloatingPointPromotion(From, To)) {
    SCS.setFirst(StandardConversionKind::Identity);
    SCS.setThird(StandardConversionKind::Promotion);
    SCS.setRank(ConversionRank::Promotion);
    return SCS;
  }

  // 4. Check for qualification conversion (pointer qualifier addition)
  if (isQualificationConversion(From, To)) {
    SCS.setThird(StandardConversionKind::QualificationAdjustment);
    SCS.setRank(ConversionRank::ExactMatch);
    return SCS;
  }

  // 5. Check for derived-to-base pointer conversion
  if (isDerivedToBaseConversion(From, To)) {
    SCS.setThird(StandardConversionKind::Conversion);
    SCS.setRank(ConversionRank::Conversion);
    return SCS;
  }

  // 5b. Check for derived-to-base reference conversion
  // e.g., Derived& → Base& or Derived& → const Base&
  if (isDerivedToBaseRefConversion(From, To)) {
    SCS.setThird(StandardConversionKind::Conversion);
    SCS.setRank(ConversionRank::Conversion);
    return SCS;
  }

  // 6. Check for array-to-pointer decay
  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);
  if (FromTy->isArrayType() && ToTy->isPointerType()) {
    const auto *ToArray = llvm::cast<ArrayType>(FromTy);
    const auto *ToPtr = llvm::cast<PointerType>(ToTy);
    if (isSameUnqualifiedType(QualType(ToArray->getElementType(), Qualifier::None),
                               QualType(ToPtr->getPointeeType(), Qualifier::None))) {
      SCS.setFirst(StandardConversionKind::LvalueTransformation);
      SCS.setRank(ConversionRank::ExactMatch);
      return SCS;
    }
  }

  // 7. Function-to-pointer decay
  if (FromTy->isFunctionType() && ToTy->isPointerType()) {
    const auto *ToPtr = llvm::cast<PointerType>(ToTy);
    if (ToPtr->getPointeeType() == FromTy) {
      SCS.setFirst(StandardConversionKind::LvalueTransformation);
      SCS.setRank(ConversionRank::ExactMatch);
      return SCS;
    }
  }

  // 8. Null pointer conversion: nullptr_t → pointer
  //    Also: nullptr_t → bool (boolean conversion from nullptr_t, value is false)
  if (FromTy->isBuiltinType()) {
    auto *FromBT = llvm::cast<BuiltinType>(FromTy);
    if (FromBT->getKind() == BuiltinKind::NullPtr) {
      if (ToTy->isPointerType()) {
        SCS.setThird(StandardConversionKind::Conversion);
        SCS.setRank(ConversionRank::Conversion);
        return SCS;
      }
      // nullptr_t → bool: a null pointer constant converts to false
      // Per C++ [conv.bool]: a prvalue of type std::nullptr_t can be
      // converted to a prvalue of type bool; the result is false.
      if (ToTy->isBooleanType()) {
        SCS.setThird(StandardConversionKind::Conversion);
        SCS.setRank(ConversionRank::Conversion);
        return SCS;
      }
    }
  }

  // 9. Integer → integer (standard conversion, not promotion)
  if (FromTy->isIntegerType() && ToTy->isIntegerType()) {
    SCS.setThird(StandardConversionKind::Conversion);
    SCS.setRank(ConversionRank::Conversion);
    return SCS;
  }

  // 10. Floating → floating (standard conversion, not promotion)
  if (FromTy->isFloatingType() && ToTy->isFloatingType()) {
    SCS.setThird(StandardConversionKind::Conversion);
    SCS.setRank(ConversionRank::Conversion);
    return SCS;
  }

  // 11. Integer ↔ Floating
  if ((FromTy->isIntegerType() && ToTy->isFloatingType()) ||
      (FromTy->isFloatingType() && ToTy->isIntegerType())) {
    SCS.setThird(StandardConversionKind::Conversion);
    SCS.setRank(ConversionRank::Conversion);
    return SCS;
  }

  // 12. Boolean conversion: arithmetic/pointer → bool
  if (ToTy->isBooleanType()) {
    if (FromTy->isIntegerType() || FromTy->isFloatingType() ||
        FromTy->isPointerType()) {
      SCS.setThird(StandardConversionKind::Conversion);
      SCS.setRank(ConversionRank::Conversion);
      return SCS;
    }
  }

  // 13. Any pointer → void*
  if (FromTy->isPointerType() && ToTy->isPointerType()) {
    const auto *ToPtr = llvm::cast<PointerType>(ToTy);
    if (ToPtr->getPointeeType()->isVoidType()) {
      SCS.setThird(StandardConversionKind::Conversion);
      SCS.setRank(ConversionRank::Conversion);
      return SCS;
    }
  }

  // No valid standard conversion found
  SCS.setRank(ConversionRank::BadConversion);
  return SCS;
}

//===----------------------------------------------------------------------===//
// ConversionChecker — GetConversion
//===----------------------------------------------------------------------===//

ImplicitConversionSequence ConversionChecker::GetConversion(QualType From,
                                                            QualType To) {
  // Try standard conversion first
  StandardConversionSequence SCS = GetStandardConversion(From, To);

  if (!SCS.isBad()) {
    return ImplicitConversionSequence::getStandard(SCS);
  }

  //--- User-defined conversions (C++ [over.match.user]) ---
  // Per C++ [class.conv]: A user-defined conversion consists of:
  //   - An initial standard conversion sequence
  //   - Followed by a user-defined conversion (constructor or conversion op)
  //   - Followed by a second standard conversion sequence
  //
  // Only attempt if:
  //   - Both types are non-null
  //   - The target is a class type (for converting constructors), or
  //     the source is a class type (for conversion operators)

  if (From.isNull() || To.isNull())
    return ImplicitConversionSequence::getBad();

  const Type *FromTy = getUnqualified(From);
  const Type *ToTy = getUnqualified(To);

  //--- Strategy 1: Converting constructor ---
  // If To is a class type, look for a non-explicit constructor that can
  // accept From (or something From converts to) as a single argument.
  if (ToTy) {
    if (auto *ToRT = llvm::dyn_cast<RecordType>(ToTy)) {
      auto *ToDecl = ToRT->getDecl();
      if (!ToDecl) return ImplicitConversionSequence::getBad();  // Incomplete type
      if (auto *ToRD = llvm::dyn_cast<CXXRecordDecl>(ToDecl)) {
        auto ICS = TryConvertingConstructor(From, ToRD);
        if (!ICS.isBad())
          return ICS;
      }
    }
  }

  //--- Strategy 2: Conversion operator ---
  // If From is a class type, look for a conversion operator in that class
  // whose return type can convert to To via a standard conversion.
  if (FromTy) {
    if (auto *FromRT = llvm::dyn_cast<RecordType>(FromTy)) {
      auto *FromDecl = FromRT->getDecl();
      if (!FromDecl) return ImplicitConversionSequence::getBad();  // Incomplete type
      if (auto *FromRD = llvm::dyn_cast<CXXRecordDecl>(FromDecl)) {
        auto ICS = TryConversionOperator(FromRD, To);
        if (!ICS.isBad())
          return ICS;
      }
    }
  }

  // No valid conversion
  return ImplicitConversionSequence::getBad();
}

//===----------------------------------------------------------------------===//
// User-defined conversion helpers
//===----------------------------------------------------------------------===//

ImplicitConversionSequence
ConversionChecker::TryConvertingConstructor(QualType From,
                                             CXXRecordDecl *ToClass) {
  if (!ToClass)
    return ImplicitConversionSequence::getBad();

  // Walk all members to find constructors
  for (auto *D : ToClass->members()) {
    auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(D);
    if (!Ctor)
      continue;

    // Skip explicit constructors — they don't participate in implicit conversion
    if (Ctor->isExplicit())
      continue;

    // Skip deleted constructors
    if (Ctor->isDeleted())
      continue;

    unsigned NumParams = Ctor->getNumParams();

    // We need a constructor that can be called with a single argument.
    // This means: exactly 1 required parameter, or 0 required + 1 with default.
    // Also: copy/move constructors (param type = same class) are not considered
    //        user-defined conversions per C++ [class.conv].
    if (NumParams == 0)
      continue;

    // Count required (non-default) parameters
    unsigned RequiredParams = 0;
    for (unsigned I = 0; I < NumParams; ++I) {
      if (!Ctor->getParamDecl(I)->getDefaultArg())
        RequiredParams = I + 1;
    }

    // We need exactly 1 required parameter for a single-argument call
    if (RequiredParams != 1)
      continue;

    // Check if the first parameter type can accept From via standard conversion
    QualType ParamType = Ctor->getParamDecl(0)->getType();

    // Skip copy/move constructors: if ParamType is the same class type
    // (reference to the same record), this is not a user-defined conversion.
    const Type *ParamTy = getUnqualified(ParamType);
    if (auto *ParamRef = llvm::dyn_cast<ReferenceType>(ParamTy)) {
      QualType ReferencedType = ParamRef->getReferencedType();
      if (auto *ParamRT = llvm::dyn_cast<RecordType>(
              getUnqualified(ReferencedType))) {
        if (ParamRT->getDecl() == ToClass)
          continue; // Copy/move constructor — skip
      }
    }

    // Try standard conversion from From to ParamType
    StandardConversionSequence InitialSCS =
        GetStandardConversion(From, ParamType);
    if (InitialSCS.isBad())
      continue;

    // Build the user-defined conversion ICS
    // The second standard conversion (after the user-defined conversion)
    // is identity since the constructor produces the target type directly.
    StandardConversionSequence SecondSCS;
    SecondSCS.setRank(ConversionRank::ExactMatch);

    return ImplicitConversionSequence::getUserDefined(Ctor, SecondSCS);
  }

  return ImplicitConversionSequence::getBad();
}

ImplicitConversionSequence
ConversionChecker::TryConversionOperator(CXXRecordDecl *FromClass,
                                          QualType To) {
  if (!FromClass || To.isNull())
    return ImplicitConversionSequence::getBad();

  // Walk all members to find conversion operators
  for (auto *D : FromClass->members()) {
    auto *ConvOp = llvm::dyn_cast<CXXConversionDecl>(D);
    if (!ConvOp)
      continue;

    // Skip explicit conversion operators (C++11)
    // Note: CXXConversionDecl doesn't have isExplicit() directly,
    // but CXXMethodDecl doesn't either. Check via CXXConstructorDecl's pattern.
    // For now, we accept all conversion operators (explicit check can be added
    // when isExplicit() is available on CXXConversionDecl).

    // Skip deleted operators
    if (ConvOp->isDeleted())
      continue;

    // Get the type that this operator converts to
    QualType ConvType = ConvOp->getConversionType();
    if (ConvType.isNull())
      continue;

    // Try standard conversion from the operator's return type to To
    StandardConversionSequence SecondSCS =
        GetStandardConversion(ConvType, To);
    if (SecondSCS.isBad())
      continue;

    // Build the user-defined conversion ICS
    // The initial standard conversion is identity (the object itself).
    StandardConversionSequence InitialSCS;
    InitialSCS.setRank(ConversionRank::ExactMatch);

    return ImplicitConversionSequence::getUserDefined(ConvOp, SecondSCS);
  }

  return ImplicitConversionSequence::getBad();
}

} // namespace blocktype

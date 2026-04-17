//===--- TemplateDeduction.cpp - Template Argument Deduction -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// TemplateDeductionInfo
//===----------------------------------------------------------------------===//

llvm::SmallVector<TemplateArgument, 4>
TemplateDeductionInfo::getDeducedArgs(unsigned NumParams) const {
  llvm::SmallVector<TemplateArgument, 4> Result;
  Result.reserve(NumParams);
  for (unsigned I = 0; I < NumParams; ++I) {
    auto It = DeducedArgs.find(I);
    if (It != DeducedArgs.end())
      Result.push_back(It->second);
    else
      Result.push_back(TemplateArgument()); // null = not deduced
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// Function Template Argument Deduction
//===----------------------------------------------------------------------===//

TemplateDeductionResult
TemplateDeduction::DeduceFunctionTemplateArguments(
    FunctionTemplateDecl *FTD, llvm::ArrayRef<Expr *> CallArgs,
    TemplateDeductionInfo &Info,
    llvm::ArrayRef<TemplateArgument> ExplicitArgs) {
  FunctionDecl *Pattern =
      llvm::dyn_cast_or_null<FunctionDecl>(FTD->getTemplatedDecl());
  if (!Pattern)
    return TemplateDeductionResult::SubstitutionFailure;

  unsigned NumParams = Pattern->getNumParams();
  bool IsVariadic = false;
  if (auto *FT = llvm::dyn_cast<FunctionType>(Pattern->getType().getTypePtr()))
    IsVariadic = FT->isVariadic();

  if (CallArgs.size() < NumParams)
    return TemplateDeductionResult::TooFewArguments;
  if (!IsVariadic && CallArgs.size() > NumParams)
    return TemplateDeductionResult::TooManyArguments;

  // Deduce from each parameter/argument pair
  Info.reset();

  // First, seed any explicitly specified template arguments
  for (unsigned I = 0; I < ExplicitArgs.size(); ++I) {
    Info.addDeducedArg(I, ExplicitArgs[I]);
  }
  for (unsigned I = 0; I < NumParams && I < CallArgs.size(); ++I) {
    ParmVarDecl *PVD = Pattern->getParamDecl(I);
    QualType ParamType = PVD->getType();
    QualType ArgType = CallArgs[I]->getType();

    // Per C++ [temp.deduct.call]: strip top-level cv-qualifiers from the
    // argument type unless the parameter is a reference type.
    // Also strip top-level reference from the argument (references are
    // non-deduced contexts from the caller side).
    if (!ParamType.isNull()) {
      const Type *PT = ParamType.getTypePtr();
      bool ParamIsRef = llvm::isa<ReferenceType>(PT);
      if (!ParamIsRef) {
        // Strip reference from ArgType if present
        if (auto *ArgRT = llvm::dyn_cast<ReferenceType>(ArgType.getTypePtr()))
          ArgType = QualType(ArgRT->getReferencedType(), Qualifier::None);
        // Strip top-level const/volatile from ArgType
        ArgType = QualType(ArgType.getTypePtr(), Qualifier::None);
      }
    }

    TemplateDeductionResult Result =
        DeduceTemplateArguments(ParamType, ArgType, Info);
    if (Result != TemplateDeductionResult::Success)
      return Result;
  }

  return TemplateDeductionResult::Success;
}

//===----------------------------------------------------------------------===//
// Type Pair Deduction
//===----------------------------------------------------------------------===//

TemplateDeductionResult
TemplateDeduction::DeduceTemplateArguments(
    llvm::ArrayRef<QualType> ParamTypes, llvm::ArrayRef<QualType> ArgTypes,
    TemplateDeductionInfo &Info) {
  if (ParamTypes.size() != ArgTypes.size()) {
    return ParamTypes.size() > ArgTypes.size()
               ? TemplateDeductionResult::TooFewArguments
               : TemplateDeductionResult::TooManyArguments;
  }

  for (unsigned I = 0; I < ParamTypes.size(); ++I) {
    TemplateDeductionResult Result =
        DeduceTemplateArguments(ParamTypes[I], ArgTypes[I], Info);
    if (Result != TemplateDeductionResult::Success)
      return Result;
  }
  return TemplateDeductionResult::Success;
}

TemplateDeductionResult
TemplateDeduction::DeduceTemplateArguments(QualType ParamType,
                                           QualType ArgType,
                                           TemplateDeductionInfo &Info) {
  // 1. Both null → success
  if (ParamType.isNull() && ArgType.isNull())
    return TemplateDeductionResult::Success;

  if (ParamType.isNull() || ArgType.isNull())
    return TemplateDeductionResult::NonDeducedMismatch;

  // 2. Identical types → nothing to deduce
  if (ParamType == ArgType)
    return TemplateDeductionResult::Success;

  // 3. Check canonical types
  if (ParamType.getCanonicalType() == ArgType.getCanonicalType())
    return TemplateDeductionResult::Success;

  const Type *ParamPtr = ParamType.getTypePtr();
  const Type *ArgPtr = ArgType.getTypePtr();

  // 4. TemplateTypeParmType → direct deduction
  if (auto *TTP = llvm::dyn_cast<TemplateTypeParmType>(ParamPtr)) {
    unsigned Index = TTP->getIndex();

    if (Info.hasDeducedArg(Index)) {
      // Check consistency with previously deduced argument
      TemplateArgument Prev = Info.getDeducedArg(Index);
      if (Prev.isType() && Prev.getAsType() == ArgType)
        return TemplateDeductionResult::Success;
      return TemplateDeductionResult::Inconsistent;
    }

    Info.addDeducedArg(Index, TemplateArgument(ArgType));
    return TemplateDeductionResult::Success;
  }

  // 5. Pointer type → recurse on pointee
  if (auto *ParamPT = llvm::dyn_cast<PointerType>(ParamPtr)) {
    if (auto *ArgPT = llvm::dyn_cast<PointerType>(ArgPtr))
      return DeduceFromPointerType(ParamPT, ArgPT, Info);
    return TemplateDeductionResult::NonDeducedMismatch;
  }

  // 6. Reference type → recurse on referenced type
  if (auto *ParamRT = llvm::dyn_cast<ReferenceType>(ParamPtr)) {
    if (auto *ArgRT = llvm::dyn_cast<ReferenceType>(ArgPtr))
      return DeduceFromReferenceType(ParamRT, ArgRT, Info);

    // T&& can deduce from non-reference (forwarding reference case)
    if (auto *ParamRVT = llvm::dyn_cast<RValueReferenceType>(ParamRT)) {
      // Check if the parameter is a template type param (forwarding ref)
      if (auto *TTP = llvm::dyn_cast<TemplateTypeParmType>(
              ParamRVT->getReferencedType())) {
        unsigned Index = TTP->getIndex();
        if (Info.hasDeducedArg(Index)) {
          TemplateArgument Prev = Info.getDeducedArg(Index);
          if (Prev.isType() && Prev.getAsType() == ArgType)
            return TemplateDeductionResult::Success;
          return TemplateDeductionResult::Inconsistent;
        }
        Info.addDeducedArg(Index, TemplateArgument(ArgType));
        return TemplateDeductionResult::Success;
      }
    }

    // T& can deduce from non-reference lvalue argument:
    // Per C++ [temp.deduct.call], if P is a reference type, A can be
    // a non-reference type (the referenced type is deduced directly).
    if (ParamRT->isLValueReference()) {
      // For T&, deduce T from the non-reference argument type
      const Type *ReferencedType = ParamRT->getReferencedType();
      if (auto *TTP = llvm::dyn_cast<TemplateTypeParmType>(ReferencedType)) {
        unsigned Index = TTP->getIndex();
        if (Info.hasDeducedArg(Index)) {
          TemplateArgument Prev = Info.getDeducedArg(Index);
          if (Prev.isType() && Prev.getAsType() == ArgType)
            return TemplateDeductionResult::Success;
          return TemplateDeductionResult::Inconsistent;
        }
        Info.addDeducedArg(Index, TemplateArgument(ArgType));
        return TemplateDeductionResult::Success;
      }
      // Non-simple T& (e.g., const T&) — recurse on referenced type
      return DeduceTemplateArguments(
          QualType(ReferencedType, Qualifier::None), ArgType, Info);
    }

    return TemplateDeductionResult::NonDeducedMismatch;
  }

  // 7. Array type → recurse
  if (auto *ParamAT = llvm::dyn_cast<ArrayType>(ParamPtr)) {
    if (auto *ArgAT = llvm::dyn_cast<ArrayType>(ArgPtr))
      return DeduceFromArrayType(ParamAT, ArgAT, Info);
    return TemplateDeductionResult::NonDeducedMismatch;
  }

  // 8. Function type → recurse
  if (auto *ParamFT = llvm::dyn_cast<FunctionType>(ParamPtr)) {
    if (auto *ArgFT = llvm::dyn_cast<FunctionType>(ArgPtr))
      return DeduceFromFunctionType(ParamFT, ArgFT, Info);
    return TemplateDeductionResult::NonDeducedMismatch;
  }

  // 9. TemplateSpecializationType → recursive deduction
  if (auto *ParamTST = llvm::dyn_cast<TemplateSpecializationType>(ParamPtr)) {
    if (auto *ArgTST = llvm::dyn_cast<TemplateSpecializationType>(ArgPtr))
      return DeduceFromTemplateSpecializationType(ParamTST, ArgTST, Info);
    return TemplateDeductionResult::NonDeducedMismatch;
  }

  // 10. Non-deduced context: types don't match and no deduction possible
  return TemplateDeductionResult::NonDeducedMismatch;
}

//===----------------------------------------------------------------------===//
// Private Deduction Helpers
//===----------------------------------------------------------------------===//

TemplateDeductionResult
TemplateDeduction::DeduceFromPointerType(
    const PointerType *Param, const PointerType *Arg,
    TemplateDeductionInfo &Info) {
  return DeduceTemplateArguments(
      QualType(Param->getPointeeType(), Qualifier::None),
      QualType(Arg->getPointeeType(), Qualifier::None),
      Info);
}

TemplateDeductionResult
TemplateDeduction::DeduceFromReferenceType(
    const ReferenceType *Param, const ReferenceType *Arg,
    TemplateDeductionInfo &Info) {
  // Reference kind must match (lvalue↔lvalue, rvalue↔rvalue)
  // except for forwarding references handled in the main deduction
  if (Param->isLValueReference() != Arg->isLValueReference())
    return TemplateDeductionResult::NonDeducedMismatch;

  return DeduceTemplateArguments(
      QualType(Param->getReferencedType(), Qualifier::None),
      QualType(Arg->getReferencedType(), Qualifier::None),
      Info);
}

// Also handle: T& parameter can match a non-reference lvalue argument.
// This is called from the main DeduceTemplateArguments when Param is a
// ReferenceType but Arg is not a ReferenceType.

TemplateDeductionResult
TemplateDeduction::DeduceFromTemplateSpecializationType(
    const TemplateSpecializationType *Param,
    const TemplateSpecializationType *Arg,
    TemplateDeductionInfo &Info) {
  // Check if the parameter's template is a template template parameter.
  // e.g., template<template<typename> class T> void f(T<int>)
  // In this case, Param has template name "T" and we need to deduce T = Arg's template.
  TemplateDecl *ParamTemplate = Param->getTemplateDecl();
  if (auto *TTPD = llvm::dyn_cast_or_null<TemplateTemplateParmDecl>(ParamTemplate)) {
    // Deduce the template template parameter to the argument's template.
    unsigned Index = TTPD->getIndex();
    TemplateDecl *ArgTemplate = Arg->getTemplateDecl();
    if (!ArgTemplate)
      return TemplateDeductionResult::NonDeducedMismatch;

    if (Info.hasDeducedArg(Index)) {
      TemplateArgument Prev = Info.getDeducedArg(Index);
      if (Prev.isTemplate() && Prev.getAsTemplate() == ArgTemplate)
        return TemplateDeductionResult::Success;
      return TemplateDeductionResult::Inconsistent;
    }

    Info.addDeducedArg(Index, TemplateArgument(ArgTemplate));

    // Still need to recursively deduce inner template arguments
    auto ParamArgs = Param->getTemplateArgs();
    auto ArgArgs = Arg->getTemplateArgs();

    if (ParamArgs.size() != ArgArgs.size())
      return TemplateDeductionResult::NonDeducedMismatch;

    for (unsigned I = 0; I < ParamArgs.size(); ++I) {
      const TemplateArgument &PA = ParamArgs[I];
      const TemplateArgument &AA = ArgArgs[I];

      if (PA.isType() && AA.isType()) {
        TemplateDeductionResult Result =
            DeduceTemplateArguments(PA.getAsType(), AA.getAsType(), Info);
        if (Result != TemplateDeductionResult::Success)
          return Result;
      } else if (PA.getKind() != AA.getKind()) {
        return TemplateDeductionResult::NonDeducedMismatch;
      }
    }

    return TemplateDeductionResult::Success;
  }

  // Template names must match
  if (Param->getTemplateName() != Arg->getTemplateName())
    return TemplateDeductionResult::NonDeducedMismatch;

  auto ParamArgs = Param->getTemplateArgs();
  auto ArgArgs = Arg->getTemplateArgs();

  if (ParamArgs.size() != ArgArgs.size())
    return TemplateDeductionResult::NonDeducedMismatch;

  // Deduce each template argument pair
  for (unsigned I = 0; I < ParamArgs.size(); ++I) {
    const TemplateArgument &PA = ParamArgs[I];
    const TemplateArgument &AA = ArgArgs[I];

    if (PA.isType() && AA.isType()) {
      TemplateDeductionResult Result =
          DeduceTemplateArguments(PA.getAsType(), AA.getAsType(), Info);
      if (Result != TemplateDeductionResult::Success)
        return Result;
    }
    // Non-type arguments: skip for now (exact match required)
    else if (PA.getKind() != AA.getKind()) {
      return TemplateDeductionResult::NonDeducedMismatch;
    }
  }

  return TemplateDeductionResult::Success;
}

TemplateDeductionResult
TemplateDeduction::DeduceFromArrayType(const ArrayType *Param,
                                       const ArrayType *Arg,
                                       TemplateDeductionInfo &Info) {
  // Deduce element types
  TemplateDeductionResult Result = DeduceTemplateArguments(
      QualType(Param->getElementType(), Qualifier::None),
      QualType(Arg->getElementType(), Qualifier::None),
      Info);
  if (Result != TemplateDeductionResult::Success)
    return Result;

  // For constant arrays, sizes should match (or deduce size if template param)
  // For now, just require same array kind
  if (Param->getTypeClass() != Arg->getTypeClass())
    return TemplateDeductionResult::NonDeducedMismatch;

  return TemplateDeductionResult::Success;
}

TemplateDeductionResult
TemplateDeduction::DeduceFromFunctionType(const FunctionType *Param,
                                          const FunctionType *Arg,
                                          TemplateDeductionInfo &Info) {
  // Deduce return type
  TemplateDeductionResult Result = DeduceTemplateArguments(
      QualType(Param->getReturnType(), Qualifier::None),
      QualType(Arg->getReturnType(), Qualifier::None),
      Info);
  if (Result != TemplateDeductionResult::Success)
    return Result;

  // Parameter count must match
  auto ParamParams = Param->getParamTypes();
  auto ArgParams = Arg->getParamTypes();
  if (ParamParams.size() != ArgParams.size())
    return TemplateDeductionResult::NonDeducedMismatch;

  // Deduce parameter types
  for (unsigned I = 0; I < ParamParams.size(); ++I) {
    Result = DeduceTemplateArguments(
        QualType(ParamParams[I], Qualifier::None),
        QualType(ArgParams[I], Qualifier::None),
        Info);
    if (Result != TemplateDeductionResult::Success)
      return Result;
  }

  return TemplateDeductionResult::Success;
}

//===----------------------------------------------------------------------===//
// Reference Collapsing
//===----------------------------------------------------------------------===//

QualType TemplateDeduction::collapseReferences(QualType Inner,
                                                bool OuterIsLValue) {
  if (Inner.isNull())
    return Inner;

  auto *InnerRef = llvm::dyn_cast<ReferenceType>(Inner.getTypePtr());
  if (!InnerRef)
    return Inner;

  // T& & → T&, T& && → T&, T&& & → T&, T&& && → T&&
  if (InnerRef->isLValueReference()) {
    // T& with anything → T&
    return Inner;
  }

  // Inner is RValueReference
  ASTContext &Ctx = SemaRef.getASTContext();
  if (OuterIsLValue) {
    // T&& & → T&
    // Create LValueReferenceType to the referenced type
    const Type *Referenced = InnerRef->getReferencedType();
    return QualType(Ctx.getLValueReferenceType(
        const_cast<Type *>(Referenced)), Qualifier::None);
  }

  // T&& && → T&&
  return Inner;
}

//===----------------------------------------------------------------------===//
// Partial Ordering
//===----------------------------------------------------------------------===//

bool TemplateDeduction::isMoreSpecialized(TemplateDecl *P1,
                                           TemplateDecl *P2) {
  if (!P1 || !P2)
    return false;

  // Same template → neither is more specialized
  if (P1 == P2)
    return false;

  auto *P1Params = P1->getTemplateParameterList();
  auto *P2Params = P2->getTemplateParameterList();
  if (!P1Params || !P2Params)
    return false;

  // Get the templated decls
  FunctionDecl *FD1 =
      llvm::dyn_cast_or_null<FunctionDecl>(P1->getTemplatedDecl());
  FunctionDecl *FD2 =
      llvm::dyn_cast_or_null<FunctionDecl>(P2->getTemplatedDecl());
  if (!FD1 || !FD2)
    return false;

  unsigned N1 = FD1->getNumParams();
  unsigned N2 = FD2->getNumParams();
  if (N1 != N2)
    return false;

  // Standard bidirectional deduction per C++ [temp.deduct.partial]:
  //
  // P1 is more specialized than P2 if:
  //   - Deduction of P2's pattern from P1's synthesized args SUCCEEDS, AND
  //   - Deduction of P1's pattern from P2's synthesized args FAILS
  //
  // Step 1: Generate unique synthetic types for P1's template parameters.
  //         Call them S1_0, S1_1, S1_2, ...
  // Step 2: Substitute P2's function parameter types with P1's params → P2_syn.
  //         Then deduce P2_syn from (S1_0, S1_1, ...) → this tests if P2
  //         accepts P1's pattern.
  // Step 3: Generate unique synthetic types for P2's template parameters.
  //         Call them S2_0, S2_1, S2_2, ...
  // Step 4: Substitute P1's function parameter types with P2's params → P1_syn.
  //         Then deduce P1_syn from (S2_0, S2_1, ...) → this tests if P1
  //         accepts P2's pattern.

  // Direction 1: Can P2 deduce from P1's synthesized args?
  unsigned UniqueID1 = 0;
  TemplateDeductionInfo Info12;
  bool P2_deduce_from_P1 = true;
  for (unsigned I = 0; I < N1; ++I) {
    QualType P1ParamType = FD1->getParamDecl(I)->getType();
    QualType P1SynthArg = transformForPartialOrdering(
        P1ParamType, P1Params->getParams(), UniqueID1);

    QualType P2ParamType = FD2->getParamDecl(I)->getType();
    TemplateDeductionResult R =
        DeduceTemplateArguments(P2ParamType, P1SynthArg, Info12);
    if (R != TemplateDeductionResult::Success) {
      P2_deduce_from_P1 = false;
      break;
    }
  }

  // Direction 2: Can P1 deduce from P2's synthesized args?
  unsigned UniqueID2 = 0;
  TemplateDeductionInfo Info21;
  bool P1_deduce_from_P2 = true;
  for (unsigned I = 0; I < N2; ++I) {
    QualType P2ParamType = FD2->getParamDecl(I)->getType();
    QualType P2SynthArg = transformForPartialOrdering(
        P2ParamType, P2Params->getParams(), UniqueID2);

    QualType P1ParamType = FD1->getParamDecl(I)->getType();
    TemplateDeductionResult R =
        DeduceTemplateArguments(P1ParamType, P2SynthArg, Info21);
    if (R != TemplateDeductionResult::Success) {
      P1_deduce_from_P2 = false;
      break;
    }
  }

  // Per C++ [temp.deduct.partial]:
  // P1 is more specialized if P2 can deduce from P1 but NOT vice versa.
  // If both succeed, neither is more specialized (ambiguous).
  // If both fail, neither is more specialized.
  return P2_deduce_from_P1 && !P1_deduce_from_P2;
}

QualType TemplateDeduction::generateDeducedType(NamedDecl *Param,
                                                  unsigned UniqueID) {
  // Generate a unique synthetic type for partial ordering.
  // Per C++ [temp.deduct.partial], we use a unique invented type for each
  // template parameter position. We create a fresh TemplateTypeParmType
  // with a unique depth/index combination.
  if (auto *TTPD = llvm::dyn_cast<TemplateTypeParmDecl>(Param)) {
    // Create a synthetic TemplateTypeParmType with a unique index.
    // We use UniqueID * 100 + original index to ensure uniqueness.
    unsigned SynthIndex = UniqueID;
    ASTContext &Ctx = SemaRef.getASTContext();
    auto *SynthType = Ctx.getTemplateTypeParmType(TTPD, SynthIndex,
                                                   /*Depth=*/999, false);
    return QualType(SynthType, Qualifier::None);
  }
  // For non-type and template template params, return null (not handled yet)
  return QualType();
}

QualType TemplateDeduction::transformForPartialOrdering(
    QualType ParamType,
    llvm::ArrayRef<NamedDecl *> Params,
    unsigned &UniqueIDCounter) {
  // Substitute template parameters in ParamType with unique synthetic types.
  // This creates the "A" type used in partial ordering deduction.
  if (ParamType.isNull())
    return ParamType;

  const Type *Ty = ParamType.getTypePtr();

  // TemplateTypeParmType → replace with synthetic
  if (auto *TTP = llvm::dyn_cast<TemplateTypeParmType>(Ty)) {
    unsigned Index = TTP->getIndex();
    if (Index < Params.size()) {
      return generateDeducedType(Params[Index], UniqueIDCounter++);
    }
    return ParamType;
  }

  // PointerType → recurse on pointee
  if (auto *PT = llvm::dyn_cast<PointerType>(Ty)) {
    QualType Inner = transformForPartialOrdering(
        QualType(PT->getPointeeType(), Qualifier::None), Params,
        UniqueIDCounter);
    if (Inner.isNull()) return QualType();
    return QualType(SemaRef.getASTContext().getPointerType(Inner.getTypePtr()),
                     ParamType.getQualifiers());
  }

  // ReferenceType → recurse on referenced
  if (auto *RT = llvm::dyn_cast<ReferenceType>(Ty)) {
    QualType Inner = transformForPartialOrdering(
        QualType(RT->getReferencedType(), Qualifier::None), Params,
        UniqueIDCounter);
    if (Inner.isNull()) return QualType();
    if (RT->isLValueReference())
      return QualType(SemaRef.getASTContext().getLValueReferenceType(
                          Inner.getTypePtr()), ParamType.getQualifiers());
    return QualType(SemaRef.getASTContext().getRValueReferenceType(
                        Inner.getTypePtr()), ParamType.getQualifiers());
  }

  // For other types, return as-is (no template params to substitute)
  return ParamType;
}

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
    TemplateDeductionInfo &Info) {
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
  for (unsigned I = 0; I < NumParams && I < CallArgs.size(); ++I) {
    ParmVarDecl *PVD = Pattern->getParamDecl(I);
    QualType ParamType = PVD->getType();
    QualType ArgType = CallArgs[I]->getType();

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

TemplateDeductionResult
TemplateDeduction::DeduceFromTemplateSpecializationType(
    const TemplateSpecializationType *Param,
    const TemplateSpecializationType *Arg,
    TemplateDeductionInfo &Info) {
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
  if (OuterIsLValue) {
    // T&& & → T&
    // Would need ASTContext to create LValueReferenceType, but for deduction
    // purposes we just return the referenced type as an lvalue ref
    return Inner; // Simplified: actual implementation needs Context
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

  // Algorithm (C++ [temp.deduct.partial]):
  // 1. For each template, transform function parameters by replacing each
  //    template parameter with a unique synthetic type.
  // 2. Attempt deduction in both directions:
  //    - P1_at_P2: use P1's pattern to deduce P2's parameters
  //    - P2_at_P1: use P2's pattern to deduce P1's parameters
  // 3. P1 is more specialized if P2_at_P1 succeeds but P1_at_P2 fails.

  unsigned N1 = FD1->getNumParams();
  unsigned N2 = FD2->getNumParams();
  if (N1 != N2)
    return false;

  // Score-based approach: compute a "specificity" score for each template.
  // A type is more specialized if it has more concrete structure:
  //   - TemplateTypeParmType: least specialized (score 0)
  //   - PointerType/ReferenceType wrapping T: +1 per level
  //   - TemplateSpecializationType: +2 per fully-specified arg
  //   - Concrete types (int, etc.): most specialized
  auto computeSpecificity = [](QualType T) -> int {
    if (T.isNull())
      return 0;

    int Score = 0;
    const Type *Ty = T.getTypePtr();

    // Recursively decompose the type
    while (Ty) {
      if (llvm::isa<TemplateTypeParmType>(Ty)) {
        // Bare template parameter: least specialized
        break;
      } else if (auto *PT = llvm::dyn_cast<PointerType>(Ty)) {
        Score += 1;
        Ty = PT->getPointeeType();
      } else if (auto *RT = llvm::dyn_cast<ReferenceType>(Ty)) {
        Score += 1;
        Ty = RT->getReferencedType();
      } else if (auto *TST = llvm::dyn_cast<TemplateSpecializationType>(Ty)) {
        // Template specialization: each concrete arg adds specificity
        Score += 2;
        // Check if args are themselves template params (less specialized)
        for (const auto &Arg : TST->getTemplateArgs()) {
          if (Arg.isType()) {
            if (llvm::isa<TemplateTypeParmType>(Arg.getAsType().getTypePtr()))
              Score -= 1; // Deduction-dependent arg is less specific
            else
              Score += 1; // Concrete arg is more specific
          }
        }
        break;
      } else if (auto *AT = llvm::dyn_cast<ArrayType>(Ty)) {
        Score += 1;
        Ty = AT->getElementType();
      } else if (auto *FT = llvm::dyn_cast<FunctionType>(Ty)) {
        Score += 1;
        // Score return type
        const Type *Ret = FT->getReturnType();
        if (Ret && !llvm::isa<TemplateTypeParmType>(Ret))
          Score += 1;
        break;
      } else {
        // BuiltinType, RecordType, EnumType, etc. → concrete type
        Score += 3;
        break;
      }
    }
    return Score;
  };

  int P1Score = 0, P2Score = 0;
  for (unsigned I = 0; I < N1; ++I) {
    P1Score += computeSpecificity(FD1->getParamDecl(I)->getType());
    P2Score += computeSpecificity(FD2->getParamDecl(I)->getType());
  }

  // P1 is more specialized than P2 if it has a higher specificity score
  return P1Score > P2Score;
}

QualType
TemplateDeduction::generateDeducedType(NamedDecl *Param) {
  // In a full implementation, this would create a unique synthetic type
  // via ASTContext for partial ordering deduction.
  return QualType(); // placeholder
}

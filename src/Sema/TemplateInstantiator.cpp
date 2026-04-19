//===--- TemplateInstantiator.cpp - Template Instantiation Engine -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/TemplateParameterList.h"  // For TemplateArgument
#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// TemplateInstantiator Implementation
//===----------------------------------------------------------------------===//

TemplateInstantiator::TemplateInstantiator(Sema &Sema)
    : SemaRef(Sema) {}

FunctionDecl *TemplateInstantiator::InstantiateFunctionTemplate(
    FunctionTemplateDecl *FuncTemplate,
    llvm::ArrayRef<TemplateArgument> TemplateArgs) {
  
  if (FuncTemplate == nullptr) {
    return nullptr;
  }
  
  // Step 1: Check if a specialization already exists (cache lookup)
  if (auto *Existing = FuncTemplate->findSpecialization(TemplateArgs)) {
    return Existing;
  }
  
  // Step 2: Enter SFINAE context for substitution
  unsigned SavedErrors = SemaRef.getDiagnostics().getNumErrors();
  SFINAEGuard SFINAEGuard(SFContext, SavedErrors, &SemaRef.getDiagnostics());
  
  // Step 3: Delegate to Sema for actual instantiation
  // The SFINAE context will catch any substitution failures
  FunctionDecl *Result = SemaRef.InstantiateFunctionTemplate(
      FuncTemplate, TemplateArgs, FuncTemplate->getLocation());
  
  // Step 4: If substitution failed during instantiation, return nullptr
  // (the candidate is removed from overload set per SFINAE rules)
  if (Result == nullptr || SemaRef.getDiagnostics().hasErrorOccurred()) {
    return nullptr;
  }
  
  return Result;
}

} // namespace blocktype

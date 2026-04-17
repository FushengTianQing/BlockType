//===--- SemaTemplate.cpp - Sema Template Handling ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Class Template
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnClassTemplateDecl(ClassTemplateDecl *CTD) {
  if (!CTD)
    return DeclResult::getInvalid();

  // 1. Validate template parameter list
  auto *Params = CTD->getTemplateParameterList();
  if (!Params || Params->empty()) {
    Diags.report(CTD->getLocation(), DiagID::err_template_not_in_scope,
                 CTD->getName());
    return DeclResult::getInvalid();
  }

  // 2. Register to symbol table as a template
  Symbols.addTemplateDecl(CTD);

  // 3. Also register as ordinary decl for general lookup
  Symbols.addDecl(CTD);

  // 4. Register to current DeclContext
  if (CurContext)
    CurContext->addDecl(CTD);

  // 5. Handle requires-clause if present (validation only for now)
  if (CTD->hasRequiresClause()) {
    // Constraint checking will be implemented in Stage 5.4
  }

  return DeclResult(CTD);
}

//===----------------------------------------------------------------------===//
// Function Template
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnFunctionTemplateDecl(FunctionTemplateDecl *FTD) {
  if (!FTD)
    return DeclResult::getInvalid();

  // 1. Validate template parameter list
  auto *Params = FTD->getTemplateParameterList();
  if (!Params || Params->empty()) {
    Diags.report(FTD->getLocation(), DiagID::err_template_not_in_scope,
                 FTD->getName());
    return DeclResult::getInvalid();
  }

  // 2. Register to symbol table
  Symbols.addTemplateDecl(FTD);
  Symbols.addDecl(FTD);

  // 3. Register to current DeclContext
  if (CurContext)
    CurContext->addDecl(FTD);

  // 4. Handle requires-clause
  if (FTD->hasRequiresClause()) {
    // Constraint checking will be implemented in Stage 5.4
  }

  return DeclResult(FTD);
}

//===----------------------------------------------------------------------===//
// Variable Template
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnVarTemplateDecl(VarTemplateDecl *VTD) {
  if (!VTD)
    return DeclResult::getInvalid();

  auto *Params = VTD->getTemplateParameterList();
  if (!Params || Params->empty()) {
    Diags.report(VTD->getLocation(), DiagID::err_template_not_in_scope,
                 VTD->getName());
    return DeclResult::getInvalid();
  }

  Symbols.addTemplateDecl(VTD);
  Symbols.addDecl(VTD);

  if (CurContext)
    CurContext->addDecl(VTD);

  return DeclResult(VTD);
}

//===----------------------------------------------------------------------===//
// Alias Template
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnTypeAliasTemplateDecl(TypeAliasTemplateDecl *TATD) {
  if (!TATD)
    return DeclResult::getInvalid();

  auto *Params = TATD->getTemplateParameterList();
  if (!Params || Params->empty()) {
    Diags.report(TATD->getLocation(), DiagID::err_template_not_in_scope,
                 TATD->getName());
    return DeclResult::getInvalid();
  }

  Symbols.addTemplateDecl(TATD);
  Symbols.addDecl(TATD);

  if (CurContext)
    CurContext->addDecl(TATD);

  return DeclResult(TATD);
}

//===----------------------------------------------------------------------===//
// Concept
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnConceptDecl(ConceptDecl *CD) {
  if (!CD)
    return DeclResult::getInvalid();

  // Register as concept
  Symbols.addConceptDecl(CD);
  Symbols.addDecl(CD);

  if (CurContext)
    CurContext->addDecl(CD);

  return DeclResult(CD);
}

//===----------------------------------------------------------------------===//
// TemplateId (e.g., vector<int>)
//===----------------------------------------------------------------------===//

TypeResult Sema::ActOnTemplateId(llvm::StringRef Name,
                                  llvm::ArrayRef<TemplateArgumentLoc> Args,
                                  SourceLocation NameLoc,
                                  SourceLocation LAngleLoc,
                                  SourceLocation RAngleLoc) {
  // 1. Look up template name
  TemplateDecl *TD = Symbols.lookupTemplate(Name);
  if (!TD) {
    // Also check ordinary symbols (template may have been added as both)
    auto Decls = Symbols.lookup(Name);
    for (auto *D : Decls) {
      if (auto *CTD = llvm::dyn_cast<ClassTemplateDecl>(D)) {
        TD = CTD;
        break;
      }
      if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
        TD = FTD;
        break;
      }
    }
  }

  if (!TD) {
    Diags.report(NameLoc, DiagID::err_template_not_in_scope, Name);
    return TypeResult::getInvalid();
  }

  // 2. Extract raw arguments
  llvm::SmallVector<TemplateArgument, 4> RawArgs;
  for (const auto &ArgLoc : Args)
    RawArgs.push_back(ArgLoc.getArgument());

  // 3. Check argument count
  auto *Params = TD->getTemplateParameterList();
  if (Params) {
    unsigned NumParams = Params->size();
    unsigned MinRequired = Params->getMinRequiredArguments();

    if (RawArgs.size() < MinRequired) {
      Diags.report(LAngleLoc, DiagID::err_template_arg_num_different,
                   std::to_string(RawArgs.size()), Name);
      return TypeResult::getInvalid();
    }

    // Allow more args only if the last param is a pack
    if (RawArgs.size() > NumParams && !Params->hasParameterPack()) {
      // Check if last parameter is a pack — if so, excess is fine
      Diags.report(RAngleLoc, DiagID::err_template_arg_num_different,
                   std::to_string(RawArgs.size()), Name);
      return TypeResult::getInvalid();
    }
  }

  // 4. Handle based on template kind
  if (auto *CTD = llvm::dyn_cast<ClassTemplateDecl>(TD)) {
    // Class template → instantiate or create TemplateSpecializationType
    auto *Spec = Instantiator->InstantiateClassTemplate(CTD, RawArgs);
    if (Spec) {
      return TypeResult(Context.getRecordType(Spec));
    }

    // Fallback: create TemplateSpecializationType
    auto *TST = Context.getTemplateSpecializationType(Name);
    for (const auto &Arg : RawArgs)
      TST->addTemplateArg(Arg);
    return TypeResult(QualType(TST, Qualifier::None));
  }

  // For function templates, create a TemplateSpecializationType
  // (actual function instantiation happens at call site via deduction)
  auto *TST = Context.getTemplateSpecializationType(Name);
  for (const auto &Arg : RawArgs)
    TST->addTemplateArg(Arg);
  return TypeResult(QualType(TST, Qualifier::None));
}

//===----------------------------------------------------------------------===//
// Explicit Specialization
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnExplicitSpecialization(SourceLocation TemplateLoc,
                                              SourceLocation LAngleLoc,
                                              SourceLocation RAngleLoc) {
  // template<> means empty template parameter list.
  // The actual processing depends on what follows (class/function/variable).
  // For now, this is a placeholder — full implementation in Stage 5.5.
  return DeclResult::getInvalid();
}

//===----------------------------------------------------------------------===//
// Explicit Instantiation
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnExplicitInstantiation(SourceLocation TemplateLoc,
                                             Decl *D) {
  if (!D)
    return DeclResult::getInvalid();

  // Trigger instantiation of the given declaration.
  // Full implementation in Stage 5.5.
  return DeclResult(D);
}

//===----------------------------------------------------------------------===//
// Function Template Deduction + Instantiation
//===----------------------------------------------------------------------===//

FunctionDecl *Sema::DeduceAndInstantiateFunctionTemplate(
    FunctionTemplateDecl *FTD, llvm::ArrayRef<Expr *> Args,
    SourceLocation CallLoc) {
  if (!FTD)
    return nullptr;

  // 1. Deduce template arguments from call arguments
  TemplateDeductionInfo Info;
  TemplateDeductionResult Result =
      Deduction->DeduceFunctionTemplateArguments(FTD, Args, Info);

  if (Result != TemplateDeductionResult::Success) {
    // Report diagnostic for the failure
    switch (Result) {
    case TemplateDeductionResult::TooFewArguments:
      Diags.report(CallLoc, DiagID::err_template_arg_num_different,
                   "too few", FTD->getName());
      break;
    case TemplateDeductionResult::TooManyArguments:
      Diags.report(CallLoc, DiagID::err_template_arg_num_different,
                   "too many", FTD->getName());
      break;
    case TemplateDeductionResult::Inconsistent:
      Diags.report(CallLoc, DiagID::err_template_arg_different_kind,
                   FTD->getName(), "inconsistent");
      break;
    default:
      Diags.report(CallLoc, DiagID::err_ovl_no_viable_function,
                   FTD->getName());
      break;
    }
    return nullptr;
  }

  // 2. Collect deduced arguments
  auto *Params = FTD->getTemplateParameterList();
  unsigned NumParams = Params ? Params->size() : 0;
  llvm::SmallVector<TemplateArgument, 4> DeducedArgs =
      Info.getDeducedArgs(NumParams);

  // 3. Instantiate the function template with deduced arguments
  FunctionDecl *InstFD =
      Instantiator->InstantiateFunctionTemplate(FTD, DeducedArgs);
  if (!InstFD) {
    Diags.report(CallLoc, DiagID::err_template_recursion);
    return nullptr;
  }

  return InstFD;
}

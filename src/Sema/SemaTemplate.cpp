//===--- SemaTemplate.cpp - Sema Template Handling ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/ConstraintSatisfaction.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Template Parameter Validation (shared by all ActOn*TemplateDecl)
//===----------------------------------------------------------------------===//

/// ValidateTemplateParameterList — Checks a template parameter list for:
///   1. Duplicate parameter names
///   2. NonTypeTemplateParmDecl type completeness
///   3. TemplateTemplateParmDecl nested parameter list validity
/// @return true if valid, false if any errors were diagnosed
static bool ValidateTemplateParameterList(Sema &S,
                                           TemplateParameterList *Params) {
  if (!Params)
    return true;

  llvm::StringSet<> SeenNames;
  bool Valid = true;

  for (unsigned I = 0; I < Params->size(); ++I) {
    NamedDecl *P = Params->getParam(I);

    // --- Check 1: Duplicate parameter names ---
    // Empty/un-named parameters are allowed (e.g., template<typename, typename>)
    llvm::StringRef Name = P->getName();
    if (!Name.empty()) {
      if (SeenNames.count(Name)) {
        S.getDiagnostics().report(P->getLocation(),
                                  DiagID::err_template_param_duplicate_name,
                                  Name);
        Valid = false;
        continue;
      }
      SeenNames.insert(Name);
    }

    // --- Check 2: NonTypeTemplateParmDecl type completeness ---
    if (auto *NTTPD = llvm::dyn_cast<NonTypeTemplateParmDecl>(P)) {
      QualType NTType = NTTPD->getType();
      if (!NTType.isNull() && !S.isCompleteType(NTType)) {
        // Allow void*, pointer types, reference types, and dependent types
        const Type *Ty = NTType.getTypePtr();
        bool AllowIncomplete = false;
        if (Ty->isPointerType() || Ty->isReferenceType())
          AllowIncomplete = true;
        if (Ty->getTypeClass() == TypeClass::TemplateTypeParm ||
            Ty->getTypeClass() == TypeClass::Dependent)
          AllowIncomplete = true;
        if (Ty->isArrayType()) {
          // Incomplete array types (T[]) are valid for non-type params
          if (Ty->getTypeClass() == TypeClass::IncompleteArray)
            AllowIncomplete = true;
        }

        if (!AllowIncomplete) {
          S.getDiagnostics().report(
              P->getLocation(),
              DiagID::err_template_param_incomplete_type,
              Name.empty() ? "<unnamed>" : Name);
          Valid = false;
        }
      }
    }

    // --- Check 3: TemplateTemplateParmDecl nested parameter validity ---
    if (auto *TTPD = llvm::dyn_cast<TemplateTemplateParmDecl>(P)) {
      TemplateParameterList *NestedParams = TTPD->getTemplateParameterList();
      if (NestedParams && !NestedParams->empty()) {
        // Recursively validate the nested parameter list
        if (!ValidateTemplateParameterList(S, NestedParams))
          Valid = false;
      } else if (!NestedParams) {
        // Template template params must have at least one parameter
        // (or a parameter pack). Null params list is invalid.
        S.getDiagnostics().report(
            TTPD->getLocation(),
            DiagID::err_template_template_param_nested_invalid,
            Name.empty() ? "<unnamed>" : Name);
        Valid = false;
      }
    }
  }

  return Valid;
}

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

  // 1b. Deep validation: duplicate names, incomplete types, nested params
  if (!ValidateTemplateParameterList(*this, Params))
    return DeclResult::getInvalid();

  // 2. Register to symbol table as a template
  Symbols.addTemplateDecl(CTD);

  // 3. Also register as ordinary decl for general lookup
  Symbols.addDecl(CTD);

  // 4. Register to current DeclContext
  if (CurContext)
    CurContext->addDecl(CTD);

  // 5. Handle requires-clause if present
  if (CTD->hasRequiresClause()) {
    // Validate the constraint expression is well-formed.
    // Full satisfaction checking happens at instantiation time.
    Expr *RC = CTD->getRequiresClause();
    if (!RC) {
      Diags.report(CTD->getLocation(), DiagID::err_concept_not_satisfied,
                   "invalid requires-clause");
    }
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

  // 1b. Deep validation: duplicate names, incomplete types, nested params
  if (!ValidateTemplateParameterList(*this, Params))
    return DeclResult::getInvalid();

  // 2. Register to symbol table
  Symbols.addTemplateDecl(FTD);
  Symbols.addDecl(FTD);

  // 3. Register to current DeclContext
  if (CurContext)
    CurContext->addDecl(FTD);

  // 4. Handle requires-clause
  if (FTD->hasRequiresClause()) {
    // Validate the constraint expression is well-formed.
    // Full satisfaction checking happens at call site via
    // DeduceAndInstantiateFunctionTemplate.
    Expr *RC = FTD->getRequiresClause();
    if (!RC) {
      Diags.report(FTD->getLocation(), DiagID::err_concept_not_satisfied,
                   "invalid requires-clause");
    }
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

  // Deep validation: duplicate names, incomplete types, nested params
  if (!ValidateTemplateParameterList(*this, Params))
    return DeclResult::getInvalid();

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

  // Deep validation: duplicate names, incomplete types, nested params
  if (!ValidateTemplateParameterList(*this, Params))
    return DeclResult::getInvalid();

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
  // This method is called when the parser encounters template<>.
  //
  // The parser handles the actual creation of ClassTemplateSpecializationDecl
  // etc. before calling this method.  Here we validate:
  //   1. The specialization's template arguments are valid
  //   2. The primary template exists and is accessible
  //
  // Since the parser has already processed template<> and created the
  // specialization node, we return a valid empty result. The actual
  // registration with the primary template happens in the follow-up
  // ActOnClassTemplateDecl / ActOnFunctionTemplateDecl / ActOnVarTemplateDecl.
  return DeclResult(nullptr);
}

/// Validate an explicit specialization after it has been created.
/// Called from ActOnClassTemplateDecl etc. when IsExplicitSpecialization is true.
static bool ValidateExplicitSpecialization(Sema &S,
                                           ClassTemplateSpecializationDecl *Spec,
                                           ClassTemplateDecl *Primary) {
  if (!Primary) {
    S.getDiagnostics().report(Spec->getLocation(),
                              DiagID::err_explicit_spec_no_primary,
                              Spec->getName());
    return false;
  }

  // Verify template argument count matches primary parameter count
  auto *PrimaryParams = Primary->getTemplateParameterList();
  if (PrimaryParams) {
    unsigned NumParams = PrimaryParams->size();
    auto SpecArgs = Spec->getTemplateArgs();
    unsigned NumSpecArgs = SpecArgs.size();

    // Allow fewer args if primary has defaults or a parameter pack
    if (NumSpecArgs > NumParams && !PrimaryParams->hasParameterPack()) {
      S.getDiagnostics().report(
          Spec->getLocation(), DiagID::err_template_arg_num_different,
          std::to_string(NumSpecArgs), Primary->getName());
      return false;
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Explicit Instantiation
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnExplicitInstantiation(SourceLocation TemplateLoc,
                                             Decl *D) {
  if (!D)
    return DeclResult::getInvalid();

  // Explicit instantiation: template class X<int>;
  // Trigger instantiation of the given declaration.

  // Class template explicit instantiation
  if (auto *CTD = llvm::dyn_cast<ClassTemplateDecl>(D)) {
    // If the class template has already been specialized with concrete
    // arguments, the parser would have provided a ClassTemplateSpecializationDecl
    // instead. For now, return the template itself.
    return DeclResult(CTD);
  }

  // Class template specialization explicit instantiation
  // e.g., template class Vector<int>;
  // Parser provides a ClassTemplateSpecializationDecl with template args.
  if (auto *Spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    ClassTemplateDecl *CTD = Spec->getSpecializedTemplate();
    if (!CTD) {
      Diags.report(TemplateLoc, DiagID::err_explicit_spec_no_primary,
                   Spec->getName());
      return DeclResult::getInvalid();
    }

    // Trigger actual instantiation
    auto *InstSpec = Instantiator->InstantiateClassTemplate(
        CTD, Spec->getTemplateArgs());
    if (!InstSpec) {
      Diags.report(TemplateLoc, DiagID::err_template_recursion);
      return DeclResult::getInvalid();
    }

    // Register the instantiation result
    if (CurContext)
      CurContext->addDecl(InstSpec);
    return DeclResult(InstSpec);
  }

  // Function template explicit instantiation
  if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
    // For function templates without explicit args, we cannot instantiate
    // here (no deduction context). Return as-is for now.
    return DeclResult(FTD);
  }

  // Function template specialization with explicit args
  if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
    // The parser provided a concrete FunctionDecl with substituted types.
    // Register it as an explicit instantiation.
    if (CurContext)
      CurContext->addDecl(FD);
    return DeclResult(FD);
  }

  return DeclResult(D);
}

//===----------------------------------------------------------------------===//
// Partial Specialization
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnClassTemplatePartialSpecialization(
    ClassTemplatePartialSpecializationDecl *PartialSpec) {
  if (!PartialSpec)
    return DeclResult::getInvalid();

  // 1. Look up the primary template
  ClassTemplateDecl *Primary = PartialSpec->getSpecializedTemplate();
  if (!Primary) {
    Diags.report(PartialSpec->getLocation(),
                 DiagID::err_template_not_in_scope,
                 PartialSpec->getName());
    return DeclResult::getInvalid();
  }

  // 2. Validate that the partial specialization has its own template
  //    parameter list (a non-empty subset of the primary's parameters)
  auto *PartialParams = PartialSpec->getTemplateParameterList();
  if (!PartialParams || PartialParams->empty()) {
    Diags.report(PartialSpec->getLocation(),
                 DiagID::err_template_not_in_scope,
                 PartialSpec->getName());
    return DeclResult::getInvalid();
  }

  // 2b. Deep validation of partial specialization parameters
  if (!ValidateTemplateParameterList(*this, PartialParams))
    return DeclResult::getInvalid();

  // 3. Verify the partial specialization args are more specialized
  //    than the primary template. Per C++ [temp.class.spec]:
  //    The arguments of the partial specialization must be more
  //    specialized than those of the primary template.
  //    For now, we accept all valid partial specializations.

  // 4. Validate template argument count matches primary template
  auto *PrimaryParams = Primary->getTemplateParameterList();
  if (PrimaryParams) {
    unsigned PrimaryArgCount = PrimaryParams->size();
    unsigned PartialArgCount = PartialSpec->getNumTemplateArgs();
    // The partial specialization args must match primary param count
    // (unless the primary has a parameter pack)
    if (PartialArgCount != PrimaryArgCount &&
        !PrimaryParams->hasParameterPack()) {
      Diags.report(PartialSpec->getLocation(),
                   DiagID::err_template_arg_num_different,
                   std::to_string(PartialArgCount), Primary->getName());
      return DeclResult::getInvalid();
    }
  }

  // 5. Register the partial specialization with the primary template
  Primary->addSpecialization(PartialSpec);

  // 6. Register to current DeclContext
  if (CurContext)
    CurContext->addDecl(PartialSpec);

  return DeclResult(PartialSpec);
}

DeclResult Sema::ActOnVarTemplatePartialSpecialization(
    VarTemplatePartialSpecializationDecl *PartialSpec) {
  if (!PartialSpec)
    return DeclResult::getInvalid();

  VarTemplateDecl *Primary = PartialSpec->getSpecializedTemplate();
  if (!Primary) {
    Diags.report(PartialSpec->getLocation(),
                 DiagID::err_template_not_in_scope,
                 PartialSpec->getName());
    return DeclResult::getInvalid();
  }

  auto *PartialParams = PartialSpec->getTemplateParameterList();
  if (!PartialParams || PartialParams->empty()) {
    Diags.report(PartialSpec->getLocation(),
                 DiagID::err_template_not_in_scope,
                 PartialSpec->getName());
    return DeclResult::getInvalid();
  }

  // Deep validation of partial specialization parameters
  if (!ValidateTemplateParameterList(*this, PartialParams))
    return DeclResult::getInvalid();

  // Register with primary template
  Primary->addSpecialization(PartialSpec);

  if (CurContext)
    CurContext->addDecl(PartialSpec);

  return DeclResult(PartialSpec);
}

//===----------------------------------------------------------------------===//
// Partial Specialization Selection
//===----------------------------------------------------------------------===//

ClassTemplatePartialSpecializationDecl *
Sema::FindBestMatchingPartialSpecialization(
    ClassTemplateDecl *Primary, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Primary)
    return nullptr;

  auto PartialSpecs = Primary->getPartialSpecializations();
  if (PartialSpecs.empty())
    return nullptr;

  // Collect all partial specializations whose template args match the
  // provided arguments. Per C++ [temp.class.spec.match]:
  //   A partial specialization matches if its template argument list
  //   can be deduced from the provided arguments.
  llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Matches;

  for (auto *PS : PartialSpecs) {
    auto PSArgs = PS->getTemplateArgs();

    // Basic size check: partial spec args must match primary param count
    if (PSArgs.size() != Args.size() && PSArgs.size() != 0)
      continue;

    // Check if this partial specialization's arguments match the given args.
    // For a simple implementation, we check if the args are compatible types.
    bool Compatible = true;
    if (PSArgs.size() == Args.size()) {
      for (unsigned I = 0; I < Args.size(); ++I) {
        if (!PSArgs[I].isType() || !Args[I].isType())
          continue;
        // If the partial spec arg is a concrete type (not dependent),
        // it must match exactly. If it's dependent (contains template params),
        // it can match via deduction.
        QualType PSArgType = PSArgs[I].getAsType();
        QualType ArgType = Args[I].getAsType();

        // If partial spec arg is dependent, it matches any type at this position
        if (PSArgType->isDependentType())
          continue;

        // Non-dependent args must match exactly
        if (PSArgType.getCanonicalType() != ArgType.getCanonicalType()) {
          Compatible = false;
          break;
        }
      }
    }
    if (Compatible)
      Matches.push_back(PS);
  }

  if (Matches.empty())
    return nullptr;
  if (Matches.size() == 1)
    return Matches[0];

  // Multiple matches: use partial ordering to find the most specialized.
  // Per C++ [temp.class.spec.match]: the most specialized partial
  // specialization is selected.
  ClassTemplatePartialSpecializationDecl *Best = Matches[0];
  bool Ambiguous = false;

  for (unsigned I = 1; I < Matches.size(); ++I) {
    bool BestIsMore = Deduction->isMoreSpecializedPartialSpec(Best, Matches[I]);
    bool CurIsMore = Deduction->isMoreSpecializedPartialSpec(Matches[I], Best);

    if (CurIsMore && !BestIsMore) {
      Best = Matches[I];
      Ambiguous = false;
    } else if (BestIsMore && !CurIsMore) {
      // Best remains
      Ambiguous = false;
    } else {
      // Neither is more specialized → ambiguous
      Ambiguous = true;
    }
  }

  if (Ambiguous)
    return nullptr; // Caller should diagnose ambiguity

  return Best;
}

//===----------------------------------------------------------------------===//
// Function Template Deduction + Instantiation
//===----------------------------------------------------------------------===//

FunctionDecl *Sema::DeduceAndInstantiateFunctionTemplate(
    FunctionTemplateDecl *FTD, llvm::ArrayRef<Expr *> Args,
    SourceLocation CallLoc) {
  if (!FTD)
    return nullptr;

  // 1. Deduce template arguments from call arguments.
  // Enter SFINAE context during deduction: per C++ [temp.deduct],
  // deduction failures in the immediate context of substitution are
  // not hard errors — the candidate is simply removed from the overload set.
  TemplateDeductionInfo Info;
  TemplateDeductionResult Result;
  unsigned SavedErrors = Diags.getNumErrors();
  {
    SFINAEGuard DeductionSFINAEGuard(Instantiator->getSFINAEContext(),
                                SavedErrors, &Diags);
    Result = Deduction->DeduceFunctionTemplateArguments(FTD, Args, Info);
  }
  // SFINAE context exited — diagnostics are no longer suppressed.

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

  // 3. Check requires-clause constraint (if any)
  // Per C++ [temp.constr.decl]: the constraint must be satisfied after
  // template argument substitution. If not satisfied, this candidate is
  // removed from the overload set (similar to SFINAE).
  if (FTD->hasRequiresClause()) {
    Expr *RequiresClause = FTD->getRequiresClause();
    TemplateArgumentList ArgList(DeducedArgs);
    bool Satisfied =
        ConstraintChecker->CheckConstraintSatisfaction(RequiresClause, ArgList);
    if (!Satisfied) {
      Diags.report(CallLoc, DiagID::err_concept_not_satisfied,
                   FTD->getName());
      return nullptr;
    }
  }

  // 4. Instantiate the function template with deduced arguments
  FunctionDecl *InstFD =
      Instantiator->InstantiateFunctionTemplate(FTD, DeducedArgs);
  if (!InstFD) {
    Diags.report(CallLoc, DiagID::err_template_recursion);
    return nullptr;
  }

  return InstFD;
}

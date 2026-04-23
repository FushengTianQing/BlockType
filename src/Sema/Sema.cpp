//===--- Sema.cpp - Semantic Analysis Engine Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/SemaCXX.h"
#include "blocktype/Sema/SemaReflection.h"
#include "blocktype/Sema/TypeDeduction.h"
#include "blocktype/AST/Attr.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/ConstraintSatisfaction.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/AST/StmtCloner.h"  // For StmtCloner

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// deduceReturnTypeFromBody - Deduce function return type from return statements.
/// Traverses the function body to find all return statements and extracts their types.
/// Returns the first non-void return type found, or null if no return statements.
static QualType deduceReturnTypeFromBody(Stmt *Body, SourceLocation Loc) {
  if (!Body) {
    return {};
  }
  
  // Simple implementation: traverse CompoundStmt to find ReturnStmt
  auto *CS = llvm::dyn_cast<CompoundStmt>(Body);
  if (!CS) {
    return {};
  }
  
  for (auto *S : CS->getBody()) {
    if (auto *RS = llvm::dyn_cast<ReturnStmt>(S)) {
      Expr *RetVal = RS->getRetValue();
      if (RetVal) {
        QualType RetType = RetVal->getType();
        if (!RetType.isNull()) {
          return RetType;
        }
      } else {
        // return; with no value -> void
        return {};
      }
    }
  }
  
  // No return statement found
  return {};
}

//===----------------------------------------------------------------------===//
// Construction / Destruction
//===----------------------------------------------------------------------===//

Sema::Sema(ASTContext &C, DiagnosticsEngine &D)
  : Context(C), Diags(D), Symbols(C, D),
    TC(C, D), ConstEval(C),
    Instantiator(std::make_unique<TemplateInstantiator>(*this)),
    Deduction(std::make_unique<TemplateDeduction>(*this)),
    ConstraintChecker(std::make_unique<ConstraintSatisfaction>(*this)),
    TypeDeduce(std::make_unique<TypeDeduction>(C, &D)),
    ModMgr(std::make_unique<ModuleManager>(C, D)) {
  PushScope(ScopeFlags::TranslationUnitScope);
  
  // Set Sema reference for TypeDeduction (needed for template arg deduction)
  TypeDeduce->setSema(*this);
  
  // Initialize std namespace and std::get for structured bindings
  InitializeStdNamespace();
}

Sema::~Sema() {
  while (CurrentScope && CurrentScope->getParent()) {
    PopScope();
  }
}

//===----------------------------------------------------------------------===//
// Scope management
//===----------------------------------------------------------------------===//

void Sema::PushScope(ScopeFlags Flags) {
  CurrentScope = new Scope(CurrentScope, Flags);
}

void Sema::PopScope() {
  if (!CurrentScope) return;
  Scope *Parent = CurrentScope->getParent();
  delete CurrentScope;
  CurrentScope = Parent;
}

NamedDecl *Sema::LookupName(llvm::StringRef Name) const {
  // 1. Search the Scope chain (lexical scopes, up to TU)
  if (CurrentScope) {
    if (NamedDecl *D = CurrentScope->lookup(Name))
      return D;
  }
  // 2. Fall back to the global SymbolTable
  auto Decls = Symbols.lookup(Name);
  return Decls.empty() ? nullptr : Decls.front();
}

/// InstantiateClassTemplate - Instantiate a class template with given arguments.
QualType Sema::InstantiateClassTemplate(llvm::StringRef TemplateName,
                                        const TemplateSpecializationType *TST) {
  // Step 1: Look up the template declaration
  NamedDecl *LookupResult = LookupName(TemplateName);
  if (!LookupResult) {
    Diags.report(SourceLocation(), DiagID::err_undeclared_var, TemplateName);
    return QualType();
  }
  
  // Step 2: Check if it's a ClassTemplateDecl
  auto *ClassTemplate = llvm::dyn_cast<ClassTemplateDecl>(LookupResult);
  if (!ClassTemplate) {
    // Not a class template, can't instantiate
    return QualType();
  }
  
  // Step 3: Get template arguments from TST
  auto Args = TST->getTemplateArgs();
  if (Args.empty()) {
    Diags.report(SourceLocation(), DiagID::err_template_arg_num_different,
                 "no arguments", TemplateName);
    return QualType();
  }
  
  // Step 4: Check if this specialization already exists
  // TODO: Implement specialization cache
  
  // Step 5: Get the templated class declaration
  auto *TemplatedDecl = ClassTemplate->getTemplatedDecl();
  if (!TemplatedDecl) {
    Diags.report(SourceLocation(), DiagID::err_template_recursion);
    return QualType();
  }
  
  // Step 6: Create TemplateInstantiation and set up substitutions
  TemplateInstantiation Inst;
  auto Params = ClassTemplate->getTemplateParameters();
  
  if (Params.empty()) {
    Diags.report(SourceLocation(), DiagID::err_template_arg_num_different,
                 "no template parameters", TemplateName);
    return QualType();
  }
  
  // Build substitution map from template parameters to arguments
  for (unsigned i = 0; i < std::min(Args.size(), Params.size()); ++i) {
    if (auto *ParamDecl = llvm::dyn_cast_or_null<TypedefNameDecl>(Params[i])) {
      Inst.addSubstitution(ParamDecl, Args[i]);
    }
  }
  
  // Step 7: Clone the CXXRecordDecl with substituted types
  auto *OriginalRecord = llvm::dyn_cast<CXXRecordDecl>(TemplatedDecl);
  if (!OriginalRecord) {
    Diags.report(SourceLocation(), DiagID::err_expected);
    return QualType();
  }
  
  // Create a new CXXRecordDecl for the specialization
  auto *SpecializedRecord = Context.create<CXXRecordDecl>(
      OriginalRecord->getLocation(),
      OriginalRecord->getName(),
      OriginalRecord->getTagKind());
  
  // Clone fields with substituted types
  for (auto *Field : OriginalRecord->fields()) {
    QualType SubstFieldType = Inst.substituteType(Field->getType());
    
    auto *NewField = Context.create<FieldDecl>(
        Field->getLocation(),
        Field->getName(),
        SubstFieldType,
        Field->getBitWidth(),
        Field->isMutable(),
        Field->getInClassInitializer(),
        Field->getAccess());
    
    SpecializedRecord->addField(NewField);
    registerDecl(NewField);
  }
  
  // Mark the specialized record as complete
  SpecializedRecord->setCompleteDefinition();
  
  // Register the specialized record
  registerDecl(SpecializedRecord);
  
  // Return the type of the specialized record
  return Context.getTypeDeclType(SpecializedRecord);
}

// P7.4.3: Lookup a namespace by name
// Supports: "std", "std::pair", etc.
NamespaceDecl *Sema::LookupNamespace(llvm::StringRef NamespaceName) const {
  // Handle nested namespaces like "std::pair"
  // Split by "::" and lookup each component
  llvm::SmallVector<llvm::StringRef, 4> Parts;
  NamespaceName.split(Parts, "::");
  
  if (Parts.empty()) {
    return nullptr;
  }
  
  // Start from the translation unit or current context
  DeclContext *CurrentDC = nullptr;
  if (CurTU) {
    // Always start from translation unit for namespace lookup
    CurrentDC = CurTU;
  } else {
    return nullptr;
  }
  
  NamespaceDecl *LastNS = nullptr; // Track the last found namespace
  
  // Lookup each part of the namespace path
  for (llvm::StringRef Part : Parts) {
    // Try to find a NamespaceDecl with this name in the current context
    NamespaceDecl *FoundNS = nullptr;
    
    for (Decl *D : CurrentDC->decls()) {
      if (auto *NS = llvm::dyn_cast<NamespaceDecl>(D)) {
        if (NS->getName() == Part) {
          FoundNS = NS;
          break;
        }
      }
    }
    
    if (!FoundNS) {
      return nullptr; // Namespace not found
    }
    
    LastNS = FoundNS; // Save the found namespace
    // Move into the found namespace for the next lookup
    CurrentDC = FoundNS;
  }
  
  // Return the last found namespace
  return LastNS;
}

// P7.4.3: Lookup a declaration within a specific namespace
NamedDecl *Sema::LookupInNamespace(NamespaceDecl *NS, llvm::StringRef Name) const {
  if (!NS) {
    return nullptr;
  }
  
  // Search in the namespace's DeclContext
  for (Decl *D : NS->decls()) {
    if (auto *ND = llvm::dyn_cast<NamedDecl>(D)) {
      if (ND->getName() == Name) {
        return ND;
      }
    }
  }
  
  return nullptr;
}

//===----------------------------------------------------------------------===//
// DeclContext management
//===----------------------------------------------------------------------===//

void Sema::PushDeclContext(DeclContext *DC) {
  assert(DC && "pushing null declaration context");
  DeclContextStack.push_back(DC);
  CurContext = DC;
}

void Sema::PopDeclContext() {
  assert(!DeclContextStack.empty() && "popping empty context stack");
  DeclContextStack.pop_back();
  CurContext = DeclContextStack.empty() ? nullptr : DeclContextStack.back();
}

//===----------------------------------------------------------------------===//
// Translation unit
//===----------------------------------------------------------------------===//

void Sema::ActOnTranslationUnit(TranslationUnitDecl *TU) {
  CurTU = TU;
  // Push TU onto the context stack as the root context
  DeclContextStack.clear();
  DeclContextStack.push_back(TU);
  CurContext = TU;
}

TranslationUnitDecl *Sema::ActOnTranslationUnitDecl(SourceLocation Loc) {
  auto *TU = Context.create<TranslationUnitDecl>(Loc);
  ActOnTranslationUnit(TU);
  return TU;
}


//===----------------------------------------------------------------------===//
// Declaration, Expression, and Statement handling methods have been
// split into separate translation units for maintainability:
//
//   SemaDecl.cpp  — ActOn*Decl, declaration factory methods,
//                    structured binding helpers, class member factories
//   SemaExpr.cpp  — ActOn*Expr, expression factory methods, call resolution
//   SemaStmt.cpp  — ActOn*Stmt, statement factory methods, C++ stmt extensions
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Type handling [Stage 4.2]
//===----------------------------------------------------------------------===//

bool Sema::isCompleteType(QualType T) const {
  if (T.isNull()) return false;

  const Type *Ty = T.getTypePtr();

  if (Ty->isBuiltinType()) return true;
  if (Ty->isPointerType() || Ty->isReferenceType()) return true;
  if (Ty->getTypeClass() == TypeClass::MemberPointer) return true;

  if (Ty->isArrayType()) {
    if (Ty->getTypeClass() == TypeClass::IncompleteArray) return false;
    if (Ty->getTypeClass() == TypeClass::ConstantArray) {
      auto *AT = static_cast<const ConstantArrayType *>(Ty);
      return isCompleteType(QualType(AT->getElementType(), Qualifier::None));
    }
    if (Ty->getTypeClass() == TypeClass::VariableArray) {
      auto *AT = static_cast<const VariableArrayType *>(Ty);
      return isCompleteType(QualType(AT->getElementType(), Qualifier::None));
    }
    return true;
  }

  if (Ty->isFunctionType()) return true;

  if (Ty->isRecordType()) {
    auto *RT = static_cast<const RecordType *>(Ty);
    auto *Decl = RT->getDecl();
    if (!Decl) return false;  // No declaration, incomplete
    return Decl->isCompleteDefinition();
  }

  if (Ty->isEnumType()) {
    auto *ET = static_cast<const EnumType *>(Ty);
    auto *Decl = ET->getDecl();
    if (!Decl) return false;  // No declaration, incomplete
    return Decl->isCompleteDefinition();
  }

  if (Ty->getTypeClass() == TypeClass::Typedef) {
    auto *TT = static_cast<const TypedefType *>(Ty);
    return isCompleteType(TT->getDecl()->getUnderlyingType());
  }

  if (Ty->getTypeClass() == TypeClass::Elaborated) {
    auto *ET = static_cast<const ElaboratedType *>(Ty);
    return isCompleteType(QualType(ET->getNamedType(), Qualifier::None));
  }

  if (Ty->getTypeClass() == TypeClass::Decltype) {
    auto *DT = static_cast<const DecltypeType *>(Ty);
    QualType Underlying = DT->getUnderlyingType();
    if (!Underlying.isNull()) return isCompleteType(Underlying);
    return false;
  }

  if (Ty->getTypeClass() == TypeClass::Auto) {
    auto *AT = static_cast<const AutoType *>(Ty);
    if (AT->isDeduced()) return isCompleteType(AT->getDeducedType());
    return false;
  }

  // TemplateSpecializationType: treat as incomplete for now
  // TODO: Instantiate the template and check the result
  if (Ty->getTypeClass() == TypeClass::TemplateSpecialization) {
    return false;  // Template not yet instantiated
  }

  if (Ty->isVoidType()) return false;

  if (Ty->getTypeClass() == TypeClass::TemplateTypeParm ||
      Ty->getTypeClass() == TypeClass::Dependent ||
      Ty->getTypeClass() == TypeClass::Unresolved) {
    return false;
  }

  if (Ty->getTypeClass() == TypeClass::TemplateSpecialization) {
    // Check if the template specialization has been instantiated as a
    // complete type. Look for an existing specialization via the instantiator.
    // TODO: Implement class template instantiation and FindExistingSpecialization
    auto *TST = static_cast<const TemplateSpecializationType *>(Ty);
    if (auto *CTD = llvm::dyn_cast_or_null<ClassTemplateDecl>(
            TST->getTemplateDecl())) {
      // For now, assume incomplete until class template instantiation is implemented
      return false;
    }
    return false;
  }

  return true;
}

bool Sema::RequireCompleteType(QualType T, SourceLocation Loc) {
  if (!isCompleteType(T)) {
    Diags.report(Loc, DiagID::err_incomplete_type);
    return false;
  }
  return true;
}

QualType Sema::getCanonicalType(QualType T) const {
  if (T.isNull()) return T;
  return T.getCanonicalType();
}

//===----------------------------------------------------------------------===//
// Name lookup [Stage 4.3 — implemented in Lookup.cpp]
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Overload resolution [Stage 4.4]
//===----------------------------------------------------------------------===//

FunctionDecl *Sema::ResolveOverload(llvm::StringRef Name,
                                     llvm::ArrayRef<Expr *> Args,
                                     const LookupResult &Candidates) {
  SourceLocation CallLoc;
  OverloadCandidateSet OCS(CallLoc);

  OCS.addCandidates(Candidates);

  if (OCS.empty()) {
    Diags.report(SourceLocation(), DiagID::err_ovl_no_viable_function, Name);
    return nullptr;
  }

  auto [Result, Best] = OCS.resolve(Args);

  if (Result == OverloadResult::Success)
    return Best;

  if (Result == OverloadResult::Deleted) {
    // P7.4.1: Check if deleted function has a custom reason
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(Best)) {
      if (FD->hasDeletedReason()) {
        Diags.report(SourceLocation(), DiagID::err_ovl_deleted_function_with_reason,
                     Best->getName(), FD->getDeletedReason()->getValue());
      } else {
        Diags.report(SourceLocation(), DiagID::err_ovl_deleted_function,
                     Best->getName());
      }
    } else {
      Diags.report(SourceLocation(), DiagID::err_ovl_deleted_function,
                   Best->getName());
    }
    return nullptr;
  }

  if (Result == OverloadResult::NoViable) {
    Diags.report(SourceLocation(), DiagID::err_ovl_no_viable_function, Name);
    for (auto &C : OCS.getCandidates()) {
      Diags.report(SourceLocation(), DiagID::note_ovl_candidate_not_viable,
                   C.getFailureReason());
    }
    return nullptr;
  }

  // Ambiguous
  auto Viable = OCS.getViableCandidates();
  Diags.report(SourceLocation(), DiagID::err_ovl_ambiguous_call, Name);
  for (auto *C : Viable) {
    Diags.report(SourceLocation(), DiagID::note_ovl_candidate);
  }
  return nullptr;
}

void Sema::AddOverloadCandidate(FunctionDecl *F,
                                 llvm::ArrayRef<Expr *> Args,
                                 OverloadCandidateSet &Set) {
  Set.addCandidate(F);
}

//===----------------------------------------------------------------------===//
// Diagnostics helpers [Stage 4.5]
//===----------------------------------------------------------------------===//

void Sema::Diag(SourceLocation Loc, DiagID ID) {
  Diags.report(Loc, ID);
}

void Sema::Diag(SourceLocation Loc, DiagID ID, llvm::StringRef Extra) {
  Diags.report(Loc, ID, Extra);
}

//===----------------------------------------------------------------------===//
// Unused declarations and unreachable code diagnostics
//===----------------------------------------------------------------------===//

namespace {
/// Walk the Stmt tree to check for unreachable code within CompoundStmts.
void checkUnreachableInStmt(Stmt *S, DiagnosticsEngine &Diags) {
  if (!S) return;

  if (auto *CS = llvm::dyn_cast<CompoundStmt>(S)) {
    bool seenReturn = false;
    for (auto *Child : CS->getBody()) {
      if (seenReturn) {
        Diags.report(Child->getLocation(), DiagID::warn_unreachable_code);
        break; // Only report once per block
      }
      if (llvm::isa<ReturnStmt>(Child))
        seenReturn = true;
    }
    // Recurse into sub-statements
    for (auto *Child : CS->getBody())
      checkUnreachableInStmt(Child, Diags);
    return;
  }

  // Recurse into if/while/for/do/switch bodies
  if (auto *IS = llvm::dyn_cast<IfStmt>(S)) {
    checkUnreachableInStmt(IS->getThen(), Diags);
    checkUnreachableInStmt(IS->getElse(), Diags);
  } else if (auto *WS = llvm::dyn_cast<WhileStmt>(S)) {
    checkUnreachableInStmt(WS->getBody(), Diags);
  } else if (auto *FS = llvm::dyn_cast<ForStmt>(S)) {
    checkUnreachableInStmt(FS->getBody(), Diags);
  } else if (auto *DS = llvm::dyn_cast<DoStmt>(S)) {
    checkUnreachableInStmt(DS->getBody(), Diags);
  } else if (auto *SS = llvm::dyn_cast<SwitchStmt>(S)) {
    checkUnreachableInStmt(SS->getBody(), Diags);
  }
}
} // anonymous namespace

void Sema::DiagnoseUnusedDecls(TranslationUnitDecl *TU) {
  if (!TU) return;

  for (Decl *D : TU->decls()) {
    // Skip declarations without a name (anonymous, etc.)
    auto *ND = llvm::dyn_cast<NamedDecl>(D);
    if (!ND || ND->getName().empty()) continue;

    // Check unused variables (top-level or function-local)
    if (auto *VD = llvm::dyn_cast<VarDecl>(D)) {
      if (!VD->isUsed() && !VD->getType().isNull()) {
        // Skip static variables (might be used in other TUs)
        // Skip variables with initializers that have side effects (simplified: skip all for now)
        Diags.report(VD->getLocation(), DiagID::warn_unused_variable, VD->getName());
      }
    }

    // Check unused functions
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      if (!FD->isUsed()) {
        // Skip main function
        if (FD->getName() == "main") continue;
        Diags.report(FD->getLocation(), DiagID::warn_unused_function, FD->getName());
      }
      // Check unreachable code in function body
      if (FD->getBody())
        checkUnreachableInStmt(FD->getBody(), Diags);
    }
  }
}

//===----------------------------------------------------------------------===//
// P7.1.2: Decay-copy expression (P0849R8)
//===----------------------------------------------------------------------===//

ExprResult Sema::ActOnDecayCopyExpr(SourceLocation AutoLoc, Expr *SubExpr,
                                    bool IsDirectInit) {
  if (!SubExpr)
    return ExprResult(nullptr);

  // The result type is the decayed type of the subexpression:
  // - Remove references
  // - Remove top-level cv-qualifiers
  // - Array-to-pointer conversion
  // - Function-to-pointer conversion
  QualType SubTy = SubExpr->getType();
  QualType ResultTy = SubTy;

  if (!SubTy.isNull()) {
    // Remove reference
    if (auto *RefTy = llvm::dyn_cast<ReferenceType>(SubTy.getTypePtr())) {
      ResultTy = QualType(RefTy->getReferencedType(), Qualifier::None);
    }
    // Remove top-level cv-qualifiers
    ResultTy = ResultTy.withoutConstQualifier().withoutVolatileQualifier();

    // Array-to-pointer decay
    if (auto *ArrTy = llvm::dyn_cast<ArrayType>(ResultTy.getTypePtr())) {
      auto *PtrTy = Context.getPointerType(ArrTy->getElementType());
      ResultTy = QualType(PtrTy, Qualifier::None);
    }
    // Function-to-pointer decay
    else if (auto *FnTy = llvm::dyn_cast<FunctionType>(ResultTy.getTypePtr())) {
      auto *PtrTy = Context.getPointerType(ResultTy.getTypePtr());
      ResultTy = QualType(PtrTy, Qualifier::None);
    }
  }

  // P7.1.2: Check that the decayed type is copyable.
  // For record types, verify the class has a copy or move constructor.
  if (!ResultTy.isNull() && ResultTy->isRecordType()) {
    if (auto *RT = llvm::dyn_cast<RecordType>(ResultTy.getTypePtr())) {
      if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (!CXXRD->hasCopyConstructor() && !CXXRD->hasMoveConstructor()) {
          Diag(AutoLoc, DiagID::err_decay_copy_non_copyable);
          return ExprResult(nullptr);
        }
      }
    }
  }

  auto *DCE = Context.create<DecayCopyExpr>(AutoLoc, SubExpr, IsDirectInit);
  DCE->setType(ResultTy);

  // P7.1.2: Warn if decay-copy is redundant (subexpression is already a prvalue).
  if (!SubTy.isNull() && SubExpr->isPRValue()) {
    Diag(AutoLoc, DiagID::warn_decay_copy_redundant);
  }

  return ExprResult(DCE);
}

//===----------------------------------------------------------------------===//
// P7.1.4: [[assume]] attribute (P1774R8)
//===----------------------------------------------------------------------===//

ExprResult Sema::ActOnAssumeAttr(SourceLocation Loc, Expr *Condition) {
  if (!Condition)
    return ExprResult(nullptr);

  // The condition must be contextually convertible to bool.
  QualType CondTy = Condition->getType();
  if (!CondTy.isNull()) {
    // Check if the condition type is convertible to bool.
    // In C++, any scalar type (integer, floating, pointer, member pointer)
    // is contextually convertible to bool.
    bool IsConvertible = false;
    if (CondTy->isBooleanType()) {
      IsConvertible = true;
    } else if (CondTy->isIntegerType() || CondTy->isFloatingType()) {
      IsConvertible = true;
    } else if (CondTy->isPointerType() || CondTy->isReferenceType()) {
      IsConvertible = true;
    } else if (auto *MPT = llvm::dyn_cast<MemberPointerType>(CondTy.getTypePtr())) {
      (void)MPT;
      IsConvertible = true;
    } else if (CondTy->isRecordType()) {
      // User-defined conversion to bool might exist — accept for now.
      IsConvertible = true;
    }

    if (!IsConvertible) {
      Diag(Loc, DiagID::err_assume_attr_not_bool);
      return ExprResult(nullptr);
    }
  }

  // P7.1.4: Warn if the condition expression has potential side effects.
  // Simple heuristic: if the expression is not a literal and contains
  // assignment, increment/decrement, or function call operators.
  if (Condition->getKind() == ASTNode::NodeKind::BinaryOperatorKind) {
    auto *BO = llvm::cast<BinaryOperator>(Condition);
    auto OPC = BO->getOpcode();
    if (OPC == BinaryOpKind::Assign || OPC == BinaryOpKind::AddAssign ||
        OPC == BinaryOpKind::SubAssign || OPC == BinaryOpKind::MulAssign ||
        OPC == BinaryOpKind::DivAssign || OPC == BinaryOpKind::RemAssign ||
        OPC == BinaryOpKind::ShlAssign || OPC == BinaryOpKind::ShrAssign ||
        OPC == BinaryOpKind::AndAssign || OPC == BinaryOpKind::OrAssign ||
        OPC == BinaryOpKind::XorAssign) {
      Diag(Loc, DiagID::warn_assume_attr_side_effects);
    }
  }

  return ExprResult(Condition);
}

//===----------------------------------------------------------------------===//
// P7.3.1: C++26 Contracts (P2900R14)
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnContractAttr(SourceLocation Loc,
                                    unsigned Kind, Expr *Condition) {
  SemaCXX SCXX(*this);
  ContractAttr *CA = SCXX.BuildContractAttr(Loc,
      static_cast<ContractKind>(Kind), Condition);
  if (!CA)
    return DeclResult(nullptr);
  return DeclResult(CA);
}

void Sema::AttachContractsToFunction(FunctionDecl *FD,
                                      llvm::ArrayRef<Decl *> Contracts) {
  if (!FD || Contracts.empty())
    return;

  SemaCXX SCXX(*this);

  // Validate placement for each contract.
  for (auto *D : Contracts) {
    auto *CA = llvm::dyn_cast<ContractAttr>(D);
    if (CA)
      SCXX.CheckContractPlacement(CA, FD);
  }

  // P1-1: Detect contract mode override.
  if (auto *ExistingAttrs = FD->getAttrs()) {
    for (auto *ExistingCA : ExistingAttrs->getContracts()) {
      for (auto *D : Contracts) {
        auto *NewCA = llvm::dyn_cast<ContractAttr>(D);
        if (!NewCA) continue;
        if (ExistingCA->getContractMode() != NewCA->getContractMode()) {
          Diags.report(NewCA->getLocation(), DiagID::warn_contract_mode_override,
                       getContractModeName(NewCA->getContractMode()),
                       getContractModeName(ExistingCA->getContractMode()));
        }
      }
    }
  }

  // Store contracts in the function's attribute list.
  // Create or reuse the function's Attrs.
  auto *AttrList = FD->getAttrs();
  if (!AttrList) {
    AttrList = llvm::cast<AttributeListDecl>(
        ActOnAttributeListDecl(FD->getLocation()).get());
    FD->setAttrs(AttrList);
  }

  // Register contracts as AttributeDecl entries in the list.
  for (auto *D : Contracts) {
    auto *CA = llvm::dyn_cast<ContractAttr>(D);
    if (!CA) continue;
    // Store ContractAttr directly for CodeGen access.
    AttrList->addContract(CA);
    // Create an AttributeDecl with the contract kind name and condition.
    auto *AD = Context.create<AttributeDecl>(
        CA->getLocation(), getContractKindName(CA->getContractKind()).str(),
        CA->getCondition());
    AttrList->addAttribute(AD);
  }
}

//===----------------------------------------------------------------------===//
// Template Instantiation
//===----------------------------------------------------------------------===//

FunctionDecl *Sema::InstantiateFunctionTemplate(
    FunctionTemplateDecl *FuncTemplate,
    llvm::ArrayRef<TemplateArgument> TemplateArgs,
    SourceLocation Loc) {
  
  if (FuncTemplate == nullptr) {
    return nullptr;
  }

  // Template instantiation depth limit to prevent infinite recursion.
  // Per C++ standard recommendation, 1024 is the typical limit.
  // Use the TemplateInstantiator's depth tracking instead of a static variable.
  if (Instantiator->isInstantiationDepthExceeded()) {
    Diags.report(Loc, DiagID::err_template_recursion);
    return nullptr;
  }
  
  // Step 1: Check if a specialization already exists (cache lookup)
  if (auto *Existing = FuncTemplate->findSpecialization(TemplateArgs)) {
    return Existing;
  }
  
  // Step 2: Get the templated function declaration
  Decl *RawTemplated = FuncTemplate->getTemplatedDecl();
  
  auto *TemplatedFunc = llvm::dyn_cast_or_null<FunctionDecl>(RawTemplated);
  
  if (TemplatedFunc == nullptr) {
    Diags.report(Loc, DiagID::err_template_recursion);
    return nullptr;
  }
  
  // Step 3: Create TemplateInstantiation and set up substitutions
  TemplateInstantiation Inst;
  auto Params = FuncTemplate->getTemplateParameters();
  
  if (Params.empty()) {
    Diags.report(Loc, DiagID::err_template_arg_num_different, 
                 "no template parameters", FuncTemplate->getName());
    return nullptr;
  }
  
  // Build substitution map from template parameters to arguments
  for (unsigned i = 0; i < std::min(TemplateArgs.size(), Params.size()); ++i) {
    if (auto *ParamDecl = llvm::dyn_cast_or_null<TypedefNameDecl>(Params[i])) {
      Inst.addSubstitution(ParamDecl, TemplateArgs[i]);
    }
  }
  
  // Step 4: Substitute function signature types
  QualType SubstFuncType = Inst.substituteType(TemplatedFunc->getType());
  
  // Extract return type from substituted function type
  QualType ReturnType;
  if (SubstFuncType.getTypePtr() && SubstFuncType->isFunctionType()) {
    auto *FT = static_cast<const FunctionType *>(SubstFuncType.getTypePtr());
    ReturnType = QualType(FT->getReturnType(), Qualifier::None);
  } else {
    ReturnType = TemplatedFunc->getType();  // Fallback
  }
  
  // If return type is AutoType, we need to deduce it from the body
  bool NeedsAutoDeduction = (ReturnType.getTypePtr() && ReturnType->getTypeClass() == TypeClass::Auto);
  
  // Step 5: Clone parameter declarations with substituted types
  llvm::SmallVector<ParmVarDecl *, 4> ClonedParams;
  for (auto *OrigParam : TemplatedFunc->getParams()) {
    // Substitute the parameter type
    QualType SubstParamType = Inst.substituteType(OrigParam->getType());
    
    // Create new ParmVarDecl with substituted type
    auto *ClonedParam = Context.create<ParmVarDecl>(
        OrigParam->getLocation(),
        OrigParam->getName(),
        SubstParamType,
        OrigParam->getFunctionScopeIndex(),
        OrigParam->getDefaultArg());  // TODO: clone default arg expression
    
    ClonedParams.push_back(ClonedParam);
  }
  
  // Step 6: Determine return type (handle auto deduction)
  // If the substituted function type still has AutoType, deduce from body
  if (ReturnType.getTypePtr() && ReturnType->getTypeClass() == TypeClass::Auto) {
    // Clone body first to get the instantiated body
    Stmt *ClonedBody = nullptr;
    if (auto *Body = TemplatedFunc->getBody()) {
      StmtCloner Cloner(Inst);
      ClonedBody = Cloner.Clone(Body);
    }
    
    // Deduce return type from cloned body
    QualType DeducedType = deduceReturnTypeFromBody(ClonedBody, Loc);
    if (!DeducedType.isNull()) {
      ReturnType = DeducedType;
    } else {
      Diags.report(Loc, DiagID::err_auto_return_no_deduction, 
                   FuncTemplate->getName());
      return nullptr;
    }
  }
  
  // Step 7: Create new FunctionDecl with determined return type and cloned params
  auto *NewFD = Context.create<FunctionDecl>(
      TemplatedFunc->getLocation(),
      TemplatedFunc->getName(),
      ReturnType,
      ClonedParams);
  
  // Step 8: Clone function body if present (if not already cloned above)
  if (!NewFD->getBody() && TemplatedFunc->getBody()) {
    StmtCloner Cloner(Inst);
    Stmt *ClonedBody = Cloner.Clone(TemplatedFunc->getBody());
    NewFD->setBody(ClonedBody);
  }
  
  // Step 8: Register the specialization
  FuncTemplate->addSpecialization(NewFD);
  
  return NewFD;
}

} // namespace blocktype

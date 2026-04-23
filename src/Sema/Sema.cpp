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
/// Delegates to TemplateInstantiator for the actual instantiation (which uses
/// addFieldWithParent to correctly set Parent pointers), then registers the
/// specialized record and its fields in the symbol table.
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
  
  // Step 4: Delegate to TemplateInstantiator which uses addFieldWithParent
  // (correctly sets Parent on fields) and handles base classes and methods.
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs(Args.begin(), Args.end());
  CXXRecordDecl *SpecializedRecord =
      Instantiator->InstantiateClassTemplate(ClassTemplate, TemplateArgs);
  
  if (!SpecializedRecord) {
    return QualType();
  }
  
  // Step 5: Register the specialized record and its fields in the symbol table.
  // TemplateInstantiator does not register declarations — that is Sema's
  // responsibility.
  registerDecl(SpecializedRecord);
  for (auto *Field : SpecializedRecord->fields()) {
    registerDecl(Field);
  }
  
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
// Declaration handling
//===----------------------------------------------------------------------===//

DeclResult Sema::ActOnDeclarator(Decl *D) {
  if (!D)
    return DeclResult::getInvalid();

  if (CurrentScope && llvm::isa<NamedDecl>(D)) {
    registerDecl(llvm::cast<NamedDecl>(D));
  }
  return DeclResult(D);
}

void Sema::ActOnFinishDecl(Decl *D) {
  if (CurContext && D) {
    CurContext->addDecl(D);
  }
}

DeclResult Sema::ActOnVarDecl(SourceLocation Loc, llvm::StringRef Name,
                               QualType T, Expr *Init,
                               class AttributeListDecl *Attrs) {
  auto *VD = Context.create<VarDecl>(Loc, Name, T, Init);
  
  // Set attributes if provided
  if (Attrs) {
    VD->setAttrs(Attrs);
  }

  // Check initializer if present
  if (Init) {
    if (!TC.CheckInitialization(T, Init, Loc))
      return DeclResult::getInvalid();
  }

  if (CurrentScope)
    Symbols.addDecl(VD);
  if (CurContext)
    CurContext->addDecl(VD);

  return DeclResult(VD);
}

DeclResult Sema::ActOnFunctionDecl(SourceLocation Loc, llvm::StringRef Name,
                                    QualType T,
                                    llvm::ArrayRef<ParmVarDecl *> Params,
                                    Stmt *Body) {
  // Auto return type deduction is handled in ActOnFinishOfFunctionDef.
  QualType ActualReturnType = T;
  
  auto *FD = Context.create<FunctionDecl>(Loc, Name, ActualReturnType, Params, Body);

  registerDecl(FD);
  if (CurContext)
    CurContext->addDecl(FD);

  return DeclResult(FD);
}

void Sema::ActOnStartOfFunctionDef(FunctionDecl *FD) {
  CurFunction = FD;
  PushScope(ScopeFlags::FunctionBodyScope);

  // Add parameters to function scope
  if (FD) {
    for (unsigned I = 0; I < FD->getNumParams(); ++I) {
      if (auto *PVD = FD->getParamDecl(I))
        registerDecl(PVD);
    }
  }
}

void Sema::ActOnFinishOfFunctionDef(FunctionDecl *FD) {
  if (FD) {
    // Deduce auto return type if needed
    QualType FnType = FD->getType();
    if (auto *FT = llvm::dyn_cast_or_null<FunctionType>(FnType.getTypePtr())) {
      QualType RetType = QualType(FT->getReturnType(), Qualifier::None);
      
      // Check if return type is AutoType
      if (RetType.getTypePtr() && RetType->getTypeClass() == TypeClass::Auto) {
        QualType DeducedType = deduceReturnTypeFromBody(FD->getBody(), FD->getLocation());
        
        if (!DeducedType.isNull()) {
          // For auto return type deduction, we use the deduced type directly
          // TypeDeduction::deduceAutoType handles stripping references, cv-qualifiers, etc.
          // We need to create a dummy expression for the deduction
          // Since we already have the type from return statement, use deduceAutoType
          // with a synthesized expression
          
          // Create a dummy IntegerLiteral for type deduction purposes
          // (the actual value doesn't matter, only the type)
          auto *DummyExpr = Context.create<IntegerLiteral>(FD->getLocation(), 
                                                            llvm::APSInt(32), 
                                                            DeducedType);
          DeducedType = TypeDeduce->deduceAutoType(RetType, DummyExpr);
          
          if (!DeducedType.isNull()) {
            // Update the function's return type
            // Create a new function type with the deduced return type
            QualType NewFnType = Context.getFunctionType(DeducedType.getTypePtr(), 
                                                          FT->getParamTypes(),
                                                          FT->isVariadic(),
                                                          FT->isConst(),
                                                          FT->isVolatile());
            FD->setType(NewFnType);
          }
        } else {
          // No return statement or return; -> deduce as void
          QualType VoidType = Context.getVoidType();
          QualType NewFnType = Context.getFunctionType(VoidType.getTypePtr(),
                                                        FT->getParamTypes(),
                                                        FT->isVariadic(),
                                                        FT->isConst(),
                                                        FT->isVolatile());
          FD->setType(NewFnType);
        }
      }
    }
  }
  
  CurFunction = nullptr;
  PopScope();
}

DeclResult Sema::ActOnEnumConstant(EnumConstantDecl *ECD) {
  if (!ECD)
    return DeclResult::getInvalid();

  // Evaluate the enum constant's initializer expression and cache the result.
  // Per Clang's pattern: Sema::ActOnEnumConstant evaluates and stores the
  // APSInt directly on the EnumConstantDecl.
  Expr *Init = ECD->getInitExpr();
  if (Init) {
    auto Result = ConstEval.Evaluate(Init);
    if (Result.isSuccess() && Result.isIntegral()) {
      ECD->setVal(Result.getInt());
    } else {
      // If evaluation fails, default to 0 and report a diagnostic
      ECD->setVal(llvm::APSInt(llvm::APInt(32, 0)));
      Diags.report(Init->getLocation(),
                   DiagID::err_non_constexpr_in_constant_context);
    }
  } else {
    // No initializer — value will be auto-incremented by the parser/driver.
    // Default to 0 here; the caller should set the correct value.
    ECD->setVal(llvm::APSInt(llvm::APInt(32, 0)));
  }

  registerDecl(ECD);
  if (CurContext)
    CurContext->addDecl(ECD);

  return DeclResult(ECD);
}

DeclResult Sema::ActOnEnumConstantDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                               QualType EnumType, Expr *Init) {
  auto *ECD = Context.create<EnumConstantDecl>(Loc, Name, EnumType, Init);
  return ActOnEnumConstant(ECD);
}

//===----------------------------------------------------------------------===//
// Declaration factory methods (Phase 2D)
//===----------------------------------------------------------------------===//

StmtResult Sema::ActOnDeclStmtFromDecl(Decl *D) {
  if (!D) return StmtResult::getInvalid();
  Decl *Decls[] = {D};
  auto *DS = Context.create<DeclStmt>(D->getLocation(), Decls);
  return StmtResult(DS);
}

// P7.4.3: Create DeclStmt from multiple declarations (structured bindings)
StmtResult Sema::ActOnDeclStmtFromDecls(llvm::ArrayRef<Decl *> Decls) {
  if (Decls.empty()) return StmtResult::getInvalid();
  
  // Use the location of the first declaration
  SourceLocation Loc = Decls[0]->getLocation();
  auto *DS = Context.create<DeclStmt>(Loc, Decls);
  return StmtResult(DS);
}

DeclResult Sema::ActOnTypeAliasDecl(SourceLocation Loc, llvm::StringRef Name,
                                    QualType Underlying) {
  auto *TAD = Context.create<TypeAliasDecl>(Loc, Name, Underlying);
  if (CurrentScope) Symbols.addDecl(TAD);
  if (CurContext) CurContext->addDecl(TAD);
  return DeclResult(TAD);
}

DeclResult Sema::ActOnUsingDecl(SourceLocation Loc, llvm::StringRef Name,
                                llvm::StringRef NestedName, bool HasNested,
                                bool IsInheritingCtor) {
  auto *UD = Context.create<UsingDecl>(Loc, Name, NestedName, HasNested,
                                        IsInheritingCtor);
  return DeclResult(UD);
}

DeclResult Sema::ActOnParmVarDecl(SourceLocation Loc, llvm::StringRef Name,
                                  QualType T, unsigned Index,
                                  Expr *DefaultArg) {
  auto *PVD = Context.create<ParmVarDecl>(Loc, Name, T, Index, DefaultArg);
  return DeclResult(PVD);
}

DeclResult Sema::ActOnNamespaceDecl(SourceLocation Loc, llvm::StringRef Name,
                                    bool IsInline) {
  auto *NS = Context.create<NamespaceDecl>(Loc, Name, IsInline);
  registerDecl(NS);
  if (CurContext) CurContext->addDecl(NS);
  return DeclResult(NS);
}

DeclResult Sema::ActOnUsingEnumDecl(SourceLocation Loc,
                                    llvm::StringRef EnumName,
                                    llvm::StringRef NestedName,
                                    bool HasNested) {
  auto *UED = Context.create<UsingEnumDecl>(Loc, EnumName, NestedName,
                                             HasNested);
  return DeclResult(UED);
}

DeclResult Sema::ActOnUsingDirectiveDecl(SourceLocation Loc,
                                         llvm::StringRef Name,
                                         llvm::StringRef NestedName,
                                         bool HasNested) {
  auto *UDD = Context.create<UsingDirectiveDecl>(Loc, Name, NestedName,
                                                  HasNested);
  return DeclResult(UDD);
}

DeclResult Sema::ActOnNamespaceAliasDecl(SourceLocation Loc,
                                         llvm::StringRef Alias,
                                         llvm::StringRef Target,
                                         llvm::StringRef NestedName) {
  auto *NAD = Context.create<NamespaceAliasDecl>(Loc, Alias, Target,
                                                  NestedName);
  registerDecl(NAD);
  return DeclResult(NAD);
}

DeclResult Sema::ActOnModuleDecl(SourceLocation Loc, llvm::StringRef Name,
                                 bool IsExported, llvm::StringRef Partition,
                                 bool IsPartition, bool IsGlobalFragment,
                                 bool IsPrivateFragment) {
  auto *MD = Context.create<ModuleDecl>(Loc, Name, IsExported, Partition,
                                         IsPartition, IsGlobalFragment,
                                         IsPrivateFragment);
  registerDecl(MD);
  if (CurContext) CurContext->addDecl(MD);
  return DeclResult(MD);
}

DeclResult Sema::ActOnImportDecl(SourceLocation Loc, llvm::StringRef ModuleName,
                                 bool IsExported, llvm::StringRef Partition,
                                 llvm::StringRef Header, bool IsHeader) {
  auto *ID = Context.create<ImportDecl>(Loc, ModuleName, IsExported, Partition,
                                         Header, IsHeader);
  registerDecl(ID);
  if (CurContext) CurContext->addDecl(ID);
  return DeclResult(ID);
}

DeclResult Sema::ActOnExportDecl(SourceLocation Loc, Decl *Exported) {
  auto *ED = Context.create<ExportDecl>(Loc, Exported);
  return DeclResult(ED);
}

DeclResult Sema::ActOnEnumDecl(SourceLocation Loc, llvm::StringRef Name) {
  auto *ED = Context.create<EnumDecl>(Loc, Name);
  registerDecl(ED);
  if (CurContext) CurContext->addDecl(ED);
  return DeclResult(ED);
}

DeclResult Sema::ActOnTypedefDecl(SourceLocation Loc, llvm::StringRef Name,
                                  QualType T) {
  auto *TD = Context.create<TypedefDecl>(Loc, Name, T);
  registerDecl(TD);
  if (CurContext) CurContext->addDecl(TD);
  return DeclResult(TD);
}

DeclResult Sema::ActOnStaticAssertDecl(SourceLocation Loc, Expr *Cond,
                                       llvm::StringRef Message) {
  auto *SAD = Context.create<StaticAssertDecl>(Loc, Cond, Message);
  return DeclResult(SAD);
}

DeclResult Sema::ActOnLinkageSpecDecl(SourceLocation Loc,
                                      LinkageSpecDecl::Language Lang,
                                      bool HasBraces) {
  auto *LSD = Context.create<LinkageSpecDecl>(Loc, Lang, HasBraces);
  return DeclResult(LSD);
}

DeclResult Sema::ActOnAsmDecl(SourceLocation Loc, llvm::StringRef AsmString) {
  auto *AD = Context.create<AsmDecl>(Loc, AsmString);
  return DeclResult(AD);
}

DeclResult Sema::ActOnCXXDeductionGuideDecl(
    SourceLocation Loc, llvm::StringRef TemplateName, QualType ReturnType,
    llvm::ArrayRef<ParmVarDecl *> Params) {
  auto *DGD = Context.create<CXXDeductionGuideDecl>(Loc, TemplateName,
                                                      ReturnType, Params);
  return DeclResult(DGD);
}

DeclResult Sema::ActOnAttributeListDecl(SourceLocation Loc) {
  auto *ALD = Context.create<AttributeListDecl>(Loc);
  return DeclResult(ALD);
}

DeclResult Sema::ActOnAttributeDecl(SourceLocation Loc, llvm::StringRef Name,
                                    Expr *Arg) {
  auto *AD = Context.create<AttributeDecl>(Loc, Name, Arg);
  return DeclResult(AD);
}

DeclResult Sema::ActOnAttributeDeclWithNamespace(SourceLocation Loc,
                                                 llvm::StringRef Namespace,
                                                 llvm::StringRef Name,
                                                 Expr *Arg) {
  auto *AD = Context.create<AttributeDecl>(Loc, Namespace, Name, Arg);
  return DeclResult(AD);
}

DeclResult Sema::ActOnVarDeclFull(SourceLocation Loc, llvm::StringRef Name,
                                  QualType T, Expr *Init, bool IsStatic,
                                  class AttributeListDecl *Attrs) {
  // Check if type needs template instantiation
  QualType ActualType = T;
  if (T.getTypePtr() && T->getTypeClass() == TypeClass::TemplateSpecialization) {
    auto *TST = static_cast<const TemplateSpecializationType *>(T.getTypePtr());
    QualType Instantiated = InstantiateClassTemplate(TST->getTemplateName(), TST);
    if (!Instantiated.isNull()) {
      ActualType = Instantiated;
    } else {
      return DeclResult::getInvalid();
    }
  }
  
  // Auto type deduction: replace AutoType with initializer's type
  if (ActualType.getTypePtr() && ActualType->getTypeClass() == TypeClass::Auto && Init) {
    // Get the initializer's type
    QualType InitType = Init->getType();
    if (!InitType.isNull()) {
      ActualType = InitType;
    } else {
      // If Init type is null, report error
      Diags.report(Loc, DiagID::err_type_mismatch);
      return DeclResult::getInvalid();
    }
  }
  
  // Check for complete type
  if (!ActualType.isNull() && !RequireCompleteType(ActualType, Loc)) {
    return DeclResult::getInvalid();
  }
  
  auto *VD = Context.create<VarDecl>(Loc, Name, ActualType, Init, IsStatic);
  
  // Set attributes if provided
  if (Attrs) {
    VD->setAttrs(Attrs);
  }
  
  registerDecl(VD);
  if (CurContext) CurContext->addDecl(VD);
  return DeclResult(VD);
}

// P7.4.2: Placeholder variable `_` implementation
DeclResult Sema::ActOnPlaceholderVarDecl(SourceLocation Loc, QualType T, Expr *Init) {
  // Create a VarDecl with name "_" but mark it as placeholder
  auto *VD = Context.create<VarDecl>(Loc, "_", T, Init, false);
  VD->setPlaceholder(true);
  
  // IMPORTANT: Do NOT add to symbol table or current context
  // Each `_` is a separate variable, even in the same scope
  // We still need to register for CodeGen, but not for name lookup
  
  // Only add to CurContext for CodeGen purposes (so it gets emitted)
  // But don't add to Symbols (symbol table)
  if (CurContext) {
    CurContext->addDecl(VD);
  }
  
  return DeclResult(VD);
}

// P7.4.3: Helper function to extract element type from tuple-like types
static QualType GetTupleElementType(QualType TupleType, unsigned Index) {
  // Remove qualifiers for analysis
  QualType UnqualType = TupleType.getCanonicalType();
  
  const Type *Ty = UnqualType.getTypePtr();
  if (!Ty) {
    return TupleType; // Fallback to original type
  }
  
  // Handle array types T[N] - all elements have the same type
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty)) {
    return AT->getElementType();
  }
  
  // Check if it's a RecordType (class/struct)
  if (auto *RT = llvm::dyn_cast<RecordType>(Ty)) {
    RecordDecl *RD = RT->getDecl();
    if (!RD) {
      return TupleType;
    }
    
    llvm::StringRef ClassName = RD->getName();
    
    // Handle std::pair<T1, T2> and std::tuple<Ts...>
    if ((ClassName == "pair" || ClassName.ends_with("::pair")) ||
        (ClassName == "tuple" || ClassName.ends_with("::tuple"))) {
      
      // Check if this is a template specialization
      if (auto *Spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        // Get template arguments
        auto Args = Spec->getTemplateArgs();
        
        if (Index < Args.size()) {
          const TemplateArgument &Arg = Args[Index];
          
          // If the argument is a type, return it directly
          if (Arg.isType()) {
            return Arg.getAsType();
          }
        }
      }
    }
    
    // For all record types (including pair/tuple), try to get field type
    unsigned FieldIndex = 0;
    for (auto *Field : RD->fields()) {
      if (FieldIndex == Index) {
        return Field->getType();
      }
      FieldIndex++;
    }
    
    // Last resort: return the original type
    return TupleType;
  }
  
  // Not a recognized tuple-like type
  return QualType();
}

/// Check if a type is tuple-like (std::pair, std::tuple, or array)
static bool IsTupleLikeType(QualType Ty) {
  if (!Ty.getTypePtr()) return false;
  
  if (llvm::isa<ArrayType>(Ty.getTypePtr())) {
    return true;
  }
  
  QualType UnqualType = Ty.getCanonicalType();
  const Type *TypePtr = UnqualType.getTypePtr();
  
  if (auto *RT = llvm::dyn_cast<RecordType>(TypePtr)) {
    RecordDecl *RD = RT->getDecl();
    if (RD) {
      // Check for std::pair or std::tuple
      llvm::StringRef Name = RD->getName();
      if (Name == "pair" || Name.ends_with("::pair") ||
          Name == "tuple" || Name.ends_with("::tuple")) {
        return true;
      }
      
      // For other record types, check if they have fields (aggregate)
      unsigned FieldCount = 0;
      for (auto *Field : RD->fields()) {
        FieldCount++;
      }
      if (FieldCount > 0) {
        return true;  // Any struct/class with fields is decomposable
      }
    }
  }
  
  return false;
}

/// Get the number of elements in a tuple-like type
static unsigned GetTupleElementCount(QualType TupleType) {
  QualType UnqualType = TupleType.getCanonicalType();
  const Type *Ty = UnqualType.getTypePtr();
  
  if (!Ty) {
    return 0;
  }
  
  // Handle array types
  if (auto *AT = llvm::dyn_cast<ConstantArrayType>(Ty)) {
    return AT->getSize().getZExtValue();
  }
  
  // Handle record types (pair/tuple)
  if (auto *RT = llvm::dyn_cast<RecordType>(Ty)) {
    RecordDecl *RD = RT->getDecl();
    if (!RD) {
      return 0;
    }
    
    // Check if it's a template specialization
    if (auto *Spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
      return Spec->getNumTemplateArgs();
    }
    
    // For non-specialization, count fields
    unsigned Count = 0;
    for (auto *Field : RD->fields()) {
      Count++;
    }
    return Count;
  }
  
  return 0;
}

// P7.4.3: Structured binding implementation
DeclGroupRef Sema::ActOnDecompositionDecl(SourceLocation Loc,
                                         llvm::ArrayRef<llvm::StringRef> Names,
                                         QualType TupleType,
                                         Expr *Init) {
  // Full implementation for structured binding (P7.4.3)
  // 
  // Steps:
  // 1. Check if TupleType is decomposable (has tuple_size, get<N>)
  //    - For std::pair: check pair<T1, T2>
  //    - For std::tuple: check tuple<Ts...>
  //    - For arrays: check array type
  //    - For custom types: check for tuple_size<T> and get<N>(t)
  // 2. For each name, create a BindingDecl with proper type
  // 3. Set binding expression to std::get<N>(init)
  // 4. Return DeclGroupRef containing all bindings
  
  // Step 1: Validate the type is decomposable
  const Type *Ty = TupleType.getCanonicalType().getTypePtr();
  if (!Ty) {
    Diags.report(Loc, DiagID::err_structured_binding_not_decomposable,
                 TupleType.getAsString());
    return DeclGroupRef::getInvalid();
  }
  
  // Check if it's a record type (pair/tuple) or array
  bool IsDecomposable = IsTupleLikeType(TupleType);
  unsigned NumElements = GetTupleElementCount(TupleType);
  
  if (!IsDecomposable) {
    Diags.report(Loc, DiagID::err_structured_binding_not_decomposable,
                 TupleType.getAsString());
    return DeclGroupRef::getInvalid();
  }
  
  // Step 2: Check binding count matches element count
  if (Names.size() != NumElements && !llvm::isa<ArrayType>(Ty)) {
    Diags.report(Loc, DiagID::err_structured_binding_wrong_count,
                 std::to_string(Names.size()), std::to_string(NumElements));
    return DeclGroupRef::getInvalid();
  }
  
  llvm::SmallVector<Decl *, 4> Decls;
  
  for (unsigned i = 0; i < Names.size(); ++i) {
    // Extract the correct type for each binding element
    QualType ElementType = GetTupleElementType(TupleType, i);
    
    if (ElementType.isNull()) {
      Diags.report(Loc, DiagID::err_structured_binding_no_get,
                   TupleType.getAsString());
      return DeclGroupRef::getInvalid();
    }
    
    Expr *BindingExpr = nullptr;
    
    // For array types, create arr[i] directly
    if (llvm::isa<ArrayType>(Ty)) {
      // Create ArraySubscriptExpr: init[i]
      llvm::APInt IndexValue(32, i);
      auto *IndexExpr = Context.create<IntegerLiteral>(Loc, IndexValue, Context.getIntType());
      BindingExpr = Context.create<ArraySubscriptExpr>(Loc, Init, IndexExpr);
      // Set the element type for the subscript expression
      BindingExpr->setType(ElementType);
    } else {
      // For tuple/pair types, create std::get<i>(init)
      ExprResult GetCall = BuildStdGetCall(i, Init, ElementType, Loc);
      
      if (GetCall.isUsable()) {
        BindingExpr = GetCall.get();
      } else {
        // Fallback: use direct indexing with warning
        Diags.report(Loc, DiagID::warn_structured_binding_reference);
        // Try to create a simple subscript expression as fallback
        llvm::APInt IndexValue(32, i);
        auto *IndexExpr = Context.create<IntegerLiteral>(Loc, IndexValue, Context.getIntType());
        BindingExpr = Context.create<ArraySubscriptExpr>(Loc, Init, IndexExpr);
        // Set the element type for the subscript expression
        BindingExpr->setType(ElementType);
      }
    }
    
    // Create a BindingDecl with the correct type and binding expression
    auto *BD = Context.create<BindingDecl>(Loc, Names[i], ElementType,
                                            BindingExpr, i);
    
    Decls.push_back(BD);
    
    if (CurContext) {
      CurContext->addDecl(BD);
    }
  }
  
  // Return DeclGroupRef containing all bindings
  return Decls.empty() ? DeclGroupRef::createEmpty() : DeclGroupRef(Decls);
}

/// P1061R10: Structured binding with pack expansion
/// Syntax: auto [a, b, ...rest] = tuple;
DeclGroupRef Sema::ActOnDecompositionDeclWithPack(
    SourceLocation Loc,
    llvm::ArrayRef<llvm::StringRef> Names,
    bool HasPackExpansion,
    SourceLocation PackExpansionLoc,
    QualType TupleType,
    Expr *Init) {
  if (!HasPackExpansion) {
    // No pack expansion, use regular implementation
    return ActOnDecompositionDecl(Loc, Names, TupleType, Init);
  }
  
  // P1061R10: Handle pack expansion
  // The last name is a pack that should expand to multiple bindings
  if (Names.empty()) {
    Diags.report(Loc, DiagID::err_expected_identifier);
    return DeclGroupRef::getInvalid();
  }
  
  // Get the number of elements in the tuple
  unsigned NumElements = GetTupleElementCount(TupleType);
  unsigned NumFixedBindings = Names.size() - 1;  // All except the pack
  
  if (NumFixedBindings > NumElements) {
    Diags.report(Loc, DiagID::err_structured_binding_wrong_count,
                 std::to_string(Names.size()), std::to_string(NumElements));
    return DeclGroupRef::getInvalid();
  }
  
  llvm::SmallVector<Decl *, 8> Decls;
  
  // Create fixed bindings (non-pack)
  for (unsigned i = 0; i < NumFixedBindings; ++i) {
    QualType ElementType = GetTupleElementType(TupleType, i);
    if (ElementType.isNull()) {
      Diags.report(Loc, DiagID::err_structured_binding_no_get,
                   TupleType.getAsString());
      return DeclGroupRef::getInvalid();
    }
    
    // Create std::get<i>(init)
    ExprResult GetCall = BuildStdGetCall(i, Init, ElementType, Loc);
    Expr *BindingExpr = GetCall.isUsable() ? GetCall.get() : nullptr;
    
    auto *BD = Context.create<BindingDecl>(Loc, Names[i], ElementType,
                                            BindingExpr, i);
    Decls.push_back(BD);
    
    if (CurContext) {
      CurContext->addDecl(BD);
    }
  }
  
  // Create pack bindings (remaining elements)
  llvm::StringRef PackName = Names.back();
  for (unsigned i = NumFixedBindings; i < NumElements; ++i) {
    QualType ElementType = GetTupleElementType(TupleType, i);
    if (ElementType.isNull()) {
      Diags.report(Loc, DiagID::err_structured_binding_no_get,
                   TupleType.getAsString());
      return DeclGroupRef::getInvalid();
    }
    
    // Create std::get<i>(init)
    ExprResult GetCall = BuildStdGetCall(i, Init, ElementType, Loc);
    Expr *BindingExpr = GetCall.isUsable() ? GetCall.get() : nullptr;
    
    // For pack elements, append index to name: rest0, rest1, ...
    std::string ExpandedName = PackName.str() + std::to_string(i - NumFixedBindings);
    auto *BD = Context.create<BindingDecl>(Loc, ExpandedName, ElementType,
                                            BindingExpr, i);
    Decls.push_back(BD);
    
    if (CurContext) {
      CurContext->addDecl(BD);
    }
  }
  
  return Decls.empty() ? DeclGroupRef::createEmpty() : DeclGroupRef(Decls);
}

bool Sema::CheckBindingCondition(llvm::ArrayRef<class BindingDecl *> Bindings,
                                  SourceLocation Loc) {
  // P0963R3: Check if structured binding can be used in condition
  // For now, just return true (allow it)
  return true;
}

//===----------------------------------------------------------------------===//
// Structured Binding Helper Methods (P7.4.3)
//===----------------------------------------------------------------------===//

void Sema::InitializeStdNamespace() {
  // Create std namespace
  auto *StdNS = Context.create<NamespaceDecl>(SourceLocation(), "std", /*IsInline=*/false);
  
  // Add to current context (translation unit)
  if (CurContext) {
    CurContext->addDecl(StdNS);
  }
  
  // Add to symbol table
  Symbols.addNamespaceDecl(StdNS);
  
  // Create std::tuple class template
  // template<class... Types> class tuple;
  {
    llvm::SmallVector<NamedDecl *, 1> TupleParams;
    auto *TypesParam = Context.create<TemplateTypeParmDecl>(
        SourceLocation(), "Types", /*Depth=*/0, /*Index=*/0,
        /*IsParameterPack=*/true, /*TypenameKeyword=*/true);
    TupleParams.push_back(TypesParam);
    
    auto *TupleTPL = new TemplateParameterList(
        SourceLocation(), SourceLocation(), SourceLocation(), TupleParams);
    
    auto *TupleClass = Context.create<ClassTemplateDecl>(
        SourceLocation(), "tuple", nullptr);
    TupleClass->setTemplateParameterList(TupleTPL);
    
    StdNS->addDecl(TupleClass);
  }
  
  // Create std::pair class template
  // template<class T1, class T2> struct pair;
  {
    llvm::SmallVector<NamedDecl *, 2> PairParams;
    auto *T1Param = Context.create<TemplateTypeParmDecl>(
        SourceLocation(), "T1", /*Depth=*/0, /*Index=*/0,
        /*IsParameterPack=*/false, /*TypenameKeyword=*/true);
    PairParams.push_back(T1Param);
    
    auto *T2Param = Context.create<TemplateTypeParmDecl>(
        SourceLocation(), "T2", /*Depth=*/0, /*Index=*/1,
        /*IsParameterPack=*/false, /*TypenameKeyword=*/true);
    PairParams.push_back(T2Param);
    
    auto *PairTPL = new TemplateParameterList(
        SourceLocation(), SourceLocation(), SourceLocation(), PairParams);
    
    auto *PairClass = Context.create<ClassTemplateDecl>(
        SourceLocation(), "pair", nullptr);
    PairClass->setTemplateParameterList(PairTPL);
    
    StdNS->addDecl(PairClass);
  }
  
  // Create std::get function template with proper signature:
  // template<size_t N, class... Types> constexpr decltype(auto) get(tuple<Types...>& t) noexcept;
  
  // For simplicity in current implementation, we create a version that works for structured bindings:
  // template<size_t I, class T> constexpr T& get(T& t) noexcept;
  
  // Create template parameter list
  llvm::SmallVector<NamedDecl *, 2> TemplateParams;
  
  // First parameter: size_t I (non-type template parameter)
  QualType SizeTType = Context.getIntType(); // Use int for simplicity (size_t would be better)
  auto *IParam = Context.create<NonTypeTemplateParmDecl>(
      SourceLocation(), "I", SizeTType,
      /*Depth=*/0, /*Index=*/0, /*IsParameterPack=*/false);
  TemplateParams.push_back(IParam);
  
  // Second parameter: class T (type parameter)
  auto *TParam = Context.create<TemplateTypeParmDecl>(
      SourceLocation(), "T", /*Depth=*/0, /*Index=*/1, 
      /*IsParameterPack=*/false, /*TypenameKeyword=*/true);
  TemplateParams.push_back(TParam);
  
  auto *TPL = new TemplateParameterList(
      SourceLocation(), SourceLocation(), SourceLocation(), TemplateParams);
  
  // Create function declaration
  // Parameter: T& t (reference to T)
  QualType TType = Context.getTemplateTypeParmType(/*Depth=*/0, /*Index=*/1, 
                                                     /*IsParameterPack=*/false, TParam);
  QualType RefType = Context.getLValueReferenceType(TType.getTypePtr());
  
  auto *Param = Context.create<ParmVarDecl>(
      SourceLocation(), "t", RefType, /*HasDefaultArg=*/false);
  
  llvm::SmallVector<ParmVarDecl *, 1> Params;
  Params.push_back(Param);
  
  // Return type: T& (same as parameter)
  auto *FD = Context.create<FunctionDecl>(
      SourceLocation(), "get", RefType, Params,
      /*Body=*/nullptr, /*IsInline=*/false, 
      /*IsConstexpr=*/true, /*IsConsteval=*/false);
  
  // Create function template
  auto *GetTemplate = Context.create<FunctionTemplateDecl>(
      SourceLocation(), "get", FD);
  
  // Set the template parameter list on the template (not the function)
  GetTemplate->setTemplateParameterList(TPL);
  
  // Add to std namespace
  StdNS->addDecl(GetTemplate);
  
  // Create std::make_pair function template
  // template<class T1, class T2> constexpr pair<T1,T2> make_pair(T1&& t, T2&& u);
  {
    llvm::SmallVector<NamedDecl *, 2> MakePairParams;
    auto *T1Param = Context.create<TemplateTypeParmDecl>(
        SourceLocation(), "T1", /*Depth=*/0, /*Index=*/0,
        /*IsParameterPack=*/false, /*TypenameKeyword=*/true);
    MakePairParams.push_back(T1Param);
    
    auto *T2Param = Context.create<TemplateTypeParmDecl>(
        SourceLocation(), "T2", /*Depth=*/0, /*Index=*/1,
        /*IsParameterPack=*/false, /*TypenameKeyword=*/true);
    MakePairParams.push_back(T2Param);
    
    auto *MakePairTPL = new TemplateParameterList(
        SourceLocation(), SourceLocation(), SourceLocation(), MakePairParams);
    
    // Return type: pair<T1, T2>
    // For now, use auto as placeholder
    QualType ReturnType = Context.getAutoType();
    
    // Parameters: T1&& t, T2&& u (forwarding references)
    QualType T1Type = Context.getTemplateTypeParmType(0, 0, false, T1Param);
    QualType T2Type = Context.getTemplateTypeParmType(0, 1, false, T2Param);
    QualType T1RefType = Context.getRValueReferenceType(T1Type.getTypePtr());
    QualType T2RefType = Context.getRValueReferenceType(T2Type.getTypePtr());
    
    auto *Param1 = Context.create<ParmVarDecl>(SourceLocation(), "t", T1RefType, false);
    auto *Param2 = Context.create<ParmVarDecl>(SourceLocation(), "u", T2RefType, false);
    
    llvm::SmallVector<ParmVarDecl *, 2> Params;
    Params.push_back(Param1);
    Params.push_back(Param2);
    
    auto *MakePairFD = Context.create<FunctionDecl>(
        SourceLocation(), "make_pair", ReturnType, Params,
        nullptr, false, true, false);
    
    auto *MakePairTemplate = Context.create<FunctionTemplateDecl>(
        SourceLocation(), "make_pair", MakePairFD);
    MakePairTemplate->setTemplateParameterList(MakePairTPL);
    
    StdNS->addDecl(MakePairTemplate);
  }
}

FunctionTemplateDecl *Sema::LookupStdGetFunction() {
  // Lookup std namespace
  NamespaceDecl *StdNS = LookupNamespace("std");
  if (!StdNS) {
    // std namespace not found - this is expected in simple test cases
    return nullptr;
  }
  
  // Lookup 'get' in std namespace
  DeclContext *DC = StdNS;
  for (Decl *D : DC->decls()) {
    if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
      if (FTD->getName() == "get") {
        return FTD;
      }
    }
  }
  
  return nullptr;
}

FunctionDecl *Sema::InstantiateStdGetSpecialization(
    FunctionTemplateDecl *GetTemplate, unsigned Index,
    QualType TupleType, SourceLocation Loc) {
  
  if (!GetTemplate) {
    return nullptr;
  }
  
  // Create template argument: non-type parameter with value Index
  // Use APSInt to create an integral template argument
  llvm::APSInt IndexValue(32, false); // 32-bit, unsigned
  IndexValue = Index;
  TemplateArgument NonTypeArg(IndexValue);
  
  llvm::SmallVector<TemplateArgument, 1> Args;
  Args.push_back(NonTypeArg);
  
  // Use TemplateInstantiator to instantiate the function template
  auto &Instantiator = getTemplateInstantiator();
  FunctionDecl *Specialization = Instantiator.InstantiateFunctionTemplate(
      GetTemplate, Args);
  
  if (!Specialization) {
    Diags.report(Loc, DiagID::err_template_recursion);
    return nullptr;
  }
  
  return Specialization;
}

ExprResult Sema::BuildStdGetCall(unsigned Index, Expr *TupleExpr,
                                  QualType ElementType, SourceLocation Loc) {
  
  if (!TupleExpr) {
    return ExprResult::getInvalid();
  }
  
  // Step 1: Lookup std::get function template
  FunctionTemplateDecl *GetTemplate = LookupStdGetFunction();
  if (!GetTemplate) {
    // std::get not found - return error
    return ExprResult::getInvalid();
  }
  
  // Step 2: Instantiate std::get<Index>
  FunctionDecl *GetSpec = InstantiateStdGetSpecialization(
      GetTemplate, Index, TupleExpr->getType(), Loc);
  
  if (!GetSpec) {
    return ExprResult::getInvalid();
  }
  
  // Step 3: Build CallExpr: std::get<Index>(tupleExpr)
  llvm::SmallVector<Expr *, 1> CallArgs;
  CallArgs.push_back(TupleExpr);
  
  // Create DeclRefExpr to refer to the function
  auto *DRE = Context.create<DeclRefExpr>(Loc, GetSpec);
  
  // Create the CallExpr
  auto *Call = Context.create<CallExpr>(Loc, DRE, CallArgs);
  
  // The result type should match ElementType
  // TODO: Validate that Call->getType() matches ElementType
  
  return ExprResult(Call);
}

DeclResult Sema::ActOnFunctionDeclFull(SourceLocation Loc, llvm::StringRef Name,
                                       QualType T,
                                       llvm::ArrayRef<ParmVarDecl *> Params,
                                       Stmt *Body, bool IsInline,
                                       bool IsConstexpr, bool IsConsteval) {
  auto *FD = Context.create<FunctionDecl>(Loc, Name, T, Params, Body,
                                            IsInline, IsConstexpr,
                                            IsConsteval);
  if (CurrentScope) Symbols.addDecl(FD);
  if (CurContext) CurContext->addDecl(FD);
  return DeclResult(FD);
}

//===----------------------------------------------------------------------===//
// Class member factory methods (Phase 2E)
//===----------------------------------------------------------------------===//

void Sema::ActOnCXXRecordDecl(CXXRecordDecl *RD) {
  if (!RD) return;
  registerDecl(RD);
  if (CurContext) CurContext->addDecl(RD);
}

void Sema::ActOnCXXMethodDecl(CXXMethodDecl *MD) {
  if (!MD) return;
  registerDecl(MD);
  
  // P2: Add method to parent class
  if (auto *Parent = MD->getParent()) {
    Parent->addMethod(MD);
  }
}

void Sema::ActOnFieldDecl(FieldDecl *FD) {
  if (!FD) return;
  registerDecl(FD);
}

void Sema::ActOnAccessSpecDecl(AccessSpecDecl *ASD) {
  // Access specifiers don't need symbol table registration
}

void Sema::ActOnCXXConstructorDecl(CXXConstructorDecl *CD) {
  if (!CD) return;
  registerDecl(CD);
}

void Sema::ActOnCXXDestructorDecl(CXXDestructorDecl *DD) {
  if (!DD) return;
  registerDecl(DD);
}

void Sema::ActOnFriendDecl(FriendDecl *FD) {
  // Friend declarations don't need symbol table registration
}

// Phase 3C: Class declaration factory methods

DeclResult Sema::ActOnCXXRecordDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                            TagDecl::TagKind Kind) {
  auto *RD = Context.create<CXXRecordDecl>(Loc, Name, Kind);
  ActOnCXXRecordDecl(RD);
  return DeclResult(RD);
}

DeclResult Sema::ActOnCXXMethodDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                            QualType Type,
                                            llvm::ArrayRef<ParmVarDecl *> Params,
                                            CXXRecordDecl *Class, Stmt *Body,
                                            bool IsStatic, bool IsConst, bool IsVolatile,
                                            bool IsVirtual, bool IsPureVirtual,
                                            bool IsOverride, bool IsFinal,
                                            bool IsDefaulted, bool IsDeleted,
                                            CXXMethodDecl::RefQualifierKind RefQual,
                                            bool HasNoexceptSpec, bool NoexceptValue,
                                            Expr *NoexceptExpr,
                                            AccessSpecifier Access) {
  auto *MD = Context.create<CXXMethodDecl>(Loc, Name, Type, Params, Class, Body,
      IsStatic, IsConst, IsVolatile, IsVirtual, IsPureVirtual,
      IsOverride, IsFinal, IsDefaulted, IsDeleted,
      RefQual, HasNoexceptSpec, NoexceptValue, NoexceptExpr, Access);
  ActOnCXXMethodDecl(MD);

  // P7.1.1: Validate explicit object parameter (deducing this) via SemaCXX.
  if (MD && MD->hasExplicitObjectParam()) {
    SemaCXX Checker(*this);
    if (!Checker.CheckExplicitObjectParameter(MD,
            MD->getExplicitObjectParam(), Loc)) {
      // Diagnostic already emitted; still register the decl for error recovery.
    }
  }

  // P7.1.3: Validate static operator via SemaCXX.
  if (MD && MD->isStaticOperator()) {
    SemaCXX Checker(*this);
    if (!Checker.CheckStaticOperator(MD, Loc)) {
      // Diagnostic already emitted.
    }
  }

  return DeclResult(MD);
}

DeclResult Sema::ActOnCXXConstructorDeclFactory(SourceLocation Loc,
                                                 CXXRecordDecl *Class,
                                                 llvm::ArrayRef<ParmVarDecl *> Params,
                                                 Stmt *Body, bool IsExplicit) {
  auto *CD = Context.create<CXXConstructorDecl>(Loc, Class, Params, Body, IsExplicit);
  ActOnCXXConstructorDecl(CD);
  return DeclResult(CD);
}

DeclResult Sema::ActOnCXXDestructorDeclFactory(SourceLocation Loc,
                                                CXXRecordDecl *Class, Stmt *Body) {
  auto *DD = Context.create<CXXDestructorDecl>(Loc, Class, Body);
  ActOnCXXDestructorDecl(DD);
  return DeclResult(DD);
}

DeclResult Sema::ActOnFriendTypeDecl(SourceLocation FriendLoc,
                                      llvm::StringRef TypeName,
                                      SourceLocation TypeNameLoc) {
  QualType FriendType;
  // 1. Try to look up type in current scope
  if (CurrentScope) {
    if (NamedDecl *Found = CurrentScope->lookup(TypeName)) {
      if (auto *TD = llvm::dyn_cast<TypeDecl>(Found)) {
        FriendType = Context.getTypeDeclType(TD);
      }
    }
  }
  // 2. If not found, create a forward declaration
  if (FriendType.isNull()) {
    auto *ForwardDecl = Context.create<RecordDecl>(TypeNameLoc, TypeName, TagDecl::TK_struct);
    FriendType = Context.getTypeDeclType(ForwardDecl);
  }
  auto *FD = Context.create<FriendDecl>(FriendLoc, nullptr, FriendType, true);
  ActOnFriendDecl(FD);
  return DeclResult(FD);
}

DeclResult Sema::ActOnFriendFunctionDecl(SourceLocation FriendLoc,
                                          SourceLocation NameLoc,
                                          llvm::StringRef Name, QualType Type,
                                          llvm::ArrayRef<ParmVarDecl *> Params) {
  auto *FriendFunc = Context.create<FunctionDecl>(NameLoc, Name, Type, Params, nullptr);
  auto *FD = Context.create<FriendDecl>(FriendLoc, FriendFunc, QualType(), false);
  ActOnFriendDecl(FD);
  return DeclResult(FD);
}

// Phase 3D: Template parameter factory methods

DeclResult Sema::ActOnTemplateTypeParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                           unsigned Depth, unsigned Index,
                                           bool IsParameterPack, bool IsTypename) {
  auto *Param = Context.create<TemplateTypeParmDecl>(Loc, Name, Depth, Index,
                                                      IsParameterPack, IsTypename);
  return DeclResult(Param);
}

DeclResult Sema::ActOnNonTypeTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                              QualType Type, unsigned Depth,
                                              unsigned Index, bool IsParameterPack) {
  auto *Param = Context.create<NonTypeTemplateParmDecl>(Loc, Name, Type, Depth,
                                                         Index, IsParameterPack);
  return DeclResult(Param);
}

DeclResult Sema::ActOnTemplateTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                               unsigned Depth, unsigned Index,
                                               bool IsParameterPack) {
  auto *Param = Context.create<TemplateTemplateParmDecl>(Loc, Name, Depth,
                                                          Index, IsParameterPack);
  return DeclResult(Param);
}

// Phase 3E: Template wrapper factory methods

DeclResult Sema::ActOnTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                          Decl *TemplatedDecl) {
  auto *TD = Context.create<TemplateDecl>(Loc, Name, TemplatedDecl);
  return DeclResult(TD);
}

DeclResult Sema::ActOnClassTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                               Decl *TemplatedDecl) {
  auto *CTD = Context.create<ClassTemplateDecl>(Loc, Name, TemplatedDecl);
  // Register the template declaration
  return ActOnClassTemplateDecl(CTD);
}

DeclResult Sema::ActOnFunctionTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                                  Decl *TemplatedDecl) {
  auto *FTD = Context.create<FunctionTemplateDecl>(Loc, Name, TemplatedDecl);
  return DeclResult(FTD);
}

DeclResult Sema::ActOnVarTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                             Decl *TemplatedDecl) {
  auto *VTD = Context.create<VarTemplateDecl>(Loc, Name, TemplatedDecl);
  return DeclResult(VTD);
}

DeclResult Sema::ActOnTypeAliasTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                                   Decl *TemplatedDecl) {
  auto *TATD = Context.create<TypeAliasTemplateDecl>(Loc, Name, TemplatedDecl);
  return DeclResult(TATD);
}

DeclResult Sema::ActOnClassTemplateSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                            ClassTemplateDecl *PrimaryTemplate,
                                            llvm::ArrayRef<TemplateArgument> Args,
                                            bool IsExplicit) {
  auto *Spec = Context.create<ClassTemplateSpecializationDecl>(Loc, Name,
      PrimaryTemplate, Args, IsExplicit);
  PrimaryTemplate->addSpecialization(Spec);
  return DeclResult(Spec);
}

DeclResult Sema::ActOnVarTemplateSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                          QualType Type, VarTemplateDecl *PrimaryTemplate,
                                          llvm::ArrayRef<TemplateArgument> Args,
                                          Expr *Init, bool IsExplicit) {
  auto *Spec = Context.create<VarTemplateSpecializationDecl>(Loc, Name, Type,
      PrimaryTemplate, Args, Init, IsExplicit);
  PrimaryTemplate->addSpecialization(Spec);
  return DeclResult(Spec);
}

DeclResult Sema::ActOnClassTemplatePartialSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                                   ClassTemplateDecl *PrimaryTemplate,
                                                   llvm::ArrayRef<TemplateArgument> Args) {
  auto *PartialSpec = Context.create<ClassTemplatePartialSpecializationDecl>(Loc, Name,
      PrimaryTemplate, Args);
  PrimaryTemplate->addSpecialization(PartialSpec);
  return DeclResult(PartialSpec);
}

DeclResult Sema::ActOnVarTemplatePartialSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                                 QualType Type,
                                                 VarTemplateDecl *PrimaryTemplate,
                                                 llvm::ArrayRef<TemplateArgument> Args,
                                                 Expr *Init) {
  auto *PartialSpec = Context.create<VarTemplatePartialSpecializationDecl>(Loc, Name, Type,
      PrimaryTemplate, Args, Init);
  PrimaryTemplate->addSpecialization(PartialSpec);
  return DeclResult(PartialSpec);
}

DeclResult Sema::ActOnConceptDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                         Expr *Constraint, SourceLocation TemplateLoc,
                                         llvm::ArrayRef<NamedDecl *> TemplateParams) {
  // Create TemplateDecl for the concept
  auto *Template = Context.create<TemplateDecl>(TemplateLoc, Name, nullptr);
  auto *TPL = new TemplateParameterList(TemplateLoc, SourceLocation(),
                                         SourceLocation(), TemplateParams);
  Template->setTemplateParameterList(TPL);

  // Create ConceptDecl
  auto *Concept = Context.create<ConceptDecl>(Loc, Name, Constraint, Template);
  return DeclResult(Concept);
}

DeclResult Sema::ActOnFieldDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                       QualType Type, Expr *BitWidth,
                                       bool IsMutable, Expr *InClassInit,
                                       AccessSpecifier Access,
                                       class AttributeListDecl *Attrs) {
  auto *FD = Context.create<FieldDecl>(Loc, Name, Type, BitWidth, IsMutable,
                                        InClassInit, Access);
  
  // Set attributes if provided
  if (Attrs) {
    FD->setAttrs(Attrs);
  }
  
  registerDecl(FD);
  return DeclResult(FD);
}

DeclResult Sema::ActOnAccessSpecDeclFactory(SourceLocation Loc,
                                           AccessSpecifier Access,
                                           SourceLocation ColonLoc) {
  auto *ASD = Context.create<AccessSpecDecl>(Loc, Access, ColonLoc);
  return DeclResult(ASD);
}

//===----------------------------------------------------------------------===//
// Expression handling
//===----------------------------------------------------------------------===//
// Literal expressions (Phase 2A)
//===----------------------------------------------------------------------===//

ExprResult Sema::ActOnIntegerLiteral(SourceLocation Loc, llvm::APInt Value) {
  QualType Ty = Context.getIntType();
  auto *Lit = Context.create<IntegerLiteral>(Loc, Value, Ty);
  return ExprResult(Lit);
}

ExprResult Sema::ActOnFloatingLiteral(SourceLocation Loc, llvm::APFloat Value) {
  QualType Ty = Context.getDoubleType();
  auto *Lit = Context.create<FloatingLiteral>(Loc, Value, Ty);
  return ExprResult(Lit);
}

ExprResult Sema::ActOnStringLiteral(SourceLocation Loc, llvm::StringRef Text) {
  QualType Ty = Context.getCharType(); // const char[] approximation
  auto *Lit = Context.create<StringLiteral>(Loc, Text, Ty);
  return ExprResult(Lit);
}

ExprResult Sema::ActOnCharacterLiteral(SourceLocation Loc, uint32_t Value) {
  QualType Ty = Context.getCharType();
  auto *Lit = Context.create<CharacterLiteral>(Loc, Value, Ty);
  return ExprResult(Lit);
}

ExprResult Sema::ActOnCXXBoolLiteral(SourceLocation Loc, bool Value) {
  QualType Ty = Context.getBoolType();
  auto *Lit = Context.create<CXXBoolLiteral>(Loc, Value, Ty);
  return ExprResult(Lit);
}

ExprResult Sema::ActOnCXXNullPtrLiteral(SourceLocation Loc) {
  QualType Ty = Context.getNullPtrType();
  auto *Lit = Context.create<CXXNullPtrLiteral>(Loc, Ty);
  return ExprResult(Lit);
}

//===----------------------------------------------------------------------===//
// Expression factory methods (Phase 2C)
//===----------------------------------------------------------------------===//

ExprResult Sema::ActOnDeclRefExpr(SourceLocation Loc, ValueDecl *D,
                                   llvm::StringRef Name) {
  auto *DRE = Context.create<DeclRefExpr>(Loc, D, Name);
  // Mark the declaration as used (for warn_unused_variable/function diagnostics)
  if (D)
    D->setUsed();
  return ExprResult(DRE);
}

ExprResult Sema::ActOnUnaryExprOrTypeTraitExpr(SourceLocation Loc,
                                               UnaryExprOrTypeTrait Kind,
                                               QualType T) {
  auto *E = Context.create<UnaryExprOrTypeTraitExpr>(Loc, Kind, T);
  return ExprResult(E);
}

ExprResult Sema::ActOnUnaryExprOrTypeTraitExpr(SourceLocation Loc,
                                               UnaryExprOrTypeTrait Kind,
                                               Expr *Arg) {
  auto *E = Context.create<UnaryExprOrTypeTraitExpr>(Loc, Kind, Arg);
  return ExprResult(E);
}

ExprResult Sema::ActOnInitListExpr(SourceLocation LBraceLoc,
                                   llvm::ArrayRef<Expr *> Inits,
                                   SourceLocation RBraceLoc,
                                   QualType ExpectedType) {
  auto *ILE = Context.create<InitListExpr>(LBraceLoc, Inits, RBraceLoc);
  if (!ExpectedType.isNull()) {
    // Use the expected type from context (variable decl type, function param type, etc.)
    ILE->setType(ExpectedType);
    // Propagate types to nested InitListExpr children (Plan B safety net)
    propagateTypesToNestedInitLists(ILE, ExpectedType);
  } else if (!Inits.empty()) {
    // Fallback: if all initializers have the same type, derive array type.
    // For simple cases, use the first initializer's type.
    QualType FirstType = Inits[0]->getType();
    if (!FirstType.isNull())
      ILE->setType(FirstType); // Simplified; full impl would create ArrayType
  }
  return ExprResult(ILE);
}

void Sema::propagateTypesToNestedInitLists(InitListExpr *ILE,
                                            QualType ExpectedType) {
  if (ExpectedType.isNull())
    return;

  auto Inits = ILE->getInits();
  for (unsigned i = 0; i < Inits.size(); ++i) {
    // Only handle nested InitListExpr children
    auto *NestedILE = llvm::dyn_cast<InitListExpr>(Inits[i]);
    if (!NestedILE)
      continue;

    // Deduce the expected type for this child from the aggregate
    QualType ElemType = deduceElementTypeForInitList(ExpectedType, i);

    // Only set type if not already set by Parser-level propagation (Plan A)
    if (!ElemType.isNull() && NestedILE->getType().isNull()) {
      NestedILE->setType(ElemType);
    }

    // Recurse into nested InitListExpr
    if (!ElemType.isNull())
      propagateTypesToNestedInitLists(NestedILE, ElemType);
    else
      propagateTypesToNestedInitLists(NestedILE, NestedILE->getType());
  }
}

QualType Sema::deduceElementTypeForInitList(QualType AggrType,
                                             unsigned Index) {
  if (AggrType.isNull())
    return QualType();

  const Type *Ty = AggrType.getTypePtr();

  // Peel wrapper types
  while (Ty) {
    if (auto *ET = llvm::dyn_cast<ElaboratedType>(Ty)) {
      Ty = ET->getNamedType();
      continue;
    }
    if (auto *RT = llvm::dyn_cast<ReferenceType>(Ty)) {
      Ty = RT->getReferencedType();
      continue;
    }
    break;
  }

  if (!Ty)
    return QualType();

  // ArrayType → element type
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty))
    return QualType(AT->getElementType(), AggrType.getQualifiers());

  // TemplateSpecializationType → resolve to ClassTemplateSpecializationDecl
  if (auto *TST = llvm::dyn_cast<TemplateSpecializationType>(Ty)) {
    auto *TD = TST->getTemplateDecl();
    if (!TD)
      return QualType();
    
    // For class templates, the templated decl should be a CXXRecordDecl
    auto *TemplatedDecl = TD->getTemplatedDecl();
    if (!TemplatedDecl)
      return QualType();
    
    // The specialized record should have fields
    if (auto *CTSD = llvm::dyn_cast<ClassTemplateSpecializationDecl>(TemplatedDecl)) {
      auto Fields = CTSD->fields();
      if (Index < Fields.size())
        return Fields[Index]->getType();
    } else if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(TemplatedDecl)) {
      auto Fields = RD->fields();
      if (Index < Fields.size())
        return Fields[Index]->getType();
    }
    return QualType();
  }

  // RecordType → field type by index
  if (auto *RT = llvm::dyn_cast<RecordType>(Ty)) {
    auto *RD = RT->getDecl();
    if (!RD)
      return QualType();
    
    // Handle ClassTemplateSpecialization - get fields from the specialization
    auto Fields = RD->fields();
    if (Index < Fields.size())
      return Fields[Index]->getType();
    return QualType();
  }

  return QualType();
}

ExprResult Sema::ActOnDesignatedInitExpr(
    SourceLocation DotLoc,
    llvm::ArrayRef<DesignatedInitExpr::Designator> Designators,
    Expr *Init) {
  auto *DIE = Context.create<DesignatedInitExpr>(DotLoc, Designators, Init);
  // The type of a designated initializer is the type of its init expression.
  // For a more precise type (e.g., struct field type), context from the
  // aggregate being initialized would be needed — not available at this point.
  if (Init && !Init->getType().isNull())
    DIE->setType(Init->getType());
  return ExprResult(DIE);
}

ExprResult Sema::ActOnTemplateSpecializationExpr(
    SourceLocation Loc, llvm::StringRef Name,
    llvm::ArrayRef<TemplateArgument> Args, ValueDecl *VD) {
  auto *TSE = Context.create<TemplateSpecializationExpr>(Loc, Name, Args, VD);
  // Try to get type from the resolved template declaration.
  // For function templates, this gives the return type; for variable templates, the variable type.
  if (VD && !VD->getType().isNull())
    TSE->setType(VD->getType());
  return ExprResult(TSE);
}

ExprResult Sema::ActOnMemberExprDirect(SourceLocation OpLoc, Expr *Base,
                                       ValueDecl *MemberDecl, bool IsArrow) {
  auto *ME = Context.create<MemberExpr>(OpLoc, Base, MemberDecl, IsArrow);
  return ExprResult(ME);
}

ExprResult Sema::ActOnCXXConstructExpr(SourceLocation Loc,
                                       QualType ConstructedType,
                                       llvm::ArrayRef<Expr *> Args) {
  auto *CE = Context.create<CXXConstructExpr>(Loc, Args);
  if (!ConstructedType.isNull())
    CE->setType(ConstructedType);
  return ExprResult(CE);
}

ExprResult Sema::ActOnCXXNewExprFactory(SourceLocation NewLoc, Expr *ArraySize,
                                         Expr *Initializer, QualType Type) {
  auto *NE = Context.create<CXXNewExpr>(NewLoc, ArraySize, Initializer, Type);
  if (!Type.isNull()) {
    auto *PtrTy = Context.getPointerType(Type.getTypePtr());
    NE->setType(QualType(PtrTy, Qualifier::None));
  }
  return ExprResult(NE);
}

ExprResult Sema::ActOnCXXDeleteExprFactory(SourceLocation DeleteLoc,
                                           Expr *Argument, bool IsArrayDelete,
                                           QualType AllocatedType) {
  auto *DE = Context.create<CXXDeleteExpr>(DeleteLoc, Argument, IsArrayDelete,
                                            AllocatedType);
  return ExprResult(DE);
}

ExprResult Sema::ActOnCXXThisExpr(SourceLocation Loc) {
  auto *TE = Context.create<CXXThisExpr>(Loc);
  if (CurContext && CurContext->isCXXRecord()) {
    // CurContext is the DeclContext base of a CXXRecordDecl.
    // We need the CXXRecordDecl pointer. Since CXXRecordDecl multiply-inherits
    // from both RecordDecl (via NamedDecl->Decl->ASTNode) and DeclContext,
    // we static_cast the DeclContext* back.
    auto *RD = static_cast<CXXRecordDecl *>(CurContext);
    auto *RecTy = Context.getPointerType(
        static_cast<Type *>(new RecordType(RD)));
    TE->setType(QualType(RecTy, Qualifier::None));
  }
  return ExprResult(TE);
}

ExprResult Sema::ActOnCXXThrowExpr(SourceLocation Loc, Expr *Operand) {
  auto *TE = Context.create<CXXThrowExpr>(Loc, Operand);
  auto *VoidType = Context.getBuiltinType(BuiltinKind::Void);
  TE->setType(QualType(VoidType, Qualifier::None));
  return ExprResult(TE);
}

ExprResult Sema::ActOnCXXNamedCastExpr(SourceLocation CastLoc, Expr *SubExpr,
                                       llvm::StringRef CastKind) {
  if (CastKind == "static_cast")
    return ExprResult(Context.create<CXXStaticCastExpr>(CastLoc, SubExpr));
  if (CastKind == "const_cast")
    return ExprResult(Context.create<CXXConstCastExpr>(CastLoc, SubExpr));
  if (CastKind == "reinterpret_cast")
    return ExprResult(Context.create<CXXReinterpretCastExpr>(CastLoc, SubExpr));
  return ExprResult(Context.create<CXXStaticCastExpr>(CastLoc, SubExpr));
}

ExprResult Sema::ActOnCXXNamedCastExprWithType(SourceLocation CastLoc,
                                               Expr *SubExpr,
                                               QualType CastType,
                                               llvm::StringRef CastKind) {
  if (CastKind == "dynamic_cast") {
    auto *E = Context.create<CXXDynamicCastExpr>(CastLoc, SubExpr, CastType);
    E->setType(CastType);
    return ExprResult(E);
  }
  if (CastKind == "static_cast") {
    auto *E = Context.create<CXXStaticCastExpr>(CastLoc, SubExpr);
    E->setType(CastType);
    return ExprResult(E);
  }
  if (CastKind == "const_cast") {
    auto *E = Context.create<CXXConstCastExpr>(CastLoc, SubExpr);
    E->setType(CastType);
    return ExprResult(E);
  }
  if (CastKind == "reinterpret_cast") {
    auto *E = Context.create<CXXReinterpretCastExpr>(CastLoc, SubExpr);
    E->setType(CastType);
    return ExprResult(E);
  }
  auto *E = Context.create<CXXStaticCastExpr>(CastLoc, SubExpr);
  E->setType(CastType);
  return ExprResult(E);
}

ExprResult Sema::ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack,
                                       Expr *Index) {
  auto *PIE = Context.create<PackIndexingExpr>(Loc, Pack, Index);
  // TODO: Pack indexing requires template pack expansion to determine type.
  // The result type depends on the Nth element of the expanded parameter pack.
  // Will be implemented when full variadic template support is available.
  return ExprResult(PIE);
}

ExprResult Sema::ActOnReflexprExpr(SourceLocation Loc, Expr *Arg) {
  SemaReflection Refl(*this);
  if (!Refl.ValidateReflexprExpr(Arg, Loc))
    return ExprResult(nullptr);

  auto *RE = Context.create<ReflexprExpr>(Loc, Arg, Context.getMetaInfoType());
  return ExprResult(RE);
}

ExprResult Sema::ActOnReflexprTypeExpr(SourceLocation Loc, QualType T) {
  SemaReflection Refl(*this);
  if (!Refl.ValidateReflexprType(T, Loc))
    return ExprResult(nullptr);

  auto *RE = Context.create<ReflexprExpr>(Loc, T, Context.getMetaInfoType());
  return ExprResult(RE);
}

ExprResult Sema::ActOnReflectTypeBuiltin(SourceLocation Loc, Expr *E) {
  SemaReflection Refl(*this);
  return Refl.ActOnReflectType(Loc, E);
}

ExprResult Sema::ActOnReflectMembersBuiltin(SourceLocation Loc, QualType T) {
  SemaReflection Refl(*this);
  return Refl.ActOnReflectMembers(Loc, T);
}

ExprResult Sema::ActOnLambdaExpr(SourceLocation Loc,
                                 llvm::SmallVectorImpl<LambdaCapture> &Captures,
                                 llvm::ArrayRef<ParmVarDecl *> Params,
                                 Stmt *Body, bool IsMutable,
                                 QualType ReturnType,
                                 SourceLocation LBraceLoc,
                                 SourceLocation RBraceLoc,
                                 TemplateParameterList *TemplateParams,
                                 AttributeListDecl *Attrs) {
  // P7.1.5: Infer return type if not specified
  if (ReturnType.isNull() && Body) {
    // Simple return type deduction: look for return statements in body
    // For now, just check if there's a return with an integer literal
    // TODO: Implement proper return type deduction
    if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
      for (auto *S : CS->getBody()) {
        if (auto *RS = llvm::dyn_cast<ReturnStmt>(S)) {
          if (auto *RetExpr = RS->getRetValue()) {
            ReturnType = RetExpr->getType();
            break; // Use the first return statement's type
          }
        }
      }
    }
    // If still null, default to void
    if (ReturnType.isNull()) {
      ReturnType = Context.getVoidType();
    }
  }
  
  // P7.1.5: Create closure class for lambda
  static unsigned LambdaCounter = 0;
  std::string ClosureName = "__lambda_" + std::to_string(++LambdaCounter);
  
  // 1. Create the closure class (anonymous class)
  auto *ClosureClass = Context.create<CXXRecordDecl>(Loc, ClosureName, TagDecl::TK_class);
  ClosureClass->setIsLambda(true);
  
  // P7.1.5 Fix: Mark closure class as complete definition
  // Lambda closure classes are always complete (they have all members defined)
  ClosureClass->setCompleteDefinition(true);
  
  // 2. Add capture members to the closure class
  for (auto &Capture : Captures) {
    // P7.1.5 Phase 1: Infer capture type from context
    QualType CaptureType;
    
    if (Capture.Kind == LambdaCapture::InitCopy && Capture.InitExpr) {
      // Init capture: [x = expr] - type from initialization expression
      CaptureType = Capture.InitExpr->getType();
    } else {
      // Named capture: [x] or [&x] - lookup in current scope
      NamedDecl *CapturedDecl = LookupName(Capture.Name);
      Capture.CapturedDecl = CapturedDecl;  // Store for CodeGen
      if (CapturedDecl) {
        if (auto *VD = llvm::dyn_cast<VarDecl>(CapturedDecl)) {
          CaptureType = VD->getType();
          // If captured by reference, keep as reference type
          // If captured by copy, use the value type
          if (Capture.Kind == LambdaCapture::ByCopy) {
            // For by-copy, we might want to strip references
            // For now, keep as-is (TODO: implement proper decay)
          }
        } else {
          // Fallback: not a variable, use int
          CaptureType = Context.getIntType();
        }
      } else {
        // Variable not found in scope, use int as fallback
        CaptureType = Context.getIntType();
      }
    }
    
    auto *Field = Context.create<FieldDecl>(Capture.Loc, Capture.Name, CaptureType);
    ClosureClass->addMember(Field);
    ClosureClass->addField(Field);  // Also add to Fields array for CodeGen
  }
  
  // 3. Create operator() method
  // Use the ReturnType from lambda expression (or void if not specified)
  QualType RetTy = ReturnType.isNull() ? Context.getVoidType() : ReturnType;
  QualType OpCallType = QualType(Context.getFunctionType(RetTy.getTypePtr(),
                                                          llvm::ArrayRef<const Type *>(), false));
  
  auto *CallOp = Context.create<CXXMethodDecl>(Loc, "operator()", OpCallType,
                                                Params, ClosureClass,
                                                Body /* Lambda body */,
                                                false /* isStatic */,
                                                !IsMutable /* isConst */,
                                                false /* isVolatile */,
                                                false /* isVirtual */);
  CallOp->setParent(ClosureClass);
  ClosureClass->addMethod(CallOp);
  
  // 4. Register closure class in current scope
  if (CurContext) {
    CurContext->addDecl(ClosureClass);
  }
  
  // 5. Create LambdaExpr with closure class
  auto *LE = Context.create<LambdaExpr>(Loc, ClosureClass, Captures, Params, Body,
                                         IsMutable, ReturnType, LBraceLoc, RBraceLoc,
                                         TemplateParams, Attrs);
  
  // Set lambda expression type to the closure class type
  QualType LambdaType = Context.getRecordType(ClosureClass);
  LE->setType(LambdaType);
  
  // P7.1.5: Build captured variable to field index mapping
  unsigned FieldIndex = 0;
  for (const auto &Capture : Captures) {
    if (Capture.CapturedDecl) {
      if (auto *VD = llvm::dyn_cast<VarDecl>(Capture.CapturedDecl)) {
        LE->setCapturedVar(VD, FieldIndex);
      }
    }
    ++FieldIndex;
  }
  
  return ExprResult(LE);
}

ExprResult Sema::ActOnCXXFoldExpr(SourceLocation Loc, Expr *LHS, Expr *RHS,
                                  Expr *Pattern, BinaryOpKind Op,
                                  bool IsRightFold) {
  auto *FE = Context.create<CXXFoldExpr>(Loc, LHS, RHS, Pattern, Op,
                                          IsRightFold);
  // Fold expression type: the fold operator preserves the element type.
  // e.g., (args + ...) has the same type as each arg.
  if (Pattern && !Pattern->getType().isNull())
    FE->setType(Pattern->getType());
  return ExprResult(FE);
}

ExprResult Sema::ActOnRequiresExpr(SourceLocation Loc,
                                   llvm::ArrayRef<Requirement *> Requirements,
                                   SourceLocation RequiresLoc,
                                   SourceLocation RBraceLoc) {
  auto *RE = Context.create<RequiresExpr>(Loc, Requirements, RequiresLoc,
                                           RBraceLoc);
  return ExprResult(RE);
}

ExprResult Sema::ActOnCallExpr(Expr *Fn, llvm::ArrayRef<Expr *> Args,
                                SourceLocation LParenLoc,
                                SourceLocation RParenLoc) {
  if (!Fn)
    return ExprResult::getInvalid();

  // P7.1.5 Phase 2: Handle lambda expression calls
  // Case 1: Direct lambda expression: [](){}()
  if (auto *LE = llvm::dyn_cast<LambdaExpr>(Fn)) {
    // Lambda call: get the operator() from closure class
    auto *ClosureClass = LE->getClosureClass();
    if (!ClosureClass) {
      // Fallback: create a basic CallExpr
      auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
      return ExprResult(CE);
    }
    
    // Find operator() method
    CXXMethodDecl *CallOp = nullptr;
    for (auto *Method : ClosureClass->methods()) {
      if (Method->getName() == "operator()") {
        CallOp = Method;
        break;
      }
    }
    
    if (!CallOp) {
      // Fallback: create a basic CallExpr
      auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
      return ExprResult(CE);
    }
    
    // Create CallExpr with operator() as callee
    auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
    
    // Set the return type from operator()
    QualType FuncType = CallOp->getType();
    if (FuncType->isFunctionType()) {
      auto *FT = static_cast<const FunctionType *>(FuncType.getTypePtr());
      CE->setType(QualType(FT->getReturnType(), Qualifier::None));
    } else {
      CE->setType(FuncType);
    }
    
    return ExprResult(CE);
  }
  
  // Case 2: Variable holding a lambda: auto lambda = [](){}; lambda()
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
    Decl *D = DRE->getDecl();
    // If D is nullptr, this is an undeclared identifier - skip lambda check
    if (!D) {
      // Continue to regular function call handling below
    } else if (auto *VD = llvm::dyn_cast<VarDecl>(D)) {
      QualType VarType = VD->getType();
      if (VarType->isRecordType()) {
        auto *RT = static_cast<const RecordType *>(VarType.getTypePtr());
        auto *RD = RT->getDecl();
        if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
          if (CXXRD->isLambda()) {
            // This is a lambda variable, find operator()
            CXXMethodDecl *CallOp = nullptr;
            for (auto *Method : CXXRD->methods()) {
              if (Method->getName() == "operator()") {
                CallOp = Method;
                break;
              }
            }
            
            if (CallOp) {
              // Create CallExpr with the variable as callee
              auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
              
              // Set return type
              QualType FuncType = CallOp->getType();
              if (FuncType->isFunctionType()) {
                auto *FT = static_cast<const FunctionType *>(FuncType.getTypePtr());
                CE->setType(QualType(FT->getReturnType(), Qualifier::None));
              } else {
                CE->setType(FuncType);
              }
              
              return ExprResult(CE);
            }
          }
        }
      }
    } // end else if VD
  }

  // Resolve the callee
  FunctionDecl *FD = nullptr;

  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
    Decl *D = DRE->getDecl();
    llvm::StringRef Name = DRE->getName();  // Get name from DeclRefExpr
    
    if (!D) {
      // D is nullptr - this happens for FunctionTemplateDecl
      // Try to lookup template by name
      if (!Name.empty()) {
        if (auto *FTD = Symbols.lookupTemplate(Name)) {
          if (auto *FuncFTD = llvm::dyn_cast_or_null<FunctionTemplateDecl>(FTD)) {
            FD = DeduceAndInstantiateFunctionTemplate(FuncFTD, Args, LParenLoc);
          }
        }
      }
      if (!FD) {
        // Still no FD, create CallExpr for error recovery
        auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
        return ExprResult(CE);
      }
    } else if (auto *FunD = llvm::dyn_cast<FunctionDecl>(D)) {
      FD = FunD;
    }
    // Handle function template: deduce arguments and instantiate
    if (!FD) {
      if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
        FD = DeduceAndInstantiateFunctionTemplate(FTD, Args, LParenLoc);
      }
    }
    // Also check if the DeclRefExpr refers to a TemplateDecl by name
    if (!FD && !Name.empty()) {
      if (auto *FTD = Symbols.lookupTemplate(Name)) {
        if (auto *FuncFTD = llvm::dyn_cast_or_null<FunctionTemplateDecl>(FTD)) {
          FD = DeduceAndInstantiateFunctionTemplate(FuncFTD, Args, LParenLoc);
        }
      }
    }
  }

  // If not a direct function reference, try overload resolution
  if (!FD) {
    if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
      llvm::StringRef Name = DRE->getName();
      if (Name.empty()) {
        auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
        return ExprResult(CE);
      }
      auto Decls = Symbols.lookup(Name);
      LookupResult LR;
      for (auto *D : Decls)
        LR.addDecl(D);
      FD = ResolveOverload(Name, Args, LR);
    }
  }

  if (!FD) {
    // During incremental migration, fall back to creating a CallExpr
    // without full resolution.
    auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
    return ExprResult(CE);
  }

  // Type-check the call arguments
  if (!TC.CheckCall(FD, Args, LParenLoc))
    return ExprResult::getInvalid();

  // Create the CallExpr
  auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
  // Set return type from resolved FunctionDecl
  if (FD) {
    QualType FnType = FD->getType();
    if (auto *FT = llvm::dyn_cast<FunctionType>(FnType.getTypePtr())) {
      CE->setType(QualType(FT->getReturnType(), Qualifier::None));
    }
  }
  return ExprResult(CE);
}

ExprResult Sema::ActOnMemberExpr(Expr *Base, llvm::StringRef Member,
                                  SourceLocation MemberLoc, bool IsArrow) {
  if (!Base)
    return ExprResult::getInvalid();

  QualType BaseType = Base->getType();

  // Defensive: during incremental migration, base type may not be set yet.
  // Skip member lookup and create MemberExpr with null MemberDecl.
  if (BaseType.isNull()) {
    auto *ME = Context.create<MemberExpr>(MemberLoc, Base, nullptr, IsArrow);
    return ExprResult(ME);
  }

  // For arrow (->), the base must be a pointer type
  if (IsArrow) {
    if (auto *PT = llvm::dyn_cast<PointerType>(BaseType.getTypePtr())) {
      BaseType = PT->getPointeeType();
    } else {
      Diags.report(MemberLoc, DiagID::err_member_access_type_invalid,
                   BaseType.isNull() ? "<unknown>" : BaseType.getAsString());
      return ExprResult::getInvalid();
    }
  }

  // Look up the member in the record type
  auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr());
  if (!RT) {
    Diags.report(MemberLoc, DiagID::err_member_access_type_invalid,
                 BaseType.isNull() ? "<unknown>" : BaseType.getAsString());
    return ExprResult::getInvalid();
  }

  auto *RD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!RD)
    return ExprResult::getInvalid();

  // Find the named member (search current class and base classes)
  ValueDecl *MemberDecl = nullptr;
  
  // 1. Search in current class
  for (auto *D : RD->members()) {
    if (auto *ND = llvm::dyn_cast<NamedDecl>(D)) {
      if (ND->getName() == Member) {
        MemberDecl = llvm::dyn_cast<ValueDecl>(D);
        break;
      }
    }
  }
  
  // 2. Search in base classes (if not found in current class)
  if (!MemberDecl) {
    for (const auto &Base : RD->bases()) {
      QualType BaseType = Base.getType();
      if (auto *BaseRT = llvm::dyn_cast_or_null<RecordType>(BaseType.getTypePtr())) {
        if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(BaseRT->getDecl())) {
          for (auto *D : BaseRD->members()) {
            if (auto *ND = llvm::dyn_cast<NamedDecl>(D)) {
              if (ND->getName() == Member) {
                MemberDecl = llvm::dyn_cast<ValueDecl>(D);
                break;
              }
            }
          }
          if (MemberDecl) break;
        }
      }
    }
  }

  if (!MemberDecl) {
    Diags.report(MemberLoc, DiagID::err_undeclared_identifier, Member);
    return ExprResult::getInvalid();
  }

  // Check access control
  if (auto *ND = llvm::dyn_cast<NamedDecl>(MemberDecl)) {
    AccessSpecifier Access = AccessControl::getEffectiveAccess(ND);
    if (!AccessControl::CheckMemberAccess(ND, Access, RD, CurContext,
                                          MemberLoc, Diags))
      return ExprResult::getInvalid();
  }

  auto *ME = Context.create<MemberExpr>(MemberLoc, Base, MemberDecl, IsArrow);
  if (MemberDecl && !MemberDecl->getType().isNull())
    ME->setType(MemberDecl->getType());
  return ExprResult(ME);
}

ExprResult Sema::ActOnBinaryOperator(BinaryOpKind Op, Expr *LHS, Expr *RHS,
                                      SourceLocation OpLoc) {
  if (!LHS || !RHS)
    return ExprResult::getInvalid();

  QualType LHSType = LHS->getType();
  QualType RHSType = RHS->getType();

  // Check for void operands — not allowed in arithmetic/comparison/etc.
  if (!LHSType.isNull() && LHSType->isVoidType()) {
    Diags.report(OpLoc, DiagID::err_void_expr_not_allowed);
    return ExprResult::getInvalid();
  }
  if (!RHSType.isNull() && RHSType->isVoidType()) {
    Diags.report(OpLoc, DiagID::err_void_expr_not_allowed);
    return ExprResult::getInvalid();
  }

  // Compute the result type via TypeCheck
  QualType ResultType;
  if (!LHSType.isNull() && !RHSType.isNull()) {
    ResultType = TC.getBinaryOperatorResultType(Op, LHSType, RHSType);
    if (ResultType.isNull()) {
      Diags.report(OpLoc, DiagID::err_bin_op_type_invalid,
                   LHSType.getAsString(), RHSType.getAsString());
      return ExprResult::getInvalid();
    }
  }

  // Create the BinaryOperator node and set its result type
  auto *BO = Context.create<BinaryOperator>(OpLoc, LHS, RHS, Op);
  if (!ResultType.isNull())
    BO->setType(ResultType);
  return ExprResult(BO);
}

ExprResult Sema::ActOnUnaryOperator(UnaryOpKind Op, Expr *Operand,
                                     SourceLocation OpLoc) {
  if (!Operand)
    return ExprResult::getInvalid();

  QualType OperandType = Operand->getType();

  // Check for void operand on operators that require a value
  // (Plus, Minus, Not, PreInc, PreDec, PostInc, PostDec, Deref)
  // AddrOf on void is technically invalid but we let it through for error recovery.
  if (!OperandType.isNull() && OperandType->isVoidType() &&
      Op != UnaryOpKind::AddrOf) {
    Diags.report(OpLoc, DiagID::err_void_expr_not_allowed);
    return ExprResult::getInvalid();
  }

  // Compute the result type via TypeCheck
  QualType ResultType;
  if (!OperandType.isNull()) {
    ResultType = TC.getUnaryOperatorResultType(Op, OperandType);
    if (ResultType.isNull()) {
      Diags.report(OpLoc, DiagID::err_bin_op_type_invalid,
                   OperandType.getAsString(), OperandType.getAsString());
      return ExprResult::getInvalid();
    }
  }

  // Create the UnaryOperator node and set its result type
  auto *UO = Context.create<UnaryOperator>(OpLoc, Operand, Op);
  if (!ResultType.isNull())
    UO->setType(ResultType);
  return ExprResult(UO);
}

ExprResult Sema::ActOnCastExpr(QualType TargetType, Expr *E,
                                SourceLocation LParenLoc,
                                SourceLocation RParenLoc) {
  if (!E || TargetType.isNull())
    return ExprResult::getInvalid();

  // Defensive: skip type compatibility check if expression type not set yet
  if (!E->getType().isNull() && !TC.isTypeCompatible(E->getType(), TargetType)) {
    Diags.report(LParenLoc, DiagID::err_cast_incompatible,
                 E->getType().getAsString(), TargetType.getAsString());
    return ExprResult::getInvalid();
  }

  auto *CE = Context.create<CStyleCastExpr>(LParenLoc, E);
  CE->setType(TargetType);
  return ExprResult(CE);
}

ExprResult Sema::ActOnArraySubscriptExpr(Expr *Base,
                                          llvm::ArrayRef<Expr *> Indices,
                                          SourceLocation LLoc,
                                          SourceLocation RLoc) {
  if (!Base)
    return ExprResult::getInvalid();

  QualType BaseType = Base->getType();

  // Defensive: skip type check if type not set yet
  if (!BaseType.isNull()) {
    const Type *BaseTy = BaseType.getTypePtr();
    if (!BaseTy->isPointerType() && !BaseTy->isArrayType()) {
      Diags.report(LLoc, DiagID::err_subscript_not_pointer,
                   BaseType.getAsString());
      return ExprResult::getInvalid();
    }
  }

  for (auto *Idx : Indices) {
    if (!Idx)
      continue;
    if (!Idx->getType().isNull() && !Idx->getType()->isIntegerType()) {
      Diags.report(LLoc, DiagID::err_bin_op_type_invalid,
                   BaseType.getAsString(), Idx->getType().getAsString());
      return ExprResult::getInvalid();
    }
  }

  auto *ASE = Context.create<ArraySubscriptExpr>(LLoc, Base, Indices);
  if (!BaseType.isNull()) {
    const Type *BaseTy = BaseType.getTypePtr();
    if (auto *PT = llvm::dyn_cast<PointerType>(BaseTy)) {
      ASE->setType(PT->getPointeeType());
    } else if (auto *AT = llvm::dyn_cast<ArrayType>(BaseTy)) {
      ASE->setType(QualType(AT->getElementType(), Qualifier::None));
    }
  }
  return ExprResult(ASE);
}

ExprResult Sema::ActOnConditionalExpr(Expr *Cond, Expr *Then, Expr *Else,
                                       SourceLocation QuestionLoc,
                                       SourceLocation ColonLoc) {
  if (!Cond || !Then || !Else)
    return ExprResult::getInvalid();

  // Check for void branches — not allowed in conditional expression
  if (!Then->getType().isNull() && Then->getType()->isVoidType()) {
    Diags.report(ColonLoc, DiagID::err_void_expr_not_allowed);
    return ExprResult::getInvalid();
  }
  if (!Else->getType().isNull() && Else->getType()->isVoidType()) {
    Diags.report(ColonLoc, DiagID::err_void_expr_not_allowed);
    return ExprResult::getInvalid();
  }

  // Defensive: skip condition check if type not set yet
  if (!Cond->getType().isNull() && !TC.CheckCondition(Cond, QuestionLoc))
    return ExprResult::getInvalid();

  // Defensive: skip common type computation if types not set yet
  if (!Then->getType().isNull() && !Else->getType().isNull()) {
    QualType ResultType = TC.getCommonType(Then->getType(), Else->getType());
    if (ResultType.isNull()) {
      Diags.report(ColonLoc, DiagID::err_bin_op_type_invalid,
                   Then->getType().getAsString(), Else->getType().getAsString());
      return ExprResult::getInvalid();
    }
  }

  auto *CO = Context.create<ConditionalOperator>(QuestionLoc, Cond, Then, Else);
  return ExprResult(CO);
}

//===----------------------------------------------------------------------===//
// Statement handling
//===----------------------------------------------------------------------===//

StmtResult Sema::ActOnReturnStmt(Expr *RetVal, SourceLocation ReturnLoc) {
  // Check return value type against current function return type
  if (CurFunction) {
    QualType FnType = CurFunction->getType();
    if (auto *FT = llvm::dyn_cast<FunctionType>(FnType.getTypePtr())) {
      QualType RetType = QualType(FT->getReturnType(), Qualifier::None);
      
      // Skip check if return type is AutoType (will be deduced during instantiation)
      if (RetType.getTypePtr() && RetType->getTypeClass() != TypeClass::Auto) {
        if (!TC.CheckReturn(RetVal, RetType, ReturnLoc))
          return StmtResult::getInvalid();
      }
    }
  }

  auto *RS = Context.create<ReturnStmt>(ReturnLoc, RetVal);
  return StmtResult(RS);
}

StmtResult Sema::ActOnIfStmt(Expr *Cond, Stmt *Then, Stmt *Else,
                              SourceLocation IfLoc,
                              VarDecl *CondVar, bool IsConsteval,
                              bool IsNegated) {
  // Defensive: skip condition check if type not yet set (incremental migration)
  if (!IsConsteval && Cond && !Cond->getType().isNull()
      && !TC.CheckCondition(Cond, IfLoc))
    return StmtResult::getInvalid();

  auto *IS = Context.create<IfStmt>(IfLoc, Cond, Then, Else, CondVar,
                                     IsConsteval, IsNegated);
  return StmtResult(IS);
}

/// P0963R3: ActOnIfStmt with structured bindings
/// Syntax: if (auto [x, y] = expr) { ... }
StmtResult Sema::ActOnIfStmtWithBindings(Expr *Cond, Stmt *Then, Stmt *Else,
                                         SourceLocation IfLoc,
                                         llvm::ArrayRef<class BindingDecl *> Bindings,
                                         bool IsConsteval, bool IsNegated) {
  // Check condition type
  if (!IsConsteval && Cond && !Cond->getType().isNull()
      && !TC.CheckCondition(Cond, IfLoc))
    return StmtResult::getInvalid();
  
  // Validate bindings
  if (Bindings.empty()) {
    Diags.report(IfLoc, DiagID::err_expected_expression);
    return StmtResult::getInvalid();
  }
  
  // Create IfStmt with structured bindings
  auto *IS = Context.create<IfStmt>(IfLoc, Cond, Then, Else, Bindings,
                                     IsConsteval, IsNegated);
  return StmtResult(IS);
}

StmtResult Sema::ActOnWhileStmt(Expr *Cond, Stmt *Body,
                                 SourceLocation WhileLoc,
                                 VarDecl *CondVar) {
  ++BreakScopeDepth;
  ++ContinueScopeDepth;
  if (Cond && !Cond->getType().isNull() && !TC.CheckCondition(Cond, WhileLoc)) {
    --BreakScopeDepth;
    --ContinueScopeDepth;
    return StmtResult::getInvalid();
  }

  auto *WS = Context.create<WhileStmt>(WhileLoc, Cond, Body, CondVar);
  --BreakScopeDepth;
  --ContinueScopeDepth;
  return StmtResult(WS);
}

StmtResult Sema::ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                               SourceLocation ForLoc) {
  ++BreakScopeDepth;
  ++ContinueScopeDepth;
  if (Cond && !Cond->getType().isNull() && !TC.CheckCondition(Cond, ForLoc)) {
    --BreakScopeDepth;
    --ContinueScopeDepth;
    return StmtResult::getInvalid();
  }

  auto *FS = Context.create<ForStmt>(ForLoc, Init, Cond, Inc, Body);
  --BreakScopeDepth;
  --ContinueScopeDepth;
  return StmtResult(FS);
}

StmtResult Sema::ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc) {
  ++BreakScopeDepth;
  ++ContinueScopeDepth;
  if (Cond && !Cond->getType().isNull() && !TC.CheckCondition(Cond, DoLoc)) {
    --BreakScopeDepth;
    --ContinueScopeDepth;
    return StmtResult::getInvalid();
  }

  auto *DS = Context.create<DoStmt>(DoLoc, Body, Cond);
  --BreakScopeDepth;
  --ContinueScopeDepth;
  return StmtResult(DS);
}

StmtResult Sema::ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                                  SourceLocation SwitchLoc,
                                  VarDecl *CondVar) {
  ++BreakScopeDepth;
  ++SwitchScopeDepth;
  // Defensive: skip type check if type not yet set (incremental migration)
  if (Cond && !Cond->getType().isNull() && !Cond->getType()->isIntegerType()) {
    Diags.report(SwitchLoc, DiagID::err_condition_not_bool);
    --BreakScopeDepth;
    --SwitchScopeDepth;
    return StmtResult::getInvalid();
  }

  auto *SS = Context.create<SwitchStmt>(SwitchLoc, Cond, Body, CondVar);
  --BreakScopeDepth;
  --SwitchScopeDepth;
  return StmtResult(SS);
}

StmtResult Sema::ActOnCaseStmt(Expr *Val, Expr *RHS, Stmt *Body,
                               SourceLocation CaseLoc) {
  if (SwitchScopeDepth == 0) {
    Diags.report(CaseLoc, DiagID::err_case_not_in_switch);
    // Fall through: still create the node for error recovery
  }
  if (Val && !Val->getType().isNull() && !TC.CheckCaseExpression(Val, CaseLoc))
    return StmtResult::getInvalid();

  // Validate GNU case range RHS
  if (RHS && !RHS->getType().isNull() && !TC.CheckCaseExpression(RHS, CaseLoc))
    return StmtResult::getInvalid();

  auto *CS = Context.create<CaseStmt>(CaseLoc, Val, RHS, Body);
  return StmtResult(CS);
}

StmtResult Sema::ActOnDefaultStmt(Stmt *Body, SourceLocation DefaultLoc) {
  if (SwitchScopeDepth == 0) {
    Diags.report(DefaultLoc, DiagID::err_case_not_in_switch);
    // Fall through: still create the node for error recovery
  }
  auto *DS = Context.create<DefaultStmt>(DefaultLoc, Body);
  return StmtResult(DS);
}

StmtResult Sema::ActOnBreakStmt(SourceLocation BreakLoc) {
  if (BreakScopeDepth == 0) {
    Diags.report(BreakLoc, DiagID::err_break_outside_loop);
    // Fall through: still create the node for error recovery
  }
  auto *BS = Context.create<BreakStmt>(BreakLoc);
  return StmtResult(BS);
}

StmtResult Sema::ActOnContinueStmt(SourceLocation ContinueLoc) {
  if (ContinueScopeDepth == 0) {
    Diags.report(ContinueLoc, DiagID::err_continue_outside_loop);
    // Fall through: still create the node for error recovery
  }
  auto *CS = Context.create<ContinueStmt>(ContinueLoc);
  return StmtResult(CS);
}

StmtResult Sema::ActOnGotoStmt(llvm::StringRef Label, SourceLocation GotoLoc) {
  // TODO: resolve label to an actual LabelDecl
  auto *LD = Context.create<LabelDecl>(GotoLoc, Label);
  auto *GS = Context.create<GotoStmt>(GotoLoc, LD);
  return StmtResult(GS);
}

StmtResult Sema::ActOnCompoundStmt(llvm::ArrayRef<Stmt *> Stmts,
                                    SourceLocation LBraceLoc,
                                    SourceLocation RBraceLoc) {
  auto *CS = Context.create<CompoundStmt>(LBraceLoc, Stmts);
  return StmtResult(CS);
}

StmtResult Sema::ActOnNullStmt(SourceLocation Loc) {
  auto *NS = Context.create<NullStmt>(Loc);
  return StmtResult(NS);
}

//===----------------------------------------------------------------------===//
// Label and expression statements (Phase 2B)
//===----------------------------------------------------------------------===//

StmtResult Sema::ActOnExprStmt(SourceLocation Loc, Expr *E) {
  auto *ES = Context.create<ExprStmt>(Loc, E);
  return StmtResult(ES);
}

StmtResult Sema::ActOnLabelStmt(SourceLocation Loc, llvm::StringRef LabelName,
                                 Stmt *SubStmt) {
  auto *LD = Context.create<LabelDecl>(Loc, LabelName);
  registerDecl(LD);
  auto *LS = Context.create<LabelStmt>(Loc, LD, SubStmt);
  return StmtResult(LS);
}

//===----------------------------------------------------------------------===//
// C++ statement extensions (Phase 2B)
//===----------------------------------------------------------------------===//

StmtResult Sema::ActOnCXXForRangeStmt(SourceLocation ForLoc,
                                       SourceLocation VarLoc, llvm::StringRef VarName,
                                       QualType VarType, Expr *Range, Stmt *Body) {
  ++BreakScopeDepth;
  ++ContinueScopeDepth;
  auto *RangeVar = Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr);
  auto *FRS = Context.create<CXXForRangeStmt>(ForLoc, RangeVar, Range, Body);
  --BreakScopeDepth;
  --ContinueScopeDepth;
  return StmtResult(FRS);
}

StmtResult Sema::ActOnCXXTryStmt(SourceLocation TryLoc, Stmt *TryBlock,
                                  llvm::ArrayRef<Stmt *> Handlers) {
  auto *TS = Context.create<CXXTryStmt>(TryLoc, TryBlock, Handlers);
  return StmtResult(TS);
}

StmtResult Sema::ActOnCXXCatchStmt(SourceLocation CatchLoc,
                                    VarDecl *ExceptionDecl,
                                    Stmt *HandlerBlock) {
  auto *CS = Context.create<CXXCatchStmt>(CatchLoc, ExceptionDecl,
                                          HandlerBlock);
  return StmtResult(CS);
}

StmtResult Sema::ActOnCoreturnStmt(SourceLocation Loc, Expr *RetVal) {
  auto *CS = Context.create<CoreturnStmt>(Loc, RetVal);
  return StmtResult(CS);
}

StmtResult Sema::ActOnCoyieldStmt(SourceLocation Loc, Expr *Value) {
  auto *CS = Context.create<CoyieldStmt>(Loc, Value);
  return StmtResult(CS);
}

ExprResult Sema::ActOnCoawaitExpr(SourceLocation Loc, Expr *Operand) {
  auto *CE = Context.create<CoawaitExpr>(Loc, Operand);
  return ExprResult(CE);
}

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
    if (auto *ParamDecl = llvm::dyn_cast_or_null<TemplateTypeParmDecl>(Params[i])) {
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

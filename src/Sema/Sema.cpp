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
#include "blocktype/AST/Attr.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/ConstraintSatisfaction.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Construction / Destruction
//===----------------------------------------------------------------------===//

Sema::Sema(ASTContext &C, DiagnosticsEngine &D)
  : Context(C), Diags(D), Symbols(C, D),
    TC(C, D), ConstEval(C),
    Instantiator(std::make_unique<TemplateInstantiator>(*this)),
    Deduction(std::make_unique<TemplateDeduction>(*this)),
    ConstraintChecker(std::make_unique<ConstraintSatisfaction>(*this)) {
  PushScope(ScopeFlags::TranslationUnitScope);
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
  if (CurContext) {
    CurrentDC = CurContext;
  } else if (CurTU) {
    CurrentDC = CurTU;
  } else {
    return nullptr;
  }
  
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
    
    // Move into the found namespace for the next lookup
    CurrentDC = FoundNS;
  }
  
  // The last found namespace should be a NamespaceDecl
  return llvm::dyn_cast_or_null<NamespaceDecl>(CurrentDC);
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
  CurContext = DC;
}

void Sema::PopDeclContext() {
  if (CurContext) {
    CurContext = CurContext->getParent();
  }
}

//===----------------------------------------------------------------------===//
// Translation unit
//===----------------------------------------------------------------------===//

void Sema::ActOnTranslationUnit(TranslationUnitDecl *TU) {
  CurTU = TU;
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
                               QualType T, Expr *Init) {
  if (!RequireCompleteType(T, Loc))
    return DeclResult::getInvalid();

  auto *VD = Context.create<VarDecl>(Loc, Name, T, Init);

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
  auto *FD = Context.create<FunctionDecl>(Loc, Name, T, Params, Body);

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
  return DeclResult(MD);
}

DeclResult Sema::ActOnImportDecl(SourceLocation Loc, llvm::StringRef ModuleName,
                                 bool IsExported, llvm::StringRef Partition,
                                 llvm::StringRef Header, bool IsHeader) {
  auto *ID = Context.create<ImportDecl>(Loc, ModuleName, IsExported, Partition,
                                         Header, IsHeader);
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
                                  QualType T, Expr *Init, bool IsStatic) {
  if (!T.isNull()) RequireCompleteType(T, Loc);
  auto *VD = Context.create<VarDecl>(Loc, Name, T, Init, IsStatic);
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
      
      // Fallback: if not a specialization or index out of range,
      // return the original type
      return TupleType;
    }
  }
  
  // Handle array types T[N]
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty)) {
    // All elements have the same type
    return AT->getElementType();
  }
  
  // Fallback: return the original type
  return TupleType;
}

// P7.4.3: Structured binding implementation
DeclResult Sema::ActOnDecompositionDecl(SourceLocation Loc,
                                         llvm::ArrayRef<llvm::StringRef> Names,
                                         QualType TupleType,
                                         Expr *Init) {
  // TODO: Full implementation requires:
  // 1. Check if TupleType is decomposable (has tuple_size, get<N>)
  //    - For std::pair: check pair<T1, T2>
  //    - For std::tuple: check tuple<Ts...>
  //    - For arrays: check array type
  //    - For custom types: check for tuple_size<T> and get<N>(t)
  // 2. For each name, create a BindingDecl with proper type
  // 3. Set binding expression to std::get<N>(init)
  // 4. Return DeclGroupRef containing all bindings
  
  // Simplified implementation: create VarDecls with init expressions
  llvm::SmallVector<Decl *, 4> Decls;
  
  for (unsigned i = 0; i < Names.size(); ++i) {
    // Extract the correct type for each binding element
    QualType ElementType = GetTupleElementType(TupleType, i);
    
    // Create a BindingDecl (currently using VarDecl as placeholder)
    auto *VD = Context.create<VarDecl>(Loc, Names[i], ElementType, nullptr, false);
    
    // TODO: Create std::get<N>(init) expression
    // This requires:
    // 1. Lookup std::get function template
    // 2. Create template specialization get<i>
    // 3. Create CallExpr: std::get<i>(init)
    // For now, just use the original init expression as placeholder
    VD->setInit(Init);  // Temporary: should be std::get<i>(init)
    
    Decls.push_back(VD);
    
    if (CurContext) {
      CurContext->addDecl(VD);
    }
  }
  
  // Return first decl as result (simplified)
  return Decls.empty() ? DeclResult(nullptr) : DeclResult(Decls[0]);
}

bool Sema::CheckBindingCondition(llvm::ArrayRef<class BindingDecl *> Bindings,
                                  SourceLocation Loc) {
  // P0963R3: Check if structured binding can be used in condition
  // For now, just return true (allow it)
  return true;
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
  return DeclResult(CTD);
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
                                       AccessSpecifier Access) {
  auto *FD = Context.create<FieldDecl>(Loc, Name, Type, BitWidth, IsMutable,
                                        InClassInit, Access);
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

ExprResult Sema::ActOnExpr(Expr *E) {
  if (!E)
    return ExprResult::getInvalid();

  // All expressions should now have their types set by ActOn* factory methods.
  // This method serves as a compatibility/verification passthrough.
  // Legacy BinaryOperator/UnaryOperator type-fixup removed — ActOnBinaryOperator
  // and ActOnUnaryOperator handle type computation directly.

  return ExprResult(E);
}

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

ExprResult Sema::ActOnDeclRefExpr(SourceLocation Loc, ValueDecl *D) {
  auto *DRE = Context.create<DeclRefExpr>(Loc, D);
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

  // RecordType → field type by index
  if (auto *RT = llvm::dyn_cast<RecordType>(Ty)) {
    auto Fields = RT->getDecl()->fields();
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
                                 llvm::ArrayRef<LambdaCapture> Captures,
                                 llvm::ArrayRef<ParmVarDecl *> Params,
                                 Stmt *Body, bool IsMutable,
                                 QualType ReturnType,
                                 SourceLocation LBraceLoc,
                                 SourceLocation RBraceLoc,
                                 TemplateParameterList *TemplateParams,
                                 AttributeListDecl *Attrs) {
  auto *LE = Context.create<LambdaExpr>(Loc, Captures, Params, Body, IsMutable,
                                         ReturnType, LBraceLoc, RBraceLoc,
                                         TemplateParams, Attrs);
  // Lambda expression type: use ReturnType if explicitly specified,
  // otherwise the closure type would need to be synthesized (TODO for full support).
  if (!ReturnType.isNull())
    LE->setType(ReturnType);
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

  // Resolve the callee
  FunctionDecl *FD = nullptr;

  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
    Decl *D = DRE->getDecl();
    if (!D) {
      // Undeclared identifier — fall back to creating CallExpr directly
      auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
      return ExprResult(CE);
    }
    if (auto *FunD = llvm::dyn_cast<FunctionDecl>(D)) {
      FD = FunD;
    }
    // Handle function template: deduce arguments and instantiate
    if (!FD) {
      if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D)) {
        FD = DeduceAndInstantiateFunctionTemplate(FTD, Args, LParenLoc);
      }
    }
    // Also check if the DeclRefExpr refers to a TemplateDecl by name
    if (!FD) {
      llvm::StringRef Name;
      if (auto *ND = llvm::dyn_cast<NamedDecl>(D))
        Name = ND->getName();
      if (!Name.empty()) {
        if (auto *FTD = Symbols.lookupTemplate(Name)) {
          if (auto *FuncFTD = llvm::dyn_cast<FunctionTemplateDecl>(FTD)) {
            FD = DeduceAndInstantiateFunctionTemplate(FuncFTD, Args, LParenLoc);
          }
        }
      }
    }
  }

  // If not a direct function reference, try overload resolution
  if (!FD) {
    if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
      Decl *D = DRE->getDecl();
      if (!D) {
        auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
        return ExprResult(CE);
      }
      llvm::StringRef Name;
      if (auto *ND = llvm::dyn_cast<NamedDecl>(D))
        Name = ND->getName();
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
    // without full resolution. ProcessAST will handle it later.
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

  // Find the named member
  ValueDecl *MemberDecl = nullptr;
  for (auto *D : RD->members()) {
    if (auto *ND = llvm::dyn_cast<NamedDecl>(D)) {
      if (ND->getName() == Member) {
        MemberDecl = llvm::dyn_cast<ValueDecl>(D);
        break;
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
      if (!TC.CheckReturn(RetVal, RetType, ReturnLoc))
        return StmtResult::getInvalid();
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

StmtResult Sema::ActOnDeclStmt(Decl *D) {
  Decl *Decls[] = {D};
  auto *DS = Context.create<DeclStmt>(D->getLocation(), Decls);
  return StmtResult(DS);
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
    return RT->getDecl()->isCompleteDefinition();
  }

  if (Ty->isEnumType()) {
    auto *ET = static_cast<const EnumType *>(Ty);
    return ET->getDecl()->isCompleteDefinition();
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

  if (Ty->isVoidType()) return false;

  if (Ty->getTypeClass() == TypeClass::TemplateTypeParm ||
      Ty->getTypeClass() == TypeClass::Dependent ||
      Ty->getTypeClass() == TypeClass::Unresolved) {
    return false;
  }

  if (Ty->getTypeClass() == TypeClass::TemplateSpecialization) {
    // Check if the template specialization has been instantiated as a
    // complete type. Look for an existing specialization via the instantiator.
    auto *TST = static_cast<const TemplateSpecializationType *>(Ty);
    if (auto *CTD = llvm::dyn_cast_or_null<ClassTemplateDecl>(
            TST->getTemplateDecl())) {
      auto *Spec = Instantiator->FindExistingSpecialization(
          CTD, TST->getTemplateArgs());
      if (Spec && Spec->isCompleteDefinition())
        return true;
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

} // namespace blocktype

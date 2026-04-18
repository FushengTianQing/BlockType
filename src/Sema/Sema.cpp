//===--- Sema.cpp - Semantic Analysis Engine Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
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
  : Context(C), Diags(D), Symbols(C),
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
    Symbols.addDecl(llvm::cast<NamedDecl>(D));
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

  if (CurrentScope)
    Symbols.addDecl(FD);
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
        Symbols.addDecl(PVD);
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

  if (CurrentScope)
    Symbols.addDecl(ECD);
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
  if (CurrentScope) Symbols.addDecl(NS);
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
  if (CurrentScope) Symbols.addDecl(NAD);
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
  if (CurrentScope) Symbols.addDecl(ED);
  if (CurContext) CurContext->addDecl(ED);
  return DeclResult(ED);
}

DeclResult Sema::ActOnTypedefDecl(SourceLocation Loc, llvm::StringRef Name,
                                  QualType T) {
  auto *TD = Context.create<TypedefDecl>(Loc, Name, T);
  if (CurrentScope) Symbols.addDecl(TD);
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
  if (CurrentScope) Symbols.addDecl(VD);
  if (CurContext) CurContext->addDecl(VD);
  return DeclResult(VD);
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
  if (CurrentScope) Symbols.addDecl(RD);
  if (CurContext) CurContext->addDecl(RD);
}

void Sema::ActOnCXXMethodDecl(CXXMethodDecl *MD) {
  if (!MD) return;
  if (CurrentScope) Symbols.addDecl(MD);
}

void Sema::ActOnFieldDecl(FieldDecl *FD) {
  if (!FD) return;
  if (CurrentScope) Symbols.addDecl(FD);
}

void Sema::ActOnAccessSpecDecl(AccessSpecDecl *ASD) {
  // Access specifiers don't need symbol table registration
}

void Sema::ActOnCXXConstructorDecl(CXXConstructorDecl *CD) {
  if (!CD) return;
  if (CurrentScope) Symbols.addDecl(CD);
}

void Sema::ActOnCXXDestructorDecl(CXXDestructorDecl *DD) {
  if (!DD) return;
  if (CurrentScope) Symbols.addDecl(DD);
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
  if (CurrentScope) Symbols.addDecl(FD);
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

  // If the expression already has a type, trust it (set by ActOn* factory).
  if (!E->getType().isNull())
    return ExprResult(E);

  // Type-check already-constructed BinaryOperator/UnaryOperator nodes
  // that the Parser created directly (rather than through ActOn* methods).
  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E)) {
    if (BO->getType().isNull()) {
      QualType LHSType = BO->getLHS() ? BO->getLHS()->getType() : QualType();
      QualType RHSType = BO->getRHS() ? BO->getRHS()->getType() : QualType();
      QualType ResultType = TC.getBinaryOperatorResultType(
          BO->getOpcode(), LHSType, RHSType);
      if (ResultType.isNull()) {
        Diags.report(BO->getLocation(), DiagID::err_type_mismatch);
        return ExprResult::getInvalid();
      }
      BO->setType(ResultType);
    }
  } else if (auto *UO = llvm::dyn_cast<UnaryOperator>(E)) {
    if (UO->getType().isNull()) {
      QualType OperandType =
          UO->getSubExpr() ? UO->getSubExpr()->getType() : QualType();
      QualType ResultType =
          TC.getUnaryOperatorResultType(UO->getOpcode(), OperandType);
      if (ResultType.isNull()) {
        Diags.report(UO->getLocation(), DiagID::err_type_mismatch);
        return ExprResult::getInvalid();
      }
      UO->setType(ResultType);
    }
  }

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
                                   SourceLocation RBraceLoc) {
  auto *ILE = Context.create<InitListExpr>(LBraceLoc, Inits, RBraceLoc);
  return ExprResult(ILE);
}

ExprResult Sema::ActOnDesignatedInitExpr(
    SourceLocation DotLoc,
    llvm::ArrayRef<DesignatedInitExpr::Designator> Designators,
    Expr *Init) {
  auto *DIE = Context.create<DesignatedInitExpr>(DotLoc, Designators, Init);
  return ExprResult(DIE);
}

ExprResult Sema::ActOnTemplateSpecializationExpr(
    SourceLocation Loc, llvm::StringRef Name,
    llvm::ArrayRef<TemplateArgument> Args, ValueDecl *VD) {
  auto *TSE = Context.create<TemplateSpecializationExpr>(Loc, Name, Args, VD);
  return ExprResult(TSE);
}

ExprResult Sema::ActOnMemberExprDirect(SourceLocation OpLoc, Expr *Base,
                                       ValueDecl *MemberDecl, bool IsArrow) {
  auto *ME = Context.create<MemberExpr>(OpLoc, Base, MemberDecl, IsArrow);
  return ExprResult(ME);
}

ExprResult Sema::ActOnCXXConstructExpr(SourceLocation Loc,
                                       llvm::ArrayRef<Expr *> Args) {
  auto *CE = Context.create<CXXConstructExpr>(Loc, Args);
  return ExprResult(CE);
}

ExprResult Sema::ActOnCXXNewExprFactory(SourceLocation NewLoc, Expr *ArraySize,
                                         Expr *Initializer, QualType Type) {
  auto *NE = Context.create<CXXNewExpr>(NewLoc, ArraySize, Initializer, Type);
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
  return ExprResult(TE);
}

ExprResult Sema::ActOnCXXThrowExpr(SourceLocation Loc, Expr *Operand) {
  auto *TE = Context.create<CXXThrowExpr>(Loc, Operand);
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
  if (CastKind == "dynamic_cast")
    return ExprResult(
        Context.create<CXXDynamicCastExpr>(CastLoc, SubExpr, CastType));
  return ActOnCXXNamedCastExpr(CastLoc, SubExpr, CastKind);
}

ExprResult Sema::ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack,
                                       Expr *Index) {
  auto *PIE = Context.create<PackIndexingExpr>(Loc, Pack, Index);
  return ExprResult(PIE);
}

ExprResult Sema::ActOnReflexprExpr(SourceLocation Loc, Expr *Arg) {
  auto *RE = Context.create<ReflexprExpr>(Loc, Arg);
  return ExprResult(RE);
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
  return ExprResult(LE);
}

ExprResult Sema::ActOnCXXFoldExpr(SourceLocation Loc, Expr *LHS, Expr *RHS,
                                  Expr *Pattern, BinaryOpKind Op,
                                  bool IsRightFold) {
  auto *FE = Context.create<CXXFoldExpr>(Loc, LHS, RHS, Pattern, Op,
                                          IsRightFold);
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
      Diags.report(MemberLoc, DiagID::err_type_mismatch);
      return ExprResult::getInvalid();
    }
  }

  // Look up the member in the record type
  auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr());
  if (!RT) {
    Diags.report(MemberLoc, DiagID::err_type_mismatch);
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
  return ExprResult(ME);
}

ExprResult Sema::ActOnBinaryOperator(BinaryOpKind Op, Expr *LHS, Expr *RHS,
                                      SourceLocation OpLoc) {
  if (!LHS || !RHS)
    return ExprResult::getInvalid();

  QualType LHSType = LHS->getType();
  QualType RHSType = RHS->getType();

  // Defensive: during incremental migration, types may not be set yet.
  // Skip type checking and let ProcessAST handle it later.
  if (LHSType.isNull() || RHSType.isNull()) {
    auto *BO = Context.create<BinaryOperator>(OpLoc, LHS, RHS, Op);
    return ExprResult(BO);
  }

  // Compute the result type via TypeCheck
  QualType ResultType = TC.getBinaryOperatorResultType(Op, LHSType, RHSType);
  if (ResultType.isNull()) {
    Diags.report(OpLoc, DiagID::err_type_mismatch);
    return ExprResult::getInvalid();
  }

  // Create the BinaryOperator node and set its result type
  auto *BO = Context.create<BinaryOperator>(OpLoc, LHS, RHS, Op);
  BO->setType(ResultType);
  return ExprResult(BO);
}

ExprResult Sema::ActOnUnaryOperator(UnaryOpKind Op, Expr *Operand,
                                     SourceLocation OpLoc) {
  if (!Operand)
    return ExprResult::getInvalid();

  QualType OperandType = Operand->getType();

  // Defensive: during incremental migration, types may not be set yet.
  if (OperandType.isNull()) {
    auto *UO = Context.create<UnaryOperator>(OpLoc, Operand, Op);
    return ExprResult(UO);
  }

  // Compute the result type via TypeCheck
  QualType ResultType = TC.getUnaryOperatorResultType(Op, OperandType);
  if (ResultType.isNull()) {
    Diags.report(OpLoc, DiagID::err_type_mismatch);
    return ExprResult::getInvalid();
  }

  // Create the UnaryOperator node and set its result type
  auto *UO = Context.create<UnaryOperator>(OpLoc, Operand, Op);
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
    Diags.report(LParenLoc, DiagID::err_type_mismatch);
    return ExprResult::getInvalid();
  }

  auto *CE = Context.create<CStyleCastExpr>(LParenLoc, E);
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
      Diags.report(LLoc, DiagID::err_type_mismatch);
      return ExprResult::getInvalid();
    }
  }

  for (auto *Idx : Indices) {
    if (!Idx)
      continue;
    if (!Idx->getType().isNull() && !Idx->getType()->isIntegerType()) {
      Diags.report(LLoc, DiagID::err_type_mismatch);
      return ExprResult::getInvalid();
    }
  }

  auto *ASE = Context.create<ArraySubscriptExpr>(LLoc, Base, Indices);
  return ExprResult(ASE);
}

ExprResult Sema::ActOnConditionalExpr(Expr *Cond, Expr *Then, Expr *Else,
                                       SourceLocation QuestionLoc,
                                       SourceLocation ColonLoc) {
  if (!Cond || !Then || !Else)
    return ExprResult::getInvalid();

  // Defensive: skip condition check if type not set yet
  if (!Cond->getType().isNull() && !TC.CheckCondition(Cond, QuestionLoc))
    return ExprResult::getInvalid();

  // Defensive: skip common type computation if types not set yet
  if (!Then->getType().isNull() && !Else->getType().isNull()) {
    QualType ResultType = TC.getCommonType(Then->getType(), Else->getType());
    if (ResultType.isNull()) {
      Diags.report(ColonLoc, DiagID::err_type_mismatch);
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
                                 SourceLocation WhileLoc) {
  if (Cond && !Cond->getType().isNull() && !TC.CheckCondition(Cond, WhileLoc))
    return StmtResult::getInvalid();

  auto *WS = Context.create<WhileStmt>(WhileLoc, Cond, Body);
  return StmtResult(WS);
}

StmtResult Sema::ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                               SourceLocation ForLoc) {
  if (Cond && !Cond->getType().isNull() && !TC.CheckCondition(Cond, ForLoc))
    return StmtResult::getInvalid();

  auto *FS = Context.create<ForStmt>(ForLoc, Init, Cond, Inc, Body);
  return StmtResult(FS);
}

StmtResult Sema::ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc) {
  if (Cond && !Cond->getType().isNull() && !TC.CheckCondition(Cond, DoLoc))
    return StmtResult::getInvalid();

  auto *DS = Context.create<DoStmt>(DoLoc, Body, Cond);
  return StmtResult(DS);
}

StmtResult Sema::ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                                  SourceLocation SwitchLoc) {
  // Defensive: skip type check if type not yet set (incremental migration)
  if (Cond && !Cond->getType().isNull() && !Cond->getType()->isIntegerType()) {
    Diags.report(SwitchLoc, DiagID::err_type_mismatch);
    return StmtResult::getInvalid();
  }

  auto *SS = Context.create<SwitchStmt>(SwitchLoc, Cond, Body);
  return StmtResult(SS);
}

StmtResult Sema::ActOnCaseStmt(Expr *Val, Stmt *Body, SourceLocation CaseLoc) {
  if (Val && !Val->getType().isNull() && !TC.CheckCaseExpression(Val, CaseLoc))
    return StmtResult::getInvalid();

  auto *CS = Context.create<CaseStmt>(CaseLoc, Val, nullptr, Body);
  return StmtResult(CS);
}

StmtResult Sema::ActOnDefaultStmt(Stmt *Body, SourceLocation DefaultLoc) {
  auto *DS = Context.create<DefaultStmt>(DefaultLoc, Body);
  return StmtResult(DS);
}

StmtResult Sema::ActOnBreakStmt(SourceLocation BreakLoc) {
  auto *BS = Context.create<BreakStmt>(BreakLoc);
  return StmtResult(BS);
}

StmtResult Sema::ActOnContinueStmt(SourceLocation ContinueLoc) {
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
  if (CurrentScope)
    Symbols.addDecl(LD);
  auto *LS = Context.create<LabelStmt>(Loc, LD, SubStmt);
  return StmtResult(LS);
}

//===----------------------------------------------------------------------===//
// C++ statement extensions (Phase 2B)
//===----------------------------------------------------------------------===//

StmtResult Sema::ActOnCXXForRangeStmt(SourceLocation ForLoc,
                                       SourceLocation VarLoc, llvm::StringRef VarName,
                                       QualType VarType, Expr *Range, Stmt *Body) {
  auto *RangeVar = Context.create<VarDecl>(VarLoc, VarName, VarType, nullptr);
  auto *FRS = Context.create<CXXForRangeStmt>(ForLoc, RangeVar, Range, Body);
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
    Diags.report(SourceLocation(), DiagID::err_ovl_deleted_function,
                 Best->getName());
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
// CXXNewExpr / CXXDeleteExpr semantic analysis
//===----------------------------------------------------------------------===//

ExprResult Sema::ActOnCXXNewExpr(CXXNewExpr *E) {
  if (!E)
    return ExprResult::getInvalid();

  QualType AllocType = E->getAllocatedType();
  if (AllocType.isNull()) {
    Diags.report(E->getLocation(), DiagID::err_expected_type);
    return ExprResult::getInvalid();
  }

  // 设置 ExprTy = AllocType*（new 表达式的结果类型是指针）
  auto *PtrTy = Context.getPointerType(AllocType.getTypePtr());
  E->setType(QualType(PtrTy, Qualifier::None));

  return ExprResult(E);
}

ExprResult Sema::ActOnCXXDeleteExpr(CXXDeleteExpr *E) {
  if (!E)
    return ExprResult::getInvalid();

  // 从 Argument 表达式的类型推导被删除的元素类型
  QualType AllocatedType;
  if (E->getArgument()) {
    QualType ArgType = E->getArgument()->getType();
    if (auto *PtrType = llvm::dyn_cast<PointerType>(ArgType.getTypePtr())) {
      AllocatedType = PtrType->getPointeeType();
    }
  }

  // 如果 AllocatedType 已由 Parse 层设置，保留它；否则使用推导结果
  if (E->getAllocatedType().isNull() && !AllocatedType.isNull()) {
    // CXXDeleteExpr 的 AllocatedType 是 const 字段，无法修改。
    // 但由于构造函数中已传入，这里只在 ExprTy 上设置 void。
  }

  // delete 表达式的结果类型是 void
  auto *VoidType = Context.getBuiltinType(BuiltinKind::Void);
  E->setType(QualType(VoidType, Qualifier::None));

  return ExprResult(E);
}

namespace {

/// ASTVisitor — 遍历 AST 树，对 CXXNewExpr/CXXDeleteExpr 调用 Sema 方法。
/// 使用递归遍历所有 Stmt/Expr 节点。
class ASTVisitor {
  Sema &S;

public:
  ASTVisitor(Sema &SemaRef) : S(SemaRef) {}

  void visitTU(TranslationUnitDecl *TU) {
    for (Decl *D : TU->decls()) {
      visitDecl(D);
    }
  }

  void visitDecl(Decl *D) {
    if (!D) return;

    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      if (FD->getBody())
        visitStmt(FD->getBody());
    } else if (auto *VD = llvm::dyn_cast<VarDecl>(D)) {
      if (VD->getInit())
        visitExpr(VD->getInit());
    }
  }

  void visitStmt(Stmt *S) {
    if (!S) return;

    // 遍历复合语句的子语句
    if (auto *CS = llvm::dyn_cast<CompoundStmt>(S)) {
      for (Stmt *Child : CS->getBody())
        visitStmt(Child);
      return;
    }

    // 表达式语句
    if (auto *ES = llvm::dyn_cast<ExprStmt>(S)) {
      visitExpr(ES->getExpr());
      return;
    }

    // if/while/for/do
    if (auto *IS = llvm::dyn_cast<IfStmt>(S)) {
      visitExpr(IS->getCond());
      visitStmt(IS->getThen());
      visitStmt(IS->getElse());
      return;
    }
    if (auto *WS = llvm::dyn_cast<WhileStmt>(S)) {
      visitExpr(WS->getCond());
      visitStmt(WS->getBody());
      return;
    }
    if (auto *FS = llvm::dyn_cast<ForStmt>(S)) {
      visitStmt(FS->getInit());
      visitExpr(FS->getCond());
      visitExpr(FS->getInc());
      visitStmt(FS->getBody());
      return;
    }
    if (auto *DS = llvm::dyn_cast<DoStmt>(S)) {
      visitStmt(DS->getBody());
      visitExpr(DS->getCond());
      return;
    }

    // return
    if (auto *RS = llvm::dyn_cast<ReturnStmt>(S)) {
      visitExpr(RS->getRetValue());
      return;
    }

    // decl statement
    if (auto *DS2 = llvm::dyn_cast<DeclStmt>(S)) {
      for (Decl *D : DS2->getDecls())
        visitDecl(D);
      return;
    }
  }

  void visitExpr(Expr *E) {
    if (!E) return;

    // 对 new/delete 表达式调用 Sema 处理
    // 防御性检查：如果节点已通过 ActOn* 设置过类型，跳过
    if (auto *NewE = llvm::dyn_cast<CXXNewExpr>(E)) {
      if (NewE->getType().isNull())
        S.ActOnCXXNewExpr(NewE);
    } else if (auto *DelE = llvm::dyn_cast<CXXDeleteExpr>(E)) {
      if (DelE->getType().isNull())
        S.ActOnCXXDeleteExpr(DelE);
    }

    // 递归遍历子表达式
    if (auto *BO = llvm::dyn_cast<BinaryOperator>(E)) {
      visitExpr(BO->getLHS());
      visitExpr(BO->getRHS());
    } else if (auto *UO = llvm::dyn_cast<UnaryOperator>(E)) {
      visitExpr(UO->getSubExpr());
    } else if (auto *CE = llvm::dyn_cast<CallExpr>(E)) {
      visitExpr(CE->getCallee());
      for (Expr *Arg : CE->getArgs())
        visitExpr(Arg);
    } else if (auto *CCE = llvm::dyn_cast<CXXConstructExpr>(E)) {
      for (Expr *Arg : CCE->getArgs())
        visitExpr(Arg);
    } else if (auto *ME = llvm::dyn_cast<MemberExpr>(E)) {
      visitExpr(ME->getBase());
    } else if (auto *ASE = llvm::dyn_cast<ArraySubscriptExpr>(E)) {
      visitExpr(ASE->getBase());
      for (Expr *Idx : ASE->getIndices())
        visitExpr(Idx);
    } else if (auto *Cast = llvm::dyn_cast<CStyleCastExpr>(E)) {
      visitExpr(Cast->getSubExpr());
    }
  }
};

} // anonymous namespace

void Sema::ProcessAST(TranslationUnitDecl *TU) {
  if (!TU) return;
  ASTVisitor Visitor(*this);
  Visitor.visitTU(TU);
}

} // namespace blocktype

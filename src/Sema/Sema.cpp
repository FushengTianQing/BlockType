//===--- Sema.cpp - Semantic Analysis Engine Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Construction / Destruction
//===----------------------------------------------------------------------===//

Sema::Sema(ASTContext &C, DiagnosticsEngine &D)
  : Context(C), Diags(D), Symbols(C) {
  // Initialize translation unit scope
  PushScope(ScopeFlags::TranslationUnitScope);
}

Sema::~Sema() {
  // Clean up scope stack
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

//===----------------------------------------------------------------------===//
// Declaration handling [Stage 4.1 stub, Stage 4.5 implements]
//===----------------------------------------------------------------------===//

// TODO: Stage 4.5 - Implement declaration handling
DeclResult Sema::ActOnDeclarator(Decl *D) {
  return DeclResult(D);
}

void Sema::ActOnFinishDecl(Decl *D) {
  // TODO: Stage 4.5 - Finalize declaration
}

DeclResult Sema::ActOnVarDecl(SourceLocation Loc, llvm::StringRef Name,
                               QualType T, Expr *Init) {
  // TODO: Stage 4.5 - Implement variable declaration
  return DeclResult::getInvalid();
}

DeclResult Sema::ActOnFunctionDecl(SourceLocation Loc, llvm::StringRef Name,
                                    QualType T,
                                    llvm::ArrayRef<ParmVarDecl *> Params,
                                    Stmt *Body) {
  // TODO: Stage 4.5 - Implement function declaration
  return DeclResult::getInvalid();
}

void Sema::ActOnStartOfFunctionDef(FunctionDecl *FD) {
  CurFunction = FD;
}

void Sema::ActOnFinishOfFunctionDef(FunctionDecl *FD) {
  CurFunction = nullptr;
}

//===----------------------------------------------------------------------===//
// Expression handling [Stage 4.1 stub, Stage 4.5 implements]
//===----------------------------------------------------------------------===//

// TODO: Stage 4.5 - Implement expression handling
ExprResult Sema::ActOnExpr(Expr *E) {
  return ExprResult(E);
}

ExprResult Sema::ActOnCallExpr(Expr *Fn, llvm::ArrayRef<Expr *> Args,
                                SourceLocation LParenLoc,
                                SourceLocation RParenLoc) {
  return ExprResult::getInvalid();
}

ExprResult Sema::ActOnMemberExpr(Expr *Base, llvm::StringRef Member,
                                  SourceLocation MemberLoc, bool IsArrow) {
  return ExprResult::getInvalid();
}

ExprResult Sema::ActOnBinaryOperator(Expr *LHS, Expr *RHS,
                                      SourceLocation OpLoc) {
  return ExprResult::getInvalid();
}

ExprResult Sema::ActOnUnaryOperator(Expr *Operand, SourceLocation OpLoc) {
  return ExprResult::getInvalid();
}

ExprResult Sema::ActOnCastExpr(QualType TargetType, Expr *E,
                                SourceLocation LParenLoc,
                                SourceLocation RParenLoc) {
  return ExprResult::getInvalid();
}

ExprResult Sema::ActOnArraySubscriptExpr(Expr *Base,
                                          llvm::ArrayRef<Expr *> Indices,
                                          SourceLocation LLoc,
                                          SourceLocation RLoc) {
  return ExprResult::getInvalid();
}

ExprResult Sema::ActOnConditionalExpr(Expr *Cond, Expr *Then, Expr *Else,
                                       SourceLocation QuestionLoc,
                                       SourceLocation ColonLoc) {
  return ExprResult::getInvalid();
}

//===----------------------------------------------------------------------===//
// Statement handling [Stage 4.1 stub, Stage 4.5 implements]
//===----------------------------------------------------------------------===//

// TODO: Stage 4.5 - Implement statement handling
StmtResult Sema::ActOnReturnStmt(Expr *RetVal, SourceLocation ReturnLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnIfStmt(Expr *Cond, Stmt *Then, Stmt *Else,
                              SourceLocation IfLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnWhileStmt(Expr *Cond, Stmt *Body,
                                 SourceLocation WhileLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                               SourceLocation ForLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                                  SourceLocation SwitchLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnCaseStmt(Expr *Val, Stmt *Body, SourceLocation CaseLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnDefaultStmt(Stmt *Body, SourceLocation DefaultLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnBreakStmt(SourceLocation BreakLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnContinueStmt(SourceLocation ContinueLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnGotoStmt(llvm::StringRef Label, SourceLocation GotoLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnCompoundStmt(llvm::ArrayRef<Stmt *> Stmts,
                                    SourceLocation LBraceLoc,
                                    SourceLocation RBraceLoc) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnDeclStmt(Decl *D) {
  return StmtResult::getInvalid();
}

StmtResult Sema::ActOnNullStmt(SourceLocation Loc) {
  return StmtResult::getInvalid();
}

//===----------------------------------------------------------------------===//
// Type handling [Stage 4.2]
//===----------------------------------------------------------------------===//

bool Sema::isCompleteType(QualType T) const {
  if (T.isNull()) return false;

  const Type *Ty = T.getTypePtr();

  // Builtin types are always complete
  if (Ty->isBuiltinType()) return true;

  // Pointer/reference types: pointee need not be complete
  if (Ty->isPointerType() || Ty->isReferenceType()) return true;

  // Member pointer types are always complete
  if (Ty->getTypeClass() == TypeClass::MemberPointer) return true;

  // Array types: element must be complete (except incomplete arrays)
  if (Ty->isArrayType()) {
    if (Ty->getTypeClass() == TypeClass::IncompleteArray) return false;
    if (Ty->getTypeClass() == TypeClass::ConstantArray) {
      auto *AT = static_cast<const ConstantArrayType *>(Ty);
      return isCompleteType(QualType(AT->getElementType(), Qualifier::None));
    }
    // VariableArray: check element completeness
    if (Ty->getTypeClass() == TypeClass::VariableArray) {
      auto *AT = static_cast<const VariableArrayType *>(Ty);
      return isCompleteType(QualType(AT->getElementType(), Qualifier::None));
    }
    return true;
  }

  // Function types are always complete
  if (Ty->isFunctionType()) return true;

  // Record types: must have a definition
  if (Ty->isRecordType()) {
    auto *RT = static_cast<const RecordType *>(Ty);
    if (auto *RD = dyn_cast<CXXRecordDecl>(RT->getDecl()))
      return !RD->members().empty() || RD->getNumBases() > 0;
    return false;
  }

  // Enum types: must have a definition
  if (Ty->isEnumType()) {
    auto *ET = static_cast<const EnumType *>(Ty);
    return !ET->getDecl()->enumerators().empty();
  }

  // Typedef: check the underlying type
  if (Ty->getTypeClass() == TypeClass::Typedef) {
    auto *TT = static_cast<const TypedefType *>(Ty);
    return isCompleteType(TT->getDecl()->getUnderlyingType());
  }

  // Elaborated type: check the named type
  if (Ty->getTypeClass() == TypeClass::Elaborated) {
    auto *ET = static_cast<const ElaboratedType *>(Ty);
    return isCompleteType(QualType(ET->getNamedType(), Qualifier::None));
  }

  // Decltype: check the underlying type if available
  if (Ty->getTypeClass() == TypeClass::Decltype) {
    auto *DT = static_cast<const DecltypeType *>(Ty);
    QualType Underlying = DT->getUnderlyingType();
    if (!Underlying.isNull()) return isCompleteType(Underlying);
    return false;
  }

  // Auto: complete only if deduced
  if (Ty->getTypeClass() == TypeClass::Auto) {
    auto *AT = static_cast<const AutoType *>(Ty);
    if (AT->isDeduced()) return isCompleteType(AT->getDeducedType());
    return false;
  }

  // Void is never complete
  if (Ty->isVoidType()) return false;

  // Template-dependent types are never complete
  if (Ty->getTypeClass() == TypeClass::TemplateTypeParm ||
      Ty->getTypeClass() == TypeClass::Dependent ||
      Ty->getTypeClass() == TypeClass::Unresolved) {
    return false;
  }

  // TemplateSpecialization: try to check if instantiated
  if (Ty->getTypeClass() == TypeClass::TemplateSpecialization) {
    // Not yet fully instantiated
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
// Name lookup [Stage 4.3 stub]
//===----------------------------------------------------------------------===//

// TODO: Stage 4.3 - Implement name lookup
LookupResult Sema::LookupUnqualifiedName(llvm::StringRef Name, Scope *S,
                                          LookupNameKind Kind) {
  return LookupResult();
}

LookupResult Sema::LookupQualifiedName(llvm::StringRef Name,
                                         NestedNameSpecifier *NNS) {
  return LookupResult();
}

void Sema::LookupADL(llvm::StringRef Name,
                      llvm::ArrayRef<Expr *> Args,
                      LookupResult &Result) {
  // TODO: Stage 4.3 - Implement ADL
}

void Sema::CollectAssociatedNamespacesAndClasses(
    QualType T,
    llvm::SmallPtrSetImpl<NamespaceDecl *> &Namespaces,
    llvm::SmallPtrSetImpl<const RecordType *> &Classes) {
  // TODO: Stage 4.3 - Implement associated namespace/class collection
}

//===----------------------------------------------------------------------===//
// Overload resolution [Stage 4.4 stub]
//===----------------------------------------------------------------------===//

// TODO: Stage 4.4 - Implement overload resolution
FunctionDecl *Sema::ResolveOverload(llvm::StringRef Name,
                                     llvm::ArrayRef<Expr *> Args,
                                     const LookupResult &Candidates) {
  return nullptr;
}

void Sema::AddOverloadCandidate(FunctionDecl *F,
                                 llvm::ArrayRef<Expr *> Args,
                                 OverloadCandidateSet &Set) {
  // TODO: Stage 4.4 - Implement overload candidate addition
}

//===----------------------------------------------------------------------===//
// Diagnostics helpers [Stage 4.5]
//===----------------------------------------------------------------------===//

void Sema::Diag(SourceLocation Loc, DiagID ID) {
  Diags.report(Loc, ID);
}

void Sema::Diag(SourceLocation Loc, DiagID ID, llvm::StringRef Extra) {
  Diags.report(Loc, ID);
}

} // namespace blocktype

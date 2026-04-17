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
#include "blocktype/AST/Type.h"

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

//===----------------------------------------------------------------------===//
// Expression handling
//===----------------------------------------------------------------------===//

ExprResult Sema::ActOnExpr(Expr *E) {
  if (!E)
    return ExprResult::getInvalid();

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

ExprResult Sema::ActOnCallExpr(Expr *Fn, llvm::ArrayRef<Expr *> Args,
                                SourceLocation LParenLoc,
                                SourceLocation RParenLoc) {
  if (!Fn)
    return ExprResult::getInvalid();

  // Resolve the callee
  FunctionDecl *FD = nullptr;

  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
    if (auto *FunD = llvm::dyn_cast<FunctionDecl>(DRE->getDecl())) {
      FD = FunD;
    }
    // Handle function template: deduce arguments and instantiate
    if (!FD) {
      if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(DRE->getDecl())) {
        FD = DeduceAndInstantiateFunctionTemplate(FTD, Args, LParenLoc);
      }
    }
    // Also check if the DeclRefExpr refers to a TemplateDecl by name
    if (!FD) {
      llvm::StringRef Name = DRE->getDecl()->getName();
      if (auto *FTD = Symbols.lookupTemplate(Name)) {
        if (auto *FuncFTD = llvm::dyn_cast<FunctionTemplateDecl>(FTD)) {
          FD = DeduceAndInstantiateFunctionTemplate(FuncFTD, Args, LParenLoc);
        }
      }
    }
  }

  // If not a direct function reference, try overload resolution
  if (!FD) {
    if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Fn)) {
      llvm::StringRef Name = DRE->getDecl()->getName();
      auto Decls = Symbols.lookup(Name);
      LookupResult LR;
      for (auto *D : Decls)
        LR.addDecl(D);
      FD = ResolveOverload(Name, Args, LR);
    }
  }

  if (!FD) {
    Diags.report(LParenLoc, DiagID::err_ovl_no_viable_function,
                 Fn->getType().isNull() ? "<unknown>" : "expression");
    return ExprResult::getInvalid();
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

  if (!TC.isTypeCompatible(E->getType(), TargetType)) {
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
  const Type *BaseTy = BaseType.getTypePtr();

  if (!BaseTy->isPointerType() && !BaseTy->isArrayType()) {
    Diags.report(LLoc, DiagID::err_type_mismatch);
    return ExprResult::getInvalid();
  }

  for (auto *Idx : Indices) {
    if (!Idx || !Idx->getType()->isIntegerType()) {
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

  if (!TC.CheckCondition(Cond, QuestionLoc))
    return ExprResult::getInvalid();

  QualType ResultType = TC.getCommonType(Then->getType(), Else->getType());
  if (ResultType.isNull()) {
    Diags.report(ColonLoc, DiagID::err_type_mismatch);
    return ExprResult::getInvalid();
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
                              SourceLocation IfLoc) {
  if (!TC.CheckCondition(Cond, IfLoc))
    return StmtResult::getInvalid();

  auto *IS = Context.create<IfStmt>(IfLoc, Cond, Then, Else);
  return StmtResult(IS);
}

StmtResult Sema::ActOnWhileStmt(Expr *Cond, Stmt *Body,
                                 SourceLocation WhileLoc) {
  if (!TC.CheckCondition(Cond, WhileLoc))
    return StmtResult::getInvalid();

  auto *WS = Context.create<WhileStmt>(WhileLoc, Cond, Body);
  return StmtResult(WS);
}

StmtResult Sema::ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                               SourceLocation ForLoc) {
  if (Cond && !TC.CheckCondition(Cond, ForLoc))
    return StmtResult::getInvalid();

  auto *FS = Context.create<ForStmt>(ForLoc, Init, Cond, Inc, Body);
  return StmtResult(FS);
}

StmtResult Sema::ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc) {
  if (!TC.CheckCondition(Cond, DoLoc))
    return StmtResult::getInvalid();

  auto *DS = Context.create<DoStmt>(DoLoc, Body, Cond);
  return StmtResult(DS);
}

StmtResult Sema::ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                                  SourceLocation SwitchLoc) {
  if (Cond && !Cond->getType()->isIntegerType()) {
    Diags.report(SwitchLoc, DiagID::err_type_mismatch);
    return StmtResult::getInvalid();
  }

  auto *SS = Context.create<SwitchStmt>(SwitchLoc, Cond, Body);
  return StmtResult(SS);
}

StmtResult Sema::ActOnCaseStmt(Expr *Val, Stmt *Body, SourceLocation CaseLoc) {
  if (!TC.CheckCaseExpression(Val, CaseLoc))
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

} // namespace blocktype

//===--- Sema.h - Semantic Analysis Engine ------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Sema class, which is the main driver for semantic
// analysis. It coordinates name lookup, type checking, overload resolution,
// access control, template instantiation, and diagnostic emission.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Sema/SymbolTable.h"
#include "blocktype/Sema/Scope.h"
#include "blocktype/Sema/Lookup.h"
#include "blocktype/Sema/Overload.h"
#include "blocktype/Sema/TypeCheck.h"
#include "blocktype/Sema/AccessControl.h"
#include "blocktype/Sema/ConstantExpr.h"
#include "llvm/ADT/DenseMap.h"

namespace blocktype {

/// ExprResult - Wrapper for expression semantic analysis results.
/// Contains either a valid Expr* or an error marker.
class ExprResult {
  Expr *Val = nullptr;
  bool Invalid = false;

public:
  ExprResult() = default;
  ExprResult(Expr *E) : Val(E) {}

  static ExprResult getInvalid() {
    ExprResult R;
    R.Invalid = true;
    return R;
  }

  bool isInvalid() const { return Invalid; }
  bool isUsable() const { return Val != nullptr && !Invalid; }
  Expr *get() const { return Val; }
  explicit operator bool() const { return isUsable(); }
};

/// StmtResult - Wrapper for statement semantic analysis results.
class StmtResult {
  Stmt *Val = nullptr;
  bool Invalid = false;

public:
  StmtResult() = default;
  StmtResult(Stmt *S) : Val(S) {}

  static StmtResult getInvalid() {
    StmtResult R;
    R.Invalid = true;
    return R;
  }

  bool isInvalid() const { return Invalid; }
  bool isUsable() const { return Val != nullptr && !Invalid; }
  Stmt *get() const { return Val; }
  explicit operator bool() const { return isUsable(); }
};

/// DeclResult - Wrapper for declaration semantic analysis results.
class DeclResult {
  Decl *Val = nullptr;
  bool Invalid = false;

public:
  DeclResult() = default;
  DeclResult(Decl *D) : Val(D) {}

  static DeclResult getInvalid() {
    DeclResult R;
    R.Invalid = true;
    return R;
  }

  bool isInvalid() const { return Invalid; }
  bool isUsable() const { return Val != nullptr && !Invalid; }
  Decl *get() const { return Val; }
  explicit operator bool() const { return isUsable(); }
};

/// Sema - Semantic analysis engine.
///
/// Coordinates all semantic analysis activities:
/// - Name lookup and resolution
/// - Type checking and conversion
/// - Overload resolution
/// - Access control checking
/// - Template instantiation
/// - Diagnostic emission
class Sema {
  ASTContext &Context;
  DiagnosticsEngine &Diags;
  SymbolTable Symbols;

  /// Checkers — owned by Sema, follow the Clang pattern where Sema
  /// is the central dispatcher that delegates to specialized checkers.
  TypeCheck TC;
  ConstantExprEvaluator ConstEval;

  /// Scope stack - tracks the current lexical scope chain.
  Scope *CurrentScope = nullptr;

  /// Current DeclContext - tracks the current semantic context.
  DeclContext *CurContext = nullptr;

  /// Current function being analyzed (for return type checking).
  FunctionDecl *CurFunction = nullptr;

  /// Translation unit being processed.
  TranslationUnitDecl *CurTU = nullptr;

public:
  Sema(ASTContext &C, DiagnosticsEngine &D);
  ~Sema();

  // Non-copyable
  Sema(const Sema &) = delete;
  Sema &operator=(const Sema &) = delete;

  //===------------------------------------------------------------------===//
  // Accessors
  //===------------------------------------------------------------------===//

  ASTContext &getASTContext() const { return Context; }
  DiagnosticsEngine &getDiagnostics() const { return Diags; }
  bool hasErrorOccurred() const { return Diags.hasErrorOccurred(); }

  /// Access the type checker.
  TypeCheck &getTypeCheck() { return TC; }

  /// Access the constant expression evaluator.
  ConstantExprEvaluator &getConstEval() { return ConstEval; }

  //===------------------------------------------------------------------===//
  // Scope management
  //===------------------------------------------------------------------===//

  void PushScope(ScopeFlags Flags);
  void PopScope();
  Scope *getCurrentScope() const { return CurrentScope; }

  //===------------------------------------------------------------------===//
  // DeclContext management
  //===------------------------------------------------------------------===//

  DeclContext *getCurrentContext() const { return CurContext; }
  void PushDeclContext(DeclContext *DC);
  void PopDeclContext();
  void setCurContext(DeclContext *DC) { CurContext = DC; }

  //===------------------------------------------------------------------===//
  // Translation unit
  //===------------------------------------------------------------------===//

  TranslationUnitDecl *getCurTranslationUnitDecl() const { return CurTU; }
  void ActOnTranslationUnit(TranslationUnitDecl *TU);

  //===------------------------------------------------------------------===//
  // Declaration handling (ActOnXXX pattern)
  //===------------------------------------------------------------------===//

  DeclResult ActOnDeclarator(Decl *D);
  void ActOnFinishDecl(Decl *D);

  DeclResult ActOnVarDecl(SourceLocation Loc, llvm::StringRef Name,
                          QualType T, Expr *Init = nullptr);

  DeclResult ActOnFunctionDecl(SourceLocation Loc, llvm::StringRef Name,
                               QualType T,
                               llvm::ArrayRef<ParmVarDecl *> Params,
                               Stmt *Body = nullptr);

  void ActOnStartOfFunctionDef(FunctionDecl *FD);
  void ActOnFinishOfFunctionDef(FunctionDecl *FD);

  /// Process an enum constant declaration: evaluate and cache its value.
  /// Per C++ [dcl.enum], enum constant values are evaluated at declaration time.
  DeclResult ActOnEnumConstant(EnumConstantDecl *ECD);

  //===------------------------------------------------------------------===//
  // Expression handling
  //===------------------------------------------------------------------===//

  ExprResult ActOnExpr(Expr *E);

  ExprResult ActOnCallExpr(Expr *Fn, llvm::ArrayRef<Expr *> Args,
                           SourceLocation LParenLoc,
                           SourceLocation RParenLoc);

  ExprResult ActOnMemberExpr(Expr *Base, llvm::StringRef Member,
                             SourceLocation MemberLoc, bool IsArrow);

  ExprResult ActOnBinaryOperator(BinaryOpKind Op, Expr *LHS, Expr *RHS,
                                 SourceLocation OpLoc);

  ExprResult ActOnUnaryOperator(UnaryOpKind Op, Expr *Operand,
                                 SourceLocation OpLoc);

  ExprResult ActOnCastExpr(QualType TargetType, Expr *E,
                           SourceLocation LParenLoc,
                           SourceLocation RParenLoc);

  ExprResult ActOnArraySubscriptExpr(Expr *Base,
                                     llvm::ArrayRef<Expr *> Indices,
                                     SourceLocation LLoc,
                                     SourceLocation RLoc);

  ExprResult ActOnConditionalExpr(Expr *Cond, Expr *Then, Expr *Else,
                                  SourceLocation QuestionLoc,
                                  SourceLocation ColonLoc);

  //===------------------------------------------------------------------===//
  // Statement handling
  //===------------------------------------------------------------------===//

  StmtResult ActOnReturnStmt(Expr *RetVal, SourceLocation ReturnLoc);
  StmtResult ActOnIfStmt(Expr *Cond, Stmt *Then, Stmt *Else,
                         SourceLocation IfLoc);
  StmtResult ActOnWhileStmt(Expr *Cond, Stmt *Body,
                            SourceLocation WhileLoc);
  StmtResult ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                          SourceLocation ForLoc);
  StmtResult ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc);
  StmtResult ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                             SourceLocation SwitchLoc);
  StmtResult ActOnCaseStmt(Expr *Val, Stmt *Body, SourceLocation CaseLoc);
  StmtResult ActOnDefaultStmt(Stmt *Body, SourceLocation DefaultLoc);
  StmtResult ActOnBreakStmt(SourceLocation BreakLoc);
  StmtResult ActOnContinueStmt(SourceLocation ContinueLoc);
  StmtResult ActOnGotoStmt(llvm::StringRef Label, SourceLocation GotoLoc);
  StmtResult ActOnCompoundStmt(llvm::ArrayRef<Stmt *> Stmts,
                               SourceLocation LBraceLoc,
                               SourceLocation RBraceLoc);
  StmtResult ActOnDeclStmt(Decl *D);
  StmtResult ActOnNullStmt(SourceLocation Loc);

  //===------------------------------------------------------------------===//
  // Type handling [Stage 4.2]
  //===------------------------------------------------------------------===//

  bool isCompleteType(QualType T) const;
  bool RequireCompleteType(QualType T, SourceLocation Loc);
  QualType getCanonicalType(QualType T) const;

  //===------------------------------------------------------------------===//
  // Name lookup [Stage 4.3]
  //===------------------------------------------------------------------===//

  LookupResult LookupUnqualifiedName(llvm::StringRef Name, Scope *S,
                                      LookupNameKind Kind);

  LookupResult LookupQualifiedName(llvm::StringRef Name,
                                     NestedNameSpecifier *NNS);

  void LookupADL(llvm::StringRef Name,
                  llvm::ArrayRef<Expr *> Args,
                  LookupResult &Result);

  void CollectAssociatedNamespacesAndClasses(
      QualType T,
      llvm::SmallPtrSetImpl<NamespaceDecl *> &Namespaces,
      llvm::SmallPtrSetImpl<const RecordType *> &Classes);

  //===------------------------------------------------------------------===//
  // Overload resolution [Stage 4.4]
  //===------------------------------------------------------------------===//

  FunctionDecl *ResolveOverload(llvm::StringRef Name,
                                 llvm::ArrayRef<Expr *> Args,
                                 const LookupResult &Candidates);

  void AddOverloadCandidate(FunctionDecl *F,
                             llvm::ArrayRef<Expr *> Args,
                             OverloadCandidateSet &Set);

  //===------------------------------------------------------------------===//
  // Diagnostics helpers [Stage 4.5]
  //===------------------------------------------------------------------===//

  void Diag(SourceLocation Loc, DiagID ID);
  void Diag(SourceLocation Loc, DiagID ID, llvm::StringRef Extra);
};

} // namespace blocktype

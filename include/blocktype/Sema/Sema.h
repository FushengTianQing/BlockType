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
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/ConstraintSatisfaction.h"
#include "blocktype/Sema/SemaCXX.h"
#include "llvm/ADT/DenseMap.h"

#include <memory>

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

/// TypeResult - Wrapper for type semantic analysis results.
class TypeResult {
  QualType Val;
  bool Invalid = false;

public:
  TypeResult() = default;
  TypeResult(QualType T) : Val(T) {}

  static TypeResult getInvalid() {
    TypeResult R;
    R.Invalid = true;
    return R;
  }

  bool isInvalid() const { return Invalid; }
  bool isUsable() const { return !Val.isNull() && !Invalid; }
  QualType get() const { return Val; }
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

  /// Template instantiation engine [Stage 5.1]
  std::unique_ptr<TemplateInstantiator> Instantiator;

  /// Template argument deduction engine [Stage 5.2]
  std::unique_ptr<TemplateDeduction> Deduction;

  /// Concept constraint satisfaction checker [Stage 5.4]
  std::unique_ptr<ConstraintSatisfaction> ConstraintChecker;

  /// Scope stack - tracks the current lexical scope chain.
  Scope *CurrentScope = nullptr;

  //===------------------------------------------------------------------===//
  // Declaration registration helpers
  //===------------------------------------------------------------------===//

  /// Register a declaration in both the Scope chain and SymbolTable.
  void registerDecl(NamedDecl *ND) {
    if (CurrentScope) CurrentScope->addDecl(ND);
    Symbols.addDecl(ND);
  }

  /// Register a declaration allowing redeclaration (e.g., template params).
  void registerDeclAllowRedecl(NamedDecl *ND) {
    if (CurrentScope) CurrentScope->addDeclAllowRedeclaration(ND);
    Symbols.addDecl(ND);
  }

  /// Current DeclContext - tracks the current semantic context.
  DeclContext *CurContext = nullptr;

  /// Current function being analyzed (for return type checking).
  FunctionDecl *CurFunction = nullptr;

  /// Translation unit being processed.
  TranslationUnitDecl *CurTU = nullptr;

  /// Statement context depth counters for break/continue/case validation.
  /// BreakScopeDepth: incremented by loops and switch, checked by break
  /// ContinueScopeDepth: incremented by loops only, checked by continue
  /// SwitchScopeDepth: incremented by switch only, checked by case/default
  unsigned BreakScopeDepth = 0;
  unsigned ContinueScopeDepth = 0;
  unsigned SwitchScopeDepth = 0;

  /// Template definition nesting depth.
  /// Incremented when entering a template definition body,
  /// decremented when exiting.  When > 0, we are inside a
  /// template definition and name lookup should account for
  /// dependent names (two-phase lookup).
  unsigned TemplateDefinitionDepth = 0;

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

  /// Get the current function being processed (for contract result binding).
  FunctionDecl *getCurrentFunction() const { return CurFunction; }

  /// Access the type checker.
  TypeCheck &getTypeCheck() { return TC; }

  /// Access the constant expression evaluator.
  ConstantExprEvaluator &getConstEval() { return ConstEval; }

  /// Access the template instantiation engine.
  TemplateInstantiator &getTemplateInstantiator() { return *Instantiator; }

  /// Access the template deduction engine.
  TemplateDeduction &getTemplateDeduction() { return *Deduction; }

  /// Access the concept constraint checker.
  ConstraintSatisfaction &getConstraintChecker() { return *ConstraintChecker; }

  /// Access the symbol table.
  SymbolTable &getSymbolTable() { return Symbols; }

  //===------------------------------------------------------------------===//
  // Scope management
  //===------------------------------------------------------------------===//

  void PushScope(ScopeFlags Flags);
  void PopScope();
  Scope *getCurrentScope() const { return CurrentScope; }

  /// Look up a name: first in the Scope chain, then in SymbolTable.
  NamedDecl *LookupName(llvm::StringRef Name) const;

  /// Register a template parameter in the current scope (allows redeclaration).
  void RegisterTemplateParam(NamedDecl *ND) {
    registerDeclAllowRedecl(ND);
  }

  /// Returns true if we are currently inside a template definition body.
  /// Checks both the explicit depth counter and the scope chain for
  /// TemplateScope flags.
  bool isDependentContext() const {
    if (TemplateDefinitionDepth > 0) return true;
    // Also check if the current scope chain has a TemplateScope.
    for (Scope *S = CurrentScope; S; S = S->getParent()) {
      if (S->hasFlags(ScopeFlags::TemplateScope))
        return true;
    }
    return false;
  }

  /// Enter a template definition body. Increments the nesting depth.
  void EnterTemplateDefinition() { ++TemplateDefinitionDepth; }

  /// Exit a template definition body. Decrements the nesting depth.
  void ExitTemplateDefinition() {
    if (TemplateDefinitionDepth > 0) --TemplateDefinitionDepth;
  }

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
  TranslationUnitDecl *ActOnTranslationUnitDecl(SourceLocation Loc);

  //===------------------------------------------------------------------===//
  // Declaration handling (ActOnXXX pattern)
  //===------------------------------------------------------------------===//

  DeclResult ActOnDeclarator(Decl *D);
  void ActOnFinishDecl(Decl *D);

  DeclResult ActOnVarDecl(SourceLocation Loc, llvm::StringRef Name,
                          QualType T, Expr *Init = nullptr);

  // P7.4.2: Placeholder variable `_` (P2169R4)
  /// Handle placeholder variable declaration `_`
  ///
  /// **Rules**:
  /// - `_` is not added to symbol table (each `_` is a new variable)
  /// - No "unused variable" warning
  /// - Multiple `auto _ = expr` in same scope are allowed
  DeclResult ActOnPlaceholderVarDecl(SourceLocation Loc, QualType T, Expr *Init);

  /// Check if identifier is the placeholder `_`
  static bool isPlaceholderIdentifier(llvm::StringRef Name) {
    return Name == "_";
  }

  // P7.4.3: Structured binding extensions (P0963R3, P1061R10)
  /// Create structured binding declaration group
  ///
  /// `auto [a, b, c] = expr` → creates multiple BindingDecl
  ///
  /// **Rules**:
  /// - Number of bindings must match std::tuple_size
  /// - Each binding extracts via std::get<N>
  /// - Binding variable types are deduced as auto
  ///
  /// **Clang reference**:
  /// - `clang/lib/Sema/SemaDeclCXX.cpp` BuildDecompositionDecl()
  /// - `clang/include/clang/AST/DeclCXX.h` DecompositionDecl
  DeclResult ActOnDecompositionDecl(SourceLocation Loc,
                                     llvm::ArrayRef<llvm::StringRef> Names,
                                     QualType TupleType,
                                     Expr *Init);

  /// Check if structured binding can be used in condition expression (P0963R3)
  bool CheckBindingCondition(llvm::ArrayRef<class BindingDecl *> Bindings,
                              SourceLocation Loc);

  DeclResult ActOnFunctionDecl(SourceLocation Loc, llvm::StringRef Name,
                               QualType T,
                               llvm::ArrayRef<ParmVarDecl *> Params,
                               Stmt *Body = nullptr);

  void ActOnStartOfFunctionDef(FunctionDecl *FD);
  void ActOnFinishOfFunctionDef(FunctionDecl *FD);

  /// Process an enum constant declaration: evaluate and cache its value.
  /// Per C++ [dcl.enum], enum constant values are evaluated at declaration time.
  DeclResult ActOnEnumConstant(EnumConstantDecl *ECD);
  DeclResult ActOnEnumConstantDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                           QualType EnumType, Expr *Init);

  // Declaration factory methods (Phase 2D)
  StmtResult ActOnDeclStmtFromDecl(Decl *D);
  DeclResult ActOnTypeAliasDecl(SourceLocation Loc, llvm::StringRef Name,
                                QualType Underlying);
  DeclResult ActOnUsingDecl(SourceLocation Loc, llvm::StringRef Name,
                            llvm::StringRef NestedName, bool HasNested,
                            bool IsInheritingCtor = false);
  DeclResult ActOnParmVarDecl(SourceLocation Loc, llvm::StringRef Name,
                              QualType T, unsigned Index, Expr *DefaultArg);
  DeclResult ActOnNamespaceDecl(SourceLocation Loc, llvm::StringRef Name,
                                bool IsInline);
  DeclResult ActOnUsingEnumDecl(SourceLocation Loc, llvm::StringRef EnumName,
                                llvm::StringRef NestedName, bool HasNested);
  DeclResult ActOnUsingDirectiveDecl(SourceLocation Loc, llvm::StringRef Name,
                                     llvm::StringRef NestedName, bool HasNested);
  DeclResult ActOnNamespaceAliasDecl(SourceLocation Loc, llvm::StringRef Alias,
                                     llvm::StringRef Target,
                                     llvm::StringRef NestedName);
  DeclResult ActOnModuleDecl(SourceLocation Loc, llvm::StringRef Name,
                             bool IsExported, llvm::StringRef Partition,
                             bool IsPartition, bool IsGlobalFragment,
                             bool IsPrivateFragment);
  DeclResult ActOnImportDecl(SourceLocation Loc, llvm::StringRef ModuleName,
                             bool IsExported, llvm::StringRef Partition,
                             llvm::StringRef Header, bool IsHeader);
  DeclResult ActOnExportDecl(SourceLocation Loc, Decl *Exported);
  DeclResult ActOnEnumDecl(SourceLocation Loc, llvm::StringRef Name);
  DeclResult ActOnTypedefDecl(SourceLocation Loc, llvm::StringRef Name,
                              QualType T);
  DeclResult ActOnStaticAssertDecl(SourceLocation Loc, Expr *Cond,
                                   llvm::StringRef Message);
  DeclResult ActOnLinkageSpecDecl(SourceLocation Loc,
                                  LinkageSpecDecl::Language Lang,
                                  bool HasBraces);
  DeclResult ActOnAsmDecl(SourceLocation Loc, llvm::StringRef AsmString);
  DeclResult ActOnCXXDeductionGuideDecl(SourceLocation Loc,
                                        llvm::StringRef TemplateName,
                                        QualType ReturnType,
                                        llvm::ArrayRef<ParmVarDecl *> Params);
  DeclResult ActOnAttributeListDecl(SourceLocation Loc);
  DeclResult ActOnAttributeDecl(SourceLocation Loc, llvm::StringRef Name,
                                Expr *Arg);
  DeclResult ActOnAttributeDeclWithNamespace(SourceLocation Loc,
                                             llvm::StringRef Namespace,
                                             llvm::StringRef Name, Expr *Arg);
  DeclResult ActOnVarDeclFull(SourceLocation Loc, llvm::StringRef Name,
                              QualType T, Expr *Init, bool IsStatic);
  DeclResult ActOnFunctionDeclFull(SourceLocation Loc, llvm::StringRef Name,
                                   QualType T,
                                   llvm::ArrayRef<ParmVarDecl *> Params,
                                   Stmt *Body, bool IsInline,
                                   bool IsConstexpr, bool IsConsteval);

  // Class member factory methods (Phase 2E)
  void ActOnCXXRecordDecl(CXXRecordDecl *RD);
  void ActOnCXXMethodDecl(CXXMethodDecl *MD);
  void ActOnFieldDecl(FieldDecl *FD);
  void ActOnAccessSpecDecl(AccessSpecDecl *ASD);
  void ActOnCXXConstructorDecl(CXXConstructorDecl *CD);
  void ActOnCXXDestructorDecl(CXXDestructorDecl *DD);
  void ActOnFriendDecl(FriendDecl *FD);

  // Class declaration factory methods (Phase 3C)
  DeclResult ActOnCXXRecordDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                       TagDecl::TagKind Kind);
  DeclResult ActOnCXXMethodDeclFactory(SourceLocation Loc, llvm::StringRef Name,
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
                                       AccessSpecifier Access);
  DeclResult ActOnCXXConstructorDeclFactory(SourceLocation Loc,
                                            CXXRecordDecl *Class,
                                            llvm::ArrayRef<ParmVarDecl *> Params,
                                            Stmt *Body, bool IsExplicit);
  DeclResult ActOnCXXDestructorDeclFactory(SourceLocation Loc,
                                           CXXRecordDecl *Class, Stmt *Body);
  DeclResult ActOnFriendTypeDecl(SourceLocation FriendLoc,
                                 llvm::StringRef TypeName,
                                 SourceLocation TypeNameLoc);
  DeclResult ActOnFriendFunctionDecl(SourceLocation FriendLoc,
                                     SourceLocation NameLoc,
                                     llvm::StringRef Name, QualType Type,
                                     llvm::ArrayRef<ParmVarDecl *> Params);

  DeclResult ActOnFieldDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                   QualType Type, Expr *BitWidth,
                                   bool IsMutable, Expr *InClassInit,
                                   AccessSpecifier Access);
  DeclResult ActOnAccessSpecDeclFactory(SourceLocation Loc,
                                        AccessSpecifier Access,
                                        SourceLocation ColonLoc);

  // Template parameter factory methods (Phase 3D)
  DeclResult ActOnTemplateTypeParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                       unsigned Depth, unsigned Index,
                                       bool IsParameterPack, bool IsTypename);
  DeclResult ActOnNonTypeTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                          QualType Type, unsigned Depth,
                                          unsigned Index, bool IsParameterPack);
  DeclResult ActOnTemplateTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                                           unsigned Depth, unsigned Index,
                                           bool IsParameterPack);

  // Template wrapper factory methods (Phase 3E)
  DeclResult ActOnTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                      Decl *TemplatedDecl);
  DeclResult ActOnClassTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                           Decl *TemplatedDecl);
  DeclResult ActOnFunctionTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                              Decl *TemplatedDecl);
  DeclResult ActOnVarTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                         Decl *TemplatedDecl);
  DeclResult ActOnTypeAliasTemplateDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                               Decl *TemplatedDecl);
  DeclResult ActOnClassTemplateSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                        ClassTemplateDecl *PrimaryTemplate,
                                        llvm::ArrayRef<TemplateArgument> Args,
                                        bool IsExplicit);
  DeclResult ActOnVarTemplateSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                      QualType Type, VarTemplateDecl *PrimaryTemplate,
                                      llvm::ArrayRef<TemplateArgument> Args,
                                      Expr *Init, bool IsExplicit);
  DeclResult ActOnClassTemplatePartialSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                               ClassTemplateDecl *PrimaryTemplate,
                                               llvm::ArrayRef<TemplateArgument> Args);
  DeclResult ActOnVarTemplatePartialSpecDecl(SourceLocation Loc, llvm::StringRef Name,
                                             QualType Type,
                                             VarTemplateDecl *PrimaryTemplate,
                                             llvm::ArrayRef<TemplateArgument> Args,
                                             Expr *Init);
  DeclResult ActOnConceptDeclFactory(SourceLocation Loc, llvm::StringRef Name,
                                     Expr *Constraint, SourceLocation TemplateLoc,
                                     llvm::ArrayRef<NamedDecl *> TemplateParams);

  //===------------------------------------------------------------------===//
  // Expression handling
  //===------------------------------------------------------------------===//

  ExprResult ActOnExpr(Expr *E);

  // Literal expressions (Phase 2A)
  ExprResult ActOnIntegerLiteral(SourceLocation Loc, llvm::APInt Value);
  ExprResult ActOnFloatingLiteral(SourceLocation Loc, llvm::APFloat Value);
  ExprResult ActOnStringLiteral(SourceLocation Loc, llvm::StringRef Text);
  ExprResult ActOnCharacterLiteral(SourceLocation Loc, uint32_t Value);
  ExprResult ActOnCXXBoolLiteral(SourceLocation Loc, bool Value);
  ExprResult ActOnCXXNullPtrLiteral(SourceLocation Loc);

  // Expression factory methods (Phase 2C)
  ExprResult ActOnDeclRefExpr(SourceLocation Loc, ValueDecl *D);
  ExprResult ActOnUnaryExprOrTypeTraitExpr(SourceLocation Loc,
                                           UnaryExprOrTypeTrait Kind,
                                           QualType T);
  ExprResult ActOnUnaryExprOrTypeTraitExpr(SourceLocation Loc,
                                           UnaryExprOrTypeTrait Kind,
                                           Expr *Arg);
  ExprResult ActOnInitListExpr(SourceLocation LBraceLoc,
                               llvm::ArrayRef<Expr *> Inits,
                               SourceLocation RBraceLoc,
                               QualType ExpectedType = QualType());
  ExprResult ActOnDesignatedInitExpr(SourceLocation DotLoc,
                                     llvm::ArrayRef<DesignatedInitExpr::Designator> Designators,
                                     Expr *Init);
  ExprResult ActOnTemplateSpecializationExpr(SourceLocation Loc,
                                             llvm::StringRef Name,
                                             llvm::ArrayRef<TemplateArgument> Args,
                                             ValueDecl *VD);
  ExprResult ActOnMemberExprDirect(SourceLocation OpLoc, Expr *Base,
                                   ValueDecl *MemberDecl, bool IsArrow);
  ExprResult ActOnCXXConstructExpr(SourceLocation Loc,
                                   QualType ConstructedType,
                                   llvm::ArrayRef<Expr *> Args);
  ExprResult ActOnCXXNewExprFactory(SourceLocation NewLoc, Expr *ArraySize,
                                    Expr *Initializer, QualType Type);
  ExprResult ActOnCXXDeleteExprFactory(SourceLocation DeleteLoc, Expr *Argument,
                                       bool IsArrayDelete,
                                       QualType AllocatedType);
  ExprResult ActOnCXXThisExpr(SourceLocation Loc);
  ExprResult ActOnCXXThrowExpr(SourceLocation Loc, Expr *Operand);
  ExprResult ActOnCXXNamedCastExpr(SourceLocation CastLoc, Expr *SubExpr,
                                   llvm::StringRef CastKind);
  ExprResult ActOnCXXNamedCastExprWithType(SourceLocation CastLoc,
                                           Expr *SubExpr, QualType CastType,
                                           llvm::StringRef CastKind);
  ExprResult ActOnPackIndexingExpr(SourceLocation Loc, Expr *Pack, Expr *Index);

  //===------------------------------------------------------------------===//
  // P7.2.1: reflexpr expression (C++26 reflection)
  //===------------------------------------------------------------------===//

  /// ActOnReflexprExpr - Semantic analysis for reflexpr(expression).
  ExprResult ActOnReflexprExpr(SourceLocation Loc, Expr *Arg);

  /// ActOnReflexprTypeExpr - Semantic analysis for reflexpr(type-id).
  ExprResult ActOnReflexprTypeExpr(SourceLocation Loc, QualType T);

  //===------------------------------------------------------------------===//
  // P7.2.2: Built-in reflection functions
  //===------------------------------------------------------------------===//

  /// Process __reflect_type(expr) built-in.
  ExprResult ActOnReflectTypeBuiltin(SourceLocation Loc, Expr *E);

  /// Process __reflect_members(type) built-in.
  ExprResult ActOnReflectMembersBuiltin(SourceLocation Loc, QualType T);

  //===------------------------------------------------------------------===//
  // P7.1.2: Decay-copy expression (P0849R8)
  //===------------------------------------------------------------------===//

  /// Process a decay-copy expression auto(expr) or auto{expr}.
  ///
  /// Performs decay (remove references, top-level cv, array-to-pointer,
  /// function-to-pointer) and creates a DecayCopyExpr.
  ExprResult ActOnDecayCopyExpr(SourceLocation AutoLoc, Expr *SubExpr,
                                bool IsDirectInit);

  //===------------------------------------------------------------------===//
  // P7.1.4: [[assume]] attribute (P1774R8)
  //===------------------------------------------------------------------===//

  /// Process an [[assume]] attribute.
  ///
  /// Validates that the condition is contextually convertible to bool
  /// and creates the appropriate AST attribute.
  ExprResult ActOnAssumeAttr(SourceLocation Loc, Expr *Condition);

  //===------------------------------------------------------------------===//
  // P7.3.1: C++26 Contracts (P2900R14)
  //===------------------------------------------------------------------===//

  /// Process a parsed contract attribute [[pre:]]/[[post:]]/[[assert:]].
  ///
  /// Validates the condition and creates a ContractAttr AST node.
  DeclResult ActOnContractAttr(SourceLocation Loc,
                               unsigned Kind, // ContractKind as unsigned
                               Expr *Condition);

  /// Wire contract attributes to a function declaration.
  void AttachContractsToFunction(FunctionDecl *FD,
                                  llvm::ArrayRef<Decl *> Contracts);

  // Complex expression factory methods (Phase 2C)
  ExprResult ActOnLambdaExpr(SourceLocation Loc,
                             llvm::ArrayRef<LambdaCapture> Captures,
                             llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body,
                             bool IsMutable, QualType ReturnType,
                             SourceLocation LBraceLoc,
                             SourceLocation RBraceLoc,
                             TemplateParameterList *TemplateParams,
                             class AttributeListDecl *Attrs);
  ExprResult ActOnCXXFoldExpr(SourceLocation Loc, Expr *LHS, Expr *RHS,
                              Expr *Pattern, BinaryOpKind Op,
                              bool IsRightFold);
  ExprResult ActOnRequiresExpr(SourceLocation Loc,
                               llvm::ArrayRef<Requirement *> Requirements,
                               SourceLocation RequiresLoc,
                               SourceLocation RBraceLoc);

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
                         SourceLocation IfLoc,
                         VarDecl *CondVar = nullptr,
                         bool IsConsteval = false,
                         bool IsNegated = false);
  StmtResult ActOnWhileStmt(Expr *Cond, Stmt *Body,
                            SourceLocation WhileLoc,
                            VarDecl *CondVar = nullptr);
  StmtResult ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                          SourceLocation ForLoc);
  StmtResult ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc);
  StmtResult ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                             SourceLocation SwitchLoc,
                             VarDecl *CondVar = nullptr);
  StmtResult ActOnCaseStmt(Expr *Val, Expr *RHS, Stmt *Body,
                           SourceLocation CaseLoc);
  StmtResult ActOnDefaultStmt(Stmt *Body, SourceLocation DefaultLoc);
  StmtResult ActOnBreakStmt(SourceLocation BreakLoc);
  StmtResult ActOnContinueStmt(SourceLocation ContinueLoc);
  StmtResult ActOnGotoStmt(llvm::StringRef Label, SourceLocation GotoLoc);
  StmtResult ActOnCompoundStmt(llvm::ArrayRef<Stmt *> Stmts,
                               SourceLocation LBraceLoc,
                               SourceLocation RBraceLoc);
  StmtResult ActOnDeclStmt(Decl *D);
  StmtResult ActOnNullStmt(SourceLocation Loc);

  // Label and expression statements (Phase 2B)
  StmtResult ActOnExprStmt(SourceLocation Loc, Expr *E);
  StmtResult ActOnLabelStmt(SourceLocation Loc, llvm::StringRef LabelName,
                            Stmt *SubStmt);

  // C++ statement extensions (Phase 2B)
  StmtResult ActOnCXXForRangeStmt(SourceLocation ForLoc,
                                  SourceLocation VarLoc, llvm::StringRef VarName,
                                  QualType VarType, Expr *Range, Stmt *Body);
  StmtResult ActOnCXXTryStmt(SourceLocation TryLoc, Stmt *TryBlock,
                             llvm::ArrayRef<Stmt *> Handlers);
  StmtResult ActOnCXXCatchStmt(SourceLocation CatchLoc, VarDecl *ExceptionDecl,
                               Stmt *HandlerBlock);
  StmtResult ActOnCoreturnStmt(SourceLocation Loc, Expr *RetVal);
  StmtResult ActOnCoyieldStmt(SourceLocation Loc, Expr *Value);
  ExprResult ActOnCoawaitExpr(SourceLocation Loc, Expr *Operand);

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
  // Template handling [Stage 5.1]
  //===------------------------------------------------------------------===//

  /// Process a class template declaration.
  /// @param CTD  ClassTemplateDecl already created by Parser
  /// @return     Semantic analysis result
  DeclResult ActOnClassTemplateDecl(ClassTemplateDecl *CTD);

  /// Process a function template declaration.
  DeclResult ActOnFunctionTemplateDecl(FunctionTemplateDecl *FTD);

  /// Process a variable template declaration.
  DeclResult ActOnVarTemplateDecl(VarTemplateDecl *VTD);

  /// Process an alias template declaration.
  DeclResult ActOnTypeAliasTemplateDecl(TypeAliasTemplateDecl *TATD);

  /// Process a Concept declaration.
  DeclResult ActOnConceptDecl(ConceptDecl *CD);

  /// Process a template-id reference (e.g., vector<int>).
  /// @param Name      Template name
  /// @param Args      Template arguments with source locations
  /// @param NameLoc   Template name location
  /// @param LAngleLoc < location
  /// @param RAngleLoc > location
  /// @return          Type or expression result
  TypeResult ActOnTemplateId(llvm::StringRef Name,
                              llvm::ArrayRef<TemplateArgumentLoc> Args,
                              SourceLocation NameLoc,
                              SourceLocation LAngleLoc,
                              SourceLocation RAngleLoc);

  /// Process an explicit specialization (template<> class X<T> { ... }).
  DeclResult ActOnExplicitSpecialization(SourceLocation TemplateLoc,
                                          SourceLocation LAngleLoc,
                                          SourceLocation RAngleLoc);

  /// Process an explicit instantiation (template class X<int>;).
  DeclResult ActOnExplicitInstantiation(SourceLocation TemplateLoc, Decl *D);

  /// Process a class template partial specialization.
  /// @param PartialSpec  The partial specialization decl (already created by Parser)
  /// @return             Semantic analysis result
  DeclResult ActOnClassTemplatePartialSpecialization(
      ClassTemplatePartialSpecializationDecl *PartialSpec);

  /// Process a variable template partial specialization.
  DeclResult ActOnVarTemplatePartialSpecialization(
      VarTemplatePartialSpecializationDecl *PartialSpec);

  /// Find the best matching partial specialization for the given arguments.
  /// Per C++ [temp.class.spec.match]: selects the most specialized partial
  /// specialization whose args match. Returns nullptr if no partial
  /// specialization matches, or if the match is ambiguous.
  ClassTemplatePartialSpecializationDecl *
  FindBestMatchingPartialSpecialization(
      ClassTemplateDecl *Primary,
      llvm::ArrayRef<TemplateArgument> Args);

  /// Deduce template arguments and instantiate a function template.
  /// Used by ActOnCallExpr when the callee is a function template.
  /// @param FTD       The function template declaration
  /// @param Args      Call arguments
  /// @param CallLoc   Location of the call (for diagnostics)
  /// @return          Instantiated FunctionDecl, or nullptr on failure
  FunctionDecl *DeduceAndInstantiateFunctionTemplate(
      FunctionTemplateDecl *FTD, llvm::ArrayRef<Expr *> Args,
      SourceLocation CallLoc);

  //===------------------------------------------------------------------===//
  // Diagnostics helpers [Stage 4.5]
  //===------------------------------------------------------------------===//

  void Diag(SourceLocation Loc, DiagID ID);
  void Diag(SourceLocation Loc, DiagID ID, llvm::StringRef Extra);

  /// Emit warnings for unused declarations and unreachable code.
  /// Should be called after parsing is complete.
  void DiagnoseUnusedDecls(TranslationUnitDecl *TU);

  //===------------------------------------------------------------------===//
  // InitListExpr type propagation helpers
  //===------------------------------------------------------------------===//

  /// Propagate expected types to nested InitListExpr children.
  /// This serves as a safety net for cases where Parser-level type
  /// propagation (Plan A) could not determine the type.
  void propagateTypesToNestedInitLists(InitListExpr *ILE,
                                       QualType ExpectedType);

  /// Deduce the expected type for the Index-th element of an aggregate.
  /// Used by propagateTypesToNestedInitLists to determine child types.
  QualType deduceElementTypeForInitList(QualType AggrType, unsigned Index);
};

} // namespace blocktype

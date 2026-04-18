//===--- TemplateInstantiation.cpp - Template Instantiation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/ConstantExpr.h"
#include "blocktype/Sema/Lookup.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// TemplateArgumentList
//===----------------------------------------------------------------------===//

llvm::ArrayRef<TemplateArgument>
TemplateArgumentList::getPackArgument() const {
  for (const auto &Arg : Args) {
    if (Arg.isPack())
      return Arg.getAsPack();
  }
  return {};
}

//===----------------------------------------------------------------------===//
// TemplateInstantiator
//===----------------------------------------------------------------------===//

TemplateInstantiator::TemplateInstantiator(Sema &S)
    : SemaRef(S), Context(S.getASTContext()) {}

//===----------------------------------------------------------------------===//
// Class Template Instantiation
//===----------------------------------------------------------------------===//

ClassTemplateSpecializationDecl *
TemplateInstantiator::InstantiateClassTemplate(
    ClassTemplateDecl *Template, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Template)
    return nullptr;

  // 1. Check cache for existing specialization
  if (auto *Spec = FindExistingSpecialization(Template, Args))
    return Spec;

  // 2. Check recursion depth
  if (CurrentDepth >= MaxInstantiationDepth) {
    if (!isSFINAEContext())
      SemaRef.getDiagnostics().report(Template->getLocation(),
                                      DiagID::err_template_recursion);
    return nullptr;
  }

  // 3. Create new specialization decl
  auto *Spec = Context.create<ClassTemplateSpecializationDecl>(
      Template->getLocation(), Template->getName(), Template, Args);

  // 4. Get the pattern (templated CXXRecordDecl)
  CXXRecordDecl *Pattern =
      llvm::dyn_cast_or_null<CXXRecordDecl>(Template->getTemplatedDecl());
  if (!Pattern)
    return Spec;

  // Mark as complete definition
  Spec->setCompleteDefinition(true);

  // 5. Build argument mapping
  TemplateArgumentList ArgList(Args);
  ++CurrentDepth;

  // 6. Iterate pattern members, substitute template parameters
  for (Decl *M : Pattern->members()) {
    Decl *InstM = SubstituteDecl(M, ArgList, Spec);
    if (InstM)
      Spec->addMember(InstM);
  }

  // 7. Iterate fields
  for (FieldDecl *F : Pattern->fields()) {
    FieldDecl *InstF = SubstituteFieldDecl(F, ArgList);
    if (InstF)
      Spec->addField(InstF);
  }

  // 8. Iterate methods
  for (CXXMethodDecl *MD : Pattern->methods()) {
    CXXMethodDecl *InstMD = SubstituteCXXMethodDecl(MD, ArgList, Spec);
    if (InstMD)
      Spec->addMethod(InstMD);
  }

  // 9. Register specialization
  Template->addSpecialization(Spec);

  --CurrentDepth;
  return Spec;
}

//===----------------------------------------------------------------------===//
// Function Template Instantiation
//===----------------------------------------------------------------------===//

FunctionDecl *
TemplateInstantiator::InstantiateFunctionTemplate(
    FunctionTemplateDecl *Template, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Template)
    return nullptr;

  // Get the pattern function
  FunctionDecl *Pattern =
      llvm::dyn_cast_or_null<FunctionDecl>(Template->getTemplatedDecl());
  if (!Pattern)
    return nullptr;

  if (CurrentDepth >= MaxInstantiationDepth) {
    if (!isSFINAEContext())
      SemaRef.getDiagnostics().report(Template->getLocation(),
                                      DiagID::err_template_recursion);
    return nullptr;
  }

  TemplateArgumentList ArgList(Args);
  ++CurrentDepth;

  // Substitute return type
  QualType RetType = SubstituteType(Pattern->getType(), ArgList);
  if (RetType.isNull())
    RetType = Pattern->getType();

  // Substitute parameter types
  llvm::SmallVector<ParmVarDecl *, 8> InstParams;
  for (unsigned I = 0; I < Pattern->getNumParams(); ++I) {
    ParmVarDecl *PVD = Pattern->getParamDecl(I);
    QualType ParamType = SubstituteType(PVD->getType(), ArgList);
    if (ParamType.isNull())
      ParamType = PVD->getType();

    auto *InstPVD = Context.create<ParmVarDecl>(
        PVD->getLocation(), PVD->getName(), ParamType,
        PVD->getFunctionScopeIndex());
    InstParams.push_back(InstPVD);
  }

  // Create the instantiated function
  auto *InstFD = Context.create<FunctionDecl>(
      Pattern->getLocation(), Pattern->getName(), RetType, InstParams,
      Pattern->getBody(), Pattern->isInline(), Pattern->isConstexpr());

  --CurrentDepth;
  return InstFD;
}

//===----------------------------------------------------------------------===//
// Pack Expansion
//===----------------------------------------------------------------------===//

llvm::SmallVector<Expr *, 4>
TemplateInstantiator::ExpandPack(Expr *Pattern,
                                 const TemplateArgumentList &Args) {
  llvm::SmallVector<Expr *, 4> Result;

  // Collect ALL pack arguments (support multi-pack expansion).
  // Per C++ [temp.variadic]: when multiple packs appear in a pattern,
  // all packs must have the same length and are expanded simultaneously.
  llvm::SmallVector<llvm::ArrayRef<TemplateArgument>, 4> AllPacks;
  llvm::ArrayRef<TemplateArgument> ArgList = Args.asArray();
  for (const auto &Arg : ArgList) {
    if (Arg.isPack())
      AllPacks.push_back(Arg.getAsPack());
  }

  if (AllPacks.empty())
    return Result;

  // Verify all packs have the same length
  size_t PackSize = AllPacks[0].size();
  for (unsigned P = 1; P < AllPacks.size(); ++P) {
    if (AllPacks[P].size() != PackSize) {
      // Pack length mismatch — per C++ this is ill-formed.
      // Return empty to signal failure.
      return Result;
    }
  }

  // Expand: for each index, build a substitution list with one element
  // from each pack, then substitute.
  for (unsigned I = 0; I < PackSize; ++I) {
    TemplateArgumentList SingleArg;
    for (unsigned P = 0; P < AllPacks.size(); ++P)
      SingleArg.push_back(AllPacks[P][I]);

    Expr *Inst = SubstituteExpr(Pattern, SingleArg);
    if (Inst)
      Result.push_back(Inst);
  }

  return Result;
}

llvm::SmallVector<QualType, 4>
TemplateInstantiator::ExpandPackType(QualType Pattern,
                                     const TemplateArgumentList &Args) {
  llvm::SmallVector<QualType, 4> Result;

  // Collect ALL pack arguments (support multi-pack expansion)
  llvm::SmallVector<llvm::ArrayRef<TemplateArgument>, 4> AllPacks;
  llvm::ArrayRef<TemplateArgument> ArgList = Args.asArray();
  for (const auto &Arg : ArgList) {
    if (Arg.isPack())
      AllPacks.push_back(Arg.getAsPack());
  }

  if (AllPacks.empty())
    return Result;

  // Verify all packs have the same length
  size_t PackSize = AllPacks[0].size();
  for (unsigned P = 1; P < AllPacks.size(); ++P) {
    if (AllPacks[P].size() != PackSize)
      return Result;
  }

  for (unsigned I = 0; I < PackSize; ++I) {
    TemplateArgumentList SingleArg;
    for (unsigned P = 0; P < AllPacks.size(); ++P)
      SingleArg.push_back(AllPacks[P][I]);

    QualType SubT = SubstituteType(Pattern, SingleArg);
    if (!SubT.isNull())
      Result.push_back(SubT);
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// Fold Expression Instantiation
//===----------------------------------------------------------------------===//

Expr *TemplateInstantiator::getIdentityElement(BinaryOpKind Op,
                                                SourceLocation Loc) {
  // Return the identity/neutral element for each operator
  switch (Op) {
  case BinaryOpKind::Add:
    // + → 0
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 0));
  case BinaryOpKind::Mul:
    // * → 1
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 1));
  case BinaryOpKind::LAnd:
    // && → true (represented as integer 1)
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(1, 1));
  case BinaryOpKind::LOr:
    // || → false (represented as integer 0)
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(1, 0));
  case BinaryOpKind::And:
    // & → ~0 (all bits set)
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(32, ~0U));
  case BinaryOpKind::Or:
    // | → 0
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 0));
  case BinaryOpKind::Xor:
    // ^ → 0
    return Context.create<IntegerLiteral>(Loc, llvm::APInt(32, 0));
  default:
    // Comma, comparison operators, etc. have no identity element
    return nullptr;
  }
}

Expr *TemplateInstantiator::InstantiateFoldExpr(
    CXXFoldExpr *FE, const TemplateArgumentList &Args) {
  if (!FE)
    return nullptr;

  // Expand the pack pattern
  llvm::SmallVector<Expr *, 4> Elems = ExpandPack(FE->getPattern(), Args);

  // Get LHS and RHS (init values for binary folds)
  Expr *LHS = FE->getLHS() ? SubstituteExpr(FE->getLHS(), Args) : nullptr;
  Expr *RHS = FE->getRHS() ? SubstituteExpr(FE->getRHS(), Args) : nullptr;

  // Empty pack expansion
  if (Elems.empty()) {
    // For unary fold with empty pack, return identity element
    if (!LHS && !RHS) {
      Expr *Identity = getIdentityElement(FE->getOperator(), FE->getLocation());
      if (Identity)
        return Identity;
      // No identity element defined → return the pattern as-is (error case)
      return FE;
    }
    // Binary fold with empty pack: return the init value
    if (LHS)
      return LHS;
    if (RHS)
      return RHS;
    return FE;
  }

  Expr *Result = Elems[0];
  BinaryOpKind Op = FE->getOperator();

  if (FE->isRightFold()) {
    // Right fold: init op elems[n-1] op ... op elems[1] op elems[0]
    // Build from right to left: start with last element
    Result = Elems[Elems.size() - 1];
    for (int I = static_cast<int>(Elems.size()) - 2; I >= 0; --I) {
      auto *BO = Context.create<BinaryOperator>(
          FE->getLocation(), Elems[I], Result, Op);
      Result = BO;
    }
    // Prepend init value (RHS for right fold): init op result
    if (RHS) {
      auto *BO = Context.create<BinaryOperator>(
          FE->getLocation(), RHS, Result, Op);
      Result = BO;
    }
  } else {
    // Left fold: elems[0] op elems[1] op ... op elems[n-1] op init
    Result = Elems[0];
    for (unsigned I = 1; I < Elems.size(); ++I) {
      auto *BO = Context.create<BinaryOperator>(
          FE->getLocation(), Result, Elems[I], Op);
      Result = BO;
    }
    // Append init value (LHS for left fold): result op init
    if (LHS) {
      auto *BO = Context.create<BinaryOperator>(
          FE->getLocation(), Result, LHS, Op);
      Result = BO;
    }
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// Pack Indexing Expression Instantiation
//===----------------------------------------------------------------------===//

Expr *TemplateInstantiator::InstantiatePackIndexingExpr(
    PackIndexingExpr *PIE, const TemplateArgumentList &Args) {
  if (!PIE)
    return nullptr;

  // 1. Evaluate the index expression (must be a constant)
  Expr *IndexExpr = SubstituteExpr(PIE->getIndex(), Args);
  if (!IndexExpr)
    return nullptr;

  // Try to evaluate as constant
  ConstantExprEvaluator Eval(Context);
  auto Result = Eval.Evaluate(IndexExpr);
  if (!Result.isSuccess() || !Result.isIntegral())
    return PIE; // Cannot evaluate index → return as-is

  llvm::APSInt IndexVal = Result.getInt();
  uint64_t Index = IndexVal.getZExtValue();

  // 2. Get the pack arguments
  llvm::ArrayRef<TemplateArgument> PackArgs = Args.getPackArgument();
  if (PackArgs.empty())
    return PIE;

  // 3. Index into the pack
  if (Index >= PackArgs.size())
    return PIE; // Out of bounds → return as-is

  const TemplateArgument &Selected = PackArgs[Index];
  if (Selected.isType()) {
    // Create a placeholder expression that carries the selected type.
    // We use a CXXBoolLiteral as a carrier since it has a simple constructor
    // that accepts a type, and then override its type with the selected type.
    QualType SelectedType = Selected.getAsType();
    auto *Result = Context.create<CXXBoolLiteral>(
        PIE->getLocation(), true, SelectedType);
    return Result;
  }
  if (Selected.isExpression()) {
    return const_cast<Expr *>(Selected.getAsExpr());
  }

  return PIE;
}

//===----------------------------------------------------------------------===//
// Type Substitution
//===----------------------------------------------------------------------===//

QualType TemplateInstantiator::SubstituteType(
    QualType T, const TemplateArgumentList &Args) {
  if (T.isNull())
    return T;

  const Type *Ty = T.getTypePtr();

  // TemplateTypeParmType → look up in mapping
  // Preserve original CVR qualifiers on the substituted result.
  // e.g., "const T" where T=int should produce "const int", not just "int".
  if (auto *TTP = llvm::dyn_cast<TemplateTypeParmType>(Ty)) {
    QualType Result = SubstituteTemplateTypeParmType(TTP, Args);
    if (Result.isNull())
      return Result;
    // Merge original qualifiers into the substituted type
    Qualifier OrigQuals = T.getQualifiers();
    if (OrigQuals != Qualifier::None &&
        Result.getQualifiers() != OrigQuals) {
      return QualType(Result.getTypePtr(), OrigQuals | Result.getQualifiers());
    }
    return Result;
  }

  // TemplateSpecializationType → recursively substitute inner args
  if (auto *TST = llvm::dyn_cast<TemplateSpecializationType>(Ty))
    return SubstituteTemplateSpecializationType(TST, Args);

  // DependentType → try to resolve
  // Preserve original CVR qualifiers on the substituted result.
  if (auto *DT = llvm::dyn_cast<DependentType>(Ty)) {
    QualType Result = SubstituteDependentType(DT, Args);
    if (Result.isNull())
      return Result;
    Qualifier OrigQuals = T.getQualifiers();
    if (OrigQuals != Qualifier::None &&
        Result.getQualifiers() != OrigQuals) {
      return QualType(Result.getTypePtr(), OrigQuals | Result.getQualifiers());
    }
    return Result;
  }

  // PointerType → recursively substitute pointee
  if (auto *PT = llvm::dyn_cast<PointerType>(Ty)) {
    QualType SubPointee =
        SubstituteType(QualType(PT->getPointeeType(), Qualifier::None), Args);
    if (SubPointee.isNull())
      return QualType();
    return QualType(Context.getPointerType(SubPointee.getTypePtr()),
                    T.getQualifiers());
  }

  // ReferenceType → recursively substitute referenced
  if (auto *RT = llvm::dyn_cast<ReferenceType>(Ty)) {
    QualType SubRef = SubstituteType(
        QualType(RT->getReferencedType(), Qualifier::None), Args);
    if (SubRef.isNull())
      return QualType();
    if (RT->isLValueReference())
      return QualType(Context.getLValueReferenceType(SubRef.getTypePtr()),
                      T.getQualifiers());
    return QualType(Context.getRValueReferenceType(SubRef.getTypePtr()),
                    T.getQualifiers());
  }

  // ArrayType → recursively substitute element
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty)) {
    QualType SubElem =
        SubstituteType(QualType(AT->getElementType(), Qualifier::None), Args);
    if (SubElem.isNull())
      return QualType();
    if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(AT))
      return QualType(Context.getConstantArrayType(SubElem.getTypePtr(),
                                                   CAT->getSizeExpr(),
                                                   CAT->getSize()),
                      T.getQualifiers());
    if (llvm::isa<IncompleteArrayType>(AT))
      return QualType(Context.getIncompleteArrayType(SubElem.getTypePtr()),
                      T.getQualifiers());
    if (auto *VAT = llvm::dyn_cast<VariableArrayType>(AT))
      return QualType(Context.getVariableArrayType(SubElem.getTypePtr(),
                                                   VAT->getSizeExpr()),
                      T.getQualifiers());
  }

  // FunctionType → substitute return + param types
  if (auto *FT = llvm::dyn_cast<FunctionType>(Ty))
    return SubstituteFunctionType(FT, Args);

  // MemberPointerType
  if (auto *MPT = llvm::dyn_cast<MemberPointerType>(Ty)) {
    QualType SubClass =
        SubstituteType(QualType(MPT->getClassType(), Qualifier::None), Args);
    QualType SubPointee =
        SubstituteType(QualType(MPT->getPointeeType(), Qualifier::None), Args);
    if (SubClass.isNull() || SubPointee.isNull())
      return QualType();
    return QualType(
        Context.getMemberPointerType(SubClass.getTypePtr(),
                                     SubPointee.getTypePtr()),
        T.getQualifiers());
  }

  // ElaboratedType
  if (auto *ET = llvm::dyn_cast<ElaboratedType>(Ty)) {
    QualType SubNamed = SubstituteType(
        QualType(ET->getNamedType(), Qualifier::None), Args);
    if (SubNamed.isNull())
      return QualType();
    return QualType(
        Context.getElaboratedType(SubNamed.getTypePtr(), ET->getQualifier()),
        T.getQualifiers());
  }

  // Non-template-parameter type → return as-is
  return T;
}

bool TemplateInstantiator::isSFINAEContext() const {
  return SFINAE.isSFINAE();
}

Expr *TemplateInstantiator::SubstituteExpr(Expr *E,
                                           const TemplateArgumentList &Args) {
  if (!E)
    return nullptr;

  // Handle various expression kinds
  // CXXFoldExpr → expand pack
  if (auto *FE = llvm::dyn_cast<CXXFoldExpr>(E))
    return InstantiateFoldExpr(FE, Args);

  // PackIndexingExpr → index into pack
  if (auto *PIE = llvm::dyn_cast<PackIndexingExpr>(E))
    return InstantiatePackIndexingExpr(PIE, Args);

  // DeclRefExpr → substitute the referenced declaration
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(E)) {
    // Check if the referenced decl is a template parameter
    if (auto *TTPD = llvm::dyn_cast<TemplateTypeParmDecl>(DRE->getDecl())) {
      // For now, return as-is; type substitution handles the actual replacement
      return E;
    }
    // For other DeclRefExprs, return as-is (non-dependent)
    return E;
  }

  // CXXDependentScopeMemberExpr → resolve member access on dependent type
  if (auto *DSME = llvm::dyn_cast<CXXDependentScopeMemberExpr>(E)) {
    // Substitute the base type
    QualType SubBaseType = SubstituteType(DSME->getBaseType(), Args);
    if (SubBaseType.isNull())
      SubBaseType = DSME->getBaseType();

    // If the substituted base is still dependent, preserve the expression
    // with the substituted base type
    if (SubBaseType->isDependentType()) {
      return Context.create<CXXDependentScopeMemberExpr>(
          DSME->getLocation(), DSME->getBase(), SubBaseType,
          DSME->isArrow(), DSME->getMemberName());
    }

    // Base is now concrete — look up the member
    if (SubBaseType->isRecordType()) {
      auto *RT = static_cast<const RecordType *>(SubBaseType.getTypePtr());
      RecordDecl *RD = RT->getDecl();
      // Only CXXRecordDecl inherits DeclContext and supports member lookup
      auto *CXXRD = dyn_cast<CXXRecordDecl>(static_cast<ASTNode *>(RD));
      if (CXXRD) {
        DeclContext *DC = static_cast<DeclContext *>(CXXRD);
        if (NamedDecl *Found = DC->lookup(DSME->getMemberName())) {
          if (auto *VD = dyn_cast<ValueDecl>(static_cast<ASTNode *>(Found))) {
            return Context.create<MemberExpr>(
                DSME->getLocation(), DSME->getBase(), VD,
                DSME->isArrow());
          }
        }
      }
    }

    // Resolution failed — return as-is (will produce an error later)
    return E;
  }

  // DependentScopeDeclRefExpr → resolve qualified dependent name reference
  if (auto *DSDRE = llvm::dyn_cast<DependentScopeDeclRefExpr>(E)) {
    NestedNameSpecifier *NNS = DSDRE->getQualifier();

    // Try to resolve the qualifier's type
    if (NNS && NNS->getKind() == NestedNameSpecifier::TypeSpec) {
      const Type *NNS_TYPE = NNS->getAsType();
      QualType SubType = SubstituteType(QualType(NNS_TYPE, Qualifier::None), Args);

      if (!SubType.isNull() && !SubType->isDependentType()) {
        // Build a new NNS with the substituted type
        NestedNameSpecifier *SubNNS =
            NestedNameSpecifier::Create(Context, nullptr, SubType.getTypePtr());

        // Try qualified lookup
        LookupResult Result = SemaRef.LookupQualifiedName(
            DSDRE->getDeclName(), SubNNS);

        if (Result.isSingleResult()) {
          NamedDecl *Found = Result.getFoundDecl();
          if (auto *VD = dyn_cast<ValueDecl>(static_cast<ASTNode *>(Found))) {
            return Context.create<DeclRefExpr>(DSDRE->getLocation(), VD);
          }
          // For type results, return the expression as-is — type substitution
          // in the enclosing context will handle it.
        }
      }
    }

    // Could not resolve — return as-is
    return E;
  }

  // BinaryOperator → substitute both operands
  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E)) {
    Expr *SubLHS = SubstituteExpr(BO->getLHS(), Args);
    Expr *SubRHS = SubstituteExpr(BO->getRHS(), Args);
    if (SubLHS && SubRHS) {
      auto *NewBO = Context.create<BinaryOperator>(
          BO->getLocation(),
          SubLHS == BO->getLHS() ? BO->getLHS() : SubLHS,
          SubRHS == BO->getRHS() ? BO->getRHS() : SubRHS,
          BO->getOpcode());
      NewBO->setType(BO->getType());
      return NewBO;
    }
    return E;
  }

  // UnaryOperator → substitute operand
  if (auto *UO = llvm::dyn_cast<UnaryOperator>(E)) {
    Expr *SubOp = SubstituteExpr(UO->getSubExpr(), Args);
    if (SubOp) {
      auto *NewUO = Context.create<UnaryOperator>(
          UO->getLocation(),
          SubOp == UO->getSubExpr() ? UO->getSubExpr() : SubOp,
          UO->getOpcode());
      NewUO->setType(UO->getType());
      return NewUO;
    }
    return E;
  }

  // CallExpr → substitute callee and arguments
  if (auto *CE = llvm::dyn_cast<CallExpr>(E)) {
    Expr *SubFn = SubstituteExpr(CE->getCallee(), Args);
    auto OrigArgs = CE->getArgs();
    llvm::SmallVector<Expr *, 4> SubArgs;
    for (unsigned I = 0; I < CE->getNumArgs(); ++I) {
      Expr *SubArg = SubstituteExpr(OrigArgs[I], Args);
      SubArgs.push_back(SubArg ? SubArg : OrigArgs[I]);
    }
    auto *NewCE = Context.create<CallExpr>(
        CE->getLocation(),
        SubFn ? SubFn : CE->getCallee(),
        SubArgs);
    return NewCE;
  }

  // For non-dependent expressions, return as-is
  return E;
}

Decl *TemplateInstantiator::SubstituteDecl(Decl *D,
                                           const TemplateArgumentList &Args,
                                           CXXRecordDecl *Parent) {
  if (!D)
    return nullptr;

  // Dispatch based on decl kind
  if (auto *VD = llvm::dyn_cast<VarDecl>(D))
    return SubstituteVarDecl(VD, Args);
  if (auto *FD = llvm::dyn_cast<FieldDecl>(D))
    return SubstituteFieldDecl(FD, Args);
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(D))
    return SubstituteCXXMethodDecl(MD, Args, Parent);

  // Other decl kinds: return as-is for now
  return D;
}

//===----------------------------------------------------------------------===//
// Specialization Lookup
//===----------------------------------------------------------------------===//

ClassTemplateSpecializationDecl *
TemplateInstantiator::FindExistingSpecialization(
    ClassTemplateDecl *Template, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Template)
    return nullptr;

  for (auto *Spec : Template->getSpecializations()) {
    auto SpecArgs = Spec->getTemplateArgs();
    if (SpecArgs.size() != Args.size())
      continue;

    bool Match = true;
    for (unsigned I = 0; I < Args.size(); ++I) {
      if (SpecArgs[I].getKind() != Args[I].getKind()) {
        Match = false;
        break;
      }
      // Simple comparison for type arguments
      if (SpecArgs[I].isType() && Args[I].isType()) {
        if (SpecArgs[I].getAsType() != Args[I].getAsType()) {
          Match = false;
          break;
        }
      } else if (SpecArgs[I].isIntegral() && Args[I].isIntegral()) {
        if (SpecArgs[I].getAsIntegral() != Args[I].getAsIntegral()) {
          Match = false;
          break;
        }
      }
    }
    if (Match)
      return Spec;
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Private Substitution Helpers
//===----------------------------------------------------------------------===//

QualType TemplateInstantiator::SubstituteTemplateTypeParmType(
    const TemplateTypeParmType *T, const TemplateArgumentList &Args) {
  unsigned Index = T->getIndex();

  // Look up by index in the argument list
  if (Index < Args.size()) {
    const TemplateArgument &Arg = Args[Index];

    // If the argument is a pack and this parameter is a pack parameter,
    // we are in a pack expansion context. The pack will be handled by
    // ExpandPackType / ExpandPack which iterate over the pack elements.
    // Here we return the first element or the whole pack depending on context.
    if (Arg.isPack()) {
      // If the template parameter is itself a pack (isParameterPack),
      // the substitution should expand the pack. For now, return the
      // pack argument — the caller (ExpandPackType/ExpandPack) handles
      // the actual expansion element-by-element.
      if (T->isParameterPack()) {
        auto PackArgs = Arg.getAsPack();
        if (!PackArgs.empty() && PackArgs[0].isType())
          return PackArgs[0].getAsType();
      }
      // Non-pack parameter matched to pack argument: use first element
      auto PackArgs = Arg.getAsPack();
      if (!PackArgs.empty() && PackArgs[0].isType())
        return PackArgs[0].getAsType();
      return QualType(const_cast<TemplateTypeParmType *>(T), Qualifier::None);
    }

    if (Arg.isType())
      return Arg.getAsType();
  }

  // No substitution found — return original type
  return QualType(const_cast<TemplateTypeParmType *>(T), Qualifier::None);
}

QualType TemplateInstantiator::SubstituteTemplateSpecializationType(
    const TemplateSpecializationType *T,
    const TemplateArgumentList &Args) {

  // Substitute each template argument
  bool Changed = false;
  llvm::SmallVector<TemplateArgument, 4> NewArgs;

  for (const auto &Arg : T->getTemplateArgs()) {
    if (Arg.isType()) {
      QualType SubType = SubstituteType(Arg.getAsType(), Args);
      if (SubType.isNull())
        return QualType();
      if (SubType.getTypePtr() != Arg.getAsType().getTypePtr())
        Changed = true;
      NewArgs.push_back(TemplateArgument(SubType));
    } else {
      NewArgs.push_back(Arg);
    }
  }

  if (!Changed)
    return QualType(const_cast<TemplateSpecializationType *>(T),
                    Qualifier::None);

  // Create new TemplateSpecializationType with substituted args
  auto *NewTST = Context.getTemplateSpecializationType(T->getTemplateName());
  for (const auto &NA : NewArgs)
    NewTST->addTemplateArg(NA);
  return QualType(NewTST, Qualifier::None);
}

QualType TemplateInstantiator::SubstituteDependentType(
    const DependentType *T, const TemplateArgumentList &Args) {

  if (!T->getBaseType())
    return QualType(const_cast<DependentType *>(T), Qualifier::None);

  QualType SubBase = SubstituteType(
      QualType(T->getBaseType(), Qualifier::None), Args);
  if (SubBase.isNull())
    return QualType(const_cast<DependentType *>(T), Qualifier::None);

  // If the substituted base is still dependent, we cannot resolve the member.
  // Preserve the DependentType with the substituted base.
  if (SubBase->isDependentType())
    return QualType(Context.getDependentType(SubBase.getTypePtr(), T->getName()),
                    Qualifier::None);

  // The base is now non-dependent. Try to resolve T::name by looking up
  // the member in the concrete type (e.g., vector<int>::iterator →
  // lookup "iterator" in vector<int>).
  if (SubBase->isRecordType()) {
    auto *RT = static_cast<const RecordType *>(SubBase.getTypePtr());
    RecordDecl *RD = RT->getDecl();

    // Only CXXRecordDecl supports qualified member lookup
    if (auto *CXXRD = dyn_cast<CXXRecordDecl>(static_cast<ASTNode *>(RD))) {
      // Build a NestedNameSpecifier for the substituted type
      NestedNameSpecifier *NNS =
          NestedNameSpecifier::Create(Context, nullptr, SubBase.getTypePtr());
      LookupResult Result = SemaRef.LookupQualifiedName(T->getName(), NNS);

      // If we found a type declaration, return the actual type.
      if (TypeDecl *TD = Result.getAsTypeDecl()) {
        if (auto *TagD = dyn_cast<TagDecl>(static_cast<ASTNode *>(TD))) {
          if (auto *RD = dyn_cast<RecordDecl>(static_cast<ASTNode *>(TagD)))
            return Context.getRecordType(RD);
          // For EnumDecl, return the enum type
          if (auto *ED = dyn_cast<EnumDecl>(static_cast<ASTNode *>(TagD)))
            return Context.getEnumType(ED);
        }
        // TypedefDecl / TypeAliasDecl
        if (auto *TDD = dyn_cast<TypedefNameDecl>(static_cast<ASTNode *>(TD))) {
          QualType Underlying = TDD->getUnderlyingType();
          if (!Underlying.isNull())
            return Underlying;
        }
        // TemplateTypeParmDecl (a type parameter used as a member type)
        if (auto *TTPD = dyn_cast<TemplateTypeParmDecl>(static_cast<ASTNode *>(TD)))
          return QualType(Context.getTemplateTypeParmType(TTPD, TTPD->getIndex(),
                                                          TTPD->getDepth()),
                          Qualifier::None);
      }
    }
  }

  // Resolution failed — keep as DependentType with substituted base.
  // This can happen for non-record types (e.g., int::name is ill-formed)
  // or when the member simply doesn't exist.
  return QualType(Context.getDependentType(SubBase.getTypePtr(), T->getName()),
                  Qualifier::None);
}

QualType TemplateInstantiator::SubstituteFunctionType(
    const FunctionType *FT, const TemplateArgumentList &Args) {

  // Substitute return type
  QualType SubRet =
      SubstituteType(QualType(FT->getReturnType(), Qualifier::None), Args);
  if (SubRet.isNull())
    return QualType();

  // Substitute parameter types
  llvm::SmallVector<const Type *, 4> SubParamTypes;
  for (const Type *PT : FT->getParamTypes()) {
    QualType SubPT = SubstituteType(QualType(PT, Qualifier::None), Args);
    if (SubPT.isNull())
      return QualType();
    SubParamTypes.push_back(SubPT.getTypePtr());
  }

  return QualType(Context.getFunctionType(SubRet.getTypePtr(), SubParamTypes,
                                          FT->isVariadic(), FT->isConst(),
                                          FT->isVolatile()),
                  Qualifier::None);
}

VarDecl *TemplateInstantiator::SubstituteVarDecl(
    VarDecl *VD, const TemplateArgumentList &Args) {
  if (!VD)
    return nullptr;

  QualType SubType = SubstituteType(VD->getType(), Args);
  if (SubType.isNull())
    SubType = VD->getType();

  Expr *SubInit = SubstituteExpr(VD->getInit(), Args);

  return Context.create<VarDecl>(VD->getLocation(), VD->getName(), SubType,
                                 SubInit, VD->isStatic(), VD->isConstexpr());
}

FieldDecl *TemplateInstantiator::SubstituteFieldDecl(
    FieldDecl *FD, const TemplateArgumentList &Args) {
  if (!FD)
    return nullptr;

  QualType SubType = SubstituteType(FD->getType(), Args);
  if (SubType.isNull())
    SubType = FD->getType();

  Expr *SubInit = SubstituteExpr(FD->getInClassInitializer(), Args);

  return Context.create<FieldDecl>(FD->getLocation(), FD->getName(), SubType,
                                   FD->getBitWidth(), FD->isMutable(), SubInit,
                                   FD->getAccess());
}

CXXMethodDecl *TemplateInstantiator::SubstituteCXXMethodDecl(
    CXXMethodDecl *MD, const TemplateArgumentList &Args,
    CXXRecordDecl *Parent) {
  if (!MD)
    return nullptr;

  QualType SubType = SubstituteType(MD->getType(), Args);
  if (SubType.isNull())
    SubType = MD->getType();

  // Substitute parameter types
  llvm::SmallVector<ParmVarDecl *, 8> InstParams;
  for (unsigned I = 0; I < MD->getNumParams(); ++I) {
    ParmVarDecl *PVD = MD->getParamDecl(I);
    QualType ParamType = SubstituteType(PVD->getType(), Args);
    if (ParamType.isNull())
      ParamType = PVD->getType();

    auto *InstPVD = Context.create<ParmVarDecl>(
        PVD->getLocation(), PVD->getName(), ParamType,
        PVD->getFunctionScopeIndex());
    InstParams.push_back(InstPVD);
  }

  // Use the instantiated parent (the ClassTemplateSpecializationDecl) if
  // provided; otherwise fall back to the original parent.
  CXXRecordDecl *MethodParent = Parent ? Parent : MD->getParent();

  return Context.create<CXXMethodDecl>(
      MD->getLocation(), MD->getName(), SubType, InstParams, MethodParent,
      MD->getBody(), MD->isStatic(), MD->isConst(), MD->isVolatile(),
      MD->isVirtual(), MD->isPureVirtual(), MD->isOverride(), MD->isFinal(),
      MD->isDefaulted(), MD->isDeleted(), MD->getRefQualifier(),
      MD->hasNoexceptSpec(), MD->getNoexceptValue(), MD->getNoexceptExpr(),
      MD->getAccess());
}

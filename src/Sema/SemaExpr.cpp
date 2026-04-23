//===--- SemaExpr.cpp - Semantic Analysis for Expressions -*- C++ -*-=======//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements expression-related Sema methods: ActOn*Expr,
// expression factory methods, and call resolution.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/SemaReflection.h"
#include "blocktype/Sema/TypeDeduction.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

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
  // 1. Validate that Pack references a parameter pack
  bool IsPack = false;
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Pack)) {
    if (auto *D = DRE->getDecl()) {
      // Check if the declaration is a parameter pack
      if (auto *TTPD = llvm::dyn_cast<TemplateTypeParmDecl>(D)) {
        IsPack = TTPD->isParameterPack();
      } else if (auto *NTTPD = llvm::dyn_cast<NonTypeTemplateParmDecl>(D)) {
        IsPack = NTTPD->isParameterPack();
      } else if (auto *TTPD = llvm::dyn_cast<TemplateTemplateParmDecl>(D)) {
        IsPack = TTPD->isParameterPack();
      } else if (llvm::isa<VarTemplateDecl>(D) ||
                 llvm::isa<FunctionTemplateDecl>(D)) {
        IsPack = true;
      }
    }
  }
  // PackExpansionExpr or type-dependent expressions are also valid packs
  if (Pack->isTypeDependent()) {
    IsPack = true;  // Assume it's a pack in dependent context
  }

  if (!IsPack) {
    Diag(Loc, DiagID::err_pack_index_not_pack);
    // Still create the node for error recovery
  }

  // 2. Validate that Index is an integral expression
  QualType IndexType = Index->getType();
  if (!IndexType.isNull() && !IndexType->isIntegerType() &&
      !IndexType->isBooleanType() && !Index->isTypeDependent()) {
    Diag(Index->getLocation(), DiagID::err_pack_index_non_integral);
    // Still create the node for error recovery
  }

  // 3. Create the node
  auto *PIE = Context.create<PackIndexingExpr>(Loc, Pack, Index);

  // 4. Type deduction
  // If the expression is type-dependent, leave type unset (deduced at instantiation)
  if (!Pack->isTypeDependent() && !Index->isTypeDependent()) {
    // Try to deduce type from the pack if index is a constant
    if (auto *IntLit = llvm::dyn_cast<IntegerLiteral>(Index)) {
      uint64_t Idx = IntLit->getValue().getZExtValue();
      // If Pack is a DeclRefExpr to a parameter pack with known substitutions,
      // try to get the Nth element type
      if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Pack)) {
        if (auto *TTPD = llvm::dyn_cast<TemplateTypeParmDecl>(DRE->getDecl())) {
          // Type template parameter pack — type depends on instantiation
          // Leave as dependent for now
        }
      }
    }
    // For now, if we can't statically determine the type, leave it dependent
    // The InstantiatePackIndexingExpr will set the type during instantiation
  }

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
    
    if (auto *FunD = llvm::dyn_cast_or_null<FunctionDecl>(D)) {
      // Fast path: direct FunctionDecl reference
      FD = FunD;
    }
    // For FunctionTemplateDecl or D==null, fall through to ResolveOverload
    // which now handles template deduction via deduceTemplateCandidates
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

} // namespace blocktype

//===--- SemaDecl.cpp - Semantic Analysis for Declarations -*- C++ -*-=======//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements declaration-related Sema methods: ActOn*Decl,
// declaration factory methods, structured binding helpers, and class
// member factory methods.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/SemaCXX.h"
#include "blocktype/Sema/TypeDeduction.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

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
        if (!RetType.isNull() && !RetType->isVoidType()) {
          return RetType;
        }
      }
    }
  }
  
  return {};
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

  // E7.5.2.3: [[indeterminate]] attribute (P2795R5)
  // Warn when [[indeterminate]] is used on a const variable
  if (Attrs && T.isConstQualified()) {
    for (auto *A : Attrs->getAttributes()) {
      if (A->getAttributeName() == "indeterminate") {
        Diags.report(Loc, DiagLevel::Warning,
                     "[[indeterminate]] on a const variable has no effect");
        break;
      }
    }
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

} // namespace blocktype

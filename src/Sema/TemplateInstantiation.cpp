//===--- TemplateInstantiation.cpp - Template Instantiation ----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/StmtCloner.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

/// TypeSubstitutionVisitor - Internal visitor that performs type substitution.
class TypeSubstitutionVisitor 
    : public TypeVisitor<TypeSubstitutionVisitor> {
  const llvm::DenseMap<NamedDecl *, TemplateArgument> &Substitutions;

public:
  explicit TypeSubstitutionVisitor(
      const llvm::DenseMap<NamedDecl *, TemplateArgument> &Subs)
      : Substitutions(Subs) {}

  // Inherit all Visit methods from base class
  using TypeVisitor<TypeSubstitutionVisitor>::Visit;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitBuiltinType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitPointerType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitReferenceType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitArrayType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitFunctionType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitRecordType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitTypedefType;
  using TypeVisitor<TypeSubstitutionVisitor>::VisitEnumType;

  /// Visit a template type parameter and substitute if found.
  const Type *VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
    // Look up the parameter in the substitution map
    auto It = Substitutions.find(T->getDecl());
    if (It != Substitutions.end()) {
      const TemplateArgument &Arg = It->second;
      if (Arg.isType()) {
        return Arg.getAsType().getTypePtr();
      }
    }
    // Not found, return unchanged
    return T;
  }
};

QualType TemplateInstantiation::substituteType(QualType InputType) const {
  if (InputType.isNull()) {
    return InputType;
  }
  
  // Create visitor with current substitutions
  TypeSubstitutionVisitor Visitor(Substitutions);
  
  // Visit and substitute
  const Type *Substituted = Visitor.Visit(InputType.getTypePtr());
  return {Substituted, InputType.getQualifiers()};
}

const Type *TemplateInstantiation::substituteTypeImpl(const Type *Ty) const {
  if (!Ty) {
    return nullptr;
  }
  
  // Delegate to the visitor
  TypeSubstitutionVisitor Visitor(Substitutions);
  return Visitor.Visit(Ty);
}

FunctionDecl *TemplateInstantiation::substituteFunctionSignature(
    FunctionDecl *Original,
    llvm::ArrayRef<TemplateArgument> Args,
    TemplateParameterList *Params) const {
  
  if (!Original || !Params) {
    return nullptr;
  }
  
  // Create a mutable copy for building substitutions
  TemplateInstantiation MutableInst;
  
  // Build substitution map
  auto ParamDecls = Params->getParams();
  for (unsigned i = 0; i < std::min(Args.size(), ParamDecls.size()); ++i) {
    if (auto *ParamDecl = llvm::dyn_cast_or_null<TemplateTypeParmDecl>(ParamDecls[i])) {
      MutableInst.addSubstitution(ParamDecl, Args[i]);
    }
  }
  
  // Step 1: Substitute return type
  QualType OriginalReturnType;
  QualType OriginalFuncType = Original->getType();
  if (OriginalFuncType.getTypePtr() && OriginalFuncType->isFunctionType()) {
    auto *FT = static_cast<const FunctionType *>(OriginalFuncType.getTypePtr());
    OriginalReturnType = QualType(FT->getReturnType(), Qualifier::None);
  } else {
    OriginalReturnType = Original->getType(); // Fallback
  }
  QualType SubstReturnType = MutableInst.substituteType(OriginalReturnType);
  
  // Step 2: Substitute parameter types and create new ParmVarDecls
  llvm::SmallVector<ParmVarDecl *, 4> ClonedParams;
  for (auto *OrigParam : Original->getParams()) {
    QualType SubstParamType = MutableInst.substituteType(OrigParam->getType());
    
    // Create new ParmVarDecl with substituted type
    // Note: We cannot use ASTContext::create here since we don't have
    // access to it in TemplateInstantiation. Instead, we clone the
    // ParmVarDecl directly.
    ParmVarDecl *ClonedParam = new ParmVarDecl(
        OrigParam->getLocation(),
        OrigParam->getName(),
        SubstParamType,
        OrigParam->getFunctionScopeIndex(),
        OrigParam->getDefaultArg());
    
    ClonedParams.push_back(ClonedParam);
  }
  
  // Step 4: Create new FunctionDecl with substituted types
  FunctionDecl *NewFD = new FunctionDecl(
      Original->getLocation(),
      Original->getName(),
      SubstReturnType,
      ClonedParams,
      nullptr,                    // Body — set later
      Original->isInline(),
      Original->isConstexpr(),
      Original->isConsteval(),
      Original->hasNoexceptSpec(),
      Original->getNoexceptValue(),
      Original->getNoexceptExpr(),
      Original->getAttrs());
  
  // Copy storage class (static)
  NewFD->setStorageClass(Original->getStorageClass());
  
  // Step 5: Copy function body (if any) using StmtCloner
  if (Stmt *OriginalBody = Original->getBody()) {
    StmtCloner Cloner(MutableInst);
    // Register mapping from original params to cloned params
    auto OrigParams = Original->getParams();
    for (unsigned I = 0; I < OrigParams.size() && I < ClonedParams.size(); ++I) {
      Cloner.registerDeclMapping(OrigParams[I], ClonedParams[I]);
    }
    Stmt *ClonedBody = Cloner.Clone(OriginalBody);
    NewFD->setBody(ClonedBody);
  }
  
  // Step 6: Copy explicit object parameter (deducing this)
  if (Original->hasExplicitObjectParam()) {
    // Find the corresponding cloned param for the explicit object param
    ParmVarDecl *OrigObjParam = Original->getExplicitObjectParam();
    for (unsigned I = 0; I < Original->getNumParams() && I < ClonedParams.size(); ++I) {
      if (Original->getParamDecl(I) == OrigObjParam) {
        NewFD->setExplicitObjectParam(ClonedParams[I]);
        break;
      }
    }
  }
  
  return NewFD;
}

bool TemplateInstantiation::hasUnsubstitutedParams(QualType T) const {
  // TODO: Check if type contains template parameters
  // For now, return false (assume all substituted)
  return false;
}

} // namespace blocktype

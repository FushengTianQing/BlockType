//===--- TemplateInstantiation.cpp - Template Instantiation ----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateInstantiation.h"
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

  // Use default implementations for other types (they recurse automatically)
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
  
  // Build substitution map
  auto ParamDecls = Params->getParams();
  for (unsigned i = 0; i < std::min(Args.size(), ParamDecls.size()); ++i) {
    if (auto *ParamDecl = llvm::dyn_cast_or_null<TypedefNameDecl>(ParamDecls[i])) {
      addSubstitution(ParamDecl, Args[i]);
    }
  }
  
  // TODO: Create new FunctionDecl with substituted types
  // This requires:
  // 1. Substitute return type
  // 2. Substitute parameter types
  // 3. Copy function body (if any)
  // 4. Set appropriate flags
  
  // For now, return the original (placeholder)
  return Original;
}

bool TemplateInstantiation::hasUnsubstitutedParams(QualType T) const {
  // TODO: Check if type contains template parameters
  // For now, return false (assume all substituted)
  return false;
}

} // namespace blocktype

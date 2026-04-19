//===--- TemplateInstantiation.h - Template Instantiation ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TemplateInstantiation class, which handles template
// argument substitution during template instantiation.
//
// P7.4.3: Used for instantiating function templates like std::get<N>.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_SEMA_TEMPLATE_INSTANTIATION_H
#define BLOCKTYPE_SEMA_TEMPLATE_INSTANTIATION_H

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/TypeVisitor.h"  // For TypeVisitor
#include "llvm/ADT/DenseMap.h"

namespace blocktype {

/// TemplateInstantiation - Handles template argument substitution.
///
/// This class performs the core operation of replacing template parameters
/// with their corresponding arguments during template instantiation.
///
/// Example:
///   template<typename T> T get(pair<T, U>& p);
///   get<int>(pair<int, double>&)  ->  int get(pair<int, double>& p);
class TemplateInstantiation {
  /// Mapping from template parameter declarations to their arguments.
  llvm::DenseMap<NamedDecl *, TemplateArgument> Substitutions;

public:
  TemplateInstantiation() = default;

  /// Add a substitution: map a template parameter to its argument.
  void addSubstitution(NamedDecl *Param, const TemplateArgument &Arg) {
    Substitutions[Param] = Arg;
  }

  /// Substitute types in a QualType.
  /// Replaces template type parameters with their corresponding arguments.
  [[nodiscard]] QualType substituteType(QualType Type) const;

  /// Substitute types in a FunctionDecl's signature.
  /// Creates a new FunctionDecl with substituted parameter and return types.
  FunctionDecl *substituteFunctionSignature(FunctionDecl *Original,
                                             llvm::ArrayRef<TemplateArgument> Args,
                                             TemplateParameterList *Params) const;

  /// Check if a type contains any unsubstituted template parameters.
  [[nodiscard]] bool hasUnsubstitutedParams(QualType Type) const;

private:
  /// Recursively substitute types in a Type.
  const Type *substituteTypeImpl(const Type *T) const;
};

} // namespace blocktype

#endif // BLOCKTYPE_SEMA_TEMPLATE_INSTANTIATION_H

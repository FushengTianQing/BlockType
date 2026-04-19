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
#include "blocktype/Sema/SFINAE.h"      // For SFINAEContext
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

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

/// TemplateInstantiator - High-level template instantiation engine.
///
/// This class coordinates the complete template instantiation process:
/// 1. Cache lookup for existing specializations
/// 2. SFINAE context management during substitution
/// 3. AST cloning with type substitution
/// 4. Specialization registration
///
/// Usage pattern in Sema:
///   auto *FD = Instantiator->InstantiateFunctionTemplate(FTD, Args);
///
class TemplateInstantiator {
  /// Reference to the parent Sema for diagnostics and context.
  class Sema &SemaRef;

  /// SFINAE context for managing substitution failures.
  SFINAEContext SFContext;

public:
  explicit TemplateInstantiator(class Sema &Sema);

  /// Get the SFINAE context (for use by deduction/instantiation).
  SFINAEContext &getSFINAEContext() { return SFContext; }

  /// Instantiate a function template with given template arguments.
  ///
  /// \param FuncTemplate The function template to instantiate.
  /// \param TemplateArgs The template arguments (from deduction or explicit).
  /// \returns The instantiated FunctionDecl, or nullptr on failure.
  ///
  /// This is the main entry point for function template instantiation.
  /// It delegates to Sema::InstantiateFunctionTemplate but manages
  /// SFINAE context and caching at this level.
  FunctionDecl *InstantiateFunctionTemplate(
      FunctionTemplateDecl *FuncTemplate,
      llvm::ArrayRef<TemplateArgument> TemplateArgs);
};

} // namespace blocktype

#endif // BLOCKTYPE_SEMA_TEMPLATE_INSTANTIATION_H

//===--- TemplateInstantiation.h - Template Instantiation ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines TemplateArgumentList and TemplateInstantiator for
// template instantiation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Sema/SFINAE.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {

class Sema;

/// TemplateArgumentList - A lightweight wrapper around template arguments.
///
/// Maintains a mapping from template parameter index to the corresponding
/// argument value, used during instantiation to substitute parameters.
class TemplateArgumentList {
  llvm::SmallVector<TemplateArgument, 4> Args;

public:
  TemplateArgumentList() = default;
  explicit TemplateArgumentList(llvm::ArrayRef<TemplateArgument> A)
      : Args(A.begin(), A.end()) {}

  unsigned size() const { return Args.size(); }
  bool empty() const { return Args.empty(); }
  llvm::ArrayRef<TemplateArgument> asArray() const { return Args; }

  const TemplateArgument &operator[](unsigned I) const { return Args[I]; }
  void push_back(const TemplateArgument &Arg) { Args.push_back(Arg); }

  /// Find the pack argument in the list (for variadic template expansion).
  /// @return The pack argument, or empty ArrayRef if none exists.
  llvm::ArrayRef<TemplateArgument> getPackArgument() const;
};

/// TemplateInstantiator - The core engine for template instantiation.
///
/// Responsible for:
/// - Class template instantiation (creating ClassTemplateSpecializationDecl)
/// - Function template instantiation (creating instantiated FunctionDecl)
/// - Template parameter substitution (recursive replacement of Type/Expr/Decl)
///
/// Follows Clang's TemplateInstantiator pattern:
/// Instantiation runs in the Sema context, all new nodes via ASTContext::create().
class TemplateInstantiator {
  Sema &SemaRef;
  ASTContext &Context;

  /// Maximum recursion depth for template instantiation.
  static constexpr unsigned MaxInstantiationDepth = 1024;

  /// Current instantiation depth (prevents infinite recursion).
  unsigned CurrentDepth = 0;

  /// SFINAE context — controls whether substitution failures are hard errors
  /// or silently remove the candidate from the overload set.
  SFINAEContext SFINAE;

public:
  explicit TemplateInstantiator(Sema &S);

  /// Access the SFINAE context.
  SFINAEContext &getSFINAEContext() { return SFINAE; }
  const SFINAEContext &getSFINAEContext() const { return SFINAE; }

  // === Class Template Instantiation ===

  /// Instantiate a class template with the given arguments.
  /// @param Template  The class template declaration
  /// @param Args      Template arguments
  /// @return          Specialization decl (possibly from cache)
  ClassTemplateSpecializationDecl *
  InstantiateClassTemplate(ClassTemplateDecl *Template,
                           llvm::ArrayRef<TemplateArgument> Args);

  // === Function Template Instantiation ===

  /// Instantiate a function template with the given arguments.
  /// @param Template  The function template declaration
  /// @param Args      Template arguments
  /// @return          Instantiated function declaration
  FunctionDecl *
  InstantiateFunctionTemplate(FunctionTemplateDecl *Template,
                              llvm::ArrayRef<TemplateArgument> Args);

  // === Pack Expansion ===

  /// Expand a parameter pack.
  /// @param Pattern   The expansion pattern (contains references to pack)
  /// @param Args      Argument list containing Pack-type arguments
  /// @return          Expanded expression list
  llvm::SmallVector<Expr *, 4>
  ExpandPack(Expr *Pattern, const TemplateArgumentList &Args);

  // === Type Substitution ===

  /// Substitute template parameters in a type with arguments.
  /// e.g., TemplateTypeParmType(T, index=0) → int
  QualType SubstituteType(QualType T, const TemplateArgumentList &Args);

  /// Substitute template parameters in an expression.
  Expr *SubstituteExpr(Expr *E, const TemplateArgumentList &Args);

  /// Substitute template parameters in a declaration (for member instantiation).
  /// @param D        The declaration to substitute
  /// @param Args     Template arguments
  /// @param Parent   The instantiated parent record (may be nullptr)
  Decl *SubstituteDecl(Decl *D, const TemplateArgumentList &Args,
                       CXXRecordDecl *Parent = nullptr);

  // === Specialization Lookup ===

  /// Find an existing specialization.
  /// @return Specialization decl, or nullptr if none exists
  ClassTemplateSpecializationDecl *
  FindExistingSpecialization(ClassTemplateDecl *Template,
                             llvm::ArrayRef<TemplateArgument> Args);

private:
  /// Check if currently in a SFINAE context.
  bool isSFINAEContext() const;

  /// Substitute TemplateTypeParmType with the corresponding argument type.
  QualType SubstituteTemplateTypeParmType(const TemplateTypeParmType *T,
                                          const TemplateArgumentList &Args);

  /// Recursively substitute arguments in a TemplateSpecializationType.
  QualType SubstituteTemplateSpecializationType(
      const TemplateSpecializationType *T,
      const TemplateArgumentList &Args);

  /// Recursively substitute a DependentType.
  QualType SubstituteDependentType(const DependentType *T,
                                   const TemplateArgumentList &Args);

  /// Substitute in a FunctionType (return type + parameter types).
  QualType SubstituteFunctionType(const FunctionType *FT,
                                  const TemplateArgumentList &Args);

  /// Substitute in a VarDecl.
  VarDecl *SubstituteVarDecl(VarDecl *VD, const TemplateArgumentList &Args);

  /// Substitute in a FieldDecl.
  FieldDecl *SubstituteFieldDecl(FieldDecl *FD,
                                 const TemplateArgumentList &Args);

  /// Substitute in a CXXMethodDecl.
  /// @param MD       The method to substitute
  /// @param Args     Template arguments
  /// @param Parent   The instantiated parent record (may be nullptr)
  CXXMethodDecl *SubstituteCXXMethodDecl(CXXMethodDecl *MD,
                                         const TemplateArgumentList &Args,
                                         CXXRecordDecl *Parent = nullptr);
};

} // namespace blocktype

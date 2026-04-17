//===--- DeclContext.h - Declaration Context -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DeclContext class, which manages the lexical and
// semantic context of declarations (namespaces, classes, functions, etc.).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

class Decl;
class NamedDecl;

/// DeclContextKind - The kind of declaration context.
enum class DeclContextKind {
  TranslationUnit,
  Namespace,
  CXXRecord,
  Function,
  Enum,
  LinkageSpec,
  Block,
  TemplateParams,
};

/// DeclContext - Base class for declaration contexts.
///
/// A declaration context is a region of code where declarations can appear.
/// Examples include translation units, namespaces, classes, functions, and
/// linkage specifications.
///
/// DeclContext provides:
/// - Storage for declarations within the context
/// - Name lookup (simple single-scope lookup)
/// - Parent/child context navigation
/// - Iteration over declarations
///
/// Memory Management:
/// DeclContext does not own its declarations; they are owned by ASTContext.
/// The DeclContext can be a mixin (multiple inheritance) with a Decl-derived
/// class, or used standalone.
class DeclContext {
  /// The parent context (nullptr for translation unit).
  DeclContext *ParentCtx = nullptr;

  /// The kind of this context.
  DeclContextKind ContextKind;

  /// All declarations in this context (in order of appearance).
  llvm::SmallVector<Decl *, 32> Decls;

  /// Name-to-declaration mapping for fast lookup.
  /// Only stores the most recent declaration for each name.
  llvm::StringMap<NamedDecl *> NameMap;

public:
  DeclContext(DeclContextKind Kind, DeclContext *Parent = nullptr)
      : ParentCtx(Parent), ContextKind(Kind) {}

  virtual ~DeclContext() = default;

  //===--------------------------------------------------------------------===//
  // Context hierarchy
  //===--------------------------------------------------------------------===//

  /// Get the parent context.
  DeclContext *getParent() const { return ParentCtx; }

  /// Set the parent context.
  void setParent(DeclContext *P) { ParentCtx = P; }

  /// Get the kind of this context.
  DeclContextKind getDeclContextKind() const { return ContextKind; }

  /// Check if this is a translation unit context.
  bool isTranslationUnit() const {
    return ContextKind == DeclContextKind::TranslationUnit;
  }

  /// Check if this is a namespace context.
  bool isNamespace() const { return ContextKind == DeclContextKind::Namespace; }

  /// Check if this is a C++ record (class/struct/union) context.
  bool isCXXRecord() const {
    return ContextKind == DeclContextKind::CXXRecord;
  }

  /// Check if this is a function context.
  bool isFunction() const { return ContextKind == DeclContextKind::Function; }

  /// Check if this is an enum context.
  bool isEnum() const { return ContextKind == DeclContextKind::Enum; }

  /// Check if this is a linkage specification context.
  bool isLinkageSpec() const {
    return ContextKind == DeclContextKind::LinkageSpec;
  }

  /// Find the enclosing context of the given kind.
  DeclContext *getEnclosingContext(DeclContextKind Kind) const {
    DeclContext *Ctx = ParentCtx;
    while (Ctx) {
      if (Ctx->getDeclContextKind() == Kind)
        return Ctx;
      Ctx = Ctx->getParent();
    }
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Declaration management
  //===--------------------------------------------------------------------===//

  /// Add a declaration to this context.
  void addDecl(Decl *D);

  /// Add a declaration to this context and update the name map.
  /// Named declarations are added to the name map for lookup.
  void addDecl(NamedDecl *D);

  /// Get all declarations in this context.
  llvm::ArrayRef<Decl *> decls() const { return Decls; }

  /// Get the number of declarations.
  unsigned getNumDecls() const { return Decls.size(); }

  /// Check if this context has no declarations.
  bool decls_empty() const { return Decls.empty(); }

  //===--------------------------------------------------------------------===//
  // Name lookup
  //===--------------------------------------------------------------------===//

  /// Look up a name in this context only (not in parent contexts).
  /// Returns nullptr if not found.
  NamedDecl *lookupInContext(llvm::StringRef Name) const;

  /// Look up a name in this context and all parent contexts.
  /// Returns the first declaration found (innermost scope wins).
  NamedDecl *lookup(llvm::StringRef Name) const;

  /// Check if a name exists in this context.
  bool containsDecl(llvm::StringRef Name) const {
    return lookupInContext(Name) != nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Iteration
  //===--------------------------------------------------------------------===//

  using iterator = llvm::SmallVector<Decl *, 32>::iterator;
  using const_iterator = llvm::SmallVector<Decl *, 32>::const_iterator;

  iterator begin() { return Decls.begin(); }
  iterator end() { return Decls.end(); }
  const_iterator begin() const { return Decls.begin(); }
  const_iterator end() const { return Decls.end(); }

  //===--------------------------------------------------------------------===//
  // Debug
  //===--------------------------------------------------------------------===//

  /// Dump this context for debugging.
  void dumpDeclContext(llvm::raw_ostream &OS, unsigned Indent = 0) const;
};

} // namespace blocktype

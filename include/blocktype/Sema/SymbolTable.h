//===--- SymbolTable.h - Symbol Table ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SymbolTable class for managing global symbol
// information across all scopes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/LLVM.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

class ASTContext;
class DiagnosticsEngine;

/// SymbolTable - Manages global symbol information across all scopes.
///
/// The SymbolTable provides fast lookup for declarations by name,
/// organized by declaration kind (ordinary names, tags, typedefs,
/// namespaces). It complements the Scope system by providing
/// persistent storage that outlives individual scope lifetimes.
///
/// Design follows Clang's IdentifierTable + multiple lookup maps pattern.
class SymbolTable {
  ASTContext &Context;
  DiagnosticsEngine &Diags;

  // Ordinary symbols: name → list of declarations (for overloading)
  llvm::StringMap<llvm::SmallVector<NamedDecl *, 4>> OrdinarySymbols;

  // Tags: class/struct/union/enum declarations
  llvm::StringMap<TagDecl *> Tags;

  // Typedefs: typedef and using-alias declarations
  llvm::StringMap<TypedefNameDecl *> Typedefs;

  // Namespaces
  llvm::StringMap<NamespaceDecl *> Namespaces;

  // Template names
  llvm::StringMap<TemplateDecl *> Templates;

  // Concepts
  llvm::StringMap<ConceptDecl *> Concepts;

public:
  explicit SymbolTable(ASTContext &C, DiagnosticsEngine &D) : Context(C), Diags(D) {}

  //===------------------------------------------------------------------===//
  // Symbol addition
  //===------------------------------------------------------------------===//

  /// Add an ordinary symbol (variable, function, etc.).
  /// Returns true on success, false on redefinition error.
  bool addOrdinarySymbol(NamedDecl *D);

  /// Add a tag declaration (class/struct/union/enum).
  bool addTagDecl(TagDecl *D);

  /// Add a typedef or type alias declaration.
  bool addTypedefDecl(TypedefNameDecl *D);

  /// Add a namespace declaration.
  void addNamespaceDecl(NamespaceDecl *D);

  /// Add a template declaration.
  void addTemplateDecl(TemplateDecl *D);

  /// Add a concept declaration.
  void addConceptDecl(ConceptDecl *D);

  /// Generic add - dispatches to the appropriate method based on Decl kind.
  bool addDecl(NamedDecl *D);

  //===------------------------------------------------------------------===//
  // Symbol lookup
  //===------------------------------------------------------------------===//

  /// Look up ordinary symbols (variables, functions).
  llvm::ArrayRef<NamedDecl *> lookupOrdinary(llvm::StringRef Name) const;

  /// Look up a tag declaration (class/struct/union/enum).
  TagDecl *lookupTag(llvm::StringRef Name) const;

  /// Look up a typedef or type alias.
  TypedefNameDecl *lookupTypedef(llvm::StringRef Name) const;

  /// Look up a namespace.
  NamespaceDecl *lookupNamespace(llvm::StringRef Name) const;

  /// Look up a template.
  TemplateDecl *lookupTemplate(llvm::StringRef Name) const;

  /// Look up a concept.
  ConceptDecl *lookupConcept(llvm::StringRef Name) const;

  /// Generic lookup - returns any NamedDecl with the given name.
  llvm::ArrayRef<NamedDecl *> lookup(llvm::StringRef Name) const;

  //===------------------------------------------------------------------===//
  // Queries
  //===------------------------------------------------------------------===//

  /// Check if a name exists in any category.
  bool contains(llvm::StringRef Name) const;

  /// Get the number of symbols in the table.
  size_t size() const;

  /// Dump the symbol table for debugging.
  void dump(llvm::raw_ostream &OS) const;
  void dump() const;
};

} // namespace blocktype

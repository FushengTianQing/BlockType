//===--- Scope.h - Scope and Symbol Table -------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Scope class for managing symbol tables and scopes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

class NamedDecl;
class Decl;
class ASTContext;

/// ScopeFlags - Flags that control the behavior of a scope.
enum class ScopeFlags : unsigned {
  /// No flags.
  None = 0x00,
  
  /// This is a function prototype scope.
  FunctionPrototypeScope = 0x01,
  
  /// This is a function body scope.
  FunctionBodyScope = 0x02,
  
  /// This is a class/struct/union scope.
  ClassScope = 0x04,
  
  /// This is a block scope (compound statement).
  BlockScope = 0x08,
  
  /// This is a template scope.
  TemplateScope = 0x10,
  
  /// This is a control scope (if, while, for, etc.).
  ControlScope = 0x20,
  
  /// This is a switch scope.
  SwitchScope = 0x40,
  
  /// This is a try/catch scope.
  TryScope = 0x80,
  
  /// This is a namespace scope.
  NamespaceScope = 0x100,
  
  /// This is a translation unit scope.
  TranslationUnitScope = 0x200,
  
  /// This scope is for a condition in a control statement.
  ConditionScope = 0x400,
  
  /// This scope is for a for-range statement.
  ForRangeScope = 0x800,
};

/// Bitwise operators for ScopeFlags.
inline ScopeFlags operator|(ScopeFlags LHS, ScopeFlags RHS) {
  return static_cast<ScopeFlags>(
      static_cast<unsigned>(LHS) | static_cast<unsigned>(RHS));
}

inline ScopeFlags operator&(ScopeFlags LHS, ScopeFlags RHS) {
  return static_cast<ScopeFlags>(
      static_cast<unsigned>(LHS) & static_cast<unsigned>(RHS));
}

inline ScopeFlags &operator|=(ScopeFlags &LHS, ScopeFlags RHS) {
  LHS = LHS | RHS;
  return LHS;
}

inline ScopeFlags &operator&=(ScopeFlags &LHS, ScopeFlags RHS) {
  LHS = LHS & RHS;
  return LHS;
}

/// Scope - Represents a scope in the program.
///
/// Scopes form a hierarchy that represents the lexical structure of the
/// program. Each scope contains a symbol table mapping names to declarations.
///
/// Memory Management:
/// Scopes are typically created on the stack during parsing and semantic
/// analysis. They do not own the declarations they contain.
///
class Scope {
  /// The parent scope (nullptr for translation unit scope).
  Scope *Parent;
  
  /// The flags for this scope.
  ScopeFlags Flags;
  
  /// The declarations in this scope, indexed by name.
  /// Using StringMap for efficient lookup.
  llvm::StringMap<NamedDecl *> Declarations;
  
  /// All declarations in this scope (for iteration).
  llvm::SmallVector<NamedDecl *, 16> DeclList;
  
public:
  /// Constructs a new scope with the given parent and flags.
  explicit Scope(Scope *Parent = nullptr, ScopeFlags Flags = ScopeFlags::None)
      : Parent(Parent), Flags(Flags) {}
  
  /// Returns the parent scope.
  Scope *getParent() const { return Parent; }
  
  /// Returns the flags for this scope.
  ScopeFlags getFlags() const { return Flags; }
  
  /// Returns true if this scope has the given flag.
  bool hasFlags(ScopeFlags F) const {
    return (Flags & F) != ScopeFlags::None;
  }
  
  /// Adds a declaration to this scope.
  /// Returns true if successful, false if a declaration with the same name
  /// already exists in this scope.
  bool addDecl(NamedDecl *D);
  
  /// Adds a declaration to this scope, allowing redeclaration.
  void addDeclAllowRedeclaration(NamedDecl *D);
  
  /// Looks up a name in this scope only (not in parent scopes).
  /// Returns the declaration if found, nullptr otherwise.
  NamedDecl *lookupInScope(llvm::StringRef Name) const;
  
  /// Looks up a name in this scope and all parent scopes.
  /// Returns the declaration if found, nullptr otherwise.
  NamedDecl *lookup(llvm::StringRef Name) const;
  
  /// Returns all declarations in this scope.
  llvm::ArrayRef<NamedDecl *> decls() const { return DeclList; }
  
  /// Returns the number of declarations in this scope.
  unsigned getNumDecls() const { return DeclList.size(); }
  
  /// Returns true if this scope is empty (has no declarations).
  bool isEmpty() const { return DeclList.empty(); }
  
  /// Returns true if this is a function scope.
  bool isFunctionScope() const {
    return hasFlags(ScopeFlags::FunctionPrototypeScope) ||
           hasFlags(ScopeFlags::FunctionBodyScope);
  }
  
  /// Returns true if this is a class scope.
  bool isClassScope() const { return hasFlags(ScopeFlags::ClassScope); }
  
  /// Returns true if this is a block scope.
  bool isBlockScope() const { return hasFlags(ScopeFlags::BlockScope); }
  
  /// Returns true if this is a namespace scope.
  bool isNamespaceScope() const { return hasFlags(ScopeFlags::NamespaceScope); }
  
  /// Returns true if this is a translation unit scope.
  bool isTranslationUnitScope() const {
    return hasFlags(ScopeFlags::TranslationUnitScope);
  }
  
  /// Finds the nearest enclosing function scope.
  /// Returns nullptr if not found.
  Scope *getEnclosingFunctionScope();
  
  /// Finds the nearest enclosing class scope.
  /// Returns nullptr if not found.
  Scope *getEnclosingClassScope();
  
  /// Finds the nearest enclosing namespace scope.
  /// Returns nullptr if not found.
  Scope *getEnclosingNamespaceScope();
  
  /// Finds the nearest enclosing translation unit scope.
  /// Returns nullptr if not found.
  Scope *getEnclosingTranslationUnitScope();
  
  /// Dumps this scope to the given stream for debugging.
  void dump(llvm::raw_ostream &OS, unsigned Indent = 0) const;
  
  /// Dumps this scope to stderr for debugging.
  void dump() const;
};

} // namespace blocktype

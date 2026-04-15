//===--- Scope.cpp - Scope and Symbol Table Implementation --*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Scope class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Scope.h"
#include "blocktype/AST/Decl.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Scope Implementation
//===----------------------------------------------------------------------===//

bool Scope::addDecl(NamedDecl *D) {
  if (!D)
    return false;
  
  llvm::StringRef Name = D->getName();
  
  // Check if a declaration with this name already exists in this scope
  if (Declarations.find(Name) != Declarations.end()) {
    return false; // Redeclaration not allowed
  }
  
  Declarations[Name] = D;
  DeclList.push_back(D);
  return true;
}

void Scope::addDeclAllowRedeclaration(NamedDecl *D) {
  if (!D)
    return;
  
  llvm::StringRef Name = D->getName();
  Declarations[Name] = D;
  DeclList.push_back(D);
}

NamedDecl *Scope::lookupInScope(llvm::StringRef Name) const {
  auto It = Declarations.find(Name);
  if (It != Declarations.end())
    return It->second;
  return nullptr;
}

NamedDecl *Scope::lookup(llvm::StringRef Name) const {
  // First, search in this scope
  if (NamedDecl *D = lookupInScope(Name))
    return D;
  
  // If not found, search in parent scopes
  if (Parent)
    return Parent->lookup(Name);
  
  return nullptr;
}

Scope *Scope::getEnclosingFunctionScope() {
  if (isFunctionScope())
    return this;
  
  if (Parent)
    return Parent->getEnclosingFunctionScope();
  
  return nullptr;
}

Scope *Scope::getEnclosingClassScope() {
  if (isClassScope())
    return this;
  
  if (Parent)
    return Parent->getEnclosingClassScope();
  
  return nullptr;
}

Scope *Scope::getEnclosingNamespaceScope() {
  if (isNamespaceScope())
    return this;
  
  if (Parent)
    return Parent->getEnclosingNamespaceScope();
  
  return nullptr;
}

Scope *Scope::getEnclosingTranslationUnitScope() {
  if (isTranslationUnitScope())
    return this;
  
  if (Parent)
    return Parent->getEnclosingTranslationUnitScope();
  
  return nullptr;
}

void Scope::dump(llvm::raw_ostream &OS, unsigned Indent) const {
  // Print indentation
  for (unsigned I = 0; I < Indent; ++I)
    OS << "  ";
  
  // Print scope flags
  OS << "Scope ";
  if (hasFlags(ScopeFlags::TranslationUnitScope))
    OS << "[TranslationUnit] ";
  if (hasFlags(ScopeFlags::NamespaceScope))
    OS << "[Namespace] ";
  if (hasFlags(ScopeFlags::FunctionPrototypeScope))
    OS << "[FunctionPrototype] ";
  if (hasFlags(ScopeFlags::FunctionBodyScope))
    OS << "[FunctionBody] ";
  if (hasFlags(ScopeFlags::ClassScope))
    OS << "[Class] ";
  if (hasFlags(ScopeFlags::BlockScope))
    OS << "[Block] ";
  if (hasFlags(ScopeFlags::ControlScope))
    OS << "[Control] ";
  if (hasFlags(ScopeFlags::SwitchScope))
    OS << "[Switch] ";
  if (hasFlags(ScopeFlags::TemplateScope))
    OS << "[Template] ";
  
  OS << "(" << DeclList.size() << " decls)\n";
  
  // Print declarations in this scope
  for (const NamedDecl *D : DeclList) {
    for (unsigned I = 0; I < Indent + 1; ++I)
      OS << "  ";
    OS << "- " << D->getName() << "\n";
  }
  
  // Recursively dump parent scope (for debugging)
  if (Parent) {
    for (unsigned I = 0; I < Indent; ++I)
      OS << "  ";
    OS << "Parent:\n";
    Parent->dump(OS, Indent + 1);
  }
}

void Scope::dump() const {
  dump(llvm::errs());
}

} // namespace blocktype

//===--- SymbolTable.cpp - Symbol Table Implementation -----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/SymbolTable.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Basic/DiagnosticIDs.h"

namespace blocktype {

bool SymbolTable::addOrdinarySymbol(NamedDecl *D) {
  llvm::StringRef Name = D->getName();
  auto &Decls = OrdinarySymbols[Name];

  // Allow function overloading
  if (isa<FunctionDecl>(D) || isa<CXXMethodDecl>(D)) {
    Decls.push_back(D);
    return true;
  }

  // Non-function: check for redefinition against any existing non-function
  for (auto *Existing : Decls) {
    // Functions are already handled by the overloading path above,
    // so any Existing here that is a function can be skipped.
    if (isa<FunctionDecl>(Existing) || isa<CXXMethodDecl>(Existing))
      continue;
    // Two non-function declarations with the same name → redefinition
    Diags.report(D->getLocation(), DiagID::err_redefinition, Name);
    Diags.report(Existing->getLocation(), DiagID::note_previous_definition);
    break; // Only report once
  }

  Decls.push_back(D);
  return true;
}

bool SymbolTable::addTagDecl(TagDecl *D) {
  llvm::StringRef Name = D->getName();
  if (Name.empty()) return true; // Anonymous tags

  auto It = Tags.find(Name);
  if (It != Tags.end()) {
    // Forward declarations are OK — if either is forward-only, allow
    if (!D->isCompleteDefinition() || !It->second->isCompleteDefinition())
      return true;
    // Both are complete definitions — redefinition error
    Diags.report(D->getLocation(), DiagID::err_redefinition, Name);
    Diags.report(It->second->getLocation(), DiagID::note_previous_definition);
    return true;
  }
  Tags[Name] = D;
  return true;
}

bool SymbolTable::addTypedefDecl(TypedefNameDecl *D) {
  llvm::StringRef Name = D->getName();
  auto It = Typedefs.find(Name);
  if (It != Typedefs.end()) {
    // C: redeclaration of typedef is OK (C11)
    // C++: redefinition is an error
    return false;
  }
  Typedefs[Name] = D;
  return true;
}

void SymbolTable::addNamespaceDecl(NamespaceDecl *D) {
  Namespaces[D->getName()] = D;
}

void SymbolTable::addTemplateDecl(TemplateDecl *D) {
  Templates[D->getName()] = D;
}

void SymbolTable::addConceptDecl(ConceptDecl *D) {
  Concepts[D->getName()] = D;
}

bool SymbolTable::addDecl(NamedDecl *D) {
  if (isa<TagDecl>(D))         return addTagDecl(cast<TagDecl>(D));
  if (isa<TypedefNameDecl>(D)) return addTypedefDecl(cast<TypedefNameDecl>(D));
  if (isa<NamespaceDecl>(D))   { addNamespaceDecl(cast<NamespaceDecl>(D)); return true; }
  if (isa<TemplateDecl>(D))    { addTemplateDecl(cast<TemplateDecl>(D)); return true; }
  if (isa<ConceptDecl>(D))     { addConceptDecl(cast<ConceptDecl>(D)); return true; }
  return addOrdinarySymbol(D);
}

llvm::ArrayRef<NamedDecl *> SymbolTable::lookupOrdinary(llvm::StringRef Name) const {
  auto It = OrdinarySymbols.find(Name);
  if (It != OrdinarySymbols.end()) return It->second;
  return {};
}

TagDecl *SymbolTable::lookupTag(llvm::StringRef Name) const {
  auto It = Tags.find(Name);
  return It != Tags.end() ? It->second : nullptr;
}

TypedefNameDecl *SymbolTable::lookupTypedef(llvm::StringRef Name) const {
  auto It = Typedefs.find(Name);
  return It != Typedefs.end() ? It->second : nullptr;
}

NamespaceDecl *SymbolTable::lookupNamespace(llvm::StringRef Name) const {
  auto It = Namespaces.find(Name);
  return It != Namespaces.end() ? It->second : nullptr;
}

TemplateDecl *SymbolTable::lookupTemplate(llvm::StringRef Name) const {
  auto It = Templates.find(Name);
  return It != Templates.end() ? It->second : nullptr;
}

ConceptDecl *SymbolTable::lookupConcept(llvm::StringRef Name) const {
  auto It = Concepts.find(Name);
  return It != Concepts.end() ? It->second : nullptr;
}

llvm::ArrayRef<NamedDecl *> SymbolTable::lookup(llvm::StringRef Name) const {
  if (auto Ord = lookupOrdinary(Name); !Ord.empty()) return Ord;
  // For single-result lookups, check tags/typedefs
  return {};
}

bool SymbolTable::contains(llvm::StringRef Name) const {
  return OrdinarySymbols.count(Name) || Tags.count(Name) ||
         Typedefs.count(Name) || Namespaces.count(Name) ||
         Templates.count(Name) || Concepts.count(Name);
}

size_t SymbolTable::size() const {
  return OrdinarySymbols.size() + Tags.size() + Typedefs.size() +
         Namespaces.size() + Templates.size() + Concepts.size();
}

void SymbolTable::dump(llvm::raw_ostream &OS) const {
  OS << "=== SymbolTable ===\n";
  OS << "Ordinary symbols: " << OrdinarySymbols.size() << "\n";
  for (auto &[Name, Decls] : OrdinarySymbols) {
    OS << "  " << Name << " -> " << Decls.size() << " decl(s)\n";
  }
  OS << "Tags: " << Tags.size() << "\n";
  for (auto &[Name, D] : Tags) {
    OS << "  " << Name << "\n";
  }
  OS << "Typedefs: " << Typedefs.size() << "\n";
  for (auto &[Name, D] : Typedefs) {
    OS << "  " << Name << "\n";
  }
  OS << "Namespaces: " << Namespaces.size() << "\n";
  for (auto &[Name, D] : Namespaces) {
    OS << "  " << Name << "\n";
  }
  OS << "Templates: " << Templates.size() << "\n";
  for (auto &[Name, D] : Templates) {
    OS << "  " << Name << "\n";
  }
  OS << "Concepts: " << Concepts.size() << "\n";
  for (auto &[Name, D] : Concepts) {
    OS << "  " << Name << "\n";
  }
  OS << "===================\n";
}

void SymbolTable::dump() const {
  dump(llvm::errs());
}

} // namespace blocktype

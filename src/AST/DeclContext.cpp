//===--- DeclContext.cpp - Declaration Context Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/DeclContext.h"
#include "blocktype/AST/Decl.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// DeclContext implementation
//===----------------------------------------------------------------------===//

void DeclContext::addDecl(Decl *D) { Decls.push_back(D); }

void DeclContext::addDecl(NamedDecl *D) {
  Decls.push_back(D);
  if (!D->getName().empty()) {
    NameMap[D->getName()] = D;
  }
}

NamedDecl *DeclContext::lookupInContext(llvm::StringRef Name) const {
  auto It = NameMap.find(Name);
  if (It != NameMap.end())
    return It->second;
  return nullptr;
}

NamedDecl *DeclContext::lookup(llvm::StringRef Name) const {
  // Search this context first
  if (NamedDecl *D = lookupInContext(Name))
    return D;

  // Then search parent contexts
  if (ParentCtx)
    return ParentCtx->lookup(Name);

  return nullptr;
}

void DeclContext::dumpDeclContext(llvm::raw_ostream &OS, unsigned Indent) const {
  for (unsigned I = 0; I < Indent; ++I)
    OS << "  ";

  const char *KindName = "Unknown";
  switch (ContextKind) {
  case DeclContextKind::TranslationUnit: KindName = "TranslationUnit"; break;
  case DeclContextKind::Namespace:       KindName = "Namespace"; break;
  case DeclContextKind::CXXRecord:       KindName = "CXXRecord"; break;
  case DeclContextKind::Function:        KindName = "Function"; break;
  case DeclContextKind::Enum:            KindName = "Enum"; break;
  case DeclContextKind::LinkageSpec:     KindName = "LinkageSpec"; break;
  case DeclContextKind::Block:           KindName = "Block"; break;
  case DeclContextKind::TemplateParams:  KindName = "TemplateParams"; break;
  }

  OS << "DeclContext(" << KindName << ") " << Decls.size() << " decls\n";

  for (auto *D : Decls) {
    D->dump(OS, Indent + 1);
  }
}

} // namespace blocktype

//===--- ASTContext.cpp - AST Context Implementation ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/ASTContext.h"

namespace blocktype {

ASTContext::~ASTContext() {
  destroyAll();
}

void ASTContext::clear() {
  destroyAll();
  Nodes.clear();
  Allocator.Reset();
}

void ASTContext::destroyAll() {
  // Destroy in reverse order of creation
  for (auto It = Nodes.rbegin(); It != Nodes.rend(); ++It) {
    ASTNode *Node = *It;
    Node->~ASTNode();
  }
}

void ASTContext::dumpMemoryUsage(raw_ostream &OS) const {
  OS << "AST Memory Usage:\n";
  OS << "  Nodes allocated: " << Nodes.size() << "\n";
  OS << "  Total memory: " << getMemoryUsage() << " bytes\n";
  OS << "  Allocator overhead: " 
     << (getMemoryUsage() - Nodes.size() * sizeof(ASTNode)) << " bytes\n";
}

} // namespace blocktype

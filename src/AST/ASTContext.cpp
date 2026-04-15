//===--- ASTContext.cpp - AST Context Implementation ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"
#include "llvm/Support/Casting.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

//===----------------------------------------------------------------------===//
// Type Creation
//===----------------------------------------------------------------------===//

BuiltinType *ASTContext::getBuiltinType(BuiltinKind Kind) {
  unsigned Index = static_cast<unsigned>(Kind);
  
  // Check cache
  if (Index < static_cast<unsigned>(BuiltinKind::NumBuiltinTypes) && BuiltinTypes[Index]) {
    return BuiltinTypes[Index];
  }
  
  // Create new builtin type
  void *Mem = Allocator.Allocate(sizeof(BuiltinType), alignof(BuiltinType));
  BuiltinType *BT = new (Mem) BuiltinType(Kind);
  
  // Cache it
  if (Index < static_cast<unsigned>(BuiltinKind::NumBuiltinTypes)) {
    BuiltinTypes[Index] = BT;
  }
  
  return BT;
}

PointerType *ASTContext::getPointerType(const Type *Pointee) {
  // Create new pointer type (no deduplication for simplicity)
  void *Mem = Allocator.Allocate(sizeof(PointerType), alignof(PointerType));
  PointerType *PT = new (Mem) PointerType(Pointee);
  return PT;
}

LValueReferenceType *ASTContext::getLValueReferenceType(const Type *Referenced) {
  // Create new reference type (no deduplication for simplicity)
  void *Mem = Allocator.Allocate(sizeof(LValueReferenceType), alignof(LValueReferenceType));
  LValueReferenceType *RT = new (Mem) LValueReferenceType(Referenced);
  return RT;
}

RValueReferenceType *ASTContext::getRValueReferenceType(const Type *Referenced) {
  // Create new reference type (no deduplication for simplicity)
  void *Mem = Allocator.Allocate(sizeof(RValueReferenceType), alignof(RValueReferenceType));
  RValueReferenceType *RT = new (Mem) RValueReferenceType(Referenced);
  return RT;
}

ArrayType *ASTContext::getArrayType(const Type *Element, Expr *Size) {
  // Create new array type (no deduplication for simplicity)
  void *Mem = Allocator.Allocate(sizeof(ArrayType), alignof(ArrayType));
  ArrayType *AT = new (Mem) ArrayType(Element, Size);
  return AT;
}

UnresolvedType *ASTContext::getUnresolvedType(llvm::StringRef Name) {
  // Create new unresolved type
  void *Mem = Allocator.Allocate(sizeof(UnresolvedType), alignof(UnresolvedType));
  auto *Unresolved = new (Mem) UnresolvedType(Name);
  return Unresolved;
}

AutoType *ASTContext::getAutoType() {
  // Create new auto type
  void *Mem = Allocator.Allocate(sizeof(AutoType), alignof(AutoType));
  auto *Auto = new (Mem) AutoType();
  return Auto;
}

TemplateSpecializationType *ASTContext::getTemplateSpecializationType(llvm::StringRef Name) {
  // Create new template specialization type
  void *Mem = Allocator.Allocate(sizeof(TemplateSpecializationType), alignof(TemplateSpecializationType));
  auto *Template = new (Mem) TemplateSpecializationType(Name);
  return Template;
}

ElaboratedType *ASTContext::getElaboratedType(const Type *NamedType, llvm::StringRef Qualifier) {
  // Create new elaborated type
  void *Mem = Allocator.Allocate(sizeof(ElaboratedType), alignof(ElaboratedType));
  auto *Elaborated = new (Mem) ElaboratedType(NamedType, Qualifier);
  return Elaborated;
}

QualType ASTContext::getTypeDeclType(const TypeDecl *D) {
  // Check the specific type of TypeDecl
  if (auto *RD = dyn_cast<RecordDecl>(D)) {
    // Create RecordType
    void *Mem = Allocator.Allocate(sizeof(RecordType), alignof(RecordType));
    auto *RT = new (Mem) RecordType(const_cast<RecordDecl*>(RD));
    return QualType(RT, Qualifier::None);
  }
  
  if (auto *ED = dyn_cast<EnumDecl>(D)) {
    // Create EnumType
    void *Mem = Allocator.Allocate(sizeof(EnumType), alignof(EnumType));
    auto *ET = new (Mem) EnumType(const_cast<EnumDecl*>(ED));
    return QualType(ET, Qualifier::None);
  }
  
  if (auto *TND = dyn_cast<TypedefNameDecl>(D)) {
    // Create TypedefType
    void *Mem = Allocator.Allocate(sizeof(TypedefType), alignof(TypedefType));
    auto *TT = new (Mem) TypedefType(const_cast<TypedefNameDecl*>(TND));
    return QualType(TT, Qualifier::None);
  }
  
  // For other TypeDecls (e.g., TemplateTypeParmDecl), create an unresolved type
  return QualType(getUnresolvedType(D->getName()), Qualifier::None);
}

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

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
#include "blocktype/AST/Expr.h"
#include "llvm/ADT/APInt.h"
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
  // Check cache first — same pointee type must produce same PointerType*
  auto It = PointerTypeCache.find(Pointee);
  if (It != PointerTypeCache.end())
    return It->second;

  void *Mem = Allocator.Allocate(sizeof(PointerType), alignof(PointerType));
  PointerType *PT = new (Mem) PointerType(Pointee);
  PointerTypeCache[Pointee] = PT;
  return PT;
}

LValueReferenceType *ASTContext::getLValueReferenceType(const Type *Referenced) {
  auto It = LValueRefTypeCache.find(Referenced);
  if (It != LValueRefTypeCache.end())
    return It->second;

  void *Mem = Allocator.Allocate(sizeof(LValueReferenceType), alignof(LValueReferenceType));
  LValueReferenceType *RT = new (Mem) LValueReferenceType(Referenced);
  LValueRefTypeCache[Referenced] = RT;
  return RT;
}

RValueReferenceType *ASTContext::getRValueReferenceType(const Type *Referenced) {
  auto It = RValueRefTypeCache.find(Referenced);
  if (It != RValueRefTypeCache.end())
    return It->second;

  void *Mem = Allocator.Allocate(sizeof(RValueReferenceType), alignof(RValueReferenceType));
  RValueReferenceType *RT = new (Mem) RValueReferenceType(Referenced);
  RValueRefTypeCache[Referenced] = RT;
  return RT;
}

ArrayType *ASTContext::getArrayType(const Type *Element, Expr *Size) {
  if (Size) {
    // Try to evaluate the size expression
    if (auto *IL = dyn_cast<IntegerLiteral>(Size)) {
      // Constant size array
      return getConstantArrayType(Element, Size, IL->getValue());
    }
    // For non-constant expressions (e.g., template parameters),
    // create a variable length array type
    return getVariableArrayType(Element, Size);
  }
  return getIncompleteArrayType(Element);
}

ConstantArrayType *ASTContext::getConstantArrayType(const Type *Element,
                                                     Expr *SizeExpr,
                                                     llvm::APInt Size) {
  void *Mem = Allocator.Allocate(sizeof(ConstantArrayType),
                                   alignof(ConstantArrayType));
  auto *AT = new (Mem) ConstantArrayType(Element, SizeExpr, Size);
  return AT;
}

IncompleteArrayType *ASTContext::getIncompleteArrayType(const Type *Element) {
  void *Mem = Allocator.Allocate(sizeof(IncompleteArrayType),
                                   alignof(IncompleteArrayType));
  auto *AT = new (Mem) IncompleteArrayType(Element);
  return AT;
}

VariableArrayType *ASTContext::getVariableArrayType(const Type *Element,
                                                     Expr *SizeExpr) {
  void *Mem = Allocator.Allocate(sizeof(VariableArrayType),
                                   alignof(VariableArrayType));
  auto *AT = new (Mem) VariableArrayType(Element, SizeExpr);
  return AT;
}

TemplateTypeParmType *ASTContext::getTemplateTypeParmType(TemplateTypeParmDecl *Decl,
                                                           unsigned Index,
                                                           unsigned Depth,
                                                           bool IsPack) {
  void *Mem = Allocator.Allocate(sizeof(TemplateTypeParmType),
                                   alignof(TemplateTypeParmType));
  auto *TPT = new (Mem) TemplateTypeParmType(Decl, Index, Depth, IsPack);
  return TPT;
}

DependentType *ASTContext::getDependentType(const Type *BaseType,
                                             llvm::StringRef Name) {
  void *Mem = Allocator.Allocate(sizeof(DependentType), alignof(DependentType));
  auto *DT = new (Mem) DependentType(BaseType, saveString(Name));
  return DT;
}

UnresolvedType *ASTContext::getUnresolvedType(llvm::StringRef Name) {
  // Create new unresolved type
  void *Mem = Allocator.Allocate(sizeof(UnresolvedType), alignof(UnresolvedType));
  auto *Unresolved = new (Mem) UnresolvedType(Name);
  return Unresolved;
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

DecltypeType *ASTContext::getDecltypeType(Expr *E, QualType Underlying) {
  // Create new decltype type
  void *Mem = Allocator.Allocate(sizeof(DecltypeType), alignof(DecltypeType));
  auto *Decltype = new (Mem) DecltypeType(E, Underlying);
  return Decltype;
}

MemberPointerType *ASTContext::getMemberPointerType(const Type *ClassType, const Type *PointeeType) {
  // Create new member pointer type
  void *Mem = Allocator.Allocate(sizeof(MemberPointerType), alignof(MemberPointerType));
  auto *MemberPtr = new (Mem) MemberPointerType(ClassType, PointeeType);
  return MemberPtr;
}

FunctionType *ASTContext::getFunctionType(const Type *ReturnType,
                                          llvm::ArrayRef<const Type *> ParamTypes,
                                          bool IsVariadic,
                                          bool IsConst,
                                          bool IsVolatile) {
  // Create new function type
  void *Mem = Allocator.Allocate(sizeof(FunctionType), alignof(FunctionType));
  auto *Func = new (Mem) FunctionType(ReturnType, ParamTypes, IsVariadic,
                                       IsConst, IsVolatile);
  return Func;
}

QualType ASTContext::getTypeDeclType(const TypeDecl *D) {
  // Check the specific type of TypeDecl
  if (auto *RD = dyn_cast<RecordDecl>(D)) {
    // Use cached getRecordType to ensure type uniqueness
    return getRecordType(const_cast<RecordDecl*>(RD));
  }
  
  if (auto *ED = dyn_cast<EnumDecl>(D)) {
    // Use cached getEnumType to ensure type uniqueness
    return getEnumType(const_cast<EnumDecl*>(ED));
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

QualType ASTContext::getAutoType() {
  // Create AutoType
  void *Mem = Allocator.Allocate(sizeof(AutoType), alignof(AutoType));
  auto *AT = new (Mem) AutoType();
  return QualType(AT, Qualifier::None);
}

//===----------------------------------------------------------------------===//
// Convenience type factories [Stage 4.2]
//===----------------------------------------------------------------------===//

QualType ASTContext::getRecordType(RecordDecl *D) {
  auto It = RecordTypeCache.find(D);
  if (It != RecordTypeCache.end())
    return QualType(It->second, Qualifier::None);
  void *Mem = Allocator.Allocate(sizeof(RecordType), alignof(RecordType));
  auto *RT = new (Mem) RecordType(D);
  RecordTypeCache[D] = RT;
  return QualType(RT, Qualifier::None);
}

QualType ASTContext::getEnumType(EnumDecl *D) {
  auto It = EnumTypeCache.find(D);
  if (It != EnumTypeCache.end())
    return QualType(It->second, Qualifier::None);
  void *Mem = Allocator.Allocate(sizeof(EnumType), alignof(EnumType));
  auto *ET = new (Mem) EnumType(D);
  EnumTypeCache[D] = ET;
  return QualType(ET, Qualifier::None);
}

QualType ASTContext::getVoidType() {
  return QualType(getBuiltinType(BuiltinKind::Void), Qualifier::None);
}

QualType ASTContext::getBoolType() {
  return QualType(getBuiltinType(BuiltinKind::Bool), Qualifier::None);
}

QualType ASTContext::getIntType() {
  return QualType(getBuiltinType(BuiltinKind::Int), Qualifier::None);
}

QualType ASTContext::getFloatType() {
  return QualType(getBuiltinType(BuiltinKind::Float), Qualifier::None);
}

QualType ASTContext::getDoubleType() {
  return QualType(getBuiltinType(BuiltinKind::Double), Qualifier::None);
}

QualType ASTContext::getLongType() {
  return QualType(getBuiltinType(BuiltinKind::Long), Qualifier::None);
}

QualType ASTContext::getNullPtrType() {
  return QualType(getBuiltinType(BuiltinKind::NullPtr), Qualifier::None);
}

QualType ASTContext::getCharType() {
  return QualType(getBuiltinType(BuiltinKind::Char), Qualifier::None);
}

QualType ASTContext::getSCharType() {
  return QualType(getBuiltinType(BuiltinKind::SignedChar), Qualifier::None);
}

QualType ASTContext::getUCharType() {
  return QualType(getBuiltinType(BuiltinKind::UnsignedChar), Qualifier::None);
}

QualType ASTContext::getWCharType() {
  return QualType(getBuiltinType(BuiltinKind::WChar), Qualifier::None);
}

QualType ASTContext::getShortType() {
  return QualType(getBuiltinType(BuiltinKind::Short), Qualifier::None);
}

QualType ASTContext::getUShortType() {
  return QualType(getBuiltinType(BuiltinKind::UnsignedShort), Qualifier::None);
}

QualType ASTContext::getUIntType() {
  return QualType(getBuiltinType(BuiltinKind::UnsignedInt), Qualifier::None);
}

QualType ASTContext::getULongType() {
  return QualType(getBuiltinType(BuiltinKind::UnsignedLong), Qualifier::None);
}

QualType ASTContext::getLongLongType() {
  return QualType(getBuiltinType(BuiltinKind::LongLong), Qualifier::None);
}

QualType ASTContext::getULongLongType() {
  return QualType(getBuiltinType(BuiltinKind::UnsignedLongLong), Qualifier::None);
}

QualType ASTContext::getQualifiedType(const Type *T, Qualifier Q) {
  return QualType(T, Q);
}

QualType ASTContext::getMemberFunctionType(const Type *ReturnType,
                                            llvm::ArrayRef<const Type *> ParamTypes,
                                            const Type *ClassType,
                                            bool IsConst, bool IsVolatile,
                                            bool IsVariadic) {
  // Create the function type with method qualifiers (const/volatile belong to
  // the function type per C++ semantics: R (C::*)(Args...) const)
  FunctionType *FT = getFunctionType(ReturnType, ParamTypes, IsVariadic,
                                      IsConst, IsVolatile);

  // Wrap in MemberPointerType — no CVR qualifiers on the member pointer itself
  MemberPointerType *MPT = getMemberPointerType(ClassType, FT);

  return QualType(MPT, Qualifier::None);
}

ASTContext::~ASTContext() {
  destroyAll();
}

void ASTContext::clear() {
  destroyAll();
  Nodes.clear();
  Cleanups.clear();
  Allocator.Reset();
}

void ASTContext::destroyAll() {
  // Run registered cleanup callbacks in reverse order (LIFO).
  // This ensures resources owned by bump-allocated objects are properly
  // released before the allocator memory is reclaimed.
  for (auto It = Cleanups.rbegin(); It != Cleanups.rend(); ++It) {
    (*It)();
  }

  // Destroy AST nodes in reverse order of creation via virtual destructor.
  // Since ~ASTNode() is virtual, this correctly calls the most-derived
  // destructor, handling SmallVector and other non-trivial members.
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

QualType ASTContext::getMetaInfoType() {
  // MetaInfoType is a singleton — the canonical reflection type.
  // It wraps a null reflectee since the actual reflected entity is set
  // per-expression, not per-type.
  static auto *MetaInfo = new MetaInfoType(nullptr, MetaInfoType::RK_Type);
  return QualType(MetaInfo, Qualifier::None);
}

} // namespace blocktype

//===--- TypeVisitor.h - Type Visitor Pattern ------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TypeVisitor class template, which implements the
// visitor pattern for traversing and transforming Type nodes in the AST.
//
// P7.4.3: Used by TemplateInstantiation for recursive type substitution.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_AST_TYPE_VISITOR_H
#define BLOCKTYPE_AST_TYPE_VISITOR_H

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"  // For TypedefNameDecl
#include "llvm/ADT/STLExtras.h"

namespace blocktype {

/// TypeVisitor - CRTP base class for visiting Type nodes.
///
/// Usage:
///   class MyVisitor : public TypeVisitor<MyVisitor> {
///   public:
///     const Type *VisitBuiltinType(const BuiltinType *T) { ... }
///     const Type *VisitPointerType(const PointerType *T) { ... }
///     // ... implement other Visit* methods as needed
///   };
///
///   MyVisitor Visitor;
///   const Type *Result = Visitor.Visit(SomeType);
///
template <typename ImplClass>
class TypeVisitor {
public:
  /// Visit a Type node and dispatch to the appropriate Visit* method.
  const Type *Visit(const Type *T) {
    if (!T) {
      return nullptr;
    }

    switch (T->getTypeClass()) {
// Undef macros from Type.h to avoid redefinition warnings
#ifdef TYPE
#undef TYPE
#endif
#ifdef ABSTRACT_TYPE
#undef ABSTRACT_TYPE
#endif
#define TYPE(CLASS, PARENT) \
  case TypeClass::CLASS: \
    return static_cast<ImplClass *>(this)->Visit##CLASS##Type( \
        llvm::cast<CLASS##Type>(T));
#define ABSTRACT_TYPE(CLASS, PARENT) \
  case TypeClass::CLASS: \
    return static_cast<ImplClass *>(this)->Visit##CLASS##Type( \
        llvm::cast<CLASS##Type>(T));
#include "blocktype/AST/TypeNodes.def"
#undef ABSTRACT_TYPE
#undef TYPE
    default:
      break;
    }

    llvm_unreachable("Unknown type class!");
  }

  /// Default implementation: recurse into child types if applicable.
  const Type *VisitType(const Type *T) {
    // Default: return unchanged
    return T;
  }

  //===------------------------------------------------------------------===//
  // Visit methods for each Type subclass
  //===------------------------------------------------------------------===//

  const Type *VisitBuiltinType(const BuiltinType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitPointerType(const PointerType *T) {
    // Recurse into pointee type
    const Type *NewPointee = Visit(T->getPointeeType());
    if (NewPointee == T->getPointeeType()) {
      return T; // No change
    }
    // TODO: Create new PointerType with NewPointee
    return T; // Placeholder
  }

  const Type *VisitReferenceType(const ReferenceType *T) {
    // Recurse into referenced type
    const Type *NewReferent = Visit(T->getReferencedType());
    if (NewReferent == T->getReferencedType()) {
      return T; // No change
    }
    // TODO: Create new ReferenceType with NewReferent
    return T; // Placeholder
  }

  const Type *VisitArrayType(const ArrayType *T) {
    // Recurse into element type
    const Type *NewElement = Visit(T->getElementType());
    if (NewElement == T->getElementType()) {
      return T; // No change
    }
    // TODO: Create new ArrayType with NewElement
    return T; // Placeholder
  }

  const Type *VisitFunctionType(const FunctionType *T) {
    // TODO: Recurse into return type and parameter types
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitRecordType(const RecordType *T) {
    // TODO: Substitute template arguments if this is a specialization
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
    // TODO: Look up in substitution map and replace
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitTypedefType(const TypedefType *T) {
    // Recurse into underlying type
    const Type *NewUnderlying = Visit(T->getDecl()->getUnderlyingType().getTypePtr());
    if (NewUnderlying == T->getDecl()->getUnderlyingType().getTypePtr()) {
      return T; // No change
    }
    // TODO: Create new TypedefType or return underlying
    return T; // Placeholder
  }

  const Type *VisitEnumType(const EnumType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  //===------------------------------------------------------------------===//
  // Additional Visit methods for all Type subclasses in TypeNodes.def
  //===------------------------------------------------------------------===//

  // Reference types (concrete)
  const Type *VisitLValueReferenceType(const LValueReferenceType *T) {
    return static_cast<ImplClass *>(this)->VisitReferenceType(T);
  }

  const Type *VisitRValueReferenceType(const RValueReferenceType *T) {
    return static_cast<ImplClass *>(this)->VisitReferenceType(T);
  }

  // Array types (concrete)
  const Type *VisitConstantArrayType(const ConstantArrayType *T) {
    return static_cast<ImplClass *>(this)->VisitArrayType(T);
  }

  const Type *VisitIncompleteArrayType(const IncompleteArrayType *T) {
    return static_cast<ImplClass *>(this)->VisitArrayType(T);
  }

  const Type *VisitVariableArrayType(const VariableArrayType *T) {
    return static_cast<ImplClass *>(this)->VisitArrayType(T);
  }

  // Other concrete types
  const Type *VisitElaboratedType(const ElaboratedType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitTemplateSpecializationType(const TemplateSpecializationType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitDependentType(const DependentType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitAutoType(const AutoType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitDecltypeType(const DecltypeType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitUnresolvedType(const UnresolvedType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitMemberPointerType(const MemberPointerType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }

  const Type *VisitMetaInfoType(const MetaInfoType *T) {
    return static_cast<ImplClass *>(this)->VisitType(T);
  }
};

} // namespace blocktype

#endif // BLOCKTYPE_AST_TYPE_VISITOR_H

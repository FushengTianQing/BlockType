//===--- Type.h - Type System -------------------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Type class and all type representations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

class Expr;
class RecordDecl;
class EnumDecl;
class TemplateDecl;

//===----------------------------------------------------------------------===//
// Type Classifications
//===----------------------------------------------------------------------===//

/// TypeClass - Enumeration of all type classes.
enum class TypeClass {
#define TYPE(Class, Base) Class,
#define ABSTRACT_TYPE(Class, Base) Class,
#define LAST_TYPE(Class) Class,
#include "TypeNodes.def"
  NumTypeClasses
};

//===----------------------------------------------------------------------===//
// CVR Qualifiers
//===----------------------------------------------------------------------===//

/// Qualifiers - CVR qualifiers for types.
enum class Qualifier : unsigned {
  None = 0,
  Const = 1 << 0,
  Volatile = 1 << 1,
  Restrict = 1 << 2,
  // C++11 ref-qualifiers
  LValueRef = 1 << 3,
  RValueRef = 1 << 4,
};

inline Qualifier operator|(Qualifier A, Qualifier B) {
  return static_cast<Qualifier>(static_cast<unsigned>(A) |
                                 static_cast<unsigned>(B));
}

inline Qualifier operator&(Qualifier A, Qualifier B) {
  return static_cast<Qualifier>(static_cast<unsigned>(A) &
                                 static_cast<unsigned>(B));
}

inline bool hasQualifier(Qualifier Q, Qualifier Mask) {
  return (Q & Mask) != Qualifier::None;
}

//===----------------------------------------------------------------------===//
// Builtin Types
//===----------------------------------------------------------------------===//

/// BuiltinKind - Enumeration of all builtin types.
enum class BuiltinKind {
#define BUILTIN_TYPE(Name, Id) Name,
#include "BuiltinTypes.def"
  NumBuiltinTypes
};

//===----------------------------------------------------------------------===//
// Type - Base class for all types
//===----------------------------------------------------------------------===//

/// Type - Base class for all types.
class Type {
  TypeClass Kind;
  /// ⚠️ TODO (Phase 4): Complete canonical type system implementation
  /// This field stores the canonical (normalized) type for type comparison.
  /// Phase 4 semantic analysis should implement:
  /// - Automatic canonical type computation during type creation
  /// - Typedef and type alias resolution
  /// - Template type substitution and normalization
  const Type *CanonicalType = nullptr; // Canonical type for type comparison

protected:
  Type(TypeClass K) : Kind(K) {}

public:
  TypeClass getTypeClass() const { return Kind; }

  // Type predicates
  bool isBuiltinType() const;
  bool isPointerType() const;
  bool isReferenceType() const;
  bool isArrayType() const;
  bool isFunctionType() const;
  bool isRecordType() const;
  bool isEnumType() const;

  bool isIntegerType() const;
  bool isFloatingType() const;
  bool isVoidType() const;
  bool isBooleanType() const;

  /// isDependentType - Returns true if this type depends on a template parameter.
  bool isDependentType() const;

  /// getCanonicalType - Returns the canonical type.
  /// The canonical type is the unique representative of an equivalence class.
  /// 
  /// ⚠️ TODO (Phase 4): Complete canonical type normalization
  /// Current implementation simply returns the stored CanonicalType or self.
  /// Full implementation should:
  /// - Recursively normalize typedef types to their underlying types
  /// - Handle type aliases and using declarations
  /// - Ensure type equivalence classes are properly merged
  /// - Support semantic analysis phase type checking
  const Type *getCanonicalType() const {
    return CanonicalType ? CanonicalType : this;
  }

  /// setCanonicalType - Sets the canonical type.
  void setCanonicalType(const Type *CT) { CanonicalType = CT; }

  /// dump - Debug dump.
  void dump() const;
  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) { return true; }
};

//===----------------------------------------------------------------------===//
// BuiltinType - Builtin types (int, char, void, etc.)
//===----------------------------------------------------------------------===//

/// BuiltinType - Represents builtin types like int, char, void, etc.
class BuiltinType : public Type {
  BuiltinKind Kind;

public:
  BuiltinType(BuiltinKind K) : Type(TypeClass::Builtin), Kind(K) {}

  BuiltinKind getKind() const { return Kind; }

  bool isSignedInteger() const;
  bool isUnsignedInteger() const;
  bool isFloatingPoint() const;
  bool isInteger() const;

  const char *getName() const;

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Builtin;
  }
};

//===----------------------------------------------------------------------===//
// PointerType - Pointer types
//===----------------------------------------------------------------------===//

/// PointerType - Represents pointer types.
class PointerType : public Type {
  const Type *Pointee;

public:
  PointerType(const Type *Pointee)
      : Type(TypeClass::Pointer), Pointee(Pointee) {}

  const Type *getPointeeType() const { return Pointee; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Pointer;
  }
};

//===----------------------------------------------------------------------===//
// ReferenceType - Reference types
//===----------------------------------------------------------------------===//

/// ReferenceType - Represents reference types (lvalue and rvalue).
class ReferenceType : public Type {
  const Type *Referenced;
  bool IsLValue;

protected:
  ReferenceType(const Type *Referenced, bool IsLValue)
      : Type(IsLValue ? TypeClass::LValueReference : TypeClass::RValueReference),
        Referenced(Referenced), IsLValue(IsLValue) {}

public:
  const Type *getReferencedType() const { return Referenced; }
  bool isLValueReference() const { return IsLValue; }
  bool isRValueReference() const { return !IsLValue; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::LValueReference ||
           T->getTypeClass() == TypeClass::RValueReference;
  }
};

/// LValueReferenceType - Lvalue reference type.
class LValueReferenceType : public ReferenceType {
public:
  LValueReferenceType(const Type *Referenced)
      : ReferenceType(Referenced, true) {}

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::LValueReference;
  }
};

/// RValueReferenceType - Rvalue reference type.
class RValueReferenceType : public ReferenceType {
public:
  RValueReferenceType(const Type *Referenced)
      : ReferenceType(Referenced, false) {}

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::RValueReference;
  }
};

//===----------------------------------------------------------------------===//
// ArrayType - Array types
//===----------------------------------------------------------------------===//

/// ArrayType - Abstract base class for array types.
class ArrayType : public Type {
protected:
  const Type *ElementType;

  ArrayType(TypeClass TC, const Type *Elem)
      : Type(TC), ElementType(Elem) {}

public:
  const Type *getElementType() const { return ElementType; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::ConstantArray ||
           T->getTypeClass() == TypeClass::IncompleteArray ||
           T->getTypeClass() == TypeClass::VariableArray;
  }
};

/// ConstantArrayType - Represents constant array types with known size.
/// Example: int[10], char[256]
class ConstantArrayType : public ArrayType {
  Expr *Size;
  llvm::APInt SizeValue; // Cached size value

public:
  ConstantArrayType(const Type *Elem, Expr *SizeExpr, llvm::APInt SizeVal)
      : ArrayType(TypeClass::ConstantArray, Elem), Size(SizeExpr), SizeValue(SizeVal) {}

  Expr *getSizeExpr() const { return Size; }
  const llvm::APInt &getSize() const { return SizeValue; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::ConstantArray;
  }
};

/// IncompleteArrayType - Represents incomplete array types without size.
/// Example: int[], char[]
/// Used in function parameter declarations and forward declarations.
class IncompleteArrayType : public ArrayType {
public:
  IncompleteArrayType(const Type *Elem)
      : ArrayType(TypeClass::IncompleteArray, Elem) {}

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::IncompleteArray;
  }
};

/// VariableArrayType - Represents variable length arrays (VLA).
/// Example: int[n], char[size]
/// Note: VLAs are a C99 feature, not standard C++, but supported by some compilers.
class VariableArrayType : public ArrayType {
  Expr *SizeExpr;

public:
  VariableArrayType(const Type *Elem, Expr *Size)
      : ArrayType(TypeClass::VariableArray, Elem), SizeExpr(Size) {}

  Expr *getSizeExpr() const { return SizeExpr; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::VariableArray;
  }
};

//===----------------------------------------------------------------------===//
// FunctionType - Function types
//===----------------------------------------------------------------------===//

/// FunctionType - Represents function types.
class FunctionType : public Type {
  const Type *ReturnType;
  llvm::SmallVector<const Type *, 4> ParamTypes;
  bool IsVariadic;

public:
  FunctionType(const Type *ReturnType, llvm::ArrayRef<const Type *> ParamTypes,
               bool IsVariadic = false)
      : Type(TypeClass::Function), ReturnType(ReturnType),
        ParamTypes(ParamTypes.begin(), ParamTypes.end()), IsVariadic(IsVariadic) {}

  const Type *getReturnType() const { return ReturnType; }
  llvm::ArrayRef<const Type *> getParamTypes() const { return ParamTypes; }
  bool isVariadic() const { return IsVariadic; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Function;
  }
};

//===----------------------------------------------------------------------===//
// RecordType - Record types (class/struct/union)
//===----------------------------------------------------------------------===//

/// RecordType - Represents class, struct, and union types.
class RecordType : public Type {
  RecordDecl *Decl;

public:
  RecordType(RecordDecl *D) : Type(TypeClass::Record), Decl(D) {}

  RecordDecl *getDecl() const { return Decl; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Record;
  }
};

//===----------------------------------------------------------------------===//
// EnumType - Enum types
//===----------------------------------------------------------------------===//

/// EnumType - Represents enum types.
class EnumType : public Type {
  EnumDecl *Decl;

public:
  EnumType(EnumDecl *D) : Type(TypeClass::Enum), Decl(D) {}

  EnumDecl *getDecl() const { return Decl; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Enum;
  }
};

//===----------------------------------------------------------------------===//
// TypedefType - Typedef types
//===----------------------------------------------------------------------===//

class TypedefNameDecl;

/// TypedefType - Represents typedef types.
class TypedefType : public Type {
  TypedefNameDecl *Decl;

public:
  TypedefType(TypedefNameDecl *D) : Type(TypeClass::Typedef), Decl(D) {}

  TypedefNameDecl *getDecl() const { return Decl; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Typedef;
  }
};

//===----------------------------------------------------------------------===//
// ElaboratedType - Elaborated types (A::B::C)
//===----------------------------------------------------------------------===//

/// ElaboratedType - Represents elaborated types with nested-name-specifier.
/// Example: A::B::C, ::std::vector
class ElaboratedType : public Type {
  const Type *NamedType;
  llvm::StringRef Qualifier; // Simplified: just store the qualifier string

public:
  ElaboratedType(const Type *T, llvm::StringRef Qual = "")
      : Type(TypeClass::Elaborated), NamedType(T), Qualifier(Qual) {}

  const Type *getNamedType() const { return NamedType; }
  llvm::StringRef getQualifier() const { return Qualifier; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Elaborated;
  }
};

//===----------------------------------------------------------------------===//
// UnresolvedType - Unresolved type (forward reference)
//===----------------------------------------------------------------------===//

/// UnresolvedType - Represents a type that hasn't been resolved yet.
/// Used for forward references and type names that haven't been looked up.
class UnresolvedType : public Type {
  llvm::StringRef Name;

public:
  UnresolvedType(llvm::StringRef N) : Type(TypeClass::Unresolved), Name(N) {}

  llvm::StringRef getName() const { return Name; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Unresolved;
  }
};

//===----------------------------------------------------------------------===//
// MemberPointerType - Member pointer type
//===----------------------------------------------------------------------===//

/// MemberPointerType - Represents pointer to member type.
/// Example: int (Class::*)
class MemberPointerType : public Type {
  const Type *ClassType;
  const Type *PointeeType;

public:
  MemberPointerType(const Type *Pointee, const Type *Class)
      : Type(TypeClass::MemberPointer), ClassType(Class), PointeeType(Pointee) {}

  const Type *getClassType() const { return ClassType; }
  const Type *getPointeeType() const { return PointeeType; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::MemberPointer;
  }
};

//===----------------------------------------------------------------------===//
// TemplateTypeParmType - Template type parameter
//===----------------------------------------------------------------------===//

// Forward declaration
class TemplateTypeParmDecl;

/// TemplateTypeParmType - Represents a template type parameter.
/// Example: T in template<typename T>
/// This type is used to represent template parameters before they are
/// substituted with actual types.
class TemplateTypeParmType : public Type {
  TemplateTypeParmDecl *Decl;
  unsigned Index;       // Index in template parameter list
  unsigned Depth;       // Depth of template parameter (for nested templates)
  bool IsParameterPack; // Whether this is a parameter pack (T...)

public:
  TemplateTypeParmType(TemplateTypeParmDecl *D, unsigned Idx, unsigned Depth,
                       bool IsPack = false)
      : Type(TypeClass::TemplateTypeParm), Decl(D), Index(Idx), Depth(Depth),
        IsParameterPack(IsPack) {}

  TemplateTypeParmDecl *getDecl() const { return Decl; }
  unsigned getIndex() const { return Index; }
  unsigned getDepth() const { return Depth; }
  bool isParameterPack() const { return IsParameterPack; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::TemplateTypeParm;
  }
};

//===----------------------------------------------------------------------===//
// DependentType - Dependent type
//===----------------------------------------------------------------------===//

/// DependentType - Represents a type that depends on template parameters.
/// Example: T::iterator, typename T::value_type, T*
/// A dependent type cannot be resolved until template arguments are provided.
class DependentType : public Type {
  const Type *BaseType;
  llvm::StringRef Name; // Name of the dependent member (for T::name)

public:
  /// Construct a dependent type for T::name
  DependentType(const Type *Base, llvm::StringRef N)
      : Type(TypeClass::Dependent), BaseType(Base), Name(N) {}

  const Type *getBaseType() const { return BaseType; }
  llvm::StringRef getName() const { return Name; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Dependent;
  }
};

//===----------------------------------------------------------------------===//
// QualType - Qualified types with CV qualifiers
//===----------------------------------------------------------------------===//

/// QualType - A type with CVR qualifiers.
class QualType {
  const Type *Ty;
  Qualifier Quals;

public:
  QualType() : Ty(nullptr), Quals(Qualifier::None) {}
  QualType(const Type *Ty, Qualifier Q = Qualifier::None)
      : Ty(Ty), Quals(Q) {}

  // Accessors
  const Type *getTypePtr() const { return Ty; }
  const Type *operator->() const { return Ty; }
  bool isNull() const { return Ty == nullptr; }
  explicit operator bool() const { return Ty != nullptr; }

  // Qualifier accessors
  Qualifier getQualifiers() const { return Quals; }
  bool isConstQualified() const {
    return hasQualifier(Quals, Qualifier::Const);
  }
  bool isVolatileQualified() const {
    return hasQualifier(Quals, Qualifier::Volatile);
  }
  bool isRestrictQualified() const {
    return hasQualifier(Quals, Qualifier::Restrict);
  }

  // Qualifier manipulation
  QualType withConst() const {
    return QualType(Ty, Quals | Qualifier::Const);
  }
  QualType withVolatile() const {
    return QualType(Ty, Quals | Qualifier::Volatile);
  }
  QualType withRestrict() const {
    return QualType(Ty, Quals | Qualifier::Restrict);
  }
  QualType withoutConstQualifier() const {
    return QualType(Ty, Quals & static_cast<Qualifier>(~static_cast<unsigned>(Qualifier::Const)));
  }
  QualType withoutVolatileQualifier() const {
    return QualType(Ty, Quals & static_cast<Qualifier>(~static_cast<unsigned>(Qualifier::Volatile)));
  }
  QualType withoutRestrictQualifier() const {
    return QualType(Ty, Quals & static_cast<Qualifier>(~static_cast<unsigned>(Qualifier::Restrict)));
  }

  // Type predicates (forward to underlying type)
  bool isBuiltinType() const { return Ty && Ty->isBuiltinType(); }
  bool isPointerType() const { return Ty && Ty->isPointerType(); }
  bool isReferenceType() const { return Ty && Ty->isReferenceType(); }
  bool isArrayType() const { return Ty && Ty->isArrayType(); }
  bool isFunctionType() const { return Ty && Ty->isFunctionType(); }
  bool isIntegerType() const { return Ty && Ty->isIntegerType(); }
  bool isFloatingType() const { return Ty && Ty->isFloatingType(); }
  bool isVoidType() const { return Ty && Ty->isVoidType(); }
  bool isBooleanType() const { return Ty && Ty->isBooleanType(); }

  // Canonical type
  /// ⚠️ TODO (Phase 4): Complete canonical type normalization
  /// Current implementation delegates to Type::getCanonicalType().
  /// Full implementation needed for semantic analysis to properly handle:
  /// - Typedef and type alias resolution
  /// - Template type substitution
  /// - Type equivalence checking
  QualType getCanonicalType() const {
    return QualType(Ty ? Ty->getCanonicalType() : nullptr, Quals);
  }

  // Comparison
  bool operator==(const QualType &Other) const {
    return Ty == Other.Ty && Quals == Other.Quals;
  }
  bool operator!=(const QualType &Other) const { return !(*this == Other); }

  // Dump
  void dump() const;
  void dump(llvm::raw_ostream &OS) const;
};

//===----------------------------------------------------------------------===//
// Template Argument
//===----------------------------------------------------------------------===//

/// TemplateArgumentKind - Kind of template argument.
enum class TemplateArgumentKind {
  Type,      // type argument: template<typename T> -> T = int
  NonType,   // non-type argument: template<int N> -> N = 42
  Template,  // template template argument: template<template<typename> class T> -> T = vector
};

/// TemplateArgument - Represents a template argument.
/// A template argument can be a type, a non-type (expression), or a template.
class TemplateArgument {
  TemplateArgumentKind Kind;
  bool IsPackExpansion = false;  // ✅ Added: Mark if this is a pack expansion (Ts...)
  union {
    QualType AsType;
    Expr *AsExpr;
    TemplateDecl *AsTemplate;
  };

public:
  /// Construct a type template argument.
  TemplateArgument(QualType T) : Kind(TemplateArgumentKind::Type), AsType(T) {}

  /// Construct a non-type template argument.
  TemplateArgument(Expr *E) : Kind(TemplateArgumentKind::NonType), AsExpr(E) {}

  /// Construct a template template argument.
  TemplateArgument(TemplateDecl *T) : Kind(TemplateArgumentKind::Template), AsTemplate(T) {}

  TemplateArgumentKind getKind() const { return Kind; }

  bool isType() const { return Kind == TemplateArgumentKind::Type; }
  bool isNonType() const { return Kind == TemplateArgumentKind::NonType; }
  bool isTemplate() const { return Kind == TemplateArgumentKind::Template; }

  /// Check if this template argument is a pack expansion (Ts...).
  bool isPackExpansion() const { return IsPackExpansion; }
  
  /// Mark this template argument as a pack expansion.
  void setPackExpansion(bool IsPack = true) { IsPackExpansion = IsPack; }

  QualType getAsType() const {
    assert(isType() && "Template argument is not a type");
    return AsType;
  }

  Expr *getAsExpr() const {
    assert(isNonType() && "Template argument is not a non-type");
    return AsExpr;
  }

  TemplateDecl *getAsTemplate() const {
    assert(isTemplate() && "Template argument is not a template");
    return AsTemplate;
  }

  void dump(llvm::raw_ostream &OS) const;
};

//===----------------------------------------------------------------------===//
// TemplateSpecializationType - Template specialization types
//===----------------------------------------------------------------------===//

class TemplateDecl;

/// TemplateSpecializationType - Represents template specialization types.
/// Example: Vector<int>, std::vector<std::string>
class TemplateSpecializationType : public Type {
  TemplateDecl *Template;
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  llvm::StringRef TemplateName;

public:
  TemplateSpecializationType(llvm::StringRef Name, TemplateDecl *T = nullptr)
      : Type(TypeClass::TemplateSpecialization), Template(T), TemplateName(Name) {}

  TemplateDecl *getTemplateDecl() const { return Template; }
  llvm::StringRef getTemplateName() const { return TemplateName; }
  llvm::ArrayRef<TemplateArgument> getTemplateArgs() const { return TemplateArgs; }
  void addTemplateArg(const TemplateArgument &Arg) { TemplateArgs.push_back(Arg); }
  unsigned getNumTemplateArgs() const { return TemplateArgs.size(); }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::TemplateSpecialization;
  }
};

//===----------------------------------------------------------------------===//
// AutoType - Auto type (C++11)
//===----------------------------------------------------------------------===//

/// AutoType - Represents auto type.
/// Example: auto x = 10; -> x has AutoType that will be deduced
class AutoType : public Type {
  QualType DeducedType;
  bool IsDeduced;

public:
  AutoType() : Type(TypeClass::Auto), IsDeduced(false) {}

  QualType getDeducedType() const { return DeducedType; }
  bool isDeduced() const { return IsDeduced; }
  void setDeducedType(QualType T) {
    DeducedType = T;
    IsDeduced = true;
  }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Auto;
  }
};

//===----------------------------------------------------------------------===//
// DecltypeType - Decltype type (C++11)
//===----------------------------------------------------------------------===//

/// DecltypeType - Represents decltype type.
/// Example: decltype(expr)
class DecltypeType : public Type {
  Expr *Expression;
  QualType UnderlyingType;

public:
  DecltypeType(Expr *E, QualType Underlying = QualType())
      : Type(TypeClass::Decltype), Expression(E), UnderlyingType(Underlying) {}

  Expr *getExpression() const { return Expression; }
  QualType getUnderlyingType() const { return UnderlyingType; }
  void setUnderlyingType(QualType T) { UnderlyingType = T; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::Decltype;
  }
};

//===----------------------------------------------------------------------===//
// Inline Type Predicates
//===----------------------------------------------------------------------===//

inline bool Type::isBuiltinType() const {
  return getTypeClass() == TypeClass::Builtin;
}

inline bool Type::isPointerType() const {
  return getTypeClass() == TypeClass::Pointer;
}

inline bool Type::isReferenceType() const {
  return getTypeClass() == TypeClass::LValueReference ||
         getTypeClass() == TypeClass::RValueReference;
}

inline bool Type::isArrayType() const {
  return getTypeClass() == TypeClass::ConstantArray ||
         getTypeClass() == TypeClass::IncompleteArray ||
         getTypeClass() == TypeClass::VariableArray;
}

inline bool Type::isFunctionType() const {
  return getTypeClass() == TypeClass::Function;
}

inline bool Type::isRecordType() const {
  return getTypeClass() == TypeClass::Record;
}

inline bool Type::isEnumType() const {
  return getTypeClass() == TypeClass::Enum;
}

inline bool Type::isVoidType() const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(this))
    return BT->getKind() == BuiltinKind::Void;
  return false;
}

inline bool Type::isBooleanType() const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(this))
    return BT->getKind() == BuiltinKind::Bool;
  return false;
}

inline bool Type::isIntegerType() const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(this))
    return BT->isInteger();
  // Enum types are also integer types
  return isEnumType();
}

inline bool Type::isFloatingType() const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(this))
    return BT->isFloatingPoint();
  return false;
}

} // namespace blocktype

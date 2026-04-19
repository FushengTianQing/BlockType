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
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "blocktype/Basic/SourceLocation.h"

namespace blocktype {

class Expr;
class RecordDecl;
class EnumDecl;
class TemplateDecl;
class ValueDecl;

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
  virtual void dump() const;
  virtual void dump(llvm::raw_ostream &OS) const;

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
  bool MethodIsConst;      ///< Method qualifier: const (for member functions)
  bool MethodIsVolatile;   ///< Method qualifier: volatile (for member functions)

public:
  FunctionType(const Type *ReturnType, llvm::ArrayRef<const Type *> ParamTypes,
               bool IsVariadic = false, bool IsConst = false,
               bool IsVolatile = false)
      : Type(TypeClass::Function), ReturnType(ReturnType),
        ParamTypes(ParamTypes.begin(), ParamTypes.end()), IsVariadic(IsVariadic),
        MethodIsConst(IsConst), MethodIsVolatile(IsVolatile) {}

  const Type *getReturnType() const { return ReturnType; }
  llvm::ArrayRef<const Type *> getParamTypes() const { return ParamTypes; }
  bool isVariadic() const { return IsVariadic; }
  bool isConst() const { return MethodIsConst; }
  bool isVolatile() const { return MethodIsVolatile; }

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
// MetaInfoType - P7.2.1 Reflection info type (std::meta::info)
//===----------------------------------------------------------------------===//

/// MetaInfoType - The type of a reflexpr expression result.
///
/// Represents the opaque reflection info type (std::meta::info in C++26).
/// This type wraps a pointer to the reflected AST node.
///
/// **Clang reference**: Similar to clang::QualType representing std::meta::info
/// in the reflection TS implementation.
class MetaInfoType : public Type {
  /// The reflected AST node (Type, Decl, or Expr)
  void *Reflectee;

public:
  /// Kind of entity being reflected
  enum ReflecteeKind {
    RK_Type,   ///< Reflecting a type (reflexpr(type-id))
    RK_Decl,   ///< Reflecting a declaration
    RK_Expr    ///< Reflecting an expression (reflexpr(expr))
  };

private:
  ReflecteeKind RefKind;

public:
  MetaInfoType(void *R, ReflecteeKind K)
      : Type(TypeClass::MetaInfo), Reflectee(R), RefKind(K) {}

  void *getReflectee() const { return Reflectee; }
  ReflecteeKind getReflecteeKind() const { return RefKind; }

  /// Check if this reflects a type
  bool reflectsType() const { return RefKind == RK_Type; }
  /// Check if this reflects a declaration
  bool reflectsDecl() const { return RefKind == RK_Decl; }
  /// Check if this reflects an expression
  bool reflectsExpr() const { return RefKind == RK_Expr; }

  void dump(llvm::raw_ostream &OS) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == TypeClass::MetaInfo;
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

  /// getAsString - 返回类型的字符串表示（如 "int"、"const MyClass*"）。
  /// 复用已有的 dump() 基础设施生成。
  std::string getAsString() const;
};

//===----------------------------------------------------------------------===//
// Template Argument
//===----------------------------------------------------------------------===//

/// TemplateArgumentKind - Kind of template argument.
///
/// Covers all forms of template arguments as specified by the C++ standard.
/// Each kind corresponds to a specific grammar production in template-argument:
///
///   template-argument ::= constant-expression        (Integral/Expression)
///                       | type-id                     (Type)
///                       | id-expression               (Declaration)
///                       | template-name               (Template/TemplateExpansion)
///                       | nullptr                     (NullPtr)
///                       | template-argument ...       (Pack)
enum class TemplateArgumentKind {
  Null,              // empty/unresolved argument (used in error recovery)
  Type,              // type argument: template<typename T> -> T = int
  Declaration,       // non-type argument resolved to a declaration
                     // (Phase 4 Sema: when a non-type template parameter
                     // is resolved to a specific declaration)
  NullPtr,           // nullptr as a template argument
  Integral,          // compile-time integer constant
                     // (Phase 4 Sema: replaces Expr* with evaluated value.
                     //  Do NOT use Expr* to represent integral constants.)
  Template,          // template template argument: template<template<typename> class T>
  TemplateExpansion, // template template argument with pack expansion
                     // (e.g., template<template<typename...> class T> -> T = vector)
  Expression,        // non-type argument as an unevaluated expression
                     // (Parser/Phase 3 produces this; Phase 4 Sema evaluates
                     //  it to Integral/Declaration/NullPtr as appropriate)
  Pack,              // a pack of template arguments (for variadic templates)
                     // Contains an array of TemplateArguments.
                     // Do NOT use IsPackExpansion on individual args to
                     // represent packs; use this kind instead.
};

/// TemplateArgument - Represents a template argument.
///
/// A template argument can be:
/// - A type (Type)
/// - A compile-time integer value (Integral)
/// - An unevaluated expression that will resolve to a value (Expression)
/// - A template name (Template)
/// - A template name with pack expansion (TemplateExpansion)
/// - A declaration (Declaration)
/// - nullptr (NullPtr)
/// - A pack of arguments (Pack)
/// - Empty/unresolved (Null)
///
/// Lifecycle:
/// - Phase 3 (Parser) produces: Type, Expression, Template, Pack
/// - Phase 4 (Sema) evaluates Expression into: Integral, Declaration, NullPtr
/// - Null is used for error recovery and placeholder arguments
class TemplateArgument {
  TemplateArgumentKind Kind;
  bool IsPackExpansion = false; // true if this arg is followed by "..."

  /// Storage for the template argument value.
  /// The active member is determined by Kind.
  /// Pack data is stored separately since it has different lifetime semantics
  /// (points to external storage).
  union Storage {
    QualType AsType;                // Kind == Type
    Expr *AsExpr;                   // Kind == Expression, Null, NullPtr
    TemplateDecl *AsTemplate;       // Kind == Template, TemplateExpansion
    ValueDecl *AsDecl;              // Kind == Declaration
    llvm::APSInt AsIntegral;        // Kind == Integral

    Storage() : AsExpr(nullptr) {}
    ~Storage() {} // Managed by TemplateArgument's dtor
  } Data;

  // Pack storage (separate from union to avoid anonymous struct issues)
  const TemplateArgument *PackArgs = nullptr;  // Kind == Pack
  unsigned PackNumArgs = 0;                     // Kind == Pack

public:
  /// Construct a null template argument (for error recovery).
  TemplateArgument() : Kind(TemplateArgumentKind::Null), Data() {}

  /// Construct a type template argument.
  TemplateArgument(QualType T) : Kind(TemplateArgumentKind::Type) { Data.AsType = T; }

  /// Construct a non-type template argument (expression).
  /// Phase 3 Parser produces these; Phase 4 Sema evaluates them.
  TemplateArgument(Expr *E) : Kind(TemplateArgumentKind::Expression) { Data.AsExpr = E; }

  /// Construct a template template argument.
  TemplateArgument(TemplateDecl *T)
      : Kind(TemplateArgumentKind::Template) { Data.AsTemplate = T; }

  /// Construct an integral template argument (Phase 4 Sema).
  /// Used when a non-type template argument is evaluated to a constant.
  TemplateArgument(const llvm::APSInt &Val)
      : Kind(TemplateArgumentKind::Integral) { ::new (&Data.AsIntegral) llvm::APSInt(Val); }

  /// Construct a declaration template argument (Phase 4 Sema).
  /// Used when a non-type template argument resolves to a declaration.
  TemplateArgument(ValueDecl *D)
      : Kind(TemplateArgumentKind::Declaration) { Data.AsDecl = D; }

  /// Construct a pack template argument from an array of arguments.
  /// The pointed-to array must outlive this TemplateArgument.
  TemplateArgument(llvm::ArrayRef<TemplateArgument> Args)
      : Kind(TemplateArgumentKind::Pack), IsPackExpansion(false), Data() {
    PackArgs = Args.data();
    PackNumArgs = Args.size();
  }

  /// Construct a TemplateExpansion template argument.
  struct TemplateExpansionTag {};
  TemplateArgument(TemplateDecl *T, TemplateExpansionTag)
      : Kind(TemplateArgumentKind::TemplateExpansion) { Data.AsTemplate = T; }

  /// Construct a NullPtr template argument.
  struct NullPtrTag {};
  TemplateArgument(NullPtrTag)
      : Kind(TemplateArgumentKind::NullPtr), Data() {}

  // --- Copy/Move support ---
  // The union contains llvm::APSInt which has a non-trivial copy/move,
  // so we must define these explicitly.
  TemplateArgument(const TemplateArgument &Other);
  TemplateArgument(TemplateArgument &&Other);
  TemplateArgument &operator=(const TemplateArgument &Other);
  TemplateArgument &operator=(TemplateArgument &&Other);
  ~TemplateArgument();

  // --- Kind accessors ---

  TemplateArgumentKind getKind() const { return Kind; }

  bool isNull() const { return Kind == TemplateArgumentKind::Null; }
  bool isType() const { return Kind == TemplateArgumentKind::Type; }
  bool isDeclaration() const { return Kind == TemplateArgumentKind::Declaration; }
  bool isNullPtr() const { return Kind == TemplateArgumentKind::NullPtr; }
  bool isIntegral() const { return Kind == TemplateArgumentKind::Integral; }
  bool isTemplate() const { return Kind == TemplateArgumentKind::Template; }
  bool isTemplateExpansion() const {
    return Kind == TemplateArgumentKind::TemplateExpansion;
  }
  bool isExpression() const { return Kind == TemplateArgumentKind::Expression; }
  bool isPack() const { return Kind == TemplateArgumentKind::Pack; }

  // Legacy alias: NonType in old code maps to Expression in new code
  bool isNonType() const { return Kind == TemplateArgumentKind::Expression; }

  /// Check if this template argument is a pack expansion (e.g., Ts...).
  bool isPackExpansion() const { return IsPackExpansion; }

  /// Mark this template argument as a pack expansion.
  void setPackExpansion(bool IsPack = true) { IsPackExpansion = IsPack; }

  // --- Value accessors ---

  QualType getAsType() const {
    assert(isType() && "Template argument is not a type");
    return Data.AsType;
  }

  Expr *getAsExpr() const {
    assert(isExpression() && "Template argument is not an expression");
    return Data.AsExpr;
  }

  /// Legacy alias for getAsExpr.
  Expr *getAsNonType() const { return getAsExpr(); }

  TemplateDecl *getAsTemplate() const {
    assert((isTemplate() || isTemplateExpansion()) &&
           "Template argument is not a template");
    return Data.AsTemplate;
  }

  TemplateDecl *getAsTemplateOrTemplateExpansion() const {
    assert((isTemplate() || isTemplateExpansion()) &&
           "Template argument is not a template");
    return Data.AsTemplate;
  }

  ValueDecl *getAsDecl() const {
    assert(isDeclaration() && "Template argument is not a declaration");
    return Data.AsDecl;
  }

  const llvm::APSInt &getAsIntegral() const {
    assert(isIntegral() && "Template argument is not integral");
    return Data.AsIntegral;
  }

  llvm::ArrayRef<TemplateArgument> getAsPack() const {
    assert(isPack() && "Template argument is not a pack");
    return llvm::ArrayRef<TemplateArgument>(PackArgs, PackNumArgs);
  }

  unsigned getNumPackArguments() const {
    assert(isPack() && "Template argument is not a pack");
    return PackNumArgs;
  }

  void dump(llvm::raw_ostream &OS) const;
  void dump() const;
};

//===----------------------------------------------------------------------===//
// TemplateArgumentLoc - Template argument with source location info
//===----------------------------------------------------------------------===//

/// TemplateArgumentLoc - A template argument together with its source
/// location information. Used by Sema for diagnostics and by CodeGen
/// for emission.
///
/// Phase 4 Sema: When evaluating template arguments, use this class
/// to preserve source locations for error reporting. Do not create
/// ad-hoc structs to carry location info alongside TemplateArgument.
class TemplateArgumentLoc {
  TemplateArgument Argument;
  SourceLocation Location;

public:
  TemplateArgumentLoc() = default;
  TemplateArgumentLoc(const TemplateArgument &Arg, SourceLocation Loc)
      : Argument(Arg), Location(Loc) {}

  const TemplateArgument &getArgument() const { return Argument; }
  TemplateArgument &getArgument() { return Argument; }

  SourceLocation getLocation() const { return Location; }
  void setLocation(SourceLocation Loc) { Location = Loc; }

  /// Convenience: forward common queries to the argument.
  TemplateArgumentKind getKind() const { return Argument.getKind(); }
  bool isType() const { return Argument.isType(); }
  bool isExpression() const { return Argument.isExpression(); }
  bool isIntegral() const { return Argument.isIntegral(); }
  bool isPack() const { return Argument.isPack(); }

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

//===--- ReflectionTypes.h - Reflection Type System -------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the reflection type system for BlockType's C++26
// static reflection support.
//
// Key components:
//   - meta::InfoType: The opaque reflection info type (std::meta::info)
//   - meta::MemberInfo: Describes a reflected class member
//   - meta::TypeInfo: Compile-time type introspection API
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Decl.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {
namespace meta {

// Re-export AccessSpecifier from Decl.h with an additional sentinel value.
// AS_none indicates "not a class member".
using ::blocktype::AccessSpecifier;
constexpr AccessSpecifier AS_none = static_cast<AccessSpecifier>(-1);

//===----------------------------------------------------------------------===//
// MemberInfo - Reflected member descriptor
//===----------------------------------------------------------------------===//

/// MemberInfo - Describes a single class member (field or method).
///
/// This is the compile-time representation of a reflected member,
/// similar to std::meta::member_info in C++26 reflection proposal.
struct MemberInfo {
  llvm::StringRef Name;       ///< Member name
  QualType Type;               ///< Member type
  AccessSpecifier Access;      ///< Access control level
  bool IsStatic;               ///< Whether this is a static member
  bool IsFunction;             ///< Whether this is a function/method

  MemberInfo(llvm::StringRef N, QualType T, AccessSpecifier A = AS_none,
             bool Static = false, bool Func = false)
      : Name(N), Type(T), Access(A), IsStatic(Static), IsFunction(Func) {}
};

//===----------------------------------------------------------------------===//
// TypeInfo - Compile-time type introspection API
//===----------------------------------------------------------------------===//

/// TypeInfo - Provides compile-time type introspection capabilities.
///
/// Given a CXXRecordDecl, this class can enumerate its members,
/// bases, and properties — serving as the foundation for the
/// meta::type_info reflection API.
///
/// **Clang reference**:
///   - clang::ASTContext::getTypeInfo() for type properties
///   - clang::CXXRecordDecl::bases() for base class traversal
///   - clang::CXXRecordDecl::fields() / methods() for members
class TypeInfo {
  const CXXRecordDecl *Record;

public:
  explicit TypeInfo(const CXXRecordDecl *RD) : Record(RD) {}

  /// Gets all members (fields + methods)
  llvm::SmallVector<MemberInfo, 16> getMembers() const;

  /// Gets all fields (non-static data members)
  llvm::SmallVector<MemberInfo, 16> getFields() const;

  /// Gets all methods (member functions)
  llvm::SmallVector<MemberInfo, 16> getMethods() const;

  /// Gets the list of direct base classes
  llvm::SmallVector<const CXXRecordDecl *, 4> getBases() const;

  /// Gets the type name
  llvm::StringRef getName() const;

  /// Checks if a member with the given name exists
  bool hasMember(llvm::StringRef Name) const;

  /// Gets the underlying record declaration
  const CXXRecordDecl *getRecord() const { return Record; }

  /// Checks if this TypeInfo is valid (has a record)
  bool isValid() const { return Record != nullptr; }
};

//===----------------------------------------------------------------------===//
// InfoType - Opaque reflection info type (std::meta::info)
//===----------------------------------------------------------------------===//

/// InfoType - The compile-time reflection information type.
///
/// This is the BlockType representation of std::meta::info from
/// the C++26 reflection proposal (P2996). It acts as an opaque
/// handle to the reflected entity.
///
/// In Clang's reflection TS implementation, this corresponds to
/// the result type of __reflect() or reflexpr expressions.
///
/// Note: The actual type in the type system is MetaInfoType (in Type.h).
/// This class provides the metaprogramming interface for working with
/// reflected information at the compiler level.
class InfoType {
  void *Handle;  ///< Opaque handle to the reflected entity

public:
  enum class EntityKind {
    EK_Type,   ///< A type was reflected
    EK_Decl,   ///< A declaration was reflected
    EK_Expr,   ///< An expression was reflected
    EK_Null    ///< No entity (null meta::info)
  };

private:
  EntityKind Kind;

public:
  /// Construct a null info
  InfoType() : Handle(nullptr), Kind(EntityKind::EK_Null) {}

  /// Construct with an entity handle
  InfoType(void *H, EntityKind K) : Handle(H), Kind(K) {}

  /// Get the opaque handle
  void *getHandle() const { return Handle; }

  /// Get the entity kind
  EntityKind getEntityKind() const { return Kind; }

  /// Check if this is a null info
  bool isNull() const { return Kind == EntityKind::EK_Null; }

  /// Check if this reflects a type
  bool isType() const { return Kind == EntityKind::EK_Type; }

  /// Check if this reflects a declaration
  bool isDecl() const { return Kind == EntityKind::EK_Decl; }

  /// Check if this reflects an expression
  bool isExpr() const { return Kind == EntityKind::EK_Expr; }
};

//===----------------------------------------------------------------------===//
// Reflection utility functions
//===----------------------------------------------------------------------===//

/// Gets the access specifier for a Decl.
AccessSpecifier getAccessSpecifier(const Decl *D);

/// Gets a human-readable string for an AccessSpecifier.
llvm::StringRef getAccessSpecifierName(AccessSpecifier AS);

} // namespace meta
} // namespace blocktype

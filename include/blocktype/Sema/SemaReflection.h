//===--- SemaReflection.h - Reflection Semantic Analysis ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SemaReflection class for static reflection
// semantic analysis (C++26, P2996).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ReflectionTypes.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

/// SemaReflection - Semantic analysis for C++26 static reflection.
///
/// Provides reflection-specific semantic checks and type introspection
/// capabilities. This class handles:
///   - Validation of reflexpr operands
///   - Type introspection queries (getMembers, getFields, getMethods, etc.)
///   - Metadata generation for reflected types
///   - Built-in reflection function analysis
///
/// **Clang reference**: Similar to Clang's reflection semantic analysis in
/// clang/lib/Sema/SemaReflection.cpp.
class SemaReflection {
  Sema &S;

public:
  explicit SemaReflection(Sema &SemaRef) : S(SemaRef) {}

  //===------------------------------------------------------------------===//
  // reflexpr validation
  //===------------------------------------------------------------------===//

  /// Validate a reflexpr operand (type form).
  /// Checks that the type is valid for reflection.
  ///
  /// @param T   The type to reflect
  /// @param Loc Source location of the reflexpr keyword
  /// @return true if valid
  bool ValidateReflexprType(QualType T, SourceLocation Loc);

  /// Validate a reflexpr operand (expression form).
  /// Checks that the expression is valid for reflection.
  ///
  /// @param E   The expression to reflect
  /// @param Loc Source location of the reflexpr keyword
  /// @return true if valid
  bool ValidateReflexprExpr(Expr *E, SourceLocation Loc);

  //===------------------------------------------------------------------===//
  // Type introspection API
  //===------------------------------------------------------------------===//

  /// Gets type introspection info for a given type.
  /// Returns a TypeInfo object if T is a class/struct/union type,
  /// or an invalid TypeInfo otherwise.
  static meta::TypeInfo getTypeInfo(QualType T);

  /// Gets type introspection info for a given CXXRecordDecl.
  static meta::TypeInfo getTypeInfo(const CXXRecordDecl *RD);

  //===------------------------------------------------------------------===//
  // Member traversal API
  //===------------------------------------------------------------------===//

  /// Traverse all members of a record type, calling Callback for each.
  ///
  /// **Clang reference**:
  ///   - clang/lib/AST/RecordLayout.cpp for field traversal
  ///   - clang/lib/AST/ASTDumper.cpp for AST node traversal
  static void forEachMember(const CXXRecordDecl *RD,
                            llvm::function_ref<void(const meta::MemberInfo &)> Callback);

  //===------------------------------------------------------------------===//
  // Metadata generation helpers
  //===------------------------------------------------------------------===//

  /// Generate a unique metadata name for a reflected type.
  static std::string getMetadataName(QualType T);

  /// Generate a unique metadata name for a reflected declaration.
  static std::string getMetadataName(const Decl *D);

  //===------------------------------------------------------------------===//
  // Built-in reflection functions
  //===------------------------------------------------------------------===//

  /// Semantic analysis for __reflect_type(expr) built-in.
  /// Returns a ReflexprExpr that reflects the type of the expression.
  ExprResult ActOnReflectType(SourceLocation Loc, Expr *E);

  /// Semantic analysis for __reflect_members(type) built-in.
  /// Returns a reflection of all members of the type.
  ExprResult ActOnReflectMembers(SourceLocation Loc, QualType T);
};

} // namespace blocktype

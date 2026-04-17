//===--- TypeCheck.h - Type Checking ------------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TypeCheck class for type compatibility checking.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceLocation.h"

namespace blocktype {

/// TypeCheck - Performs type checking operations.
///
/// This class provides methods for checking type compatibility,
/// initialization, assignment, and other type-related operations.
class TypeCheck {
  ASTContext &Context;
  DiagnosticsEngine &Diags;

public:
  explicit TypeCheck(ASTContext &C, DiagnosticsEngine &D)
      : Context(C), Diags(D) {}

  //===------------------------------------------------------------------===//
  // Assignment and initialization
  //===------------------------------------------------------------------===//

  /// Check assignment compatibility: LHS = RHS.
  bool CheckAssignment(QualType LHS, QualType RHS, SourceLocation Loc);

  /// Check initialization compatibility: T x = init.
  bool CheckInitialization(QualType Dest, Expr *Init, SourceLocation Loc);

  /// Check direct initialization: T x(args...).
  bool CheckDirectInitialization(QualType Dest,
                                  llvm::ArrayRef<Expr *> Args,
                                  SourceLocation Loc);

  /// Check list initialization: T x = {args...}.
  bool CheckListInitialization(QualType Dest,
                                llvm::ArrayRef<Expr *> Args,
                                SourceLocation Loc);

  /// Check reference binding: T& ref = expr.
  bool CheckReferenceBinding(QualType RefType, Expr *Init,
                              SourceLocation Loc);

  //===------------------------------------------------------------------===//
  // Function call checking
  //===------------------------------------------------------------------===//

  /// Check a function call's arguments against the function declaration.
  bool CheckCall(FunctionDecl *F, llvm::ArrayRef<Expr *> Args,
                 SourceLocation CallLoc);

  /// Check that a return statement's expression matches the function type.
  bool CheckReturn(Expr *RetVal, QualType FuncRetType,
                   SourceLocation ReturnLoc);

  /// Check a condition expression (if, while, for, etc.).
  bool CheckCondition(Expr *Cond, SourceLocation Loc);

  /// Check a case expression (must be integral constant).
  bool CheckCaseExpression(Expr *Val, SourceLocation Loc);

  //===------------------------------------------------------------------===//
  // Type compatibility
  //===------------------------------------------------------------------===//

  /// Check if From is convertible to To.
  bool isTypeCompatible(QualType From, QualType To) const;

  /// Check if two types are the same (ignoring CVR qualifiers).
  bool isSameType(QualType T1, QualType T2) const;

  /// Get the common type for binary operations (usual arithmetic conversions).
  QualType getCommonType(QualType T1, QualType T2) const;

  /// Get the result type of a binary operator.
  /// Takes the operator kind into account: comparison/logical → bool,
  /// assignment → LHS, comma → RHS, arithmetic/bitwise/shift → common type.
  QualType getBinaryOperatorResultType(BinaryOpKind Op, QualType LHS,
                                        QualType RHS) const;

  /// Get the result type of a unary operator.
  /// Takes the operator kind into account: ! → bool, ~ → promoted operand,
  /// +/- → promoted operand, * → dereferenced, & → pointer.
  QualType getUnaryOperatorResultType(UnaryOpKind Op, QualType Operand) const;

  /// Check if a type can be compared (operator<, >, ==, etc.).
  bool isComparable(QualType T) const;

  /// Check if a type can be called (function type, class with operator()).
  bool isCallable(QualType T) const;

private:
  //===------------------------------------------------------------------===//
  // Private helpers for arithmetic conversions
  //===------------------------------------------------------------------===//

  /// Perform integral promotion on a type (C++ [conv.prom]).
  /// Returns int for types smaller than int; enum → int.
  QualType performIntegralPromotion(QualType T) const;

  /// Get integer rank for a builtin type (-1 if not integer).
  int getIntegerRankForType(const BuiltinType *BT) const;

  /// Check if a type is long double or float128.
  bool isLongDoubleType(const Type *T) const;

  /// Check if a type is double.
  bool isDoubleType(const Type *T) const;

  /// Check if a type is float.
  bool isFloatType(const Type *T) const;

  /// Get the common integer type via usual arithmetic conversions.
  QualType getIntegerCommonType(QualType T1, QualType T2) const;
};

} // namespace blocktype

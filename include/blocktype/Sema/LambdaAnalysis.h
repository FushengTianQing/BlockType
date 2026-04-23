//===--- LambdaAnalysis.h - Lambda Sema Analysis -------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LambdaAnalysis class for lambda expression
// semantic analysis: capture checking, init capture deduction,
// generic lambda support, and function pointer conversion.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Basic/SourceLocation.h"

namespace blocktype {

class Sema;

/// LambdaAnalysis - Performs lambda expression semantic analysis.
///
/// This class provides methods for:
/// - Checking capture variable ODR-use rules
/// - Deducing types for init captures (x = expr)
/// - Checking and supporting generic lambdas
/// - Lambda to function pointer conversion
class LambdaAnalysis {
  Sema &SemaRef;
  ASTContext &Context;

public:
  explicit LambdaAnalysis(Sema &S);

  //===------------------------------------------------------------------===//
  // Capture variable ODR-use checking
  //===------------------------------------------------------------------===//

  /// Check ODR-use rules for all captures in a lambda expression.
  /// Per C++ [expr.prim.lambda.capture]:
  ///   - Reference captures must refer to variables with automatic storage
  ///   - Copy captures must be odr-usable
  /// Returns true if all captures are valid.
  bool CheckCaptureODRUse(LambdaExpr *Lambda);

  //===------------------------------------------------------------------===//
  // Init capture type deduction
  //===------------------------------------------------------------------===//

  /// Deduce the type of an init capture (x = expr).
  /// Per C++ [expr.prim.lambda.capture]:
  ///   The type is deduced using auto deduction rules from the
  ///   initializer expression.
  /// Returns the deduced type, or null on failure.
  QualType DeduceInitCaptureType(const LambdaCapture &Capture);

  //===------------------------------------------------------------------===//
  // Generic lambda support
  //===------------------------------------------------------------------===//

  /// Check a generic lambda for validity.
  /// A generic lambda has template parameters and at least one
  /// auto parameter.
  bool CheckGenericLambda(LambdaExpr *Lambda);

  /// Deduce the call operator type for a generic lambda given
  /// specific call arguments.
  /// This performs template argument deduction for the auto parameters.
  QualType DeduceGenericLambdaCallOperatorType(
      LambdaExpr *Lambda, llvm::ArrayRef<Expr *> CallArgs);

  //===------------------------------------------------------------------===//
  // Lambda to function pointer conversion
  //===------------------------------------------------------------------===//

  /// Get the function pointer type that a non-capturing, non-generic
  /// lambda can convert to.
  /// Per C++ [expr.prim.lambda.closure]:
  ///   A closure object with no captures can be converted to a
  ///   function pointer with the same parameter and return types.
  /// Returns null type if conversion is not possible.
  QualType GetLambdaConversionFunctionType(LambdaExpr *Lambda) const;

  /// Check if a lambda can be converted to a function pointer.
  bool CanConvertToFunctionPointer(LambdaExpr *Lambda) const;

private:
  /// Check if a variable declaration has automatic storage duration
  /// (i.e., is a local variable, not static or global).
  bool isLocalVariable(VarDecl *VD) const;
};

} // namespace blocktype

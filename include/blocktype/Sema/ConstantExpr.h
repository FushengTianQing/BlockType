//===--- ConstantExpr.h - Constant Expression Evaluation ----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines EvalResult and ConstantExprEvaluator for evaluating
// constant expressions.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/Expr.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Type.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APFloat.h"

#include <optional>

namespace blocktype {

/// EvalResult - Result of constant expression evaluation.
class EvalResult {
public:
  enum ResultKind {
    /// Successfully evaluated.
    Success,

    /// Expression is not a constant expression.
    NotConstantExpression,

    /// Expression has side effects.
    HasSideEffects,

    /// Expression depends on a template parameter.
    DependsOnTemplateParameter,

    /// Evaluation failed (overflow, division by zero, etc.).
    EvaluationFailed,
  };

private:
  ResultKind Kind = NotConstantExpression;

  /// Integral result (for integer/boolean/enum types).
  llvm::APSInt IntVal;

  /// Float result (for floating-point types).
  llvm::APFloat FloatVal{llvm::APFloat::IEEEdouble()};

  /// Whether the result is integral or floating.
  bool IsIntegral = true;

  /// Diagnostic message (on failure).
  llvm::StringRef DiagMessage;

public:
  EvalResult() = default;

  static EvalResult getSuccess(llvm::APSInt Val) {
    EvalResult R;
    R.Kind = Success;
    R.IntVal = Val;
    R.IsIntegral = true;
    return R;
  }

  static EvalResult getSuccess(llvm::APFloat Val) {
    EvalResult R;
    R.Kind = Success;
    R.FloatVal = Val;
    R.IsIntegral = false;
    return R;
  }

  static EvalResult getFailure(ResultKind K, llvm::StringRef Msg = "") {
    EvalResult R;
    R.Kind = K;
    R.DiagMessage = Msg;
    return R;
  }

  ResultKind getKind() const { return Kind; }
  bool isSuccess() const { return Kind == Success; }
  bool isIntegral() const { return IsIntegral; }

  const llvm::APSInt &getInt() const { return IntVal; }
  const llvm::APFloat &getFloat() const { return FloatVal; }

  llvm::StringRef getDiagMessage() const { return DiagMessage; }
};

/// ConstantExprEvaluator - Evaluates constant expressions.
///
/// Used for:
/// - Array bounds: int arr[10];
/// - Template arguments: template<int N>
/// - Case labels: case 1:
/// - Enum values: enum { X = 10 };
/// - constexpr variables
/// - static_assert conditions
class ConstantExprEvaluator {
  ASTContext &Context;

public:
  explicit ConstantExprEvaluator(ASTContext &C) : Context(C) {}

  /// Evaluate an expression as a constant expression.
  EvalResult Evaluate(Expr *E);

  /// Evaluate as a boolean constant.
  std::optional<bool> EvaluateAsBooleanCondition(Expr *E);

  /// Evaluate as an integer constant.
  std::optional<llvm::APSInt> EvaluateAsInt(Expr *E);

  /// Evaluate as a floating-point constant.
  std::optional<llvm::APFloat> EvaluateAsFloat(Expr *E);

  /// Check if an expression is a constant expression (without evaluating).
  bool isConstantExpr(Expr *E);

  /// Evaluate a constexpr function call.
  EvalResult EvaluateCall(FunctionDecl *F, llvm::ArrayRef<Expr *> Args);

private:
  // Recursive evaluation dispatch
  EvalResult EvaluateExpr(Expr *E);
  EvalResult EvaluateIntegerLiteral(IntegerLiteral *E);
  EvalResult EvaluateFloatingLiteral(FloatingLiteral *E);
  EvalResult EvaluateBooleanLiteral(CXXBoolLiteral *E);
  EvalResult EvaluateCharacterLiteral(CharacterLiteral *E);
  EvalResult EvaluateBinaryOperator(BinaryOperator *E);
  EvalResult EvaluateUnaryOperator(UnaryOperator *E);
  EvalResult EvaluateConditionalOperator(ConditionalOperator *E);
  EvalResult EvaluateDeclRefExpr(DeclRefExpr *E);
  EvalResult EvaluateCastExpr(CastExpr *E);
  EvalResult EvaluateCallExpr(CallExpr *E);
};

} // namespace blocktype

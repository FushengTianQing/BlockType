//===--- ConstantExpr.cpp - Constant Expression Evaluation ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements EvalResult and ConstantExprEvaluator for evaluating
// constant expressions at compile time.
//
// Task 4.5.4 — 常量表达式求值
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/ConstantExpr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"

#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// ConstantExprEvaluator — public API
//===----------------------------------------------------------------------===//

EvalResult ConstantExprEvaluator::Evaluate(Expr *E) {
  if (!E)
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "null expression");
  return EvaluateExpr(E);
}

std::optional<bool> ConstantExprEvaluator::EvaluateAsBooleanCondition(Expr *E) {
  EvalResult Result = Evaluate(E);
  if (!Result.isSuccess())
    return std::nullopt;

  if (Result.isIntegral()) {
    return Result.getInt().getBoolValue();
  }
  return std::nullopt;
}

std::optional<llvm::APSInt>
ConstantExprEvaluator::EvaluateAsInt(Expr *E) {
  EvalResult Result = Evaluate(E);
  if (!Result.isSuccess() || !Result.isIntegral())
    return std::nullopt;
  return Result.getInt();
}

std::optional<llvm::APFloat>
ConstantExprEvaluator::EvaluateAsFloat(Expr *E) {
  EvalResult Result = Evaluate(E);
  if (!Result.isSuccess() || Result.isIntegral())
    return std::nullopt;
  return Result.getFloat();
}

bool ConstantExprEvaluator::isConstantExpr(Expr *E) {
  if (!E)
    return false;

  // Literal expressions are always constant
  if (llvm::isa<IntegerLiteral>(E) || llvm::isa<FloatingLiteral>(E) ||
      llvm::isa<CharacterLiteral>(E) || llvm::isa<CXXBoolLiteral>(E) ||
      llvm::isa<CXXNullPtrLiteral>(E) || llvm::isa<StringLiteral>(E))
    return true;

  // Binary operators: both operands must be constant
  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E)) {
    return isConstantExpr(BO->getLHS()) && isConstantExpr(BO->getRHS());
  }

  // Unary operators: operand must be constant
  if (auto *UO = llvm::dyn_cast<UnaryOperator>(E)) {
    return isConstantExpr(UO->getSubExpr());
  }

  // Conditional operator
  if (auto *CO = llvm::dyn_cast<ConditionalOperator>(E)) {
    return isConstantExpr(CO->getCond()) &&
           isConstantExpr(CO->getTrueExpr()) &&
           isConstantExpr(CO->getFalseExpr());
  }

  // Cast expressions
  if (llvm::isa<CastExpr>(E)) {
    auto *CE = llvm::cast<CastExpr>(E);
    return isConstantExpr(CE->getSubExpr());
  }

  // Declaration references: constexpr/const integral variables
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(E)) {
    if (auto *VD = llvm::dyn_cast<VarDecl>(DRE->getDecl())) {
      // constexpr variables are always constant expressions
      if (VD->isConstexpr()) {
        Expr *Init = VD->getInit();
        return Init != nullptr && isConstantExpr(Init);
      }
      // const integral types initialized with constant expression
      if (VD->getType().isConstQualified() && VD->getType()->isIntegerType()) {
        return VD->getInit() != nullptr && isConstantExpr(VD->getInit());
      }
    }
    // Enum constants are always constant
    if (llvm::isa<EnumConstantDecl>(DRE->getDecl()))
      return true;
    return false;
  }

  // Other expressions are not constant by default
  return false;
}

EvalResult ConstantExprEvaluator::EvaluateCall(FunctionDecl *F,
                                               llvm::ArrayRef<Expr *> Args) {
  if (!F) {
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "null function in constexpr call");
  }

  if (!F->isConstexpr()) {
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "non-constexpr function call");
  }

  // Evaluate the constexpr function body by inlining it.
  // Per C++ [expr.const]: a constexpr function call is evaluated by
  // substituting the function parameters with the corresponding arguments
  // and evaluating the function body.
  Stmt *Body = F->getBody();
  if (!Body) {
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "constexpr function has no body");
  }

  // For simple return-statement bodies, evaluate the return expression.
  // We look for a ReturnStmt as the sole statement in the function body.
  // A more complete implementation would handle compound statements with
  // variable declarations, if/else, loops, etc.
  auto *RS = [&]() -> ReturnStmt * {
    // Check if Body is a CompoundStmt with a single ReturnStmt
    if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
      auto Stmts = CS->getBody();
      if (Stmts.size() == 1) {
        return llvm::dyn_cast<ReturnStmt>(Stmts[0]);
      }
    }
    // Body itself might be a ReturnStmt (unusual but possible)
    return llvm::dyn_cast<ReturnStmt>(Body);
  }();

  if (RS && RS->getRetValue()) {
    // TODO: For a full implementation, we would need to:
    // 1. Create a scope with parameter-to-argument bindings
    // 2. Substitute parameter references in the body with argument values
    // 3. Evaluate the substituted body
    // For now, evaluate the return expression directly. This works for
    // trivial constexpr functions that don't reference parameters.
    return EvaluateExpr(RS->getRetValue());
  }

  // void constexpr functions
  if (RS && !RS->getRetValue()) {
    // void return
    return EvalResult::getSuccess(llvm::APSInt(llvm::APInt(32, 0)));
  }

  return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                "constexpr function body is not a simple "
                                "return statement");
}

//===----------------------------------------------------------------------===//
// ConstantExprEvaluator — private dispatch
//===----------------------------------------------------------------------===//

EvalResult ConstantExprEvaluator::EvaluateExpr(Expr *E) {
  if (!E)
    return EvalResult::getFailure(EvalResult::NotConstantExpression);

  // Dispatch based on expression type
  if (auto *IL = llvm::dyn_cast<IntegerLiteral>(E))
    return EvaluateIntegerLiteral(IL);
  if (auto *FL = llvm::dyn_cast<FloatingLiteral>(E))
    return EvaluateFloatingLiteral(FL);
  if (auto *BL = llvm::dyn_cast<CXXBoolLiteral>(E))
    return EvaluateBooleanLiteral(BL);
  if (auto *CL = llvm::dyn_cast<CharacterLiteral>(E))
    return EvaluateCharacterLiteral(CL);
  if (auto *BO = llvm::dyn_cast<BinaryOperator>(E))
    return EvaluateBinaryOperator(BO);
  if (auto *UO = llvm::dyn_cast<UnaryOperator>(E))
    return EvaluateUnaryOperator(UO);
  if (auto *CO = llvm::dyn_cast<ConditionalOperator>(E))
    return EvaluateConditionalOperator(CO);
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(E))
    return EvaluateDeclRefExpr(DRE);
  if (auto *CE = llvm::dyn_cast<CastExpr>(E))
    return EvaluateCastExpr(CE);
  if (auto *Call = llvm::dyn_cast<CallExpr>(E))
    return EvaluateCallExpr(Call);

  // Unsupported expression type
  return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                "unsupported expression type");
}

EvalResult
ConstantExprEvaluator::EvaluateIntegerLiteral(IntegerLiteral *E) {
  // IntegerLiteral::getValue() returns APInt, convert to APSInt (signed)
  llvm::APSInt Val(E->getValue(), false); // Treat as signed by default
  return EvalResult::getSuccess(Val);
}

EvalResult
ConstantExprEvaluator::EvaluateFloatingLiteral(FloatingLiteral *E) {
  return EvalResult::getSuccess(E->getValue());
}

EvalResult
ConstantExprEvaluator::EvaluateBooleanLiteral(CXXBoolLiteral *E) {
  llvm::APSInt Val(llvm::APInt(1, E->getValue() ? 1 : 0));
  return EvalResult::getSuccess(Val);
}

EvalResult
ConstantExprEvaluator::EvaluateCharacterLiteral(CharacterLiteral *E) {
  // Character literals have type char (integral). Evaluate as an APSInt.
  llvm::APSInt Val(llvm::APInt(32, E->getValue()));
  return EvalResult::getSuccess(Val);
}

EvalResult
ConstantExprEvaluator::EvaluateCallExpr(CallExpr *E) {
  // Try to resolve the callee to a FunctionDecl
  Expr *Callee = E->getCallee();
  if (!Callee)
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "null callee in call expression");

  // Direct DeclRefExpr callee: the common case for named function calls
  FunctionDecl *FD = nullptr;
  if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(Callee)) {
    FD = llvm::dyn_cast<FunctionDecl>(DRE->getDecl());
  }

  if (!FD) {
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "callee is not a named function");
  }

  return EvaluateCall(FD, E->getArgs());
}

EvalResult
ConstantExprEvaluator::EvaluateBinaryOperator(BinaryOperator *E) {
  EvalResult LHSResult = EvaluateExpr(E->getLHS());
  if (!LHSResult.isSuccess())
    return LHSResult;

  // Short-circuit for logical operators
  if (E->getOpcode() == BinaryOpKind::LAnd) {
    if (LHSResult.isIntegral() && !LHSResult.getInt().getBoolValue())
      return EvalResult::getSuccess(llvm::APSInt(llvm::APInt(1, 0)));
  }
  if (E->getOpcode() == BinaryOpKind::LOr) {
    if (LHSResult.isIntegral() && LHSResult.getInt().getBoolValue())
      return EvalResult::getSuccess(llvm::APSInt(llvm::APInt(1, 1)));
  }

  EvalResult RHSResult = EvaluateExpr(E->getRHS());
  if (!RHSResult.isSuccess())
    return RHSResult;

  // Both must be the same kind (both integral or both float)
  // For simplicity: handle integral operations
  if (LHSResult.isIntegral() && RHSResult.isIntegral()) {
    llvm::APSInt LVal = LHSResult.getInt();
    llvm::APSInt RVal = RHSResult.getInt();

    // Extend to common bit width
    if (LVal.getBitWidth() < RVal.getBitWidth())
      LVal = LVal.extend(RVal.getBitWidth());
    else if (RVal.getBitWidth() < LVal.getBitWidth())
      RVal = RVal.extend(LVal.getBitWidth());

    switch (E->getOpcode()) {
    // Arithmetic
    case BinaryOpKind::Add:
      return EvalResult::getSuccess(LVal + RVal);
    case BinaryOpKind::Sub:
      return EvalResult::getSuccess(LVal - RVal);
    case BinaryOpKind::Mul:
      return EvalResult::getSuccess(LVal * RVal);
    case BinaryOpKind::Div:
      if (RVal == 0) {
        return EvalResult::getFailure(EvalResult::EvaluationFailed,
                                      "division by zero");
      }
      return EvalResult::getSuccess(LVal / RVal);
    case BinaryOpKind::Rem:
      if (RVal == 0) {
        return EvalResult::getFailure(EvalResult::EvaluationFailed,
                                      "remainder by zero");
      }
      return EvalResult::getSuccess(LVal % RVal);

    // Shift
    case BinaryOpKind::Shl:
      return EvalResult::getSuccess(
          llvm::APSInt(LVal << RVal.getLimitedValue()));
    case BinaryOpKind::Shr:
      return EvalResult::getSuccess(
          llvm::APSInt(LVal >> RVal.getLimitedValue()));

    // Comparison
    case BinaryOpKind::LT:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, LVal.slt(RVal) ? 1 : 0)));
    case BinaryOpKind::GT:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, LVal.sgt(RVal) ? 1 : 0)));
    case BinaryOpKind::LE:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, LVal.sle(RVal) ? 1 : 0)));
    case BinaryOpKind::GE:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, LVal.sge(RVal) ? 1 : 0)));
    case BinaryOpKind::EQ:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, LVal.eq(RVal) ? 1 : 0)));
    case BinaryOpKind::NE:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, LVal.ne(RVal) ? 1 : 0)));

    // Bitwise
    case BinaryOpKind::And:
      return EvalResult::getSuccess(LVal & RVal);
    case BinaryOpKind::Or:
      return EvalResult::getSuccess(LVal | RVal);
    case BinaryOpKind::Xor:
      return EvalResult::getSuccess(LVal ^ RVal);

    // Logical
    case BinaryOpKind::LAnd:
      return EvalResult::getSuccess(llvm::APSInt(
          llvm::APInt(1, LVal.getBoolValue() && RVal.getBoolValue())));
    case BinaryOpKind::LOr:
      return EvalResult::getSuccess(llvm::APSInt(
          llvm::APInt(1, LVal.getBoolValue() || RVal.getBoolValue())));

    // Comma
    case BinaryOpKind::Comma:
      return EvalResult::getSuccess(RVal);

    default:
      return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                    "unsupported binary operator");
    }
  }

  // Floating-point operations
  if (!LHSResult.isIntegral() && !RHSResult.isIntegral()) {
    llvm::APFloat LVal = LHSResult.getFloat();
    llvm::APFloat RVal = RHSResult.getFloat();

    switch (E->getOpcode()) {
    case BinaryOpKind::Add: {
      LVal.add(RVal, llvm::APFloat::rmNearestTiesToEven);
      return EvalResult::getSuccess(LVal);
    }
    case BinaryOpKind::Sub: {
      LVal.subtract(RVal, llvm::APFloat::rmNearestTiesToEven);
      return EvalResult::getSuccess(LVal);
    }
    case BinaryOpKind::Mul: {
      LVal.multiply(RVal, llvm::APFloat::rmNearestTiesToEven);
      return EvalResult::getSuccess(LVal);
    }
    case BinaryOpKind::Div: {
      LVal.divide(RVal, llvm::APFloat::rmNearestTiesToEven);
      return EvalResult::getSuccess(LVal);
    }
    default:
      return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                    "unsupported float binary operator");
    }
  }

  return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                "mixed-type binary operation");
}

EvalResult
ConstantExprEvaluator::EvaluateUnaryOperator(UnaryOperator *E) {
  EvalResult SubResult = EvaluateExpr(E->getSubExpr());
  if (!SubResult.isSuccess())
    return SubResult;

  if (SubResult.isIntegral()) {
    llvm::APSInt Val = SubResult.getInt();
    switch (E->getOpcode()) {
    case UnaryOpKind::Plus:
      return EvalResult::getSuccess(Val);
    case UnaryOpKind::Minus:
      return EvalResult::getSuccess(-Val);
    case UnaryOpKind::Not:
      return EvalResult::getSuccess(~Val);
    case UnaryOpKind::LNot:
      return EvalResult::getSuccess(
          llvm::APSInt(llvm::APInt(1, !Val.getBoolValue())));
    default:
      return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                    "unsupported unary operator for constant eval");
    }
  }

  // Floating-point unary
  if (!SubResult.isIntegral()) {
    llvm::APFloat Val = SubResult.getFloat();
    switch (E->getOpcode()) {
    case UnaryOpKind::Plus:
      return EvalResult::getSuccess(Val);
    case UnaryOpKind::Minus:
      Val.changeSign();
      return EvalResult::getSuccess(Val);
    default:
      return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                    "unsupported unary operator for float");
    }
  }

  return EvalResult::getFailure(EvalResult::NotConstantExpression);
}

EvalResult
ConstantExprEvaluator::EvaluateConditionalOperator(ConditionalOperator *E) {
  EvalResult CondResult = EvaluateExpr(E->getCond());
  if (!CondResult.isSuccess())
    return CondResult;

  // Evaluate condition
  bool CondValue = false;
  if (CondResult.isIntegral()) {
    CondValue = CondResult.getInt().getBoolValue();
  } else {
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "non-integral condition in ternary");
  }

  // Evaluate the selected branch
  if (CondValue) {
    return EvaluateExpr(E->getTrueExpr());
  } else {
    return EvaluateExpr(E->getFalseExpr());
  }
}

EvalResult
ConstantExprEvaluator::EvaluateDeclRefExpr(DeclRefExpr *E) {
  ValueDecl *D = E->getDecl();
  if (!D)
    return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                  "null declaration");

  // Enum constants — use cached value if available, otherwise evaluate init expr
  if (auto *ECD = llvm::dyn_cast<EnumConstantDecl>(D)) {
    if (ECD->hasVal())
      return EvalResult::getSuccess(ECD->getVal());
    // Fallback: try to evaluate the init expression
    if (Expr *Init = ECD->getInitExpr())
      return EvaluateExpr(Init);
    return EvalResult::getSuccess(llvm::APSInt(llvm::APInt(32, 0)));
  }

  // Const integral variables
  if (auto *VD = llvm::dyn_cast<VarDecl>(D)) {
    // constexpr variables
    if (VD->isConstexpr()) {
      Expr *Init = VD->getInit();
      if (Init)
        return EvaluateExpr(Init);
      return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                    "constexpr variable has no initializer");
    }
    if (VD->getType().isConstQualified() && VD->getType()->isIntegerType()) {
      Expr *Init = VD->getInit();
      if (Init)
        return EvaluateExpr(Init);
    }
  }

  return EvalResult::getFailure(EvalResult::NotConstantExpression,
                                "non-constant declaration reference");
}

EvalResult
ConstantExprEvaluator::EvaluateCastExpr(CastExpr *E) {
  EvalResult SubResult = EvaluateExpr(E->getSubExpr());
  if (!SubResult.isSuccess())
    return SubResult;

  // For constant evaluation, we allow casts between arithmetic types
  QualType DestType = E->getType();
  if (DestType.isNull())
    return SubResult; // Return as-is if no destination type

  // Int → Int cast: adjust bit width
  if (SubResult.isIntegral() && DestType->isIntegerType()) {
    // TODO: adjust bit width based on destination type
    return SubResult;
  }

  // Float → Int cast
  if (!SubResult.isIntegral() && DestType->isIntegerType()) {
    llvm::APSInt IntVal(32, false);
    bool IsExact = false;
    SubResult.getFloat().convertToInteger(IntVal, llvm::APFloat::rmTowardZero,
                                           &IsExact);
    return EvalResult::getSuccess(IntVal);
  }

  // Int → Float cast
  if (SubResult.isIntegral() && DestType->isFloatingType()) {
    llvm::APFloat FloatVal(llvm::APFloat::IEEEdouble());
    FloatVal.convertFromAPInt(SubResult.getInt(), SubResult.getInt().isSigned(),
                              llvm::APFloat::rmNearestTiesToEven);
    return EvalResult::getSuccess(FloatVal);
  }

  return SubResult;
}

} // namespace blocktype

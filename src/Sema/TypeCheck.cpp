//===--- TypeCheck.cpp - Type Checking Implementation -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TypeCheck class for type compatibility checking,
// initialization, assignment, and type-related semantic operations.
//
// Task 4.5.1 — 类型检查
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TypeCheck.h"
#include "blocktype/Sema/Conversion.h"
#include "blocktype/AST/Decl.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Assignment and initialization
//===----------------------------------------------------------------------===//

bool TypeCheck::CheckAssignment(QualType LHS, QualType RHS, SourceLocation Loc) {
  if (LHS.isNull() || RHS.isNull()) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  // Check if LHS is const-qualified (cannot assign to const)
  if (LHS.isConstQualified()) {
    Diags.report(Loc, DiagID::err_assigning_to_const);
    return false;
  }

  // Check type compatibility
  if (!isTypeCompatible(RHS, LHS)) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  return true;
}

bool TypeCheck::CheckInitialization(QualType Dest, Expr *Init,
                                    SourceLocation Loc) {
  if (Dest.isNull() || !Init) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  QualType InitType = Init->getType();
  if (InitType.isNull()) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  // Reference binding
  if (Dest.isReferenceType()) {
    return CheckReferenceBinding(Dest, Init, Loc);
  }

  // Check convertibility
  if (!isTypeCompatible(InitType, Dest)) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  return true;
}

bool TypeCheck::CheckDirectInitialization(QualType Dest,
                                          llvm::ArrayRef<Expr *> Args,
                                          SourceLocation Loc) {
  if (Dest.isNull()) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  // Single argument: check convertibility
  if (Args.size() == 1) {
    return CheckInitialization(Dest, Args[0], Loc);
  }

  // Zero arguments: default initialization
  if (Args.empty()) {
    return true; // Default construction — always valid for now
  }

  // Multiple arguments: check if Dest is a class type with a matching
  // constructor (TODO: constructor overload resolution)
  return true;
}

bool TypeCheck::CheckListInitialization(QualType Dest,
                                        llvm::ArrayRef<Expr *> Args,
                                        SourceLocation Loc) {
  if (Dest.isNull()) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  // Single-element list: check direct initialization
  if (Args.size() == 1) {
    return CheckInitialization(Dest, Args[0], Loc);
  }

  // Empty list: value initialization
  if (Args.empty()) {
    return true;
  }

  // Multi-element list: for array types, check each element
  if (Dest->isArrayType()) {
    // TODO: check each element type compatibility
    return true;
  }

  // For aggregate types, check each initializer
  // TODO: aggregate initialization checking
  return true;
}

bool TypeCheck::CheckReferenceBinding(QualType RefType, Expr *Init,
                                      SourceLocation Loc) {
  if (RefType.isNull() || !Init) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  const auto *Ref = llvm::dyn_cast<ReferenceType>(RefType.getTypePtr());
  if (!Ref) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  QualType ReferencedType(Ref->getReferencedType(), Qualifier::None);
  QualType InitType = Init->getType();

  // Non-const lvalue reference: must bind to same-type lvalue
  if (Ref->isLValueReference() && !ReferencedType.isConstQualified()) {
    if (!Init->isLValue()) {
      // Non-const lvalue reference cannot bind to rvalue
      Diags.report(Loc, DiagID::err_non_const_lvalue_ref_binds_to_rvalue);
      return false;
    }
    if (!isSameType(InitType, QualType(Ref->getReferencedType(),
                                        RefType.getQualifiers()))) {
      Diags.report(Loc, DiagID::err_type_mismatch);
      return false;
    }
    return true;
  }

  // Const lvalue reference or rvalue reference: can bind to convertible type
  if (!isTypeCompatible(InitType, QualType(Ref->getReferencedType(),
                                            Qualifier::None))) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Function call checking
//===----------------------------------------------------------------------===//

bool TypeCheck::CheckCall(FunctionDecl *F, llvm::ArrayRef<Expr *> Args,
                          SourceLocation CallLoc) {
  if (!F)
    return false;

  QualType FnType = F->getType();
  const auto *FT = llvm::dyn_cast<FunctionType>(FnType.getTypePtr());
  if (!FT)
    return false;

  unsigned NumParams = F->getNumParams();
  bool IsVariadic = FT->isVariadic();

  // Count required params (those without default args)
  unsigned MinParams = 0;
  for (unsigned I = 0; I < NumParams; ++I) {
    if (!F->getParamDecl(I)->getDefaultArg()) {
      MinParams = I + 1;
    }
  }

  // Check argument count
  if (Args.size() < MinParams) {
    Diags.report(CallLoc, DiagID::err_type_mismatch);
    return false;
  }
  if (!IsVariadic && Args.size() > NumParams) {
    Diags.report(CallLoc, DiagID::err_type_mismatch);
    return false;
  }

  // Check each argument's type
  for (unsigned I = 0; I < Args.size() && I < NumParams; ++I) {
    QualType ParamType = F->getParamDecl(I)->getType();
    QualType ArgType = Args[I]->getType();

    if (!isTypeCompatible(ArgType, ParamType)) {
      Diags.report(CallLoc, DiagID::err_type_mismatch);
      return false;
    }
  }

  return true;
}

bool TypeCheck::CheckReturn(Expr *RetVal, QualType FuncRetType,
                            SourceLocation ReturnLoc) {
  if (FuncRetType.isNull() || !RetVal) {
    return FuncRetType.isNull(); // void function with no value is OK
  }

  QualType RetType = RetVal->getType();
  if (RetType.isNull()) {
    Diags.report(ReturnLoc, DiagID::err_return_type_mismatch);
    return false;
  }

  // void function: return expression is allowed if it's void
  if (FuncRetType->isVoidType()) {
    return RetType->isVoidType();
  }

  // Non-void function: return value must be convertible
  if (!isTypeCompatible(RetType, FuncRetType)) {
    Diags.report(ReturnLoc, DiagID::err_return_type_mismatch);
    return false;
  }

  return true;
}

bool TypeCheck::CheckCondition(Expr *Cond, SourceLocation Loc) {
  if (!Cond)
    return false;

  QualType CondType = Cond->getType();
  if (CondType.isNull()) {
    Diags.report(Loc, DiagID::err_type_mismatch);
    return false;
  }

  // Condition must be convertible to bool (contextually converted to bool)
  // Per C++ [stmt.select]: arithmetic, pointer, pointer-to-member,
  // enumeration, or unscoped enumeration types are allowed.
  if (CondType->isBooleanType() || CondType->isIntegerType() ||
      CondType->isPointerType() || CondType->isFloatingType()) {
    return true;
  }

  Diags.report(Loc, DiagID::err_type_mismatch);
  return false;
}

bool TypeCheck::CheckCaseExpression(Expr *Val, SourceLocation Loc) {
  if (!Val)
    return false;

  // Case expression must be an integral constant expression
  if (!Val->getType()->isIntegerType()) {
    Diags.report(Loc, DiagID::err_non_constexpr_in_constant_context);
    return false;
  }

  // TODO: evaluate as constant expression using ConstantExprEvaluator
  return true;
}

//===----------------------------------------------------------------------===//
// Type compatibility
//===----------------------------------------------------------------------===//

bool TypeCheck::isTypeCompatible(QualType From, QualType To) const {
  if (From.isNull() || To.isNull())
    return false;

  // Same type: always compatible
  if (From == To)
    return true;

  // Check implicit conversion using ConversionChecker
  ImplicitConversionSequence ICS = ConversionChecker::GetConversion(From, To);
  return !ICS.isBad();
}

bool TypeCheck::isSameType(QualType T1, QualType T2) const {
  if (T1.isNull() && T2.isNull())
    return true;
  if (T1.isNull() || T2.isNull())
    return false;

  // Compare canonical types ignoring qualifiers
  QualType C1 = T1.getCanonicalType();
  QualType C2 = T2.getCanonicalType();
  return C1.getTypePtr() == C2.getTypePtr();
}

QualType TypeCheck::getCommonType(QualType T1, QualType T2) const {
  // Usual arithmetic conversions per C++ [expr.arith.conv]
  if (T1.isNull() || T2.isNull())
    return QualType();

  const Type *Ty1 = T1.getTypePtr();
  const Type *Ty2 = T2.getTypePtr();

  // Same type: no conversion needed
  if (isSameType(T1, T2))
    return T1;

  // Both arithmetic types: apply usual arithmetic conversions
  if ((Ty1->isIntegerType() || Ty1->isFloatingType()) &&
      (Ty2->isIntegerType() || Ty2->isFloatingType())) {

    // If either is long double, result is long double
    if (isLongDoubleType(Ty1) || isLongDoubleType(Ty2)) {
      return QualType(Ty1->isFloatingType() &&
                              llvm::cast<BuiltinType>(Ty1)->getKind() ==
                                  BuiltinKind::LongDouble
                          ? Ty1
                          : Ty2,
                      Qualifier::None);
    }

    // If either is double, result is double
    if (isDoubleType(Ty1) || isDoubleType(Ty2)) {
      return QualType(
          Ty1->isFloatingType() &&
                  llvm::cast<BuiltinType>(Ty1)->getKind() == BuiltinKind::Double
              ? Ty1
              : Ty2,
          Qualifier::None);
    }

    // If either is float, result is float
    if (isFloatType(Ty1) || isFloatType(Ty2)) {
      return QualType(
          Ty1->isFloatingType() &&
                  llvm::cast<BuiltinType>(Ty1)->getKind() == BuiltinKind::Float
              ? Ty1
              : Ty2,
          Qualifier::None);
    }

    // Both are integer types: apply integral promotions first, then find
    // the common type. Per C++ [expr.arith.conv]:
    //   1. Integral promotions are applied to both operands.
    //   2. Then usual arithmetic conversions determine the result type.
    QualType Promoted1 = performIntegralPromotion(T1);
    QualType Promoted2 = performIntegralPromotion(T2);
    return getIntegerCommonType(Promoted1, Promoted2);
  }

  // Pointer types
  if (Ty1->isPointerType() && Ty2->isPointerType()) {
    // TODO: pointer comparison common type
    return T1;
  }

  return QualType();
}

//===----------------------------------------------------------------------===//
// Binary operator result type (per C++ [expr])
//===----------------------------------------------------------------------===//

QualType TypeCheck::getBinaryOperatorResultType(BinaryOpKind Op,
                                                QualType LHS,
                                                QualType RHS) const {
  if (LHS.isNull() || RHS.isNull())
    return QualType();

  // Comparison operators: result is always bool
  // Per C++ [expr.rel], [expr.eq]
  if (Op == BinaryOpKind::LT || Op == BinaryOpKind::GT ||
      Op == BinaryOpKind::LE || Op == BinaryOpKind::GE ||
      Op == BinaryOpKind::EQ || Op == BinaryOpKind::NE) {
    return Context.getBoolType();
  }

  // Spaceship operator <=> (C++20): returns int (-1, 0, 1)
  // Simplified: real C++20 returns std::strong_ordering etc.
  if (Op == BinaryOpKind::Spaceship) {
    return Context.getIntType();
  }

  // Logical operators: result is always bool
  // Per C++ [expr.log.and], [expr.log.or]
  if (Op == BinaryOpKind::LAnd || Op == BinaryOpKind::LOr) {
    return Context.getBoolType();
  }

  // Assignment operators (compound and plain): result is LHS type
  // Per C++ [expr.ass], [expr.mptr.oper] — returns an lvalue referring to
  // the left operand.
  if (Op == BinaryOpKind::Assign || Op == BinaryOpKind::MulAssign ||
      Op == BinaryOpKind::DivAssign || Op == BinaryOpKind::RemAssign ||
      Op == BinaryOpKind::AddAssign || Op == BinaryOpKind::SubAssign ||
      Op == BinaryOpKind::ShlAssign || Op == BinaryOpKind::ShrAssign ||
      Op == BinaryOpKind::AndAssign || Op == BinaryOpKind::OrAssign ||
      Op == BinaryOpKind::XorAssign) {
    return LHS;
  }

  // Comma operator: result is RHS type
  // Per C++ [expr.comma]
  if (Op == BinaryOpKind::Comma) {
    return RHS;
  }

  // Pointer arithmetic: pointer ± integer, pointer - pointer
  // Per C++ [expr.add]
  if (Op == BinaryOpKind::Add || Op == BinaryOpKind::Sub) {
    const Type *LTy = LHS.getTypePtr();
    const Type *RTy = RHS.getTypePtr();

    bool LIsPtr = LTy->isPointerType();
    bool RIsPtr = RTy->isPointerType();
    bool LIsInt = LTy->isIntegerType() || LTy->isEnumType() || LTy->isBooleanType();
    bool RIsInt = RTy->isIntegerType() || RTy->isEnumType() || RTy->isBooleanType();

    if (LIsPtr && RIsInt) {
      // pointer + integer or pointer - integer → pointer type
      return LHS;
    }
    if (Op == BinaryOpKind::Add && RIsPtr && LIsInt) {
      // integer + pointer → pointer type
      return RHS;
    }
    if (LIsPtr && RIsPtr && Op == BinaryOpKind::Sub) {
      // pointer - pointer → ptrdiff_t (long)
      return Context.getLongType();
    }
  }

  // Arithmetic operators: Mul, Div, Rem, Add, Sub
  // Shift operators: Shl, Shr
  // Bitwise operators: And, Or, Xor
  // Per C++ [expr.mul], [expr.add], [expr.shift], [expr.bit.and], etc.
  // Result is the usual arithmetic conversions applied to both operands.
  return getCommonType(LHS, RHS);
}

//===----------------------------------------------------------------------===//
// Unary operator result type (per C++ [expr])
//===----------------------------------------------------------------------===//

QualType TypeCheck::getUnaryOperatorResultType(UnaryOpKind Op,
                                               QualType Operand) const {
  if (Operand.isNull())
    return QualType();

  // Logical NOT: result is always bool
  // Per C++ [expr.unary.op]: The result is a bool prvalue.
  if (Op == UnaryOpKind::LNot) {
    return Context.getBoolType();
  }

  // Dereference: result is the pointee type
  // Per C++ [expr.unary.op]: The result is an lvalue referring to the
  // object or function to which the operand points.
  if (Op == UnaryOpKind::Deref) {
    if (auto *PT = llvm::dyn_cast<PointerType>(Operand.getTypePtr())) {
      return PT->getPointeeType();
    }
    return QualType();
  }

  // Address-of: result is a pointer to the operand type
  // Per C++ [expr.unary.op]: The result is a prvalue of type "pointer to T".
  if (Op == UnaryOpKind::AddrOf) {
    return QualType(Context.getPointerType(Operand.getTypePtr()),
                    Qualifier::None);
  }

  // Prefix/postfix increment/decrement: result is operand type
  // Per C++ [expr.pre.incr], [expr.post.incr]
  if (Op == UnaryOpKind::PreInc || Op == UnaryOpKind::PreDec ||
      Op == UnaryOpKind::PostInc || Op == UnaryOpKind::PostDec) {
    return Operand;
  }

  // Unary plus/minus and bitwise NOT: integral promotion then return
  // Per C++ [expr.unary.op]:
  //   - The operand of unary +/− is promoted per integral promotion.
  //   - The operand of ~ is promoted per integral promotion.
  if (Op == UnaryOpKind::Plus || Op == UnaryOpKind::Minus ||
      Op == UnaryOpKind::Not) {
    return performIntegralPromotion(Operand);
  }

  return Operand;
}

bool TypeCheck::isComparable(QualType T) const {
  if (T.isNull())
    return false;

  const Type *Ty = T.getTypePtr();
  return Ty->isIntegerType() || Ty->isFloatingType() ||
         Ty->isPointerType() || Ty->isEnumType() ||
         Ty->isBooleanType();
}

bool TypeCheck::isCallable(QualType T) const {
  if (T.isNull())
    return false;

  const Type *Ty = T.getTypePtr();

  // Direct function type
  if (Ty->isFunctionType())
    return true;

  // Pointer to function
  if (auto *PT = llvm::dyn_cast<PointerType>(Ty)) {
    if (PT->getPointeeType()->isFunctionType())
      return true;
  }

  // TODO: class types with operator()

  return false;
}

//===----------------------------------------------------------------------===//
// Private helpers
//===----------------------------------------------------------------------===//

QualType TypeCheck::performIntegralPromotion(QualType T) const {
  if (T.isNull())
    return T;

  const Type *Ty = T.getTypePtr();

  // Enum types promote to int (C++ [conv.prom])
  if (Ty->isEnumType()) {
    return Context.getIntType();
  }

  // BuiltinType integral promotion
  if (auto *BT = llvm::dyn_cast<BuiltinType>(Ty)) {
    if (!BT->isInteger())
      return T;

    int Rank = getIntegerRankForType(BT);
    // Types with rank < int (rank 4) promote to int
    if (Rank >= 0 && Rank < 4) {
      // bool, char, wchar_t, short → int
      return Context.getIntType();
    }

    // Unsigned types: if rank == int, might promote to unsigned int
    // but for simplicity, types at or above int rank stay the same.
    // (Full implementation would check if int can represent all values.)
  }

  return T;
}

int TypeCheck::getIntegerRankForType(const BuiltinType *BT) const {
  switch (BT->getKind()) {
  case BuiltinKind::Bool:
    return 0;
  case BuiltinKind::Char:
  case BuiltinKind::SignedChar:
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::Char8:
  case BuiltinKind::Char16:
    return 1;
  case BuiltinKind::WChar:
  case BuiltinKind::Char32:
    return 2;
  case BuiltinKind::Short:
  case BuiltinKind::UnsignedShort:
    return 3;
  case BuiltinKind::Int:
  case BuiltinKind::UnsignedInt:
    return 4;
  case BuiltinKind::Long:
  case BuiltinKind::UnsignedLong:
    return 5;
  case BuiltinKind::LongLong:
  case BuiltinKind::UnsignedLongLong:
    return 6;
  case BuiltinKind::Int128:
  case BuiltinKind::UnsignedInt128:
    return 7;
  default:
    return -1;
  }
}

bool TypeCheck::isLongDoubleType(const Type *T) const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(T))
    return BT->getKind() == BuiltinKind::LongDouble ||
           BT->getKind() == BuiltinKind::Float128;
  return false;
}

bool TypeCheck::isDoubleType(const Type *T) const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(T))
    return BT->getKind() == BuiltinKind::Double;
  return false;
}

bool TypeCheck::isFloatType(const Type *T) const {
  if (auto *BT = llvm::dyn_cast<BuiltinType>(T))
    return BT->getKind() == BuiltinKind::Float;
  return false;
}

QualType TypeCheck::getIntegerCommonType(QualType T1, QualType T2) const {
  const auto *BT1 = llvm::dyn_cast<BuiltinType>(T1.getTypePtr());
  const auto *BT2 = llvm::dyn_cast<BuiltinType>(T2.getTypePtr());

  if (!BT1 || !BT2)
    return T1; // Fallback

  int Rank1 = getIntegerRankForType(BT1);
  int Rank2 = getIntegerRankForType(BT2);

  // Per C++ [expr.arith.conv] for integer types:
  // If both have the same signedness, the type with greater rank.
  // If the unsigned type has greater or equal rank, use the unsigned type.
  // If the signed type can represent all values of the unsigned type,
  // use the signed type. Otherwise, use the unsigned counterpart of
  // the signed type.
  // Simplified: use the higher-ranked type.
  if (Rank1 >= Rank2)
    return T1;
  return T2;
}

} // namespace blocktype

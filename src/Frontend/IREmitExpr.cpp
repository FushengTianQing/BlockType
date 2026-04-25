//===--- IREmitExpr.cpp - IR Expression Emitter -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IREmitExpr.h"
#include "blocktype/Frontend/ASTToIRConverter.h"

#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRFunction.h"

namespace blocktype {
namespace frontend {

using NodeKind = ASTNode::NodeKind;

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

IREmitExpr::IREmitExpr(ASTToIRConverter& Converter)
  : Converter_(Converter) {}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

ir::IRBuilder& IREmitExpr::getBuilder() {
  return Converter_.getBuilder();
}

ir::IRValue* IREmitExpr::emitErrorPlaceholder(ir::IRType* T) {
  return Converter_.emitErrorPlaceholder(T);
}

ir::IRType* IREmitExpr::emitErrorType() {
  return Converter_.emitErrorType();
}

ir::IRType* IREmitExpr::mapType(QualType T) {
  return Converter_.getTypeMapper().mapType(T);
}

bool IREmitExpr::isSignedType(QualType T) {
  if (T.isNull()) return true;
  if (auto* BT = dyn_cast<BuiltinType>(T.getTypePtr())) {
    return BT->isSignedInteger();
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Emit - General Dispatch
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::Emit(const Expr* E) {
  if (!E) return nullptr;

  switch (E->getKind()) {
    // Literals
    case NodeKind::IntegerLiteralKind:
      return EmitIntegerLiteral(static_cast<const IntegerLiteral*>(E));
    case NodeKind::FloatingLiteralKind:
      return EmitFloatingLiteral(static_cast<const FloatingLiteral*>(E));
    case NodeKind::StringLiteralKind:
      return EmitStringLiteral(static_cast<const StringLiteral*>(E));
    case NodeKind::CharacterLiteralKind:
      return EmitCharacterLiteral(static_cast<const CharacterLiteral*>(E));
    case NodeKind::CXXBoolLiteralKind:
      return EmitBoolLiteral(static_cast<const CXXBoolLiteral*>(E));
    case NodeKind::CXXNullPtrLiteralKind:
      return getBuilder().getNull(mapType(E->getType()));

    // Operators
    case NodeKind::BinaryOperatorKind:
      return EmitBinaryExpr(static_cast<const BinaryOperator*>(E));
    case NodeKind::UnaryOperatorKind:
      return EmitUnaryExpr(static_cast<const UnaryOperator*>(E));

    // References
    case NodeKind::DeclRefExprKind:
      return EmitDeclRefExpr(static_cast<const DeclRefExpr*>(E));
    case NodeKind::MemberExprKind:
      return EmitMemberExpr(static_cast<const MemberExpr*>(E));

    // Calls
    case NodeKind::CallExprKind:
      return EmitCallExpr(static_cast<const CallExpr*>(E));
    case NodeKind::CXXMemberCallExprKind:
      return EmitCXXMemberCallExpr(
          static_cast<const CXXMemberCallExpr*>(E));

    // Casts
    case NodeKind::CastExprKind:
    case NodeKind::CXXStaticCastExprKind:
    case NodeKind::CXXDynamicCastExprKind:
    case NodeKind::CXXConstCastExprKind:
    case NodeKind::CXXReinterpretCastExprKind:
    case NodeKind::CStyleCastExprKind:
      return EmitCastExpr(static_cast<const CastExpr*>(E));

    // C++ Specific
    case NodeKind::CXXConstructExprKind:
    case NodeKind::CXXTemporaryObjectExprKind:
      return EmitCXXConstructExpr(static_cast<const CXXConstructExpr*>(E));
    case NodeKind::CXXNewExprKind:
      return EmitCXXNewExpr(static_cast<const CXXNewExpr*>(E));
    case NodeKind::CXXDeleteExprKind:
      return EmitCXXDeleteExpr(static_cast<const CXXDeleteExpr*>(E));
    case NodeKind::CXXThisExprKind:
      return EmitCXXThisExpr(static_cast<const CXXThisExpr*>(E));

    // Conditional
    case NodeKind::ConditionalOperatorKind:
      return EmitConditionalOperator(
          static_cast<const ConditionalOperator*>(E));

    // Init list
    case NodeKind::InitListExprKind:
      return EmitInitListExpr(static_cast<const InitListExpr*>(E));

    default:
      return emitErrorPlaceholder(mapType(E->getType()));
  }
}

//===----------------------------------------------------------------------===//
// EmitBinaryExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitBinaryExpr(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  BinaryOpKind Op = BO->getOpcode();

  // Short-circuit logical operators
  if (Op == BinaryOpKind::LAnd || Op == BinaryOpKind::LOr)
    return emitShortCircuitEval(BO);

  // Assignment operators
  if (Op == BinaryOpKind::Assign)
    return emitAssignment(BO);

  // Compound assignment operators
  if (Op == BinaryOpKind::AddAssign || Op == BinaryOpKind::SubAssign ||
      Op == BinaryOpKind::MulAssign || Op == BinaryOpKind::DivAssign ||
      Op == BinaryOpKind::RemAssign || Op == BinaryOpKind::ShlAssign ||
      Op == BinaryOpKind::ShrAssign || Op == BinaryOpKind::AndAssign ||
      Op == BinaryOpKind::OrAssign  || Op == BinaryOpKind::XorAssign)
    return emitCompoundAssignment(BO);

  // Comma operator
  if (Op == BinaryOpKind::Comma) {
    Emit(BO->getLHS());
    return Emit(BO->getRHS());
  }

  // Standard binary operators: evaluate both operands
  ir::IRValue* LHS = Emit(BO->getLHS());
  ir::IRValue* RHS = Emit(BO->getRHS());
  if (!LHS || !RHS)
    return emitErrorPlaceholder(mapType(BO->getType()));

  ir::IRType* LHSTy = LHS->getType();

  switch (Op) {
    // Arithmetic
    case BinaryOpKind::Add:
      return getBuilder().createAdd(LHS, RHS);
    case BinaryOpKind::Sub:
      return getBuilder().createSub(LHS, RHS);
    case BinaryOpKind::Mul:
      return getBuilder().createMul(LHS, RHS);
    case BinaryOpKind::Div:
      if (LHSTy->isFloat())
        return emitErrorPlaceholder(LHSTy); // TODO: createFDiv
      return isSignedType(BO->getLHS()->getType())
                 ? getBuilder().createSDiv(LHS, RHS)
                 : getBuilder().createUDiv(LHS, RHS);
    case BinaryOpKind::Rem:
      if (LHSTy->isFloat())
        return emitErrorPlaceholder(LHSTy); // TODO: createFRem
      return isSignedType(BO->getLHS()->getType())
                 ? getBuilder().createSRem(LHS, RHS)
                 : getBuilder().createURem(LHS, RHS);

    // Shift
    case BinaryOpKind::Shl:
      return getBuilder().createShl(LHS, RHS);
    case BinaryOpKind::Shr:
      return isSignedType(BO->getLHS()->getType())
                 ? getBuilder().createAShr(LHS, RHS)
                 : getBuilder().createLShr(LHS, RHS);

    // Bitwise
    case BinaryOpKind::And:
      return getBuilder().createAnd(LHS, RHS);
    case BinaryOpKind::Or:
      return getBuilder().createOr(LHS, RHS);
    case BinaryOpKind::Xor:
      return getBuilder().createXor(LHS, RHS);

    // Comparison
    case BinaryOpKind::LT:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SLT : ir::ICmpPred::ULT,
          LHS, RHS);
    case BinaryOpKind::GT:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SGT : ir::ICmpPred::UGT,
          LHS, RHS);
    case BinaryOpKind::LE:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SLE : ir::ICmpPred::ULE,
          LHS, RHS);
    case BinaryOpKind::GE:
      return getBuilder().createICmp(
          isSignedType(BO->getLHS()->getType())
              ? ir::ICmpPred::SGE : ir::ICmpPred::UGE,
          LHS, RHS);
    case BinaryOpKind::EQ:
      return getBuilder().createICmp(ir::ICmpPred::EQ, LHS, RHS);
    case BinaryOpKind::NE:
      return getBuilder().createICmp(ir::ICmpPred::NE, LHS, RHS);

    // Spaceship (C++20)
    case BinaryOpKind::Spaceship:
      return emitErrorPlaceholder(mapType(BO->getType()));

    default:
      return emitErrorPlaceholder(mapType(BO->getType()));
  }
}

//===----------------------------------------------------------------------===//
// EmitUnaryExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitUnaryExpr(const UnaryOperator* UO) {
  if (!UO) return nullptr;

  ir::IRValue* SubVal = Emit(UO->getSubExpr());
  if (!SubVal)
    return emitErrorPlaceholder(mapType(UO->getType()));

  switch (UO->getOpcode()) {
    case UnaryOpKind::Plus:
      return SubVal;

    case UnaryOpKind::Minus:
      return getBuilder().createNeg(SubVal);

    case UnaryOpKind::Not:
      return getBuilder().createNot(SubVal);

    case UnaryOpKind::LNot: {
      ir::IRValue* Zero = getBuilder().getInt32(0);
      ir::IRType* SubTy = SubVal->getType();
      if (SubTy->isInteger()) {
        return getBuilder().createICmp(ir::ICmpPred::EQ, SubVal, Zero);
      }
      return emitErrorPlaceholder(
          Converter_.getTypeContext().getInt1Ty());
    }

    case UnaryOpKind::Deref:
      return SubVal;

    case UnaryOpKind::AddrOf:
      return SubVal;

    case UnaryOpKind::PreInc: {
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* Result = getBuilder().createAdd(SubVal, One);
      getBuilder().createStore(Result, SubVal);
      return Result;
    }

    case UnaryOpKind::PreDec: {
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* Result = getBuilder().createSub(SubVal, One);
      getBuilder().createStore(Result, SubVal);
      return Result;
    }

    case UnaryOpKind::PostInc: {
      ir::IRValue* OldVal = SubVal;
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* NewVal = getBuilder().createAdd(SubVal, One);
      getBuilder().createStore(NewVal, SubVal);
      return OldVal;
    }

    case UnaryOpKind::PostDec: {
      ir::IRValue* OldVal = SubVal;
      ir::IRValue* One = getBuilder().getInt32(1);
      ir::IRValue* NewVal = getBuilder().createSub(SubVal, One);
      getBuilder().createStore(NewVal, SubVal);
      return OldVal;
    }

    default:
      return emitErrorPlaceholder(mapType(UO->getType()));
  }
}

//===----------------------------------------------------------------------===//
// EmitCallExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCallExpr(const CallExpr* CE) {
  if (!CE) return nullptr;

  Expr* CalleeExpr = CE->getCallee();
  if (!CalleeExpr) return emitErrorPlaceholder(emitErrorType());

  auto* DRE = dyn_cast<DeclRefExpr>(CalleeExpr);
  if (!DRE) {
    ir::IRValue* CalleeVal = Emit(CalleeExpr);
    if (!CalleeVal) return emitErrorPlaceholder(emitErrorType());
    // TODO: Support indirect calls via function pointer
    return emitErrorPlaceholder(emitErrorType());
  }

  ValueDecl* VD = DRE->getDecl();
  auto* FD = dyn_cast<FunctionDecl>(VD);
  if (!FD) return emitErrorPlaceholder(emitErrorType());

  ir::IRFunction* IRFn = Converter_.getFunction(FD);
  if (!IRFn) {
    IRFn = Converter_.emitFunction(FD);
  }
  if (!IRFn) return emitErrorPlaceholder(emitErrorType());

  ir::SmallVector<ir::IRValue*, 8> IRArgs;
  for (Expr* Arg : CE->getArgs()) {
    ir::IRValue* ArgVal = Emit(Arg);
    if (!ArgVal) {
      ArgVal = emitErrorPlaceholder(emitErrorType());
    }
    IRArgs.push_back(ArgVal);
  }

  return getBuilder().createCall(IRFn, IRArgs);
}

//===----------------------------------------------------------------------===//
// EmitCXXMemberCallExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXMemberCallExpr(const CXXMemberCallExpr* MCE) {
  if (!MCE) return nullptr;
  return EmitCallExpr(static_cast<const CallExpr*>(MCE));
}

//===----------------------------------------------------------------------===//
// EmitMemberExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitMemberExpr(const MemberExpr* ME) {
  if (!ME) return nullptr;

  ir::IRValue* BaseVal = Emit(ME->getBase());
  if (!BaseVal) return emitErrorPlaceholder(mapType(ME->getType()));

  ValueDecl* Member = ME->getMemberDecl();
  if (!Member) return emitErrorPlaceholder(mapType(ME->getType()));

  ir::IRValue* MemberVal = Converter_.getDeclValue(Member);
  if (MemberVal) return MemberVal;

  // If not found, compute GEP offset
  ir::IRType* BaseType = BaseVal->getType();
  if (BaseType->isPointer()) {
    auto* PtrTy = static_cast<ir::IRPointerType*>(BaseType);
    ir::IRType* PointeeTy = PtrTy->getPointeeType();
    if (PointeeTy->isStruct()) {
      ir::SmallVector<ir::IRValue*, 2> Indices;
      Indices.push_back(getBuilder().getInt32(0));
      Indices.push_back(getBuilder().getInt32(0));
      return getBuilder().createGEP(PointeeTy, BaseVal, Indices);
    }
  }

  return emitErrorPlaceholder(mapType(ME->getType()));
}

//===----------------------------------------------------------------------===//
// EmitDeclRefExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitDeclRefExpr(const DeclRefExpr* DRE) {
  if (!DRE) return nullptr;

  ValueDecl* VD = DRE->getDecl();
  if (!VD) return emitErrorPlaceholder(emitErrorType());

  // 1. Check DeclValues map
  ir::IRValue* Val = Converter_.getDeclValue(VD);
  if (Val) return Val;

  // 2. Check if it's a VarDecl — could be a global variable
  if (auto* VarD = dyn_cast<VarDecl>(VD)) {
    ir::IRGlobalVariable* GV = Converter_.getGlobalVar(VarD);
    if (GV) {
      ir::IRConstantGlobalRef* Ref =
          Converter_.getIRContext().create<ir::IRConstantGlobalRef>(GV);
      return Ref;
    }
  }

  // 3. Check if it's a FunctionDecl
  if (auto* FnD = dyn_cast<FunctionDecl>(VD)) {
    ir::IRFunction* IRFn = Converter_.getFunction(FnD);
    if (!IRFn) {
      IRFn = Converter_.emitFunction(FnD);
    }
    if (IRFn) {
      ir::IRConstantFunctionRef* Ref =
          Converter_.getIRContext().create<ir::IRConstantFunctionRef>(IRFn);
      return Ref;
    }
  }

  return emitErrorPlaceholder(mapType(DRE->getType()));
}

//===----------------------------------------------------------------------===//
// EmitCastExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCastExpr(const CastExpr* CE) {
  if (!CE) return nullptr;

  ir::IRValue* SubVal = Emit(CE->getSubExpr());
  if (!SubVal) return emitErrorPlaceholder(mapType(CE->getType()));

  ir::IRType* DestTy = mapType(CE->getType());
  if (!DestTy) DestTy = emitErrorType();

  switch (CE->getCastKind()) {
    case CastKind::NoOp:
      return SubVal;

    case CastKind::LValueToRValue:
      return getBuilder().createLoad(SubVal->getType(), SubVal);

    case CastKind::IntegralCast: {
      ir::IRType* SrcTy = SubVal->getType();
      auto* SrcInt = static_cast<ir::IRIntegerType*>(SrcTy);
      auto* DstInt = static_cast<ir::IRIntegerType*>(DestTy);
      if (SrcInt->getBitWidth() < DstInt->getBitWidth()) {
        return getBuilder().createSExt(SubVal, DestTy);
      } else if (SrcInt->getBitWidth() > DstInt->getBitWidth()) {
        return getBuilder().createTrunc(SubVal, DestTy);
      }
      return SubVal;
    }

    case CastKind::FloatingCast:
      return SubVal;

    case CastKind::IntegralToFloating:
      return isSignedType(CE->getSubExpr()->getType())
                 ? getBuilder().createSIToFP(SubVal, DestTy)
                 : getBuilder().createUIToFP(SubVal, DestTy);

    case CastKind::FloatingToIntegral:
      return isSignedType(CE->getType())
                 ? getBuilder().createFPToSI(SubVal, DestTy)
                 : getBuilder().createFPToUI(SubVal, DestTy);

    case CastKind::PointerToIntegral:
      return getBuilder().createPtrToInt(SubVal, DestTy);

    case CastKind::IntegralToPointer:
      return getBuilder().createIntToPtr(SubVal, DestTy);

    case CastKind::BitCast:
      return getBuilder().createBitCast(SubVal, DestTy);

    case CastKind::DerivedToBase:
    case CastKind::BaseToDerived:
      return getBuilder().createBitCast(SubVal, DestTy);

    case CastKind::CStyle:
    case CastKind::CXXStatic:
    case CastKind::CXXDynamic:
    case CastKind::CXXConst:
    case CastKind::CXXReinterpret:
      if (SubVal->getType()->isPointer() && DestTy->isPointer())
        return getBuilder().createBitCast(SubVal, DestTy);
      if (SubVal->getType()->isInteger() && DestTy->isPointer())
        return getBuilder().createIntToPtr(SubVal, DestTy);
      if (SubVal->getType()->isPointer() && DestTy->isInteger())
        return getBuilder().createPtrToInt(SubVal, DestTy);
      return SubVal;

    default:
      return emitErrorPlaceholder(DestTy);
  }
}

//===----------------------------------------------------------------------===//
// EmitCXXConstructExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXConstructExpr(const CXXConstructExpr* CCE) {
  if (!CCE) return nullptr;

  ir::IRType* ObjTy = mapType(CCE->getType());
  if (!ObjTy) ObjTy = emitErrorType();

  ir::IRValue* Alloca = getBuilder().createAlloca(ObjTy, "ctor.tmp");

  ir::SmallVector<ir::IRValue*, 4> Args;
  Args.push_back(Alloca);
  for (Expr* Arg : CCE->getArgs()) {
    ir::IRValue* ArgVal = Emit(Arg);
    if (!ArgVal) ArgVal = emitErrorPlaceholder(emitErrorType());
    Args.push_back(ArgVal);
  }

  CXXConstructorDecl* Ctor = CCE->getConstructor();
  if (Ctor) {
    ir::IRFunction* CtorFn = Converter_.getFunction(Ctor);
    if (CtorFn) {
      getBuilder().createCall(CtorFn, Args);
    }
  }

  return Alloca;
}

//===----------------------------------------------------------------------===//
// EmitCXXNewExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXNewExpr(const CXXNewExpr* NE) {
  if (!NE) return nullptr;

  ir::IRType* AllocTy = mapType(NE->getAllocatedType());
  if (!AllocTy) AllocTy = emitErrorType();

  if (NE->isArray()) {
    Emit(NE->getArraySize());
    return getBuilder().createAlloca(AllocTy, "new.array");
  }

  ir::IRValue* Mem = getBuilder().createAlloca(AllocTy, "new.obj");

  if (Expr* Init = NE->getInitializer()) {
    Emit(Init);
  }

  return Mem;
}

//===----------------------------------------------------------------------===//
// EmitCXXDeleteExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXDeleteExpr(const CXXDeleteExpr* DE) {
  if (!DE) return nullptr;

  Emit(DE->getArgument());
  return nullptr;
}

//===----------------------------------------------------------------------===//
// EmitCXXThisExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCXXThisExpr(const CXXThisExpr* TE) {
  (void)TE;
  return Converter_.getDeclValue(nullptr);
}

//===----------------------------------------------------------------------===//
// EmitConditionalOperator
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitConditionalOperator(const ConditionalOperator* CO) {
  if (!CO) return nullptr;

  ir::IRValue* Cond = Emit(CO->getCond());
  if (!Cond) return emitErrorPlaceholder(mapType(CO->getType()));

  ir::IRValue* TrueVal = Emit(CO->getTrueExpr());
  ir::IRValue* FalseVal = Emit(CO->getFalseExpr());
  if (!TrueVal || !FalseVal)
    return emitErrorPlaceholder(mapType(CO->getType()));

  return getBuilder().createSelect(Cond, TrueVal, FalseVal, "ternary");
}

//===----------------------------------------------------------------------===//
// EmitInitListExpr
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitInitListExpr(const InitListExpr* ILE) {
  if (!ILE) return nullptr;

  ir::IRType* AggTy = mapType(ILE->getType());
  if (!AggTy) AggTy = emitErrorType();

  ir::IRValue* AggAddr = getBuilder().createAlloca(AggTy, "initlist");

  unsigned Idx = 0;
  for (Expr* Init : ILE->getInits()) {
    ir::IRValue* InitVal = Emit(Init);
    if (!InitVal) InitVal = emitErrorPlaceholder(emitErrorType());

    ir::SmallVector<ir::IRValue*, 2> Indices;
    Indices.push_back(getBuilder().getInt32(0));
    Indices.push_back(getBuilder().getInt32(Idx));
    ir::IRValue* FieldAddr = getBuilder().createGEP(
        AggTy, AggAddr, Indices, "init.field");

    getBuilder().createStore(InitVal, FieldAddr);
    ++Idx;
  }

  return AggAddr;
}

//===----------------------------------------------------------------------===//
// EmitStringLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitStringLiteral(const StringLiteral* SL) {
  if (!SL) return nullptr;
  return emitErrorPlaceholder(mapType(SL->getType()));
}

//===----------------------------------------------------------------------===//
// EmitIntegerLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitIntegerLiteral(const IntegerLiteral* IL) {
  if (!IL) return nullptr;

  ir::IRType* Ty = mapType(IL->getType());
  ir::IRIntegerType* IntTy = (Ty && Ty->isInteger())
      ? static_cast<ir::IRIntegerType*>(Ty)
      : Converter_.getTypeContext().getInt32Ty();

  auto* C = Converter_.getIRContext().create<ir::IRConstantInt>(
      IntTy, IL->getValue().getZExtValue());
  return C;
}

//===----------------------------------------------------------------------===//
// EmitFloatingLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitFloatingLiteral(const FloatingLiteral* FL) {
  if (!FL) return nullptr;

  ir::IRType* Ty = mapType(FL->getType());
  ir::IRFloatType* FloatTy = (Ty && Ty->isFloat())
      ? static_cast<ir::IRFloatType*>(Ty)
      : Converter_.getTypeContext().getDoubleTy();

  ir::APFloat APF(FloatTy->getBitWidth() == 32 ? ir::APFloat::Semantics::Float
                                                 : ir::APFloat::Semantics::Double,
                    FL->getValue().convertToDouble());
  auto* C = Converter_.getIRContext().create<ir::IRConstantFP>(FloatTy, APF);
  return C;
}

//===----------------------------------------------------------------------===//
// EmitCharacterLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitCharacterLiteral(const CharacterLiteral* CL) {
  if (!CL) return nullptr;

  ir::IRType* Ty = mapType(CL->getType());
  ir::IRIntegerType* IntTy = (Ty && Ty->isInteger())
      ? static_cast<ir::IRIntegerType*>(Ty)
      : Converter_.getTypeContext().getInt32Ty();

  auto* C = Converter_.getIRContext().create<ir::IRConstantInt>(IntTy, CL->getValue());
  return C;
}

//===----------------------------------------------------------------------===//
// EmitBoolLiteral
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::EmitBoolLiteral(const CXXBoolLiteral* BLE) {
  if (!BLE) return nullptr;
  return getBuilder().getInt1(BLE->getValue());
}

//===----------------------------------------------------------------------===//
// Short-Circuit Evaluation (&& / ||)
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::emitShortCircuitEval(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  ir::IRValue* LHS = Emit(BO->getLHS());
  ir::IRValue* RHS = Emit(BO->getRHS());
  if (!LHS || !RHS)
    return emitErrorPlaceholder(Converter_.getTypeContext().getInt1Ty());

  if (BO->getOpcode() == BinaryOpKind::LAnd) {
    ir::IRValue* FalseVal = getBuilder().getInt1(false);
    return getBuilder().createSelect(LHS, RHS, FalseVal, "and");
  } else {
    ir::IRValue* TrueVal = getBuilder().getInt1(true);
    return getBuilder().createSelect(LHS, TrueVal, RHS, "or");
  }
}

//===----------------------------------------------------------------------===//
// emitAssignment
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::emitAssignment(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  ir::IRValue* LHSPtr = Emit(BO->getLHS());
  ir::IRValue* RHSVal = Emit(BO->getRHS());
  if (!LHSPtr || !RHSVal)
    return emitErrorPlaceholder(mapType(BO->getType()));

  getBuilder().createStore(RHSVal, LHSPtr);
  return RHSVal;
}

//===----------------------------------------------------------------------===//
// emitCompoundAssignment
//===----------------------------------------------------------------------===//

ir::IRValue* IREmitExpr::emitCompoundAssignment(const BinaryOperator* BO) {
  if (!BO) return nullptr;

  ir::IRValue* LHSPtr = Emit(BO->getLHS());

  ir::IRType* LoadTy = LHSPtr->getType();
  if (LoadTy->isPointer()) {
    auto* PtrTy = static_cast<ir::IRPointerType*>(LoadTy);
    LoadTy = PtrTy->getPointeeType();
  }
  ir::IRValue* LHSVal = getBuilder().createLoad(LoadTy, LHSPtr, "lhs.load");

  ir::IRValue* RHSVal = Emit(BO->getRHS());
  if (!LHSVal || !RHSVal)
    return emitErrorPlaceholder(mapType(BO->getType()));

  ir::IRValue* Result = nullptr;
  switch (BO->getOpcode()) {
    case BinaryOpKind::AddAssign:  Result = getBuilder().createAdd(LHSVal, RHSVal); break;
    case BinaryOpKind::SubAssign:  Result = getBuilder().createSub(LHSVal, RHSVal); break;
    case BinaryOpKind::MulAssign:  Result = getBuilder().createMul(LHSVal, RHSVal); break;
    case BinaryOpKind::DivAssign:
      Result = isSignedType(BO->getLHS()->getType())
                   ? getBuilder().createSDiv(LHSVal, RHSVal)
                   : getBuilder().createUDiv(LHSVal, RHSVal);
      break;
    case BinaryOpKind::RemAssign:
      Result = isSignedType(BO->getLHS()->getType())
                   ? getBuilder().createSRem(LHSVal, RHSVal)
                   : getBuilder().createURem(LHSVal, RHSVal);
      break;
    case BinaryOpKind::ShlAssign:  Result = getBuilder().createShl(LHSVal, RHSVal); break;
    case BinaryOpKind::ShrAssign:
      Result = isSignedType(BO->getLHS()->getType())
                   ? getBuilder().createAShr(LHSVal, RHSVal)
                   : getBuilder().createLShr(LHSVal, RHSVal);
      break;
    case BinaryOpKind::AndAssign:  Result = getBuilder().createAnd(LHSVal, RHSVal); break;
    case BinaryOpKind::OrAssign:   Result = getBuilder().createOr(LHSVal, RHSVal); break;
    case BinaryOpKind::XorAssign:  Result = getBuilder().createXor(LHSVal, RHSVal); break;
    default: return emitErrorPlaceholder(mapType(BO->getType()));
  }

  getBuilder().createStore(Result, LHSPtr);
  return Result;
}

} // namespace frontend
} // namespace blocktype

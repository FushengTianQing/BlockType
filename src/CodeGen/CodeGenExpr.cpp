//===--- CodeGenExpr.cpp - Expression Code Generation ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CGCXX.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CodeGenConstant.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Task 6.2.2 — 算术与逻辑表达式
//===----------------------------------------------------------------------===//

/// 判断类型是否为有符号整数
static bool isSignedType(QualType Type) {
  if (Type.isNull()) {
    return true;
  }
  if (auto *Builtin = llvm::dyn_cast<BuiltinType>(Type.getTypePtr())) {
    return Builtin->isSignedInteger();
  }
  return true; // 默认有符号
}

/// 整数比较 → i1
llvm::Value *emitIntegerComparison(llvm::Value *LeftHandSide,
                                    llvm::Value *RightHandSide,
                                    BinaryOpKind Opcode, bool IsSigned,
                                    llvm::IRBuilder<> &Builder) {
  llvm::CmpInst::Predicate Predicate = llvm::CmpInst::ICMP_EQ;
  switch (Opcode) {
  case BinaryOpKind::LT:
    Predicate =
        IsSigned ? llvm::CmpInst::ICMP_SLT : llvm::CmpInst::ICMP_ULT;
    break;
  case BinaryOpKind::GT:
    Predicate =
        IsSigned ? llvm::CmpInst::ICMP_SGT : llvm::CmpInst::ICMP_UGT;
    break;
  case BinaryOpKind::LE:
    Predicate =
        IsSigned ? llvm::CmpInst::ICMP_SLE : llvm::CmpInst::ICMP_ULE;
    break;
  case BinaryOpKind::GE:
    Predicate =
        IsSigned ? llvm::CmpInst::ICMP_SGE : llvm::CmpInst::ICMP_UGE;
    break;
  case BinaryOpKind::EQ:
    Predicate = llvm::CmpInst::ICMP_EQ;
    break;
  case BinaryOpKind::NE:
    Predicate = llvm::CmpInst::ICMP_NE;
    break;
  default:
    break;
  }
  return Builder.CreateICmp(Predicate, LeftHandSide, RightHandSide, "icmp");
}

/// 浮点比较 → i1
llvm::Value *emitFloatComparison(llvm::Value *LeftHandSide,
                                  llvm::Value *RightHandSide,
                                  BinaryOpKind Opcode, llvm::IRBuilder<> &Builder) {
  llvm::CmpInst::Predicate Predicate = llvm::CmpInst::FCMP_OEQ;
  switch (Opcode) {
  case BinaryOpKind::LT:
    Predicate = llvm::CmpInst::FCMP_OLT;
    break;
  case BinaryOpKind::GT:
    Predicate = llvm::CmpInst::FCMP_OGT;
    break;
  case BinaryOpKind::LE:
    Predicate = llvm::CmpInst::FCMP_OLE;
    break;
  case BinaryOpKind::GE:
    Predicate = llvm::CmpInst::FCMP_OGE;
    break;
  case BinaryOpKind::EQ:
    Predicate = llvm::CmpInst::FCMP_OEQ;
    break;
  case BinaryOpKind::NE:
    Predicate = llvm::CmpInst::FCMP_ONE;
    break;
  default:
    break;
  }
  return Builder.CreateFCmp(Predicate, LeftHandSide, RightHandSide, "fcmp");
}

/// 短路逻辑与 (&&)
llvm::Value *CodeGenFunction::EmitLogicalAnd(BinaryOperator *BinaryOp) {
  llvm::Value *LeftHandSide = EmitExpr(BinaryOp->getLHS());
  if (!LeftHandSide) {
    return nullptr;
  }

  llvm::BasicBlock *AndBB = createBasicBlock("and.rhs");
  llvm::BasicBlock *MergeBB = createBasicBlock("and.end");

  // 保存 LHS 所在的基本块（在 EmitConversionToBool 之前）
  llvm::BasicBlock *LHBB = getCurrentBlock();

  // 转换为 i1
  LeftHandSide = EmitConversionToBool(LeftHandSide,
                                       BinaryOp->getLHS()->getType());

  Builder.CreateCondBr(LeftHandSide, AndBB, MergeBB);

  // RHS
  EmitBlock(AndBB);
  llvm::Value *RightHandSide = EmitExpr(BinaryOp->getRHS());
  if (!RightHandSide) {
    RightHandSide = llvm::ConstantInt::getFalse(CGM.getLLVMContext());
  }
  RightHandSide = EmitConversionToBool(RightHandSide,
                                         BinaryOp->getRHS()->getType());

  Builder.CreateBr(MergeBB);

  // Merge: phi
  EmitBlock(MergeBB);
  llvm::PHINode *PhiNode = Builder.CreatePHI(llvm::Type::getInt1Ty(CGM.getLLVMContext()),
                                              2, "and.result");
  PhiNode->addIncoming(llvm::ConstantInt::getFalse(CGM.getLLVMContext()),
                       LHBB);
  PhiNode->addIncoming(RightHandSide, AndBB);
  return PhiNode;
}

/// 短路逻辑或 (||)
llvm::Value *CodeGenFunction::EmitLogicalOr(BinaryOperator *BinaryOp) {
  llvm::Value *LeftHandSide = EmitExpr(BinaryOp->getLHS());
  if (!LeftHandSide) {
    return nullptr;
  }

  llvm::BasicBlock *OrBB = createBasicBlock("or.rhs");
  llvm::BasicBlock *MergeBB = createBasicBlock("or.end");

  // 保存 LHS 所在的基本块（在 EmitConversionToBool 之前）
  llvm::BasicBlock *LHBB = getCurrentBlock();

  LeftHandSide = EmitConversionToBool(LeftHandSide,
                                       BinaryOp->getLHS()->getType());

  Builder.CreateCondBr(LeftHandSide, MergeBB, OrBB);

  // RHS
  EmitBlock(OrBB);
  llvm::Value *RightHandSide = EmitExpr(BinaryOp->getRHS());
  if (!RightHandSide) {
    RightHandSide = llvm::ConstantInt::getFalse(CGM.getLLVMContext());
  }
  RightHandSide = EmitConversionToBool(RightHandSide,
                                         BinaryOp->getRHS()->getType());

  Builder.CreateBr(MergeBB);

  // Merge: phi
  EmitBlock(MergeBB);
  llvm::PHINode *PhiNode = Builder.CreatePHI(llvm::Type::getInt1Ty(CGM.getLLVMContext()),
                                              2, "or.result");
  PhiNode->addIncoming(llvm::ConstantInt::getTrue(CGM.getLLVMContext()),
                       LHBB);
  PhiNode->addIncoming(RightHandSide, OrBB);
  return PhiNode;
}

/// 赋值运算
llvm::Value *CodeGenFunction::EmitAssignment(BinaryOperator *BinaryOp) {
  Expr *LeftHandExpr = BinaryOp->getLHS();
  Expr *RightHandExpr = BinaryOp->getRHS();

  llvm::Value *RightValue = EmitExpr(RightHandExpr);
  if (!RightValue) {
    return nullptr;
  }

  // LHS 是 lvalue: 获取地址
  if (auto *DeclRef = llvm::dyn_cast<DeclRefExpr>(LeftHandExpr)) {
    if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(DeclRef->getDecl())) {
      if (auto *Alloca = getLocalDecl(VariableDecl)) {
        Builder.CreateStore(RightValue, Alloca);
        return RightValue;
      }
      // 全局变量
      if (auto *GlobalVariable = CGM.GetGlobalVar(VariableDecl)) {
        Builder.CreateStore(RightValue, GlobalVariable);
        return RightValue;
      }
    }
  }

  // 成员赋值
  if (auto *MemberExpression = llvm::dyn_cast<MemberExpr>(LeftHandExpr)) {
    llvm::Value *Address = EmitLValue(MemberExpression);
    if (Address) {
      Builder.CreateStore(RightValue, Address);
      return RightValue;
    }
  }

  // 数组下标赋值
  if (auto *ArraySubscript = llvm::dyn_cast<ArraySubscriptExpr>(LeftHandExpr)) {
    llvm::Value *Address = EmitLValue(ArraySubscript);
    if (Address) {
      Builder.CreateStore(RightValue, Address);
      return RightValue;
    }
  }

  return RightValue;
}

/// 复合赋值运算
llvm::Value *CodeGenFunction::EmitCompoundAssignment(BinaryOperator *BinaryOp) {
  // 先计算 load + op + store
  BinaryOpKind BaseOpcode;
  switch (BinaryOp->getOpcode()) {
  case BinaryOpKind::AddAssign:    BaseOpcode = BinaryOpKind::Add; break;
  case BinaryOpKind::SubAssign:    BaseOpcode = BinaryOpKind::Sub; break;
  case BinaryOpKind::MulAssign:    BaseOpcode = BinaryOpKind::Mul; break;
  case BinaryOpKind::DivAssign:    BaseOpcode = BinaryOpKind::Div; break;
  case BinaryOpKind::RemAssign:    BaseOpcode = BinaryOpKind::Rem; break;
  case BinaryOpKind::ShlAssign:    BaseOpcode = BinaryOpKind::Shl; break;
  case BinaryOpKind::ShrAssign:    BaseOpcode = BinaryOpKind::Shr; break;
  case BinaryOpKind::AndAssign:    BaseOpcode = BinaryOpKind::And; break;
  case BinaryOpKind::OrAssign:     BaseOpcode = BinaryOpKind::Or; break;
  case BinaryOpKind::XorAssign:    BaseOpcode = BinaryOpKind::Xor; break;
  default: return nullptr;
  }

  // 简化：EmitExpr(LHS) + EmitExpr(RHS) → op → store back
  llvm::Value *LeftValue = EmitExpr(BinaryOp->getLHS());
  llvm::Value *RightValue = EmitExpr(BinaryOp->getRHS());
  if (!LeftValue || !RightValue) {
    return nullptr;
  }

  QualType LHSType = BinaryOp->getLHS()->getType();
  QualType RHSType = BinaryOp->getRHS()->getType();
  QualType ResultType = LHSType;

  // 类型提升: 将 RHS 转换到 LHS 类型，确保运算在相同类型下进行
  // 例如 short += int: RHS(int) 先提升到公共类型，运算后截断回 short
  if (LeftValue->getType()->isIntegerTy() && RightValue->getType()->isIntegerTy()) {
    if (RightValue->getType() != LeftValue->getType()) {
      RightValue = EmitScalarConversion(RightValue, RHSType, LHSType);
    }
  } else if (LeftValue->getType()->isFloatingPointTy() &&
             RightValue->getType()->isFloatingPointTy()) {
    if (RightValue->getType() != LeftValue->getType()) {
      RightValue = EmitScalarConversion(RightValue, RHSType, LHSType);
    }
  }

  bool IsSigned = isSignedType(ResultType);
  llvm::Value *Result = nullptr;

  if (ResultType->isIntegerType()) {
    switch (BaseOpcode) {
    case BinaryOpKind::Add: Result = Builder.CreateAdd(LeftValue, RightValue, "add"); break;
    case BinaryOpKind::Sub: Result = Builder.CreateSub(LeftValue, RightValue, "sub"); break;
    case BinaryOpKind::Mul: Result = Builder.CreateMul(LeftValue, RightValue, "mul"); break;
    case BinaryOpKind::Div:
      Result = IsSigned ? Builder.CreateSDiv(LeftValue, RightValue, "sdiv")
                        : Builder.CreateUDiv(LeftValue, RightValue, "udiv");
      break;
    case BinaryOpKind::Rem:
      Result = IsSigned ? Builder.CreateSRem(LeftValue, RightValue, "srem")
                        : Builder.CreateURem(LeftValue, RightValue, "urem");
      break;
    case BinaryOpKind::Shl: Result = Builder.CreateShl(LeftValue, RightValue, "shl"); break;
    case BinaryOpKind::Shr:
      Result = IsSigned ? Builder.CreateAShr(LeftValue, RightValue, "ashr")
                        : Builder.CreateLShr(LeftValue, RightValue, "lshr");
      break;
    case BinaryOpKind::And: Result = Builder.CreateAnd(LeftValue, RightValue, "and"); break;
    case BinaryOpKind::Or:  Result = Builder.CreateOr(LeftValue, RightValue, "or"); break;
    case BinaryOpKind::Xor: Result = Builder.CreateXor(LeftValue, RightValue, "xor"); break;
    default: break;
    }
  } else if (ResultType->isFloatingType()) {
    switch (BaseOpcode) {
    case BinaryOpKind::Add: Result = Builder.CreateFAdd(LeftValue, RightValue, "fadd"); break;
    case BinaryOpKind::Sub: Result = Builder.CreateFSub(LeftValue, RightValue, "fsub"); break;
    case BinaryOpKind::Mul: Result = Builder.CreateFMul(LeftValue, RightValue, "fmul"); break;
    case BinaryOpKind::Div: Result = Builder.CreateFDiv(LeftValue, RightValue, "fdiv"); break;
    case BinaryOpKind::Rem: Result = Builder.CreateFRem(LeftValue, RightValue, "frem"); break;
    default: break;
    }
  }

  if (!Result) {
    return nullptr;
  }

  // 存储回 LHS（支持局部变量和全局变量）
  if (auto *DeclRef = llvm::dyn_cast<DeclRefExpr>(BinaryOp->getLHS())) {
    if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(DeclRef->getDecl())) {
      if (getLocalDecl(VariableDecl)) {
        StoreLocalVar(VariableDecl, Result);
      } else if (auto *GlobalVariable = CGM.GetGlobalVar(VariableDecl)) {
        Builder.CreateStore(Result, GlobalVariable);
      }
    }
  } else {
    // 成员或数组下标的复合赋值
    llvm::Value *Address = EmitLValue(BinaryOp->getLHS());
    if (Address) {
      Builder.CreateStore(Result, Address);
    }
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// BinaryOperator 主入口
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitBinaryOperator(BinaryOperator *BinaryOp) {
  if (!BinaryOp) {
    return nullptr;
  }

  BinaryOpKind Opcode = BinaryOp->getOpcode();

  // 短路逻辑
  if (Opcode == BinaryOpKind::LAnd) {
    return EmitLogicalAnd(BinaryOp);
  }
  if (Opcode == BinaryOpKind::LOr) {
    return EmitLogicalOr(BinaryOp);
  }

  // 赋值
  if (Opcode == BinaryOpKind::Assign) {
    return EmitAssignment(BinaryOp);
  }

  // 复合赋值
  if (Opcode == BinaryOpKind::AddAssign || Opcode == BinaryOpKind::SubAssign ||
      Opcode == BinaryOpKind::MulAssign || Opcode == BinaryOpKind::DivAssign ||
      Opcode == BinaryOpKind::RemAssign || Opcode == BinaryOpKind::ShlAssign ||
      Opcode == BinaryOpKind::ShrAssign || Opcode == BinaryOpKind::AndAssign ||
      Opcode == BinaryOpKind::OrAssign || Opcode == BinaryOpKind::XorAssign) {
    return EmitCompoundAssignment(BinaryOp);
  }

  // 逗号运算符
  if (Opcode == BinaryOpKind::Comma) {
    EmitExpr(BinaryOp->getLHS());
    return EmitExpr(BinaryOp->getRHS());
  }

  llvm::Value *LeftHandSide = EmitExpr(BinaryOp->getLHS());
  llvm::Value *RightHandSide = EmitExpr(BinaryOp->getRHS());
  if (!LeftHandSide || !RightHandSide) {
    return nullptr;
  }

  QualType ResultType = BinaryOp->getType();
  bool IsSigned = isSignedType(ResultType);

  // 整数/浮点提升: 使用 Sema 推导的公共类型 (BinaryOp->getType())
  // 将两操作数转换到公共类型，覆盖 short+long→long 等场景
  QualType LHSType = BinaryOp->getLHS()->getType();
  QualType RHSType = BinaryOp->getRHS()->getType();
  if (LeftHandSide->getType()->isIntegerTy() &&
      RightHandSide->getType()->isIntegerTy()) {
    llvm::Type *ResultLLVMTy = CGM.getTypes().ConvertType(ResultType);
    if (ResultLLVMTy) {
      if (LeftHandSide->getType() != ResultLLVMTy)
        LeftHandSide = EmitScalarConversion(LeftHandSide, LHSType, ResultType);
      if (RightHandSide->getType() != ResultLLVMTy)
        RightHandSide = EmitScalarConversion(RightHandSide, RHSType, ResultType);
    }
  } else if (LeftHandSide->getType()->isFloatingPointTy() &&
             RightHandSide->getType()->isFloatingPointTy()) {
    llvm::Type *ResultLLVMTy = CGM.getTypes().ConvertType(ResultType);
    if (ResultLLVMTy) {
      if (LeftHandSide->getType() != ResultLLVMTy)
        LeftHandSide = EmitScalarConversion(LeftHandSide, LHSType, ResultType);
      if (RightHandSide->getType() != ResultLLVMTy)
        RightHandSide = EmitScalarConversion(RightHandSide, RHSType, ResultType);
    }
  }

  // 指针运算: pointer ± integer → GEP
  if (LeftHandSide->getType()->isPointerTy() &&
      RightHandSide->getType()->isIntegerTy()) {
    // 获取指针指向的元素类型
    QualType PointeeType;
    if (auto *PtrType = llvm::dyn_cast<PointerType>(
            BinaryOp->getLHS()->getType().getTypePtr())) {
      PointeeType = QualType(PtrType->getPointeeType(), Qualifier::None);
    }
    llvm::Type *PointeeLLVMType = PointeeType.isNull()
                                      ? llvm::Type::getInt8Ty(CGM.getLLVMContext())
                                      : CGM.getTypes().ConvertType(PointeeType);
    if (Opcode == BinaryOpKind::Add) {
      return Builder.CreateGEP(PointeeLLVMType, LeftHandSide,
                               RightHandSide, "ptradd");
    }
    if (Opcode == BinaryOpKind::Sub) {
      auto *NegativeOffset = Builder.CreateNeg(RightHandSide, "neg");
      return Builder.CreateGEP(PointeeLLVMType, LeftHandSide,
                               NegativeOffset, "ptrsub");
    }
  }

  // 指针差值: pointer - pointer → ptrdiff_t
  if (LeftHandSide->getType()->isPointerTy() &&
      RightHandSide->getType()->isPointerTy() &&
      Opcode == BinaryOpKind::Sub) {
    llvm::Type *PtrDiffTy = llvm::Type::getInt64Ty(CGM.getLLVMContext());
    llvm::Value *LHSInt = Builder.CreatePtrToInt(LeftHandSide, PtrDiffTy, "pdiff.l");
    llvm::Value *RHSInt = Builder.CreatePtrToInt(RightHandSide, PtrDiffTy, "pdiff.r");
    llvm::Value *ByteDiff = Builder.CreateSub(LHSInt, RHSInt, "pdiff.bytes");
    // 除以元素大小得到元素个数差
    QualType PointeeType;
    if (auto *PtrType = llvm::dyn_cast<PointerType>(
            BinaryOp->getLHS()->getType().getTypePtr())) {
      PointeeType = QualType(PtrType->getPointeeType(), Qualifier::None);
    }
    if (!PointeeType.isNull()) {
      uint64_t ElemSize = CGM.getTarget().getTypeSize(PointeeType);
      if (ElemSize > 1) {
        llvm::Value *ElemSizeVal = llvm::ConstantInt::get(PtrDiffTy, ElemSize);
        return Builder.CreateSDiv(ByteDiff, ElemSizeVal, "pdiff");
      }
    }
    return ByteDiff;
  }

  if (ResultType->isIntegerType()) {
    switch (Opcode) {
    case BinaryOpKind::Add:
      return Builder.CreateAdd(LeftHandSide, RightHandSide, "add");
    case BinaryOpKind::Sub:
      return Builder.CreateSub(LeftHandSide, RightHandSide, "sub");
    case BinaryOpKind::Mul:
      return Builder.CreateMul(LeftHandSide, RightHandSide, "mul");
    case BinaryOpKind::Div:
      return IsSigned ? Builder.CreateSDiv(LeftHandSide, RightHandSide, "sdiv")
                      : Builder.CreateUDiv(LeftHandSide, RightHandSide, "udiv");
    case BinaryOpKind::Rem:
      return IsSigned ? Builder.CreateSRem(LeftHandSide, RightHandSide, "srem")
                      : Builder.CreateURem(LeftHandSide, RightHandSide, "urem");
    case BinaryOpKind::Shl:
      return Builder.CreateShl(LeftHandSide, RightHandSide, "shl");
    case BinaryOpKind::Shr:
      return IsSigned ? Builder.CreateAShr(LeftHandSide, RightHandSide, "ashr")
                      : Builder.CreateLShr(LeftHandSide, RightHandSide, "lshr");
    case BinaryOpKind::And:
      return Builder.CreateAnd(LeftHandSide, RightHandSide, "and");
    case BinaryOpKind::Or:
      return Builder.CreateOr(LeftHandSide, RightHandSide, "or");
    case BinaryOpKind::Xor:
      return Builder.CreateXor(LeftHandSide, RightHandSide, "xor");
    case BinaryOpKind::LT:
    case BinaryOpKind::GT:
    case BinaryOpKind::LE:
    case BinaryOpKind::GE:
    case BinaryOpKind::EQ:
    case BinaryOpKind::NE:
      return emitIntegerComparison(LeftHandSide, RightHandSide, Opcode,
                                    IsSigned, Builder);
    case BinaryOpKind::Spaceship: {
      // <=> 三路比较: (LHS > RHS) - (LHS < RHS) → -1, 0, or 1
      auto *GT = Builder.CreateICmp(
          IsSigned ? llvm::CmpInst::ICMP_SGT : llvm::CmpInst::ICMP_UGT,
          LeftHandSide, RightHandSide, "scmp.gt");
      auto *LT = Builder.CreateICmp(
          IsSigned ? llvm::CmpInst::ICMP_SLT : llvm::CmpInst::ICMP_ULT,
          LeftHandSide, RightHandSide, "scmp.lt");
      auto *GTVal = Builder.CreateZExt(GT, LeftHandSide->getType(), "scmp.gtv");
      auto *LTVal = Builder.CreateZExt(LT, LeftHandSide->getType(), "scmp.ltv");
      return Builder.CreateSub(GTVal, LTVal, "spaceship");
    }
    default:
      break;
    }
  } else if (ResultType->isFloatingType()) {
    switch (Opcode) {
    case BinaryOpKind::Add:
      return Builder.CreateFAdd(LeftHandSide, RightHandSide, "fadd");
    case BinaryOpKind::Sub:
      return Builder.CreateFSub(LeftHandSide, RightHandSide, "fsub");
    case BinaryOpKind::Mul:
      return Builder.CreateFMul(LeftHandSide, RightHandSide, "fmul");
    case BinaryOpKind::Div:
      return Builder.CreateFDiv(LeftHandSide, RightHandSide, "fdiv");
    case BinaryOpKind::Rem:
      return Builder.CreateFRem(LeftHandSide, RightHandSide, "frem");
    case BinaryOpKind::LT:
    case BinaryOpKind::GT:
    case BinaryOpKind::LE:
    case BinaryOpKind::GE:
    case BinaryOpKind::EQ:
    case BinaryOpKind::NE:
      return emitFloatComparison(LeftHandSide, RightHandSide, Opcode, Builder);
    case BinaryOpKind::Spaceship: {
      // <=> 三路比较 (浮点): (LHS > RHS) - (LHS < RHS) → int -1, 0, or 1
      auto *GT = Builder.CreateFCmp(llvm::CmpInst::FCMP_OGT,
                                     LeftHandSide, RightHandSide, "scmp.gt");
      auto *LT = Builder.CreateFCmp(llvm::CmpInst::FCMP_OLT,
                                     LeftHandSide, RightHandSide, "scmp.lt");
      llvm::Type *IntTy = CGM.getTypes().ConvertType(ResultType);
      auto *GTVal = Builder.CreateZExt(GT, IntTy, "scmp.gtv");
      auto *LTVal = Builder.CreateZExt(LT, IntTy, "scmp.ltv");
      return Builder.CreateSub(GTVal, LTVal, "spaceship");
    }
    default:
      break;
    }
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// sizeof/alignof expression
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitUnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTraitExpr *E) {
  if (!E) return nullptr;

  QualType ArgTy = E->getTypeOfArgument();
  if (ArgTy.isNull()) return nullptr;

  uint64_t Val = 0;
  if (E->getTraitKind() == UnaryExprOrTypeTrait::SizeOf) {
    Val = CGM.getTarget().getTypeSize(ArgTy);
  } else {
    Val = CGM.getTarget().getTypeAlign(ArgTy);
  }

  // sizeof/alignof always evaluates to a compile-time constant (i64)
  return llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), Val);
}

//===----------------------------------------------------------------------===//
// UnaryOperator
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitUnaryOperator(UnaryOperator *UnaryOp) {
  if (!UnaryOp) {
    return nullptr;
  }

  switch (UnaryOp->getOpcode()) {
  case UnaryOpKind::Minus: {
    llvm::Value *Operand = EmitExpr(UnaryOp->getSubExpr());
    if (!Operand) {
      return nullptr;
    }
    // Sema 已对 unary minus 做 integral promotion，UnaryOp->getType() 是提升后类型
    // 先将操作数提升到目标类型，再取反
    QualType SrcType = UnaryOp->getSubExpr()->getType();
    QualType DstType = UnaryOp->getType();
    if (Operand->getType()->isIntegerTy()) {
      Operand = EmitScalarConversion(Operand, SrcType, DstType);
      return Builder.CreateNeg(Operand, "neg");
    }
    if (Operand->getType()->isFloatingPointTy()) {
      return Builder.CreateFNeg(Operand, "fneg");
    }
    return Operand;
  }
  case UnaryOpKind::Plus:
    return EmitExpr(UnaryOp->getSubExpr());
  case UnaryOpKind::Not: {
    llvm::Value *Operand = EmitExpr(UnaryOp->getSubExpr());
    if (!Operand) {
      return nullptr;
    }
    // Sema 已对 ~ 做 integral promotion，UnaryOp->getType() 是提升后类型
    QualType SrcType = UnaryOp->getSubExpr()->getType();
    QualType DstType = UnaryOp->getType();
    if (Operand->getType()->isIntegerTy()) {
      Operand = EmitScalarConversion(Operand, SrcType, DstType);
    }
    return Builder.CreateNot(Operand, "not");
  }
  case UnaryOpKind::LNot: {
    llvm::Value *Operand = EmitExpr(UnaryOp->getSubExpr());
    if (!Operand) {
      return nullptr;
    }
    return Builder.CreateICmpEQ(
        Operand, llvm::Constant::getNullValue(Operand->getType()), "lnot");
  }
  case UnaryOpKind::Deref: {
    llvm::Value *Operand = EmitExpr(UnaryOp->getSubExpr());
    if (!Operand) {
      return nullptr;
    }
    llvm::Type *LoadType =
        CGM.getTypes().ConvertType(UnaryOp->getType());
    return Builder.CreateLoad(LoadType, Operand, "deref");
  }
  case UnaryOpKind::AddrOf: {
    // 返回 lvalue 的地址
    return EmitLValue(UnaryOp->getSubExpr());
  }
  case UnaryOpKind::PreInc:
  case UnaryOpKind::PreDec:
  case UnaryOpKind::PostInc:
  case UnaryOpKind::PostDec:
    return EmitIncDec(UnaryOp);
  default:
    return nullptr;
  }
}

/// ++/-- 运算
llvm::Value *CodeGenFunction::EmitIncDec(UnaryOperator *UnaryOp) {
  Expr *SubExpression = UnaryOp->getSubExpr();
  llvm::Value *OldValue = EmitExpr(SubExpression);
  if (!OldValue) {
    return nullptr;
  }

  bool IsIncrement =
      (UnaryOp->getOpcode() == UnaryOpKind::PreInc ||
       UnaryOp->getOpcode() == UnaryOpKind::PostInc);
  bool IsPrefix =
      (UnaryOp->getOpcode() == UnaryOpKind::PreInc ||
       UnaryOp->getOpcode() == UnaryOpKind::PreDec);

  llvm::Value *One = llvm::ConstantInt::get(OldValue->getType(), 1);
  llvm::Value *NewValue =
      IsIncrement ? Builder.CreateAdd(OldValue, One, "inc")
                  : Builder.CreateSub(OldValue, One, "dec");

  // 存储回变量（支持局部变量、全局变量和成员）
  if (auto *DeclRef = llvm::dyn_cast<DeclRefExpr>(SubExpression)) {
    if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(DeclRef->getDecl())) {
      if (getLocalDecl(VariableDecl)) {
        StoreLocalVar(VariableDecl, NewValue);
      } else if (auto *GlobalVariable = CGM.GetGlobalVar(VariableDecl)) {
        Builder.CreateStore(NewValue, GlobalVariable);
      }
    }
  } else {
    // 成员或数组下标：使用 EmitLValue 获取地址
    llvm::Value *Address = EmitLValue(SubExpression);
    if (Address) {
      Builder.CreateStore(NewValue, Address);
    }
  }

  return IsPrefix ? NewValue : OldValue;
}

//===----------------------------------------------------------------------===//
// Task 6.2.3 — 函数调用
//===----------------------------------------------------------------------===//

llvm::Value *
CodeGenFunction::EmitScalarConversion(llvm::Value *Src, QualType SrcType,
                                      QualType DstType) {
  if (!Src || SrcType.isNull() || DstType.isNull()) {
    return Src;
  }

  llvm::Type *DstLLVMTy = CGM.getTypes().ConvertType(DstType);
  if (!DstLLVMTy) {
    return Src;
  }

  // 类型相同，无需转换
  if (Src->getType() == DstLLVMTy) {
    return Src;
  }

  // 整数 → 整数
  if (Src->getType()->isIntegerTy() && DstLLVMTy->isIntegerTy()) {
    return Builder.CreateIntCast(Src, DstLLVMTy, isSignedType(SrcType),
                                 "argconv");
  }

  // 浮点 → 浮点
  if (Src->getType()->isFloatingPointTy() && DstLLVMTy->isFloatingPointTy()) {
    return Builder.CreateFPCast(Src, DstLLVMTy, "argconv");
  }

  // 整数 → 浮点
  if (Src->getType()->isIntegerTy() && DstLLVMTy->isFloatingPointTy()) {
    if (isSignedType(SrcType)) {
      return Builder.CreateSIToFP(Src, DstLLVMTy, "argconv");
    }
    return Builder.CreateUIToFP(Src, DstLLVMTy, "argconv");
  }

  // 浮点 → 整数
  if (Src->getType()->isFloatingPointTy() && DstLLVMTy->isIntegerTy()) {
    if (isSignedType(DstType)) {
      return Builder.CreateFPToSI(Src, DstLLVMTy, "argconv");
    }
    return Builder.CreateFPToUI(Src, DstLLVMTy, "argconv");
  }

  // 指针 → 指针
  if (Src->getType()->isPointerTy() && DstLLVMTy->isPointerTy()) {
    return Builder.CreateBitCast(Src, DstLLVMTy, "argconv");
  }

  // 其他情况：保守返回原值
  return Src;
}

llvm::Value *
CodeGenFunction::emitDefaultArgPromotion(llvm::Value *Arg, QualType ArgType) {
  if (!Arg || ArgType.isNull()) return Arg;

  // float → double (C ABI default argument promotion)
  if (Arg->getType()->isFloatTy()) {
    return Builder.CreateFPExt(Arg, llvm::Type::getDoubleTy(CGM.getLLVMContext()),
                               "vararg.promote");
  }

  // < 32-bit integer → int (integral promotion to at least int)
  if (Arg->getType()->isIntegerTy() &&
      Arg->getType()->getIntegerBitWidth() < 32) {
    bool IsSigned = isSignedType(ArgType);
    return Builder.CreateIntCast(Arg, llvm::Type::getInt32Ty(CGM.getLLVMContext()),
                                 IsSigned, "vararg.promote");
  }

  return Arg;
}

llvm::Value *CodeGenFunction::EmitCallExpr(CallExpr *CallExpression) {
  if (!CallExpression) {
    return nullptr;
  }

  // 获取被调用函数声明
  Expr *CalleeExpr = CallExpression->getCallee();
  FunctionDecl *CalleeDecl = nullptr;

  if (auto *DeclRef = llvm::dyn_cast<DeclRefExpr>(CalleeExpr)) {
    CalleeDecl = llvm::dyn_cast<FunctionDecl>(DeclRef->getDecl());
  } else if (auto *MemberExpression = llvm::dyn_cast<MemberExpr>(CalleeExpr)) {
    CalleeDecl =
        llvm::dyn_cast<FunctionDecl>(MemberExpression->getMemberDecl());
  }

  if (!CalleeDecl) {
    return nullptr;
  }

  llvm::Function *CalleeFunction = CGM.GetOrCreateFunctionDecl(CalleeDecl);
  if (!CalleeFunction) {
    return nullptr;
  }

  // 生成参数
  llvm::SmallVector<llvm::Value *, 8> Arguments;

  // 成员函数：添加 this 指针
  if (auto *MemberDecl = llvm::dyn_cast<CXXMethodDecl>(CalleeDecl)) {
    // P7.1.1: Deducing this — 有显式对象参数的方法不添加隐式 this。
    // 对象作为第一个显式参数传递（在后面的参数循环中处理）。
    if (!MemberDecl->isStatic() && !MemberDecl->hasExplicitObjectParam()) {
      // 从 callee 表达式获取 this
      if (auto *MemberExpression = llvm::dyn_cast<MemberExpr>(CalleeExpr)) {
        llvm::Value *BaseValue = nullptr;
        if (MemberExpression->isArrow()) {
          // ptr->method() → base 已经是指针
          BaseValue = EmitExpr(MemberExpression->getBase());
        } else {
          // obj.method() → base 是对象值，需要取地址得到 this 指针
          BaseValue = EmitLValue(MemberExpression->getBase());
        }
        if (BaseValue) {
          Arguments.push_back(BaseValue);
        }
      }

      // 虚函数调用：使用 vtable 进行间接调用
      if (MemberDecl->isVirtual() && !Arguments.empty()) {
        // 收集非 this 参数（带隐式类型转换）
        llvm::SmallVector<llvm::Value *, 8> NonThisArgs;
        auto Params = MemberDecl->getParams();
        bool IsVarArg = MemberDecl->isVariadic();
        for (unsigned I = 0; I < CallExpression->getNumArgs(); ++I) {
          Expr *ArgExpr = CallExpression->getArgs()[I];
          llvm::Value *ArgValue = EmitExpr(ArgExpr);
          if (ArgValue && I < Params.size()) {
            ArgValue = EmitScalarConversion(ArgValue, ArgExpr->getType(),
                                            Params[I]->getType());
          } else if (ArgValue && IsVarArg && I >= Params.size()) {
            ArgValue = emitDefaultArgPromotion(ArgValue, ArgExpr->getType());
          }
          if (ArgValue) {
            NonThisArgs.push_back(ArgValue);
          }
        }

        // 获取静态类型（用于多重继承场景确定正确的 vptr 位置）
        CXXRecordDecl *StaticType = nullptr;
        if (auto *ME = llvm::dyn_cast<MemberExpr>(CalleeExpr)) {
          QualType BaseType = ME->getBase()->getType();
          // 去掉指针/引用层
          if (auto *PtrTy = llvm::dyn_cast<PointerType>(BaseType.getTypePtr())) {
            BaseType = PtrTy->getPointeeType();
          } else if (auto *RefTy =
                         llvm::dyn_cast<ReferenceType>(BaseType.getTypePtr())) {
            BaseType = RefTy->getReferencedType();
          }
          if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
            StaticType = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
          }
        }

        // 使用 CGCXX::EmitVirtualCall 生成虚函数调用
        llvm::Value *ThisPtr = Arguments[0];
        llvm::Value *VirtualCallResult =
            CGM.getCXX().EmitVirtualCall(*this, MemberDecl, ThisPtr,
                                          NonThisArgs, StaticType);
        if (VirtualCallResult) {
          return VirtualCallResult;
        }
        // 虚函数调用失败，降级为普通调用
      }
    }
  }

  // 非虚函数参数（带隐式类型转换 + 变参 default argument promotion）
  auto Params = CalleeDecl->getParams();
  auto *CalleeAsMD = llvm::dyn_cast<CXXMethodDecl>(CalleeDecl);
  bool IsNonStaticMember = CalleeAsMD && !CalleeAsMD->isStatic()
                           && !CalleeAsMD->hasExplicitObjectParam();
  unsigned ParamOffset = IsNonStaticMember ? 1 : 0;
  bool IsVarArg = CalleeDecl->isVariadic();

  // 检测被调函数是否需要 sret 返回
  const FunctionABITy *CalleeABI = CGM.getTypes().GetFunctionABI(CalleeDecl);
  bool CalleeNeedsSRet = CalleeABI && CalleeABI->RetInfo.isSRet();

  // sret 缓冲区：如果被调函数需要 sret，在参数列表最前面插入缓冲区指针
  llvm::AllocaInst *SRetAlloca = nullptr;
  if (CalleeNeedsSRet) {
    QualType RetQT;
    QualType FDType = CalleeDecl->getType();
    if (auto *FnTy = llvm::dyn_cast<FunctionType>(FDType.getTypePtr())) {
      RetQT = QualType(FnTy->getReturnType(), Qualifier::None);
    }
    SRetAlloca = CreateAlloca(RetQT, "sret.call");
    Arguments.insert(Arguments.begin(), SRetAlloca);
  }

  // P7.1.1: Deducing this — 有显式对象参数时，对象作为第一个显式参数传递。
  if (CalleeAsMD && CalleeAsMD->hasExplicitObjectParam()) {
    if (auto *ME = llvm::dyn_cast<MemberExpr>(CalleeExpr)) {
      llvm::Value *ObjValue = nullptr;
      ParmVarDecl *ExplicitParam = CalleeAsMD->getExplicitObjectParam();
      QualType ParamType = ExplicitParam ? ExplicitParam->getType() : QualType();

      // 根据参数类型决定如何传递对象：
      // - 引用类型：传地址（lvalue）
      // - 值类型：传值（rvalue）
      bool PassByReference = !ParamType.isNull() && ParamType.isReferenceType();

      if (ME->isArrow()) {
        // ptr->method() — base 已经是指针
        ObjValue = EmitExpr(ME->getBase());
        // 对于引用参数，指针可以直接使用
        // 对于值参数，需要 load
        if (ObjValue && !PassByReference) {
          llvm::Type *PointeeTy = CGM.getTypes().ConvertType(
              QualType(llvm::cast<PointerType>(ME->getBase()->getType().getTypePtr())
                           ->getPointeeType(),
                       Qualifier::None));
          if (PointeeTy)
            ObjValue = Builder.CreateLoad(PointeeTy, ObjValue, "obj.val");
        }
      } else {
        // obj.method() — base 是对象
        if (PassByReference) {
          ObjValue = EmitLValue(ME->getBase());
        } else {
          ObjValue = EmitExpr(ME->getBase());
        }
      }
      if (ObjValue)
        Arguments.push_back(ObjValue);
    }
  }

  for (unsigned I = 0; I < CallExpression->getNumArgs(); ++I) {
    Expr *ArgExpr = CallExpression->getArgs()[I];
    llvm::Value *ArgValue = EmitExpr(ArgExpr);
    if (ArgValue && I < Params.size()) {
      ArgValue = EmitScalarConversion(ArgValue, ArgExpr->getType(),
                                      Params[I]->getType());
    } else if (ArgValue && IsVarArg && I >= Params.size()) {
      ArgValue = emitDefaultArgPromotion(ArgValue, ArgExpr->getType());
    }
    if (ArgValue) {
      Arguments.push_back(ArgValue);
    }
  }

  // 在 try 块中：生成 invoke 指令以支持异常传播到 landingpad
  if (isInTryBlock()) {
    const auto &InvokeTarget = getCurrentInvokeTarget();
    auto *Invoke = Builder.CreateInvoke(CalleeFunction, InvokeTarget.NormalBB,
                               InvokeTarget.UnwindBB, Arguments, "invoke");
    if (CalleeFunction->doesNotReturn()) {
      Invoke->setDoesNotReturn();
    }
    // sret: 从缓冲区 load 返回值
    if (CalleeNeedsSRet && SRetAlloca) {
      llvm::Type *RetTy = CGM.getTypes().ConvertType(
          QualType(llvm::cast<FunctionType>(CalleeDecl->getType().getTypePtr())
                       ->getReturnType(),
                   Qualifier::None));
      return Builder.CreateLoad(RetTy, SRetAlloca, "sret.val");
    }
    return Invoke;
  }

  auto *Call = Builder.CreateCall(CalleeFunction, Arguments, "call");
  if (CalleeFunction->doesNotReturn()) {
    Call->setDoesNotReturn();
  }
  // sret: 从缓冲区 load 返回值
  if (CalleeNeedsSRet && SRetAlloca) {
    llvm::Type *RetTy = CGM.getTypes().ConvertType(
        QualType(llvm::cast<FunctionType>(CalleeDecl->getType().getTypePtr())
                     ->getReturnType(),
                 Qualifier::None));
    return Builder.CreateLoad(RetTy, SRetAlloca, "sret.val");
  }
  return Call;
}

//===----------------------------------------------------------------------===//
// Task 6.2.4 — 成员访问与类型转换
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitMemberExpr(MemberExpr *MemberExpression) {
  if (!MemberExpression) {
    return nullptr;
  }

  ValueDecl *MemberDecl = MemberExpression->getMemberDecl();
  if (!MemberDecl) {
    return nullptr;
  }

  auto *Field = llvm::dyn_cast<FieldDecl>(MemberDecl);
  if (!Field) {
    // 非字段成员（方法等）— 返回对象指针
    if (MemberExpression->isArrow()) {
      return EmitExpr(MemberExpression->getBase());
    }
    return EmitLValue(MemberExpression->getBase());
  }

  // 获取对象基地址（指针）
  // ptr->field: EmitExpr(base) 返回指针值
  // obj.field:  EmitLValue(base) 返回对象地址
  llvm::Value *BasePtr = nullptr;
  if (MemberExpression->isArrow()) {
    BasePtr = EmitExpr(MemberExpression->getBase());
  } else {
    BasePtr = EmitLValue(MemberExpression->getBase());
  }
  if (!BasePtr) {
    return nullptr;
  }

  // 获取结构体类型（GEP 的 source type）
  QualType BaseQualType = MemberExpression->getBase()->getType();
  llvm::StructType *StructType = nullptr;
  if (MemberExpression->isArrow()) {
    // ptr->field: 从指针类型解引用得到结构体类型
    if (auto *PtrType = llvm::dyn_cast<PointerType>(BaseQualType.getTypePtr())) {
      StructType = llvm::dyn_cast<llvm::StructType>(
          CGM.getTypes().ConvertType(QualType(PtrType->getPointeeType(), Qualifier::None)));
    }
  } else {
    // obj.field: 直接从记录类型获取结构体类型
    StructType = llvm::dyn_cast<llvm::StructType>(
        CGM.getTypes().ConvertType(BaseQualType));
  }
  if (!StructType) {
    return nullptr;
  }

  unsigned FieldIndex = CGM.getTypes().GetFieldIndex(Field);
  llvm::Value *FieldPtr = Builder.CreateStructGEP(
      StructType, BasePtr, FieldIndex, Field->getName());

  // 加载字段值（rvalue 语义）
  llvm::Type *FieldType = CGM.getTypes().ConvertType(Field->getType());
  return Builder.CreateLoad(FieldType, FieldPtr, Field->getName());
}

llvm::Value *CodeGenFunction::EmitArraySubscriptExpr(
    ArraySubscriptExpr *ArraySubscript) {
  if (!ArraySubscript) {
    return nullptr;
  }

  llvm::Value *BaseValue = EmitExpr(ArraySubscript->getBase());
  llvm::Value *IndexValue = EmitExpr(ArraySubscript->getIndex());
  if (!BaseValue || !IndexValue) {
    return nullptr;
  }

  // GEP: base + indices [0, index]
  llvm::Value *Zero =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(CGM.getLLVMContext()), 0);
  llvm::Value *GEP = Builder.CreateGEP(
      CGM.getTypes().ConvertTypeForValue(ArraySubscript->getBase()->getType()),
      BaseValue, {Zero, IndexValue}, "arrayidx");

  llvm::Type *ElementType =
      CGM.getTypes().ConvertType(ArraySubscript->getType());
  return Builder.CreateLoad(ElementType, GEP, "arrayelem");
}

llvm::Value *CodeGenFunction::EmitCastExpr(CastExpr *CastExpression) {
  if (!CastExpression) {
    return nullptr;
  }

  llvm::Value *SubExpression = EmitExpr(CastExpression->getSubExpr());
  if (!SubExpression) {
    return nullptr;
  }

  QualType FromType = CastExpression->getSubExpr()->getType();
  QualType ToType = CastExpression->getType();
  llvm::Type *DestLLVMType = CGM.getTypes().ConvertType(ToType);

  if (!DestLLVMType) {
    return SubExpression;
  }

  // 如果类型相同，直接返回
  if (SubExpression->getType() == DestLLVMType) {
    return SubExpression;
  }

  switch (CastExpression->getCastKind()) {
  case CastKind::LValueToRValue: {
    // glvalue → prvalue: 从地址加载值
    llvm::Value *Address = EmitLValue(CastExpression->getSubExpr());
    if (!Address) {
      return SubExpression;
    }
    return Builder.CreateLoad(DestLLVMType, Address, "lv2rv");
  }
  case CastKind::IntegralCast: {
    // 整数类型之间转换（含整数提升）
    if (SubExpression->getType()->isIntegerTy() && DestLLVMType->isIntegerTy()) {
      return Builder.CreateIntCast(SubExpression, DestLLVMType,
                                    isSignedType(FromType), "intcast");
    }
    return SubExpression;
  }
  case CastKind::FloatingCast: {
    if (SubExpression->getType()->isFloatingPointTy() &&
        DestLLVMType->isFloatingPointTy()) {
      return Builder.CreateFPCast(SubExpression, DestLLVMType, "fpcast");
    }
    return SubExpression;
  }
  case CastKind::IntegralToFloating: {
    if (SubExpression->getType()->isIntegerTy() &&
        DestLLVMType->isFloatingPointTy()) {
      if (isSignedType(FromType)) {
        return Builder.CreateSIToFP(SubExpression, DestLLVMType, "sitofp");
      }
      return Builder.CreateUIToFP(SubExpression, DestLLVMType, "uitofp");
    }
    return SubExpression;
  }
  case CastKind::FloatingToIntegral: {
    if (SubExpression->getType()->isFloatingPointTy() &&
        DestLLVMType->isIntegerTy()) {
      if (isSignedType(ToType)) {
        return Builder.CreateFPToSI(SubExpression, DestLLVMType, "fptosi");
      }
      return Builder.CreateFPToUI(SubExpression, DestLLVMType, "fptoui");
    }
    return SubExpression;
  }
  case CastKind::PointerToIntegral: {
    if (SubExpression->getType()->isPointerTy() && DestLLVMType->isIntegerTy()) {
      return Builder.CreatePtrToInt(SubExpression, DestLLVMType, "ptrtoint");
    }
    return SubExpression;
  }
  case CastKind::IntegralToPointer: {
    if (SubExpression->getType()->isIntegerTy() && DestLLVMType->isPointerTy()) {
      return Builder.CreateIntToPtr(SubExpression, DestLLVMType, "inttoptr");
    }
    return SubExpression;
  }
  case CastKind::BitCast: {
    if (SubExpression->getType()->isPointerTy() && DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "bitcast");
    }
    return SubExpression;
  }
  case CastKind::DerivedToBase: {
    // 派生类 → 基类: 指针偏移调整（简化为 bitcast）
    if (SubExpression->getType()->isPointerTy() && DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "d2b");
    }
    return SubExpression;
  }
  case CastKind::BaseToDerived: {
    // 基类 → 派生类: 需要 offset 调整（简化为 bitcast）
    if (SubExpression->getType()->isPointerTy() && DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "b2d");
    }
    return SubExpression;
  }
  case CastKind::NoOp:
    return SubExpression;
  case CastKind::CStyle:
  case CastKind::CXXStatic: {
    // 通用 static_cast / C-style cast
    // 整数 → 整数
    if (SubExpression->getType()->isIntegerTy() &&
        DestLLVMType->isIntegerTy()) {
      return Builder.CreateIntCast(SubExpression, DestLLVMType,
                                    isSignedType(FromType), "intcast");
    }
    // 浮点 → 浮点
    if (SubExpression->getType()->isFloatingPointTy() &&
        DestLLVMType->isFloatingPointTy()) {
      return Builder.CreateFPCast(SubExpression, DestLLVMType, "fpcast");
    }
    // 整数 → 浮点
    if (SubExpression->getType()->isIntegerTy() &&
        DestLLVMType->isFloatingPointTy()) {
      if (isSignedType(FromType)) {
        return Builder.CreateSIToFP(SubExpression, DestLLVMType, "sitofp");
      }
      return Builder.CreateUIToFP(SubExpression, DestLLVMType, "uitofp");
    }
    // 浮点 → 整数
    if (SubExpression->getType()->isFloatingPointTy() &&
        DestLLVMType->isIntegerTy()) {
      if (isSignedType(ToType)) {
        return Builder.CreateFPToSI(SubExpression, DestLLVMType, "fptosi");
      }
      return Builder.CreateFPToUI(SubExpression, DestLLVMType, "fptoui");
    }
    // 指针 → 整数
    if (SubExpression->getType()->isPointerTy() &&
        DestLLVMType->isIntegerTy()) {
      return Builder.CreatePtrToInt(SubExpression, DestLLVMType, "ptrtoint");
    }
    // 整数 → 指针
    if (SubExpression->getType()->isIntegerTy() &&
        DestLLVMType->isPointerTy()) {
      return Builder.CreateIntToPtr(SubExpression, DestLLVMType, "inttoptr");
    }
    // 指针 → 指针 (bitcast)
    if (SubExpression->getType()->isPointerTy() &&
        DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "bitcast");
    }
    return SubExpression;
  }
  case CastKind::CXXDynamic: {
    // dynamic_cast: 委托给 CGCXX::EmitDynamicCast 进行运行时类型检查
    if (auto *DynCast = llvm::dyn_cast<CXXDynamicCastExpr>(CastExpression)) {
      return CGM.getCXX().EmitDynamicCast(*this, DynCast);
    }
    // fallback: 无 CXXDynamicCastExpr 节点，bitcast
    if (SubExpression->getType()->isPointerTy() &&
        DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "dyn.cast");
    }
    return SubExpression;
  }
  case CastKind::CXXConst:
    // const_cast: 类型不变（LLVM 不区分 const）
    return SubExpression;
  case CastKind::CXXReinterpret:
    // reinterpret_cast
    if (SubExpression->getType()->isPointerTy() &&
        DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "reinterpret");
    }
    if (SubExpression->getType()->isIntegerTy() &&
        DestLLVMType->isPointerTy()) {
      return Builder.CreateIntToPtr(SubExpression, DestLLVMType, "inttoptr");
    }
    if (SubExpression->getType()->isPointerTy() &&
        DestLLVMType->isIntegerTy()) {
      return Builder.CreatePtrToInt(SubExpression, DestLLVMType, "ptrtoint");
    }
    return SubExpression;
  default:
    return SubExpression;
  }
}

//===----------------------------------------------------------------------===//
// 条件运算符 (?:)
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitConditionalOperator(
    ConditionalOperator *Conditional) {
  if (!Conditional) {
    return nullptr;
  }

  llvm::Value *Condition = EmitExpr(Conditional->getCond());
  if (!Condition) {
    return nullptr;
  }

  llvm::BasicBlock *ThenBB = createBasicBlock("cond.then");
  llvm::BasicBlock *ElseBB = createBasicBlock("cond.else");
  llvm::BasicBlock *MergeBB = createBasicBlock("cond.end");

  Condition = EmitConversionToBool(Condition,
                                    Conditional->getCond()->getType());
  Builder.CreateCondBr(Condition, ThenBB, ElseBB);

  // Then
  EmitBlock(ThenBB);
  llvm::Value *TrueValue = EmitExpr(Conditional->getTrueExpr());
  if (TrueValue && haveInsertPoint()) {
    Builder.CreateBr(MergeBB);
  }
  ThenBB = getCurrentBlock(); // 可能被嵌套修改

  // Else
  EmitBlock(ElseBB);
  llvm::Value *FalseValue = EmitExpr(Conditional->getFalseExpr());
  if (FalseValue && haveInsertPoint()) {
    Builder.CreateBr(MergeBB);
  }
  ElseBB = getCurrentBlock();

  // Merge
  EmitBlock(MergeBB);

  if (!TrueValue || !FalseValue) {
    return TrueValue ? TrueValue : FalseValue;
  }

  llvm::PHINode *PhiNode =
      Builder.CreatePHI(TrueValue->getType(), 2, "condval");
  PhiNode->addIncoming(TrueValue, ThenBB);
  PhiNode->addIncoming(FalseValue, ElseBB);
  return PhiNode;
}

//===----------------------------------------------------------------------===//
// 初始化列表
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitInitListExpr(InitListExpr *InitList) {
  if (!InitList) {
    return nullptr;
  }

  // 运行时初始化列表：分配 alloca + 逐个 store
  llvm::Type *AllocType = CGM.getTypes().ConvertType(InitList->getType());
  if (!AllocType) {
    return nullptr;
  }

  llvm::AllocaInst *Alloca = CreateAlloca(InitList->getType(), "initlist");

  if (auto *StructType = llvm::dyn_cast<llvm::StructType>(AllocType)) {
    unsigned Index = 0;
    for (Expr *Initializer : InitList->getInits()) {
      if (Index >= StructType->getNumElements()) {
        break;
      }
      llvm::Value *FieldValue = EmitExpr(Initializer);
      if (FieldValue) {
        llvm::Value *GEP = Builder.CreateStructGEP(StructType, Alloca, Index,
                                                     "field");
        Builder.CreateStore(FieldValue, GEP);
      }
      ++Index;
    }
  } else if (auto *ArrayType = llvm::dyn_cast<llvm::ArrayType>(AllocType)) {
    unsigned Index = 0;
    for (Expr *Initializer : InitList->getInits()) {
      if (Index >= ArrayType->getNumElements()) {
        break;
      }
      llvm::Value *ElementValue = EmitExpr(Initializer);
      if (ElementValue) {
        llvm::Value *GEP = Builder.CreateGEP(
            ArrayType, Alloca,
            {llvm::ConstantInt::get(llvm::Type::getInt32Ty(CGM.getLLVMContext()),
                                     0),
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(CGM.getLLVMContext()),
                                     Index)},
            "arrayidx");
        Builder.CreateStore(ElementValue, GEP);
      }
      ++Index;
    }
  }

  return Builder.CreateLoad(AllocType, Alloca, "initlist.val");
}

//===----------------------------------------------------------------------===//
// LValue 辅助方法
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitLValue(Expr *Expression) {
  if (!Expression) {
    return nullptr;
  }

  if (auto *DeclRef = llvm::dyn_cast<DeclRefExpr>(Expression)) {
    if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(DeclRef->getDecl())) {
      if (auto *Alloca = getLocalDecl(VariableDecl)) {
        return Alloca;
      }
      if (auto *GlobalVariable = CGM.GetGlobalVar(VariableDecl)) {
        return GlobalVariable;
      }
    }
  }

  if (auto *MemberExpression = llvm::dyn_cast<MemberExpr>(Expression)) {
    ValueDecl *MemberDecl = MemberExpression->getMemberDecl();
    auto *Field = llvm::dyn_cast<FieldDecl>(MemberDecl);
    if (!Field) {
      if (MemberExpression->isArrow()) {
        return EmitExpr(MemberExpression->getBase());
      }
      return EmitLValue(MemberExpression->getBase());
    }

    // 获取对象基地址
    llvm::Value *BasePtr = nullptr;
    if (MemberExpression->isArrow()) {
      BasePtr = EmitExpr(MemberExpression->getBase());
    } else {
      BasePtr = EmitLValue(MemberExpression->getBase());
    }
    if (!BasePtr) {
      return nullptr;
    }

    // 获取结构体类型（GEP source type 必须是整个结构体类型）
    QualType BaseQualType = MemberExpression->getBase()->getType();
    llvm::StructType *StructType = nullptr;
    if (MemberExpression->isArrow()) {
      if (auto *PtrType = llvm::dyn_cast<PointerType>(BaseQualType.getTypePtr())) {
        StructType = llvm::dyn_cast<llvm::StructType>(
            CGM.getTypes().ConvertType(QualType(PtrType->getPointeeType(), Qualifier::None)));
      }
    } else {
      StructType = llvm::dyn_cast<llvm::StructType>(
          CGM.getTypes().ConvertType(BaseQualType));
    }
    if (!StructType) {
      return nullptr;
    }

    unsigned FieldIndex = CGM.getTypes().GetFieldIndex(Field);
    return Builder.CreateStructGEP(StructType, BasePtr, FieldIndex,
                                    Field->getName());
  }

  if (auto *ArraySubscript = llvm::dyn_cast<ArraySubscriptExpr>(Expression)) {
    llvm::Value *BaseValue = EmitExpr(ArraySubscript->getBase());
    llvm::Value *IndexValue = EmitExpr(ArraySubscript->getIndex());
    if (!BaseValue || !IndexValue) {
      return nullptr;
    }

    llvm::Value *Zero = llvm::ConstantInt::get(
        llvm::Type::getInt32Ty(CGM.getLLVMContext()), 0);
    return Builder.CreateGEP(
        CGM.getTypes().ConvertTypeForValue(
            ArraySubscript->getBase()->getType()),
        BaseValue, {Zero, IndexValue}, "arrayidx");
  }

  if (auto *UnaryOp = llvm::dyn_cast<UnaryOperator>(Expression)) {
    if (UnaryOp->getOpcode() == UnaryOpKind::Deref) {
      return EmitExpr(UnaryOp->getSubExpr());
    }
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// C++ 特有表达式
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitCXXThisExpr(CXXThisExpr *ThisExpression) {
  // 优先使用 setThisPointer 设置的值（构造/析构函数场景）
  if (ThisValue) {
    return ThisValue;
  }
  // this 指针是函数的第一个参数
  if (!CurFn || CurFn->arg_empty()) {
    return nullptr;
  }
  return &*CurFn->arg_begin(); // 第一个参数是 this
}

llvm::Value *CodeGenFunction::EmitCXXConstructExpr(
    CXXConstructExpr *ConstructExpression, llvm::Value *DestPtr) {
  if (!ConstructExpression) {
    return nullptr;
  }

  CXXConstructorDecl *CtorDecl = ConstructExpression->getConstructor();
  QualType ConstructType = ConstructExpression->getType();

  llvm::Type *Type = CGM.getTypes().ConvertType(ConstructType);
  if (!Type) {
    return nullptr;
  }

  // 如果提供了 DestPtr（copy elision），直接在目标地址构造
  // 否则分配临时 alloca
  llvm::Value *ConstructAddr = DestPtr;
  bool NeedsLoad = false;
  if (!ConstructAddr) {
    auto *Alloca = CreateAlloca(ConstructType, "construct");
    ConstructAddr = Alloca;
    NeedsLoad = true;
  }

  if (CtorDecl) {
    // 获取构造函数的 LLVM Function
    llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(CtorDecl);
    if (CtorFn) {
      // 构建参数列表：this + 构造函数参数
      llvm::SmallVector<llvm::Value *, 8> Args;
      Args.push_back(ConstructAddr); // this 指针指向构造地址

      for (Expr *Arg : ConstructExpression->getArgs()) {
        llvm::Value *ArgVal = EmitExpr(Arg);
        if (ArgVal) {
          Args.push_back(ArgVal);
        }
      }

      // 调用构造函数（try 块内使用 invoke 以支持异常传播）
      EmitCallOrInvoke(CtorFn, Args, "ctor.call");
    } else {
      // 无法获取构造函数，零初始化
      Builder.CreateStore(llvm::Constant::getNullValue(Type), ConstructAddr);
    }
  } else {
    // 无构造函数声明（trivial 类型或隐式默认构造）
    Builder.CreateStore(llvm::Constant::getNullValue(Type), ConstructAddr);
  }

  // Copy elision 模式：返回构造地址（而非 load 值）
  if (!NeedsLoad) {
    return ConstructAddr;
  }
  return Builder.CreateLoad(Type, ConstructAddr, "construct.val");
}

llvm::Value *CodeGenFunction::EmitCXXNewExpr(CXXNewExpr *NewExpression) {
  if (!NewExpression) {
    return nullptr;
  }

  // 获取被分配的元素类型 T
  QualType ElementType = NewExpression->getAllocatedType();
  if (ElementType.isNull()) {
    // Sema::ProcessAST 应已设置 ExprTy = T*，从中推导
    QualType AllocType = NewExpression->getType();
    if (auto *PtrType = llvm::dyn_cast<PointerType>(AllocType.getTypePtr())) {
      ElementType = PtrType->getPointeeType();
    }
  }
  if (ElementType.isNull()) {
    return nullptr;
  }

  auto &Ctx = CGM.getLLVMContext();
  llvm::IRBuilder<> &B = Builder;
  uint64_t TypeSize = CGM.getTarget().getTypeSize(ElementType);

  // 获取或声明 malloc
  llvm::FunctionType *MallocType = llvm::FunctionType::get(
      llvm::PointerType::get(Ctx, 0),
      {llvm::Type::getInt64Ty(Ctx)}, false);
  llvm::FunctionCallee Malloc =
      CGM.getModule()->getOrInsertFunction("malloc", MallocType);

  // 获取或声明 memset（用于零初始化）
  llvm::FunctionType *MemsetType = llvm::FunctionType::get(
      llvm::Type::getVoidTy(Ctx),
      {llvm::PointerType::get(Ctx, 0), llvm::Type::getInt32Ty(Ctx),
       llvm::Type::getInt64Ty(Ctx)},
      false);
  llvm::FunctionCallee Memset =
      CGM.getModule()->getOrInsertFunction("llvm.memset.p0.i64", MemsetType);

  Expr *ArraySizeExpr = NewExpression->getArraySize();
  llvm::Value *ResultPtr = nullptr;

  if (ArraySizeExpr) {
    // === 数组形式: new T[n] ===
    // 布局: [size_t count][T elem0][T elem1]...[T elemN-1]
    // 返回指向 elem0 的指针（跳过 count cookie）

    llvm::Value *ArrayCount = EmitExpr(ArraySizeExpr);
    if (!ArrayCount) return nullptr;

    // 确保数组计数是 i64
    llvm::Type *Int64Ty = llvm::Type::getInt64Ty(Ctx);
    if (ArrayCount->getType() != Int64Ty) {
      ArrayCount = B.CreateIntCast(ArrayCount, Int64Ty, false, "arr.count");
    }

    // 计算总大小 = CookieSize + sizeof(T) * n
    llvm::Value *ElemSize = llvm::ConstantInt::get(Int64Ty, TypeSize);
    llvm::Value *DataSize = B.CreateMul(ElemSize, ArrayCount, "data.size");
    llvm::Value *CookieSize = llvm::ConstantInt::get(Int64Ty, ArrayCookie::CookieSize);
    llvm::Value *TotalSize = B.CreateAdd(CookieSize, DataSize, "total.size");

    // malloc(total_size)
    llvm::Value *RawMemory = B.CreateCall(Malloc, {TotalSize}, "new.arr.raw");

    // 在 cookie 位置存储数组长度
    llvm::Value *CookiePtr = B.CreateBitCast(
        RawMemory, llvm::PointerType::get(Int64Ty, 0), "cookie.ptr");
    B.CreateStore(ArrayCount, CookiePtr);

    // 计算返回指针 = raw + sizeof(size_t)
    llvm::Value *RawInt = B.CreatePtrToInt(
        RawMemory, Int64Ty, "raw.int");
    llvm::Value *DataInt = B.CreateAdd(
        RawInt, CookieSize, "data.int");
    ResultPtr = B.CreateIntToPtr(
        DataInt, llvm::PointerType::get(Ctx, 0), "new.arr");

    // 零初始化数组内存（可选，但对 POD 类型很重要）
    B.CreateCall(Memset, {ResultPtr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0), DataSize});

    // 如果元素类型有非 trivial 析构函数，需要对每个元素调用构造函数
    // 查找元素类型的 CXXRecordDecl
    CXXRecordDecl *ElementRD = nullptr;
    if (auto *RT = llvm::dyn_cast<RecordType>(ElementType.getTypePtr())) {
      ElementRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    }

    if (ElementRD && ElementRD->hasDestructor()) {
      // 查找默认构造函数
      CXXConstructorDecl *DefaultCtor = nullptr;
      for (CXXMethodDecl *MD : ElementRD->methods()) {
        if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(MD)) {
          if (Ctor->getNumParams() == 0) {
            DefaultCtor = Ctor;
            break;
          }
        }
      }

      if (DefaultCtor) {
        llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(DefaultCtor);
        if (CtorFn) {
          // 构造循环: for (i = 0; i < count; ++i) ctor((char*)ptr + i * sizeof(T))
          llvm::Function *CurFn = getCurrentFunction();
          llvm::BasicBlock *LoopBB =
              llvm::BasicBlock::Create(Ctx, "arr.ctor.loop", CurFn);
          llvm::BasicBlock *BodyBB =
              llvm::BasicBlock::Create(Ctx, "arr.ctor.body", CurFn);
          llvm::BasicBlock *ExitBB =
              llvm::BasicBlock::Create(Ctx, "arr.ctor.exit", CurFn);

          // i = 0 — 使用统一的 CreateAlloca
          llvm::AllocaInst *Counter =
              CreateAlloca(Int64Ty, "arr.ctor.i");
          B.CreateStore(llvm::ConstantInt::get(Int64Ty, 0), Counter);
          B.CreateBr(LoopBB);

          // LoopBB: if (i < count) goto Body else goto Exit
          B.SetInsertPoint(LoopBB);
          llvm::Value *CounterVal = B.CreateLoad(Int64Ty, Counter, "i");
          llvm::Value *Cond = B.CreateICmpSLT(CounterVal, ArrayCount, "arr.ctor.cond");
          B.CreateCondBr(Cond, BodyBB, ExitBB);

          // BodyBB: ctor(base + i * sizeof(T)); i++
          B.SetInsertPoint(BodyBB);
          llvm::Value *Offset = B.CreateMul(CounterVal, ElemSize, "elem.offset");
          llvm::Value *ElemIntPtr = B.CreateAdd(
              B.CreatePtrToInt(ResultPtr, Int64Ty, "base.int"),
              Offset, "elem.int");
          llvm::Value *ElemPtr = B.CreateIntToPtr(
              ElemIntPtr, llvm::PointerType::get(Ctx, 0), "elem.ptr");
          EmitCallOrInvoke(CtorFn, {ElemPtr}, "arr.ctor.call");

          // i++
          llvm::Value *NextCounter = B.CreateAdd(
              CounterVal, llvm::ConstantInt::get(Int64Ty, 1), "i.next");
          B.CreateStore(NextCounter, Counter);
          B.CreateBr(LoopBB);

          // ExitBB: 继续后续代码
          B.SetInsertPoint(ExitBB);
        }
      }
    }
  } else {
    // === 单元素形式: new T ===
    llvm::Value *Size = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(Ctx), TypeSize);
    llvm::Value *Memory = B.CreateCall(Malloc, {Size}, "new");

    // 零初始化
    B.CreateCall(Memset, {Memory,
                          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0),
                          Size});

    // 处理初始化器
    Expr *Initializer = NewExpression->getInitializer();
    if (auto *ConstructExpr = llvm::dyn_cast_or_null<CXXConstructExpr>(Initializer)) {
      // 在分配的内存上调用构造函数
      CXXConstructorDecl *CtorDecl = ConstructExpr->getConstructor();
      if (CtorDecl) {
        llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(CtorDecl);
        if (CtorFn) {
          llvm::SmallVector<llvm::Value *, 8> Args;
          Args.push_back(Memory); // this
          for (Expr *Arg : ConstructExpr->getArgs()) {
            llvm::Value *ArgVal = EmitExpr(Arg);
            if (ArgVal) Args.push_back(ArgVal);
          }
          EmitCallOrInvoke(CtorFn, Args, "new.ctor");
        }
      }
    }

    ResultPtr = Memory;
  }

  // 转换为正确的指针类型（Sema::ProcessAST 已设置 ExprTy = T*）
  QualType AllocType = NewExpression->getType();
  if (AllocType.isNull()) {
    // 后备：构造指针类型 ElementType*
    auto *PtrTy = CGM.getASTContext().getPointerType(ElementType.getTypePtr());
    AllocType = QualType(PtrTy, Qualifier::None);
  }
  llvm::Type *LLVMAllocType = CGM.getTypes().ConvertType(AllocType);
  if (LLVMAllocType && LLVMAllocType->isPointerTy() && ResultPtr) {
    return B.CreateBitCast(ResultPtr, LLVMAllocType, "new.cast");
  }
  return ResultPtr;
}

llvm::Value *CodeGenFunction::EmitCXXDeleteExpr(
    CXXDeleteExpr *DeleteExpression) {
  if (!DeleteExpression) {
    return nullptr;
  }

  llvm::Value *Argument = EmitExpr(DeleteExpression->getArgument());
  if (!Argument) {
    return nullptr;
  }

  auto &Ctx = CGM.getLLVMContext();
  llvm::IRBuilder<> &B = Builder;

  // 获取被删除的元素类型 — 优先使用 AllocatedType（Parse 层/Sema 已设置）
  QualType ElementType = DeleteExpression->getAllocatedType();
  if (ElementType.isNull()) {
    // 后备：从 Argument 表达式类型推导 T* → T
    QualType ArgType = DeleteExpression->getArgument()->getType();
    if (auto *PtrType = llvm::dyn_cast<PointerType>(ArgType.getTypePtr())) {
      ElementType = PtrType->getPointeeType();
    }
    if (ElementType.isNull()) {
      ElementType = ArgType;
    }
  }

  // 转换参数为 i8*
  llvm::Value *PtrArgument = B.CreateBitCast(
      Argument, llvm::PointerType::get(Ctx, 0), "del.cast");

  // 获取元素类型的 CXXRecordDecl（如果有的话）
  CXXRecordDecl *ElementRD = nullptr;
  if (auto *RT = llvm::dyn_cast<RecordType>(ElementType.getTypePtr())) {
    ElementRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
  }

  // 获取 free
  llvm::FunctionType *FreeType = llvm::FunctionType::get(
      llvm::Type::getVoidTy(Ctx),
      {llvm::PointerType::get(Ctx, 0)}, false);
  llvm::FunctionCallee Free =
      CGM.getModule()->getOrInsertFunction("free", FreeType);

  if (DeleteExpression->isArrayForm()) {
    // === 数组形式: delete[] ptr ===
    // 读取 cookie 获取数组长度，逆序调用析构，free(raw)

    llvm::Type *Int64Ty = llvm::Type::getInt64Ty(Ctx);

    // RawPtr = ptr - CookieSize
    llvm::Value *PtrInt = B.CreatePtrToInt(PtrArgument, Int64Ty, "del.ptr.int");
    llvm::Value *CookieOffset = llvm::ConstantInt::get(Int64Ty, ArrayCookie::CookieSize);
    llvm::Value *RawIntPtr = B.CreateSub(PtrInt, CookieOffset, "del.raw.int");
    llvm::Value *RawPtr = B.CreateIntToPtr(
        RawIntPtr, llvm::PointerType::get(Ctx, 0), "del.raw");

    // 读取数组长度
    llvm::Value *CookiePtr = B.CreateBitCast(
        RawPtr, llvm::PointerType::get(Int64Ty, 0), "del.cookie.ptr");
    llvm::Value *ArrayCount = B.CreateLoad(Int64Ty, CookiePtr, "del.arr.count");

    // 如果元素类型有析构函数，逆序调用
    if (ElementRD && ElementRD->hasDestructor()) {
      llvm::Function *DtorFn = CGM.getCXX().GetDestructor(ElementRD);
      if (DtorFn) {
        uint64_t ElemSize = CGM.getTarget().getTypeSize(ElementType);
        llvm::Value *ElemSizeVal = llvm::ConstantInt::get(Int64Ty, ElemSize);

        // 逆序析构循环: for (i = count-1; i >= 0; --i) dtor(base + i * sizeof(T))
        llvm::Function *CurFn = getCurrentFunction();
        llvm::BasicBlock *LoopBB =
            llvm::BasicBlock::Create(Ctx, "del.dtor.loop", CurFn);
        llvm::BasicBlock *BodyBB =
            llvm::BasicBlock::Create(Ctx, "del.dtor.body", CurFn);
        llvm::BasicBlock *ExitBB =
            llvm::BasicBlock::Create(Ctx, "del.dtor.exit", CurFn);

        // i = count - 1
        llvm::AllocaInst *Counter =
            CreateAlloca(Int64Ty, "del.dtor.i");
        llvm::Value *CountMinusOne = B.CreateSub(
            ArrayCount, llvm::ConstantInt::get(Int64Ty, 1), "count.dec");
        B.CreateStore(CountMinusOne, Counter);
        B.CreateBr(LoopBB);

        // LoopBB: if (i >= 0) goto Body else goto Exit
        B.SetInsertPoint(LoopBB);
        llvm::Value *CounterVal = B.CreateLoad(Int64Ty, Counter, "i");
        llvm::Value *Cond = B.CreateICmpSGE(
            CounterVal, llvm::ConstantInt::get(Int64Ty, 0), "del.dtor.cond");
        B.CreateCondBr(Cond, BodyBB, ExitBB);

        // BodyBB: dtor(base + i * sizeof(T)); --i
        B.SetInsertPoint(BodyBB);
        llvm::Value *Offset = B.CreateMul(CounterVal, ElemSizeVal, "dtor.offset");
        llvm::Value *ElemIntPtr = B.CreateAdd(
            B.CreatePtrToInt(PtrArgument, Int64Ty, "del.base.int"),
            Offset, "del.elem.int");
        llvm::Value *ElemPtr = B.CreateIntToPtr(
            ElemIntPtr, llvm::PointerType::get(Ctx, 0), "del.elem.ptr");
        EmitNounwindCall(DtorFn, {ElemPtr}, "del.dtor.call");

        // --i
        llvm::Value *NextCounter = B.CreateSub(
            CounterVal, llvm::ConstantInt::get(Int64Ty, 1), "i.dec");
        B.CreateStore(NextCounter, Counter);
        B.CreateBr(LoopBB);

        // ExitBB: free(raw)
        B.SetInsertPoint(ExitBB);
      }
    }

    // free(raw_ptr) — 释放原始 malloc 返回的地址（含 cookie）
    B.CreateCall(Free, {RawPtr});
  } else {
    // === 单元素形式: delete ptr ===
    // 如果元素类型有析构函数，先调用析构
    if (ElementRD && ElementRD->hasDestructor()) {
      CGM.getCXX().EmitDestructorCall(*this, ElementRD, PtrArgument);
    }

    // free(ptr)
    B.CreateCall(Free, {PtrArgument});
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// C++ throw 表达式
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitCXXThrowExpr(CXXThrowExpr *ThrowExpression) {
  if (!ThrowExpression) {
    return nullptr;
  }

  auto &Ctx = CGM.getLLVMContext();
  auto &B = Builder;

  if (ThrowExpression->getSubExpr()) {
    // throw expr: __cxa_allocate_exception(sizeof(T)) → memcpy → __cxa_throw
    Expr *SubExpr = ThrowExpression->getSubExpr();
    QualType ThrowType = SubExpr->getType();
    llvm::Type *ThrowLLVMTy = CGM.getTypes().ConvertType(ThrowType);

    if (ThrowType.isNull() || !ThrowLLVMTy) {
      if (haveInsertPoint()) Builder.CreateUnreachable();
      return nullptr;
    }

    uint64_t ThrowSize = CGM.getTarget().getTypeSize(ThrowType);

    // 声明 __cxa_allocate_exception(size_t) → i8*
    llvm::FunctionType *AllocExnTy = llvm::FunctionType::get(
        llvm::PointerType::get(Ctx, 0),
        {llvm::Type::getInt64Ty(Ctx)}, false);
    llvm::FunctionCallee AllocExn =
        CGM.getModule()->getOrInsertFunction("__cxa_allocate_exception", AllocExnTy);

    // 调用 __cxa_allocate_exception
    llvm::Value *ExnPtr = B.CreateCall(AllocExn,
        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), ThrowSize)},
        "exn.alloc");

    // 求值 throw 表达式
    llvm::Value *ThrowVal = EmitExpr(SubExpr);
    if (!ThrowVal) {
      if (haveInsertPoint()) Builder.CreateUnreachable();
      return nullptr;
    }

    // 将异常值存入 __cxa_allocate_exception 返回的内存
    llvm::Value *TypedExnPtr = B.CreateBitCast(ExnPtr,
        llvm::PointerType::get(ThrowLLVMTy, 0), "exn.typed");
    B.CreateStore(ThrowVal, TypedExnPtr);

    // 获取 typeinfo（如果有 record 类型）
    llvm::Value *TypeInfoPtr = llvm::ConstantPointerNull::get(
        llvm::PointerType::get(Ctx, 0));
    if (ThrowType->isRecordType()) {
      if (auto *RT = llvm::dyn_cast<RecordType>(ThrowType.getTypePtr())) {
        if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          if (auto *TI = CGM.getCXX().EmitTypeInfo(CXXRD)) {
            TypeInfoPtr = B.CreateBitCast(TI,
                llvm::PointerType::get(Ctx, 0), "ti.ptr");
          }
        }
      }
    }

    // 析构函数指针（异常对象销毁回调）— 简化为 null（trivial 析构）
    // TODO: 如果类型有非 trivial 析构函数，需要生成一个适配函数
    llvm::Value *DtorPtr = llvm::ConstantPointerNull::get(
        llvm::PointerType::get(Ctx, 0));

    // 声明 __cxa_throw(void*, typeinfo*, dtor*) → noreturn
    llvm::FunctionType *ThrowTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(Ctx),
        {llvm::PointerType::get(Ctx, 0),
         llvm::PointerType::get(Ctx, 0),
         llvm::PointerType::get(Ctx, 0)}, false);
    llvm::FunctionCallee CxaThrow =
        CGM.getModule()->getOrInsertFunction("__cxa_throw", ThrowTy);

    auto *ThrowCall = B.CreateCall(CxaThrow,
        {ExnPtr, TypeInfoPtr, DtorPtr}, "throw");
    ThrowCall->setDoesNotReturn();
  } else {
    // throw; （rethrow）
    llvm::FunctionType *RethrowTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(Ctx), false);
    llvm::FunctionCallee CxaRethrow =
        CGM.getModule()->getOrInsertFunction("__cxa_rethrow", RethrowTy);
    auto *RethrowCall = B.CreateCall(CxaRethrow, {}, "rethrow");
    RethrowCall->setDoesNotReturn();
  }

  // throw/rethrow 之后是 dead code
  if (haveInsertPoint()) {
    Builder.CreateUnreachable();
  }

  // 创建新的基本块用于后续 dead code
  llvm::BasicBlock *AfterThrow = createBasicBlock("throw.cont");
  Builder.SetInsertPoint(AfterThrow);

  return nullptr;
}

//===----------------------------------------------------------------------===//
// P7.1.2: Decay-copy expression (P0849R8)
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitDecayCopyExpr(DecayCopyExpr *DCE) {
  if (!DCE)
    return nullptr;

  Expr *SubExpr = DCE->getSubExpr();
  if (!SubExpr)
    return nullptr;

  // Evaluate the subexpression — the decay (removing references, cv, etc.)
  // is purely a type-level operation. At the IR level, we just emit the
  // subexpression and optionally perform type conversions.
  llvm::Value *SubValue = EmitExpr(SubExpr);
  if (!SubValue)
    return nullptr;

  // If the subexpression type differs from the result type (e.g., reference
  // stripping, array-to-pointer decay), apply the conversion.
  QualType SubTy = SubExpr->getType();
  QualType ResultTy = DCE->getType();

  if (!SubTy.isNull() && !ResultTy.isNull() && SubTy != ResultTy) {
    SubValue = EmitScalarConversion(SubValue, SubTy, ResultTy);
  }

  // For class types, create a temporary and copy-construct into it.
  // For scalar types, the value itself is the prvalue result.
  if (!ResultTy.isNull() && ResultTy->isRecordType()) {
    // Create a temporary alloca for the decay-copy result
    llvm::AllocaInst *TempAlloca = CreateAlloca(ResultTy, "decaycopy.tmp");
    Builder.CreateStore(SubValue, TempAlloca);
    // Return the loaded value (prvalue)
    llvm::Type *LLVMResultTy = CGM.getTypes().ConvertType(ResultTy);
    return Builder.CreateLoad(LLVMResultTy, TempAlloca, "decaycopy.val");
  }

  return SubValue;
}

//===----------------------------------------------------------------------===//
// P7.1.4: [[assume]] attribute (P1774R8)
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitAssumeAttr(Expr *Condition) {
  if (!Condition)
    return;

  llvm::Value *CondVal = EmitExpr(Condition);
  if (!CondVal)
    return;

  // Convert to i1 if needed
  llvm::Type *BoolTy = llvm::Type::getInt1Ty(CGM.getLLVMContext());
  if (CondVal->getType() != BoolTy) {
    CondVal = Builder.CreateIsNotNull(CondVal, "assume.bool");
  }

  // Emit llvm.assume intrinsic
  llvm::Function *AssumeIntrinsic = llvm::Intrinsic::getDeclaration(
      CGM.getModule(), llvm::Intrinsic::assume);
  Builder.CreateCall(AssumeIntrinsic, {CondVal});
}

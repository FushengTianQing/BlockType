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

  // 保存 LHS 所在的基本块（在 CreateICmpNE 之前）
  llvm::BasicBlock *LHBB = getCurrentBlock();

  // 转换为 i1
  LeftHandSide = Builder.CreateICmpNE(
      LeftHandSide,
      llvm::Constant::getNullValue(LeftHandSide->getType()), "and.cond");

  Builder.CreateCondBr(LeftHandSide, AndBB, MergeBB);

  // RHS
  EmitBlock(AndBB);
  llvm::Value *RightHandSide = EmitExpr(BinaryOp->getRHS());
  if (!RightHandSide) {
    RightHandSide = llvm::ConstantInt::getFalse(CGM.getLLVMContext());
  }
  RightHandSide = Builder.CreateICmpNE(
      RightHandSide,
      llvm::Constant::getNullValue(RightHandSide->getType()), "and.cond");

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

  // 保存 LHS 所在的基本块（在 CreateICmpNE 之前）
  llvm::BasicBlock *LHBB = getCurrentBlock();

  LeftHandSide = Builder.CreateICmpNE(
      LeftHandSide,
      llvm::Constant::getNullValue(LeftHandSide->getType()), "or.cond");

  Builder.CreateCondBr(LeftHandSide, MergeBB, OrBB);

  // RHS
  EmitBlock(OrBB);
  llvm::Value *RightHandSide = EmitExpr(BinaryOp->getRHS());
  if (!RightHandSide) {
    RightHandSide = llvm::ConstantInt::getFalse(CGM.getLLVMContext());
  }
  RightHandSide = Builder.CreateICmpNE(
      RightHandSide,
      llvm::Constant::getNullValue(RightHandSide->getType()), "or.cond");

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

  QualType ResultType = BinaryOp->getLHS()->getType();
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

  // 整数提升 (integral promotion): 将操作数提升到至少 i32
  // 参照 Clang EmitUsualArithmeticConversions
  auto promoteToInt32 = [&](llvm::Value *Val) -> llvm::Value * {
    if (Val && Val->getType()->isIntegerTy() &&
        Val->getType()->getIntegerBitWidth() < 32) {
      return Builder.CreateIntCast(Val, llvm::Type::getInt32Ty(CGM.getLLVMContext()),
                                   IsSigned, "promote");
    }
    return Val;
  };
  if (LeftHandSide->getType()->isIntegerTy() &&
      RightHandSide->getType()->isIntegerTy() &&
      !LeftHandSide->getType()->isPointerTy() &&
      !RightHandSide->getType()->isPointerTy()) {
    LeftHandSide = promoteToInt32(LeftHandSide);
    RightHandSide = promoteToInt32(RightHandSide);
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
    default:
      break;
    }
  }

  return nullptr;
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
    if (Operand->getType()->isIntegerTy()) {
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
    if (!MemberDecl->isStatic()) {
      // 从 callee 表达式获取 this
      if (auto *MemberExpression = llvm::dyn_cast<MemberExpr>(CalleeExpr)) {
        llvm::Value *BaseValue = EmitExpr(MemberExpression->getBase());
        if (BaseValue) {
          if (!MemberExpression->isArrow()) {
            // obj.method() → Base 是对象值，需要取地址
            // 简化：假设 Base 已经是指针
          }
          Arguments.push_back(BaseValue);
        }
      }

      // 虚函数调用：使用 vtable 进行间接调用
      if (MemberDecl->isVirtual() && !Arguments.empty()) {
        // 收集非 this 参数
        llvm::SmallVector<llvm::Value *, 8> NonThisArgs;
        for (Expr *Argument : CallExpression->getArgs()) {
          llvm::Value *ArgValue = EmitExpr(Argument);
          if (ArgValue) {
            NonThisArgs.push_back(ArgValue);
          }
        }

        // 使用 CGCXX::EmitVirtualCall 生成虚函数调用
        llvm::Value *ThisPtr = Arguments[0];
        llvm::Value *VirtualCallResult =
            CGM.getCXX().EmitVirtualCall(*this, MemberDecl, ThisPtr,
                                          NonThisArgs);
        if (VirtualCallResult) {
          return VirtualCallResult;
        }
        // 虚函数调用失败，降级为普通调用
      }
    }
  }

  for (Expr *Argument : CallExpression->getArgs()) {
    llvm::Value *ArgValue = EmitExpr(Argument);
    if (ArgValue) {
      Arguments.push_back(ArgValue);
    }
  }

  // 在 try 块中：生成 invoke 指令以支持异常传播到 landingpad
  if (isInTryBlock()) {
    const auto &InvokeTarget = getCurrentInvokeTarget();
    return Builder.CreateInvoke(CalleeFunction, InvokeTarget.NormalBB,
                               InvokeTarget.UnwindBB, Arguments, "invoke");
  }

  return Builder.CreateCall(CalleeFunction, Arguments, "call");
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
  case CastKind::CXXDynamic:
    // dynamic_cast: 简化为 bitcast + null check
    if (SubExpression->getType()->isPointerTy() &&
        DestLLVMType->isPointerTy()) {
      return Builder.CreateBitCast(SubExpression, DestLLVMType, "dyn.cast");
    }
    return SubExpression;
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

  Condition = Builder.CreateICmpNE(
      Condition, llvm::Constant::getNullValue(Condition->getType()), "cond");
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
    CXXConstructExpr *ConstructExpression) {
  if (!ConstructExpression) {
    return nullptr;
  }

  CXXConstructorDecl *CtorDecl = ConstructExpression->getConstructor();
  QualType ConstructType = ConstructExpression->getType();

  llvm::Type *Type = CGM.getTypes().ConvertType(ConstructType);
  if (!Type) {
    return nullptr;
  }

  // 分配 alloca 存储构造的对象
  llvm::AllocaInst *Alloca = CreateAlloca(ConstructType, "construct");

  if (CtorDecl) {
    // 获取构造函数的 LLVM Function
    llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(CtorDecl);
    if (CtorFn) {
      // 构建参数列表：this + 构造函数参数
      llvm::SmallVector<llvm::Value *, 8> Args;
      Args.push_back(Alloca); // this 指针指向 alloca

      for (Expr *Arg : ConstructExpression->getArgs()) {
        llvm::Value *ArgVal = EmitExpr(Arg);
        if (ArgVal) {
          Args.push_back(ArgVal);
        }
      }

      // 调用构造函数
      Builder.CreateCall(CtorFn, Args, "ctor.call");
    } else {
      // 无法获取构造函数，零初始化
      Builder.CreateStore(llvm::Constant::getNullValue(Type), Alloca);
    }
  } else {
    // 无构造函数声明（trivial 类型或隐式默认构造）
    Builder.CreateStore(llvm::Constant::getNullValue(Type), Alloca);
  }

  return Builder.CreateLoad(Type, Alloca, "construct.val");
}

llvm::Value *CodeGenFunction::EmitCXXNewExpr(CXXNewExpr *NewExpression) {
  if (!NewExpression) {
    return nullptr;
  }

  // new T() 返回 T*，需要获取被分配的类型 T
  QualType AllocType = NewExpression->getType();
  QualType ElementType = AllocType;
  if (auto *PtrType = llvm::dyn_cast<PointerType>(AllocType.getTypePtr())) {
    ElementType = QualType(PtrType->getPointeeType(), Qualifier::None);
  }

  uint64_t TypeSize = CGM.getTarget().getTypeSize(ElementType);
  llvm::Value *Size = llvm::ConstantInt::get(
      llvm::Type::getInt64Ty(CGM.getLLVMContext()), TypeSize);

  // 获取或声明 malloc
  llvm::FunctionType *MallocType = llvm::FunctionType::get(
      llvm::PointerType::get(CGM.getLLVMContext(), 0),
      {llvm::Type::getInt64Ty(CGM.getLLVMContext())}, false);
  llvm::FunctionCallee Malloc =
      CGM.getModule()->getOrInsertFunction("malloc", MallocType);

  llvm::Value *Memory = Builder.CreateCall(Malloc, {Size}, "new");

  // 转换为正确的指针类型
  llvm::Type *LLVMAllocType = CGM.getTypes().ConvertType(AllocType);
  if (LLVMAllocType && LLVMAllocType->isPointerTy()) {
    return Builder.CreateBitCast(Memory, LLVMAllocType, "new.cast");
  }
  return Memory;
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

  // 调用 free
  llvm::FunctionType *FreeType = llvm::FunctionType::get(
      llvm::Type::getVoidTy(CGM.getLLVMContext()),
      {llvm::PointerType::get(CGM.getLLVMContext(), 0)}, false);
  llvm::FunctionCallee Free =
      CGM.getModule()->getOrInsertFunction("free", FreeType);

  llvm::Value *PtrArgument = Builder.CreateBitCast(
      Argument, llvm::PointerType::get(CGM.getLLVMContext(), 0), "del.cast");
  Builder.CreateCall(Free, {PtrArgument});
  return nullptr;
}

//===----------------------------------------------------------------------===//
// C++ throw 表达式
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitCXXThrowExpr(CXXThrowExpr *ThrowExpression) {
  if (!ThrowExpression) {
    return nullptr;
  }

  // 简化实现：调用 __cxa_throw 或 __cxa_rethrow
  if (ThrowExpression->getSubExpr()) {
    // throw expr: 分配异常对象并抛出
    llvm::Value *ExceptionValue = EmitExpr(ThrowExpression->getSubExpr());

    // 简化：使用 __cxa_begin_catch / __cxa_throw 的占位
    // 实际实现需要调用 __cxa_allocate_exception + 构造 + __cxa_throw
    // 这里简化为 unreachable（throw 后不会继续执行）
    if (haveInsertPoint()) {
      Builder.CreateUnreachable();
    }
  } else {
    // throw; （rethrow）
    // 简化：标记为 unreachable
    if (haveInsertPoint()) {
      Builder.CreateUnreachable();
    }
  }

  // throw 后创建新的基本块（dead code）
  if (haveInsertPoint()) {
    llvm::BasicBlock *AfterThrow = createBasicBlock("throw.cont");
    Builder.SetInsertPoint(AfterThrow);
  }

  return nullptr;
}

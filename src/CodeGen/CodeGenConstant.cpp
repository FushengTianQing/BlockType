//===--- CodeGenConstant.cpp - Constant Expression CodeGen -----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CodeGenConstant.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 常量生成主接口
//===----------------------------------------------------------------------===//

llvm::Constant *CodeGenConstant::EmitConstant(Expr *E) {
  if (!E) return nullptr;

  // 按 Expr 类型分派
  switch (E->getKind()) {
  case ASTNode::NodeKind::IntegerLiteralKind:
    return EmitIntLiteral(llvm::cast<IntegerLiteral>(E));
  case ASTNode::NodeKind::FloatingLiteralKind:
    return EmitFloatLiteral(llvm::cast<FloatingLiteral>(E));
  case ASTNode::NodeKind::StringLiteralKind:
    return EmitStringLiteral(llvm::cast<StringLiteral>(E));
  case ASTNode::NodeKind::CharacterLiteralKind:
    return EmitCharLiteral(llvm::cast<CharacterLiteral>(E));
  case ASTNode::NodeKind::CXXBoolLiteralKind:
    return EmitBoolLiteral(llvm::cast<CXXBoolLiteral>(E)->getValue());
  case ASTNode::NodeKind::InitListExprKind:
    return EmitInitListExpr(llvm::cast<InitListExpr>(E));
  case ASTNode::NodeKind::DeclRefExprKind: {
    auto *DRE = llvm::cast<DeclRefExpr>(E);
    if (auto *ECD = llvm::dyn_cast<EnumConstantDecl>(DRE->getDecl())) {
      if (ECD->hasVal()) {
        return llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(getLLVMContext()), ECD->getVal());
      }
    }
    break;
  }
  case ASTNode::NodeKind::CastExprKind: {
    auto *CE = llvm::cast<CastExpr>(E);
    llvm::Constant *SubConst = EmitConstant(CE->getSubExpr());
    if (!SubConst) return nullptr;
    llvm::Type *DestTy = CGM.getTypes().ConvertType(CE->getType());
    if (DestTy && SubConst->getType() != DestTy) {
      if (SubConst->getType()->isIntegerTy() && DestTy->isIntegerTy()) {
        return llvm::ConstantInt::get(DestTy,
            SubConst->getUniqueInteger().sextOrTrunc(
                DestTy->getIntegerBitWidth()));
      }
      if (SubConst->getType()->isPointerTy() && DestTy->isPointerTy()) {
        return llvm::ConstantExpr::getBitCast(SubConst, DestTy);
      }
    }
    return SubConst;
  }
  case ASTNode::NodeKind::UnaryOperatorKind: {
    auto *UO = llvm::cast<UnaryOperator>(E);
    llvm::Constant *SubConst = EmitConstant(UO->getSubExpr());
    if (!SubConst) return nullptr;
    switch (UO->getOpcode()) {
    case UnaryOpKind::Minus:
      if (SubConst->getType()->isIntegerTy())
        return llvm::ConstantInt::get(SubConst->getType(),
            -SubConst->getUniqueInteger());
      if (SubConst->getType()->isFloatingPointTy()) {
        auto *FP = llvm::cast<llvm::ConstantFP>(SubConst);
        return llvm::ConstantFP::get(getLLVMContext(),
            llvm::APFloat(-FP->getValueAPF().convertToDouble()));
      }
      return SubConst;
    case UnaryOpKind::Not:
      return llvm::ConstantExpr::getNot(SubConst);
    case UnaryOpKind::LNot: {
      bool IsZero = SubConst->isNullValue();
      return llvm::ConstantInt::get(llvm::Type::getInt1Ty(getLLVMContext()),
                                     !IsZero);
    }
    default:
      return SubConst;
    }
  }
  case ASTNode::NodeKind::UnaryExprOrTypeTraitExprKind: {
    auto *SE = llvm::cast<UnaryExprOrTypeTraitExpr>(E);
    QualType ArgTy = SE->getTypeOfArgument();
    if (ArgTy.isNull()) return nullptr;

    uint64_t Val = 0;
    if (SE->getTraitKind() == UnaryExprOrTypeTrait::SizeOf) {
      Val = CGM.getTarget().getTypeSize(ArgTy);
    } else {
      Val = CGM.getTarget().getTypeAlign(ArgTy);
    }
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(getLLVMContext()), Val);
  }
  default:
    break;
  }

  return nullptr;
}

llvm::Constant *CodeGenConstant::EmitConstantForType(Expr *E, QualType DestType) {
  llvm::Constant *C = EmitConstant(E);
  if (!C) return nullptr;

  if (DestType.isNull()) return C;

  llvm::Type *DestTy = CGM.getTypes().ConvertType(DestType);
  if (!DestTy || C->getType() == DestTy) return C;

  if (C->getType()->isIntegerTy() && DestTy->isIntegerTy()) {
    return llvm::ConstantInt::get(DestTy,
        C->getUniqueInteger().sextOrTrunc(DestTy->getIntegerBitWidth()));
  }
  if (C->getType()->isPointerTy() && DestTy->isPointerTy()) {
    return llvm::ConstantExpr::getBitCast(C, DestTy);
  }
  if (C->getType()->isFloatingPointTy() && DestTy->isFloatingPointTy()) {
    auto *FP = llvm::dyn_cast<llvm::ConstantFP>(C);
    if (FP) {
      return llvm::ConstantFP::get(DestTy, FP->getValueAPF());
    }
  }

  return C;
}

//===----------------------------------------------------------------------===//
// 字面量常量
//===----------------------------------------------------------------------===//

llvm::Constant *CodeGenConstant::EmitIntLiteral(IntegerLiteral *IL) {
  if (!IL) return nullptr;
  llvm::Type *Ty = CGM.getTypes().ConvertType(IL->getType());
  if (!Ty) Ty = llvm::Type::getInt32Ty(getLLVMContext());
  return llvm::ConstantInt::get(Ty, IL->getValue());
}

llvm::Constant *CodeGenConstant::EmitFloatLiteral(FloatingLiteral *FL) {
  if (!FL) return nullptr;
  llvm::Type *Ty = CGM.getTypes().ConvertType(FL->getType());
  if (!Ty) Ty = llvm::Type::getDoubleTy(getLLVMContext());
  return llvm::ConstantFP::get(Ty, FL->getValue());
}

llvm::Constant *CodeGenConstant::EmitStringLiteral(StringLiteral *SL) {
  if (!SL) return nullptr;

  llvm::StringRef Str = SL->getValue();

  // 查找字符串池，如果已有相同内容的全局变量则复用
  auto &Pool = CGM.getStringLiteralPool();
  auto It = Pool.find(Str);
  if (It != Pool.end()) {
    // 已存在：复用已有的全局变量
    llvm::GlobalVariable *ExistingGV = It->second;
    llvm::Constant *StrConstant = ExistingGV->getInitializer();
    llvm::SmallVector<llvm::Constant *, 2> Indices;
    Indices.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(getLLVMContext()), 0));
    Indices.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(getLLVMContext()), 0));
    return llvm::ConstantExpr::getGetElementPtr(StrConstant->getType(), ExistingGV, Indices);
  }

  // 创建新的字符串全局变量
  llvm::Constant *StrConstant = llvm::ConstantDataArray::getString(
      getLLVMContext(), Str);

  auto *GV = new llvm::GlobalVariable(
      *CGM.getModule(), StrConstant->getType(), true,
      llvm::GlobalValue::PrivateLinkage, StrConstant, ".str");

  // 加入字符串池
  Pool[Str] = GV;

  // GEP: get pointer to first character
  llvm::SmallVector<llvm::Constant *, 2> Indices;
  Indices.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(getLLVMContext()), 0));
  Indices.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(getLLVMContext()), 0));
  return llvm::ConstantExpr::getGetElementPtr(StrConstant->getType(), GV, Indices);
}

llvm::Constant *CodeGenConstant::EmitBoolLiteral(bool Value) {
  return llvm::ConstantInt::get(llvm::Type::getInt1Ty(getLLVMContext()), Value);
}

llvm::Constant *CodeGenConstant::EmitCharLiteral(CharacterLiteral *CL) {
  if (!CL) return nullptr;
  return llvm::ConstantInt::get(
      llvm::Type::getInt32Ty(getLLVMContext()), CL->getValue());
}

llvm::Constant *CodeGenConstant::EmitNullPointer(QualType T) {
  llvm::Type *Ty = CGM.getTypes().ConvertType(T);
  if (!Ty || !Ty->isPointerTy())
    Ty = llvm::PointerType::get(getLLVMContext(), 0);
  return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(Ty));
}

//===----------------------------------------------------------------------===//
// 聚合常量
//===----------------------------------------------------------------------===//

llvm::Constant *CodeGenConstant::EmitInitListExpr(InitListExpr *ILE) {
  if (!ILE) return nullptr;

  llvm::SmallVector<llvm::Constant *, 16> Elts;
  for (Expr *Init : ILE->getInits()) {
    llvm::Constant *C = EmitConstant(Init);
    if (!C) {
      C = llvm::Constant::getNullValue(
          CGM.getTypes().ConvertType(Init->getType()));
    }
    if (C) Elts.push_back(C);
  }

  if (Elts.empty()) return nullptr;

  llvm::Type *Ty = CGM.getTypes().ConvertType(ILE->getType());
  if (!Ty) return nullptr;

  if (auto *STy = llvm::dyn_cast<llvm::StructType>(Ty)) {
    return llvm::ConstantStruct::get(STy, Elts);
  }
  if (auto *ATy = llvm::dyn_cast<llvm::ArrayType>(Ty)) {
    return llvm::ConstantArray::get(ATy, Elts);
  }

  if (Elts.size() == 1) return Elts[0];

  return nullptr;
}

//===----------------------------------------------------------------------===//
// 零值 / 特殊常量
//===----------------------------------------------------------------------===//

llvm::Constant *CodeGenConstant::EmitZeroValue(QualType T) {
  llvm::Type *Ty = CGM.getTypes().ConvertType(T);
  if (!Ty) return nullptr;
  return llvm::Constant::getNullValue(Ty);
}

llvm::Constant *CodeGenConstant::EmitUndefValue(QualType T) {
  llvm::Type *Ty = CGM.getTypes().ConvertType(T);
  if (!Ty) return nullptr;
  return llvm::UndefValue::get(Ty);
}

//===------------------------------------------------------------------===//
// 类型转换常量
//===------------------------------------------------------------------===//

llvm::Constant *CodeGenConstant::EmitIntCast(llvm::Constant *C,
                                             QualType /*From*/,
                                             QualType To) {
  if (!C) return nullptr;
  llvm::Type *DestTy = CGM.getTypes().ConvertType(To);
  if (!DestTy || !DestTy->isIntegerTy()) return C;

  llvm::Type *SrcTy = C->getType();
  if (!SrcTy->isIntegerTy()) return C;
  if (SrcTy == DestTy) return C;

  unsigned DstBits = DestTy->getIntegerBitWidth();
  llvm::APInt Val = C->getUniqueInteger();
  Val = Val.sextOrTrunc(DstBits);
  return llvm::ConstantInt::get(DestTy, Val);
}

llvm::Constant *CodeGenConstant::EmitFloatToIntCast(llvm::Constant *C,
                                                     QualType /*From*/,
                                                     QualType To) {
  if (!C) return nullptr;
  llvm::Type *DestTy = CGM.getTypes().ConvertType(To);
  if (!DestTy || !DestTy->isIntegerTy()) return C;

  auto *FP = llvm::dyn_cast<llvm::ConstantFP>(C);
  if (!FP) return C;

  llvm::APFloat APF = FP->getValueAPF();
  llvm::APSInt Result(DestTy->getIntegerBitWidth(), false);
  bool IsExact = false;
  APF.convertToInteger(Result, llvm::APFloat::rmTowardZero, &IsExact);
  return llvm::ConstantInt::get(DestTy, Result);
}

llvm::Constant *CodeGenConstant::EmitIntToFloatCast(llvm::Constant *C,
                                                     QualType /*From*/,
                                                     QualType To) {
  if (!C) return nullptr;
  llvm::Type *DestTy = CGM.getTypes().ConvertType(To);
  if (!DestTy || !DestTy->isFloatingPointTy()) return C;

  llvm::APInt Val = C->getUniqueInteger();
  llvm::APFloat APF(DestTy->getFltSemantics(),
                    llvm::APSInt(Val, false));
  return llvm::ConstantFP::get(DestTy, APF);
}

//===------------------------------------------------------------------===//
// 工具方法
//===------------------------------------------------------------------===//

llvm::ConstantPointerNull *CodeGenConstant::GetNullPointer(QualType T) {
  llvm::Type *Ty = CGM.getTypes().ConvertType(T);
  if (!Ty || !Ty->isPointerTy())
    Ty = llvm::PointerType::get(getLLVMContext(), 0);
  return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(Ty));
}

llvm::ConstantInt *CodeGenConstant::GetIntZero(QualType T) {
  llvm::Type *Ty = CGM.getTypes().ConvertType(T);
  if (!Ty || !Ty->isIntegerTy())
    Ty = llvm::Type::getInt32Ty(getLLVMContext());
  return llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(Ty, 0));
}

llvm::ConstantInt *CodeGenConstant::GetIntOne(QualType T) {
  llvm::Type *Ty = CGM.getTypes().ConvertType(T);
  if (!Ty || !Ty->isIntegerTy())
    Ty = llvm::Type::getInt32Ty(getLLVMContext());
  return llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(Ty, 1));
}

llvm::LLVMContext &CodeGenConstant::getLLVMContext() const {
  return CGM.getLLVMContext();
}

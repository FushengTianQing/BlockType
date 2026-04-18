//===--- CodeGenFunction.cpp - Function-level CodeGen ---------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CodeGenConstant.h"
#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 构造
//===----------------------------------------------------------------------===//

CodeGenFunction::CodeGenFunction(CodeGenModule &ModuleRef)
    : CGM(ModuleRef), Builder(ModuleRef.getLLVMContext()) {}

//===----------------------------------------------------------------------===//
// 函数体生成
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitFunctionBody(FunctionDecl *FunctionDecl,
                                        llvm::Function *Function) {
  if (!FunctionDecl || !Function) {
    return;
  }

  CurFn = Function;
  CurFD = FunctionDecl;

  // 创建入口块
  llvm::BasicBlock *EntryBlock =
      llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry", Function);
  Builder.SetInsertPoint(EntryBlock);

  // 为返回值创建 alloca（非 void 返回类型）
  QualType ReturnType = FunctionDecl->getType().isNull()
                            ? QualType()
                            : FunctionDecl->getType();
  // 获取函数类型的返回类型
  if (auto *FunctionType =
          llvm::dyn_cast<blocktype::FunctionType>(ReturnType.getTypePtr())) {
    ReturnType =
        QualType(FunctionType->getReturnType(), Qualifier::None);
  }

  if (!ReturnType.isNull() && !ReturnType->isVoidType()) {
    ReturnBlock =
        llvm::BasicBlock::Create(CGM.getLLVMContext(), "return", Function);
    ReturnValue = CreateAlloca(ReturnType, "retval");
  }

  // 为函数参数创建 alloca 并拷贝参数值
  unsigned ArgIndex = 0;
  for (auto &Arg : Function->args()) {
    if (ArgIndex < FunctionDecl->getNumParams()) {
      ParmVarDecl *ParamDecl = FunctionDecl->getParamDecl(ArgIndex);
      llvm::AllocaInst *AllocaInst =
          CreateAlloca(ParamDecl->getType(), ParamDecl->getName());
      Builder.CreateStore(&Arg, AllocaInst);
      setLocalDecl(ParamDecl, AllocaInst);
    }
    ++ArgIndex;
  }

  // 生成函数体
  if (Stmt *Body = FunctionDecl->getBody()) {
    EmitStmt(Body);
  }

  // 处理 ReturnBlock
  if (ReturnBlock) {
    if (haveInsertPoint()) {
      Builder.CreateBr(ReturnBlock);
    }
    Builder.SetInsertPoint(ReturnBlock);
    llvm::Type *ReturnTy = CGM.getTypes().ConvertType(ReturnType);
    llvm::Value *RetVal = Builder.CreateLoad(ReturnTy, ReturnValue, "ret");
    Builder.CreateRet(RetVal);
  } else {
    // void 函数：确保有终止指令
    if (haveInsertPoint()) {
      Builder.CreateRetVoid();
    }
  }
}

//===----------------------------------------------------------------------===//
// 表达式生成
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitExpr(Expr *Expression) {
  if (!Expression) {
    return nullptr;
  }

  switch (Expression->getKind()) {
  // 字面量
  case ASTNode::NodeKind::IntegerLiteralKind:
    return CGM.getConstants().EmitIntLiteral(
        llvm::cast<IntegerLiteral>(Expression));
  case ASTNode::NodeKind::FloatingLiteralKind:
    return CGM.getConstants().EmitFloatLiteral(
        llvm::cast<FloatingLiteral>(Expression));
  case ASTNode::NodeKind::StringLiteralKind:
    return CGM.getConstants().EmitStringLiteral(
        llvm::cast<StringLiteral>(Expression));
  case ASTNode::NodeKind::CharacterLiteralKind:
    return CGM.getConstants().EmitCharLiteral(
        llvm::cast<CharacterLiteral>(Expression));
  case ASTNode::NodeKind::CXXBoolLiteralKind:
    return CGM.getConstants().EmitBoolLiteral(
        llvm::cast<CXXBoolLiteral>(Expression)->getValue());
  case ASTNode::NodeKind::CXXNullPtrLiteralKind:
    return CGM.getConstants().EmitNullPointer(Expression->getType());

  // 引用
  case ASTNode::NodeKind::DeclRefExprKind:
    return EmitDeclRefExpr(llvm::cast<DeclRefExpr>(Expression));
  case ASTNode::NodeKind::MemberExprKind:
    return EmitMemberExpr(llvm::cast<MemberExpr>(Expression));
  case ASTNode::NodeKind::ArraySubscriptExprKind:
    return EmitArraySubscriptExpr(llvm::cast<ArraySubscriptExpr>(Expression));

  // 运算符
  case ASTNode::NodeKind::BinaryOperatorKind:
    return EmitBinaryOperator(llvm::cast<BinaryOperator>(Expression));
  case ASTNode::NodeKind::UnaryOperatorKind:
    return EmitUnaryOperator(llvm::cast<UnaryOperator>(Expression));
  case ASTNode::NodeKind::ConditionalOperatorKind:
    return EmitConditionalOperator(llvm::cast<ConditionalOperator>(Expression));

  // 调用
  case ASTNode::NodeKind::CallExprKind:
  case ASTNode::NodeKind::CXXMemberCallExprKind:
    return EmitCallExpr(llvm::cast<CallExpr>(Expression));

  // C++ 特有
  case ASTNode::NodeKind::CXXThisExprKind:
    return EmitCXXThisExpr(llvm::cast<CXXThisExpr>(Expression));
  case ASTNode::NodeKind::CXXConstructExprKind:
  case ASTNode::NodeKind::CXXTemporaryObjectExprKind:
    return EmitCXXConstructExpr(llvm::cast<CXXConstructExpr>(Expression));
  case ASTNode::NodeKind::CXXNewExprKind:
    return EmitCXXNewExpr(llvm::cast<CXXNewExpr>(Expression));
  case ASTNode::NodeKind::CXXDeleteExprKind:
    return EmitCXXDeleteExpr(llvm::cast<CXXDeleteExpr>(Expression));
  case ASTNode::NodeKind::CXXThrowExprKind:
    return EmitCXXThrowExpr(llvm::cast<CXXThrowExpr>(Expression));

  // 类型转换
  case ASTNode::NodeKind::CXXStaticCastExprKind:
  case ASTNode::NodeKind::CXXDynamicCastExprKind:
  case ASTNode::NodeKind::CXXConstCastExprKind:
  case ASTNode::NodeKind::CXXReinterpretCastExprKind:
  case ASTNode::NodeKind::CStyleCastExprKind:
    return EmitCastExpr(llvm::cast<CastExpr>(Expression));

  // 初始化列表
  case ASTNode::NodeKind::InitListExprKind:
    return EmitInitListExpr(llvm::cast<InitListExpr>(Expression));

  default:
    break;
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// 语句生成
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitStmt(Stmt *Statement) {
  if (!Statement) {
    return;
  }

  switch (Statement->getKind()) {
  case ASTNode::NodeKind::CompoundStmtKind:
    EmitCompoundStmt(llvm::cast<CompoundStmt>(Statement));
    break;
  case ASTNode::NodeKind::ReturnStmtKind:
    EmitReturnStmt(llvm::cast<ReturnStmt>(Statement));
    break;
  case ASTNode::NodeKind::ExprStmtKind: {
    auto *ExpressionStatement = llvm::cast<ExprStmt>(Statement);
    EmitExpr(ExpressionStatement->getExpr());
    break;
  }
  case ASTNode::NodeKind::DeclStmtKind:
    EmitDeclStmt(llvm::cast<DeclStmt>(Statement));
    break;
  case ASTNode::NodeKind::IfStmtKind:
    EmitIfStmt(llvm::cast<IfStmt>(Statement));
    break;
  case ASTNode::NodeKind::SwitchStmtKind:
    EmitSwitchStmt(llvm::cast<SwitchStmt>(Statement));
    break;
  case ASTNode::NodeKind::ForStmtKind:
    EmitForStmt(llvm::cast<ForStmt>(Statement));
    break;
  case ASTNode::NodeKind::WhileStmtKind:
    EmitWhileStmt(llvm::cast<WhileStmt>(Statement));
    break;
  case ASTNode::NodeKind::DoStmtKind:
    EmitDoStmt(llvm::cast<DoStmt>(Statement));
    break;
  case ASTNode::NodeKind::BreakStmtKind:
    EmitBreakStmt(llvm::cast<BreakStmt>(Statement));
    break;
  case ASTNode::NodeKind::ContinueStmtKind:
    EmitContinueStmt(llvm::cast<ContinueStmt>(Statement));
    break;
  case ASTNode::NodeKind::GotoStmtKind:
    EmitGotoStmt(llvm::cast<GotoStmt>(Statement));
    break;
  case ASTNode::NodeKind::LabelStmtKind:
    EmitLabelStmt(llvm::cast<LabelStmt>(Statement));
    break;
  case ASTNode::NodeKind::NullStmtKind:
    // 空语句，什么都不做
    break;
  case ASTNode::NodeKind::CXXTryStmtKind:
    EmitCXXTryStmt(llvm::cast<CXXTryStmt>(Statement));
    break;
  case ASTNode::NodeKind::CXXForRangeStmtKind:
    EmitCXXForRangeStmt(llvm::cast<CXXForRangeStmt>(Statement));
    break;
  case ASTNode::NodeKind::CoreturnStmtKind:
    EmitCoreturnStmt(llvm::cast<CoreturnStmt>(Statement));
    break;
  case ASTNode::NodeKind::CoyieldStmtKind:
    EmitCoyieldStmt(llvm::cast<CoyieldStmt>(Statement));
    break;
  case ASTNode::NodeKind::CaseStmtKind:
  case ASTNode::NodeKind::DefaultStmtKind:
    // CaseStmt/DefaultStmt 由 EmitSwitchStmt 内部处理
    // 如果到达这里说明在 switch 外部，忽略
    break;
  case ASTNode::NodeKind::CXXCatchStmtKind:
    // CXXCatchStmt 由 EmitCXXTryStmt 内部处理
    break;
  default:
    break;
  }
}

void CodeGenFunction::EmitStmts(llvm::ArrayRef<Stmt *> Statements) {
  for (Stmt *Statement : Statements) {
    EmitStmt(Statement);
  }
}

//===----------------------------------------------------------------------===//
// 基本块管理
//===----------------------------------------------------------------------===//

llvm::BasicBlock *CodeGenFunction::createBasicBlock(llvm::StringRef Name) {
  return llvm::BasicBlock::Create(CGM.getLLVMContext(), Name, CurFn);
}

void CodeGenFunction::EmitBlock(llvm::BasicBlock *BasicBlock) {
  // 如果当前块没有终止指令，创建一个分支
  if (haveInsertPoint()) {
    llvm::Instruction *Terminator =
        Builder.GetInsertBlock()->getTerminator();
    if (!Terminator) {
      Builder.CreateBr(BasicBlock);
    }
  }
  CurFn->insert(CurFn->end(), BasicBlock);
  Builder.SetInsertPoint(BasicBlock);
}

//===----------------------------------------------------------------------===//
// 局部变量管理
//===----------------------------------------------------------------------===//

llvm::AllocaInst *CodeGenFunction::CreateAlloca(QualType Type,
                                                  llvm::StringRef Name) {
  llvm::Type *LLVMType = CGM.getTypes().ConvertType(Type);
  if (!LLVMType) {
    return nullptr;
  }

  // 在函数入口块创建 alloca（参照 Clang 的 alloca 插入模式）
  llvm::BasicBlock *EntryBlock = &CurFn->getEntryBlock();
  llvm::IRBuilder<>::InsertPoint SavedIP = Builder.saveIP();
  llvm::Instruction *InsertPos = EntryBlock->getFirstNonPHIOrDbg();
  if (InsertPos) {
    Builder.SetInsertPoint(InsertPos);
  } else {
    Builder.SetInsertPoint(EntryBlock);
  }
  llvm::AllocaInst *AllocaInst = Builder.CreateAlloca(LLVMType, nullptr, Name);
  Builder.restoreIP(SavedIP);
  return AllocaInst;
}

void CodeGenFunction::setLocalDecl(VarDecl *VariableDecl,
                                    llvm::AllocaInst *Alloca) {
  LocalDecls[VariableDecl] = Alloca;
}

llvm::AllocaInst *CodeGenFunction::getLocalDecl(VarDecl *VariableDecl) const {
  auto Iterator = LocalDecls.find(VariableDecl);
  if (Iterator != LocalDecls.end()) {
    return Iterator->second;
  }
  return nullptr;
}

llvm::Value *CodeGenFunction::LoadLocalVar(VarDecl *VariableDecl) {
  llvm::AllocaInst *Alloca = getLocalDecl(VariableDecl);
  if (!Alloca) {
    return nullptr;
  }
  llvm::Type *Type = CGM.getTypes().ConvertType(VariableDecl->getType());
  return Builder.CreateLoad(Type, Alloca, VariableDecl->getName());
}

void CodeGenFunction::StoreLocalVar(VarDecl *VariableDecl,
                                     llvm::Value *Value) {
  llvm::AllocaInst *Alloca = getLocalDecl(VariableDecl);
  if (Alloca && Value) {
    Builder.CreateStore(Value, Alloca);
  }
}

//===----------------------------------------------------------------------===//
// 声明引用表达式
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitDeclRefExpr(DeclRefExpr *Expression) {
  if (!Expression) {
    return nullptr;
  }

  ValueDecl *Declaration = Expression->getDecl();
  if (!Declaration) {
    return nullptr;
  }

  // 局部变量 / 参数
  if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(Declaration)) {
    if (auto *Alloca = getLocalDecl(VariableDecl)) {
      llvm::Type *Type = CGM.getTypes().ConvertType(VariableDecl->getType());
      return Builder.CreateLoad(Type, Alloca, VariableDecl->getName());
    }
    // 全局变量
    if (auto *GlobalVar = CGM.GetGlobalVar(VariableDecl)) {
      return Builder.CreateLoad(
          CGM.getTypes().ConvertType(VariableDecl->getType()), GlobalVar,
          VariableDecl->getName());
    }
  }

  // 函数引用
  if (auto *FunctionDeclaration =
          llvm::dyn_cast<FunctionDecl>(Declaration)) {
    return CGM.GetOrCreateFunctionDecl(FunctionDeclaration);
  }

  // 枚举常量 — 使用枚举的底层类型而非硬编码 i32
  if (auto *EnumConstant = llvm::dyn_cast<EnumConstantDecl>(Declaration)) {
    llvm::Type *EnumTy = CGM.getTypes().ConvertType(EnumConstant->getType());
    if (!EnumTy) {
      EnumTy = llvm::Type::getInt32Ty(CGM.getLLVMContext());
    }
    return llvm::ConstantInt::get(EnumTy, EnumConstant->getVal());
  }

  return nullptr;
}

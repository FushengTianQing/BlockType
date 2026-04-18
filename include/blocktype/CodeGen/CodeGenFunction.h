//===--- CodeGenFunction.h - Function-level CodeGen ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CodeGenFunction class for generating LLVM IR
// within a single function body.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"

namespace blocktype {

class CodeGenModule;

/// IRBuilderTy — 类型别名，简化 IRBuilder 使用。
using IRBuilderTy = llvm::IRBuilder<>;

/// CodeGenFunction — 函数级代码生成引擎。
///
/// 职责（参照 Clang CodeGenFunction）：
/// 1. 管理当前函数的 IRBuilder（插入点）
/// 2. 维护局部变量映射表（VarDecl → AllocaInst）
/// 3. 管理控制流栈（break/continue/return 目标）
/// 4. 生成表达式求值代码（EmitExpr → llvm::Value*）
/// 5. 生成语句执行代码（EmitStmt）
class CodeGenFunction {
  CodeGenModule &CGM;
  IRBuilderTy Builder;

  /// 当前正在生成的 LLVM 函数。
  llvm::Function *CurFn = nullptr;

  /// 当前函数的 FunctionDecl。
  FunctionDecl *CurFD = nullptr;

  /// 当前函数的 this 指针值（用于 CXXThisExpr）。
  llvm::Value *ThisValue = nullptr;

  /// 局部变量映射：VarDecl → AllocaInst*
  llvm::DenseMap<const VarDecl *, llvm::AllocaInst *> LocalDecls;

  //===------------------------------------------------------------------===//
  // 控制流栈
  //===------------------------------------------------------------------===//

  /// Break/Continue 跳转目标栈
  struct BreakContinue {
    llvm::BasicBlock *BreakBB;
    llvm::BasicBlock *ContinueBB;
  };
  llvm::SmallVector<BreakContinue, 4> BreakContinueStack;

  /// Return 目标基本块
  llvm::BasicBlock *ReturnBlock = nullptr;

  /// 返回值 alloca
  llvm::AllocaInst *ReturnValue = nullptr;

  /// Label → BasicBlock 映射（用于 goto/label 前向引用）
  llvm::DenseMap<const LabelDecl *, llvm::BasicBlock *> LabelMap;

  //===------------------------------------------------------------------===//
  // 异常处理上下文
  //===------------------------------------------------------------------===//

  /// 当前处于 try 块中的 invoke 目标对（NormalBB, UnwindBB）
  /// 为空表示不在 try 块中
  struct InvokeTarget {
    llvm::BasicBlock *NormalBB;
    llvm::BasicBlock *UnwindBB;
  };
  llvm::SmallVector<InvokeTarget, 4> InvokeTargets;

public:
  explicit CodeGenFunction(CodeGenModule &M);

  //===------------------------------------------------------------------===//
  // 函数体生成
  //===------------------------------------------------------------------===//

  /// 生成函数的完整 LLVM IR。
  void EmitFunctionBody(FunctionDecl *FD, llvm::Function *Fn);

  //===------------------------------------------------------------------===//
  // 表达式生成（返回 llvm::Value*）
  //===------------------------------------------------------------------===//

  /// 生成表达式的求值代码，返回结果值。
  llvm::Value *EmitExpr(Expr *E);

  //===------------------------------------------------------------------===//
  // 语句生成
  //===------------------------------------------------------------------===//

  /// 生成语句的执行代码。
  void EmitStmt(Stmt *S);

  /// 生成语句序列。
  void EmitStmts(llvm::ArrayRef<Stmt *> Stmts);

  //===------------------------------------------------------------------===//
  // 基本块管理
  //===------------------------------------------------------------------===//

  /// 创建一个新的基本块。
  llvm::BasicBlock *createBasicBlock(llvm::StringRef Name);

  /// 将基本块追加到当前函数并设置插入点。
  void EmitBlock(llvm::BasicBlock *BB);

  /// 获取当前基本块。
  llvm::BasicBlock *getCurrentBlock() const { return Builder.GetInsertBlock(); }

  /// 当前是否有有效的插入点。
  bool haveInsertPoint() const {
    return Builder.GetInsertBlock() != nullptr;
  }

  //===------------------------------------------------------------------===//
  // 局部变量管理
  //===------------------------------------------------------------------===//

  /// 为局部变量创建 alloca 指令。
  llvm::AllocaInst *CreateAlloca(QualType T, llvm::StringRef Name = "");

  /// 注册局部变量的 alloca。
  void setLocalDecl(VarDecl *VD, llvm::AllocaInst *Alloca);

  /// 获取局部变量的 alloca。
  llvm::AllocaInst *getLocalDecl(VarDecl *VD) const;

  /// 加载局部变量的值。
  llvm::Value *LoadLocalVar(VarDecl *VD);

  /// 存储值到局部变量。
  void StoreLocalVar(VarDecl *VD, llvm::Value *Val);

  //===------------------------------------------------------------------===//
  // 辅助方法
  //===------------------------------------------------------------------===//

  IRBuilderTy &getBuilder() { return Builder; }
  CodeGenModule &getCGM() const { return CGM; }
  llvm::Function *getCurrentFunction() const { return CurFn; }
  FunctionDecl *getCurrentFunctionDecl() const { return CurFD; }

  /// 设置当前 LLVM 函数（供 CGCXX 的构造/析构函数生成使用）
  void setCurrentFunction(llvm::Function *Fn) { CurFn = Fn; }

  /// 设置 this 指针（用于 CXXThisExpr 求值）
  void setThisPointer(llvm::Value *This) { ThisValue = This; }

  /// 获取 this 指针
  llvm::Value *getThisPointer() const { return ThisValue; }

private:
  //===------------------------------------------------------------------===//
  // 表达式生成子方法
  //===------------------------------------------------------------------===//

  llvm::Value *EmitBinaryOperator(BinaryOperator *BO);
  llvm::Value *EmitUnaryOperator(UnaryOperator *UO);
  llvm::Value *EmitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E);
  llvm::Value *EmitCallExpr(CallExpr *CE);
  llvm::Value *EmitMemberExpr(MemberExpr *ME);
  llvm::Value *EmitDeclRefExpr(DeclRefExpr *DRE);
  llvm::Value *EmitCastExpr(CastExpr *CE);
  llvm::Value *EmitArraySubscriptExpr(ArraySubscriptExpr *ASE);
  llvm::Value *EmitConditionalOperator(ConditionalOperator *CO);
  llvm::Value *EmitInitListExpr(InitListExpr *ILE);
  llvm::Value *EmitCXXConstructExpr(CXXConstructExpr *CCE);
  llvm::Value *EmitCXXThisExpr(CXXThisExpr *TE);
  llvm::Value *EmitCXXNewExpr(CXXNewExpr *NE);
  llvm::Value *EmitCXXDeleteExpr(CXXDeleteExpr *DE);
  llvm::Value *EmitCXXThrowExpr(CXXThrowExpr *TE);

  //===------------------------------------------------------------------===//
  // 二元运算辅助
  //===------------------------------------------------------------------===//

  llvm::Value *EmitLogicalAnd(BinaryOperator *BO);
  llvm::Value *EmitLogicalOr(BinaryOperator *BO);
  llvm::Value *EmitAssignment(BinaryOperator *BO);
  llvm::Value *EmitCompoundAssignment(BinaryOperator *BO);
  llvm::Value *EmitIncDec(UnaryOperator *UO);

  //===------------------------------------------------------------------===//
  // LValue 辅助
  //===------------------------------------------------------------------===//

  /// 获取表达式的地址（lvalue），用于赋值、取地址等。
  llvm::Value *EmitLValue(Expr *E);

  //===------------------------------------------------------------------===//
  // 语句生成子方法
  //===------------------------------------------------------------------===//

  void EmitCompoundStmt(CompoundStmt *CS);
  void EmitIfStmt(IfStmt *IS);
  void EmitSwitchStmt(SwitchStmt *SS);
  void EmitForStmt(ForStmt *FS);
  void EmitWhileStmt(WhileStmt *WS);
  void EmitDoStmt(DoStmt *DS);
  void EmitReturnStmt(ReturnStmt *RS);
  void EmitDeclStmt(DeclStmt *DS);
  void EmitBreakStmt(BreakStmt *BS);
  void EmitContinueStmt(ContinueStmt *CS);
  void EmitGotoStmt(GotoStmt *GS);
  void EmitLabelStmt(LabelStmt *LS);
  void EmitCXXTryStmt(CXXTryStmt *TS);
  void EmitCXXForRangeStmt(CXXForRangeStmt *FRS);
  void EmitCoreturnStmt(CoreturnStmt *CRS);
  void EmitCoyieldStmt(CoyieldStmt *CYS);

  //===------------------------------------------------------------------===//
  // 控制流辅助
  //===------------------------------------------------------------------===//

  /// 将值转换为 i1 布尔值（用于 if/while/for 条件判断）
  llvm::Value *EmitConversionToBool(llvm::Value *SrcValue, QualType SrcType);

  /// 生成条件变量声明（if/switch/while/for 的 CondVar）
  void EmitCondVarDecl(VarDecl *CondVariable);

  /// 获取或创建 label 对应的 BasicBlock
  llvm::BasicBlock *getOrCreateLabelBB(LabelDecl *Label);

  /// 当前是否处于 try 块中（需要生成 invoke 而非 call）
  bool isInTryBlock() const { return !InvokeTargets.empty(); }

  /// 获取当前 try 块的 invoke 目标
  const InvokeTarget &getCurrentInvokeTarget() const {
    return InvokeTargets.back();
  }

  /// 进入 try 块
  void pushInvokeTarget(llvm::BasicBlock *NormalBB,
                        llvm::BasicBlock *UnwindBB) {
    InvokeTargets.push_back({NormalBB, UnwindBB});
  }

  /// 离开 try 块
  void popInvokeTarget() { InvokeTargets.pop_back(); }
};

} // namespace blocktype

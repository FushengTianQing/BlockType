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

  /// 返回值 alloca（sret 模式下保存 sret 指针的 alloca）
  llvm::AllocaInst *ReturnValue = nullptr;

  /// 当前函数是否使用 sret 返回
  bool IsSRetFn = false;

  /// NRVO (Named Return Value Optimization) 候选变量集合。
  /// 如果一个局部变量被识别为 NRVO 候选，它直接使用 ReturnValue alloca，
  /// 避免返回时的 copy。
  llvm::SmallPtrSet<const VarDecl *, 4> NRVOCandidates;

  /// 查询变量是否是 NRVO 候选
  bool isNRVOCandidate(const VarDecl *VD) const {
    return NRVOCandidates.count(VD) > 0;
  }

  /// 分析函数体，识别 NRVO 候选变量
  void analyzeNRVOCandidates(Stmt *Body, QualType ReturnType);

  /// Label → BasicBlock 映射（用于 goto/label 前向引用）
  llvm::DenseMap<const LabelDecl *, llvm::BasicBlock *> LabelMap;

  /// Alloca 插入点 — 所有 alloca 指令统一在此位置之前插入。
  /// 在 EmitFunctionBody 中参数 alloca 完成后保存，避免每次 saveIP/restoreIP。
  llvm::AllocaInst *AllocaInsertPt = nullptr;

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

  //===------------------------------------------------------------------===//
  // Cleanup 栈（作用域结束时自动调用析构函数）
  //===------------------------------------------------------------------===//

  /// 记录需要析构的局部变量
  struct CleanupEntry {
    VarDecl *VD;
    CXXRecordDecl *RD;
  };
  llvm::SmallVector<CleanupEntry, 8> CleanupStack;

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

  /// 为局部变量创建 alloca 指令（使用 AllocaInsertPt 插入点）。
  llvm::AllocaInst *CreateAlloca(QualType T, llvm::StringRef Name = "");

  /// 为原始 LLVM 类型创建 alloca（使用 AllocaInsertPt 插入点）。
  /// 统一的 alloca 创建入口，所有 alloca 都应通过此方法或 QualType 版本。
  llvm::AllocaInst *CreateAlloca(llvm::Type *Ty, llvm::StringRef Name = "");

  /// 在 entry 块的 AllocaInsertPt 位置创建原始 LLVM 类型的 alloca。
  /// 内部实现，由 CreateAlloca 调用，外部不应直接使用。
  llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Type *Ty,
                                            llvm::StringRef Name = "");

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

  /// 设置 AllocaInsertPt（供 CGCXX 的构造函数生成使用）
  void setAllocaInsertPoint(llvm::AllocaInst *Pt) { AllocaInsertPt = Pt; }

  /// 设置 this 指针（用于 CXXThisExpr 求值）
  void setThisPointer(llvm::Value *This) { ThisValue = This; }

  /// 获取 this 指针
  llvm::Value *getThisPointer() const { return ThisValue; }

  /// 根据 isInTryBlock() 自动选择 call 或 invoke。
  llvm::CallBase *EmitCallOrInvoke(llvm::FunctionCallee Callee,
                                    llvm::ArrayRef<llvm::Value *> Args,
                                    llvm::StringRef Name = "");

  /// 生成 nounwind 的 call（用于析构函数等不抛异常的调用）。
  llvm::CallBase *EmitNounwindCall(llvm::FunctionCallee Callee,
                                    llvm::ArrayRef<llvm::Value *> Args,
                                    llvm::StringRef Name = "");

  /// 创建 BranchWeights 元数据（用于 [[likely]]/[[unlikely]] 分支提示）
  /// TrueWeight: true 分支权重, FalseWeight: false 分支权重
  static llvm::MDNode *createBranchWeights(llvm::LLVMContext &Ctx,
                                            unsigned TrueWeight,
                                            unsigned FalseWeight);

  /// 根据语句的 [[likely]]/[[unlikely]] 属性创建 BranchWeights 元数据
  /// ThenStmt: then 分支语句, HasElse: 是否有 else 分支
  static llvm::MDNode *createIfBranchWeights(llvm::LLVMContext &Ctx,
                                              const Stmt *ThenStmt,
                                              bool HasElse);

  /// 创建循环条件的 BranchWeights 元数据
  /// LoopBody: 循环体语句（[[likely]] 表示循环更可能继续）
  static llvm::MDNode *createLoopBranchWeights(llvm::LLVMContext &Ctx,
                                                 const Stmt *LoopBody);

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
  llvm::Value *EmitCXXConstructExpr(CXXConstructExpr *CCE,
                                     llvm::Value *DestPtr = nullptr);
  llvm::Value *EmitCXXThisExpr(CXXThisExpr *TE);
  llvm::Value *EmitCXXNewExpr(CXXNewExpr *NE);
  llvm::Value *EmitCXXDeleteExpr(CXXDeleteExpr *DE);
  llvm::Value *EmitCXXThrowExpr(CXXThrowExpr *TE);

  //===------------------------------------------------------------------===//
  // P7.1: C++23 expression generation
  //===------------------------------------------------------------------===//

  /// Generate code for a decay-copy expression (P0849R8).
  /// Creates a temporary by evaluating the subexpression and performing
  /// materialization.
  llvm::Value *EmitDecayCopyExpr(DecayCopyExpr *DCE);

  /// Generate code for an [[assume]] attribute (P1774R8).
  /// Emits llvm.assume intrinsic if the condition is evaluatable.
  void EmitAssumeAttr(Expr *Condition);

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

  /// 隐式标量类型转换：将 Src 从 SrcType 转换为 DstType
  /// 用于函数调用时实参到形参的隐式转换
  llvm::Value *EmitScalarConversion(llvm::Value *Src, QualType SrcType,
                                    QualType DstType);

  /// 变参默认参数提升 (default argument promotion)
  /// float → double, < 32-bit integer → int, bool → int
  /// 用于变参函数调用时可变参数部分的类型提升
  llvm::Value *emitDefaultArgPromotion(llvm::Value *Arg, QualType ArgType);

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

  //===------------------------------------------------------------------===//
  // Cleanup 栈辅助
  //===------------------------------------------------------------------===//

  /// 注册需要析构的局部变量到 cleanup 栈
  void pushCleanup(VarDecl *VD);

  /// 从栈顶到 OldSize 逆序调用析构函数
  void EmitCleanupsForScope(unsigned OldSize);

  /// RunCleanupsScope — RAII 包装，退出作用域时自动调用析构。
  /// 用法：在 CompoundStmt/控制流 body 入口创建，析构时自动清理。
  class RunCleanupsScope {
    CodeGenFunction &CGF;
    unsigned SavedCleanupDepth;

  public:
    RunCleanupsScope(CodeGenFunction &CGF)
        : CGF(CGF), SavedCleanupDepth(CGF.CleanupStack.size()) {}
    ~RunCleanupsScope() { CGF.EmitCleanupsForScope(SavedCleanupDepth); }

    RunCleanupsScope(const RunCleanupsScope &) = delete;
    RunCleanupsScope &operator=(const RunCleanupsScope &) = delete;
  };
};

} // namespace blocktype

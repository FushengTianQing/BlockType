//===--- CodeGenStmt.cpp - Statement Code Generation ----------*- C++ -*-===//
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
#include "blocktype/CodeGen/CGDebugInfo.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// CompoundStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitCompoundStmt(CompoundStmt *CompoundStatement) {
  if (!CompoundStatement) {
    return;
  }
  RunCleanupsScope Scope(*this);
  EmitStmts(CompoundStatement->getBody());
}

//===----------------------------------------------------------------------===//
// IfStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitIfStmt(IfStmt *IfStatement) {
  if (!IfStatement) {
    return;
  }

  // consteval if — 编译时已确定分支，只生成对应分支
  // C++20 语义：if consteval 在常量求值上下文中为 true，运行时为 false。
  // CodeGen 生成的是运行时代码，因此：
  //   if consteval  → false（生成 else 分支）
  //   if !consteval → true（生成 then 分支）
  if (IfStatement->isConsteval()) {
    if (IfStatement->isNegated()) {
      // if !consteval → always true → 只生成 then 分支
      if (IfStatement->getThen()) {
        RunCleanupsScope Scope(*this);
        EmitStmt(IfStatement->getThen());
      }
    } else {
      // if consteval → always false → 只生成 else 分支
      if (IfStatement->getElse()) {
        RunCleanupsScope Scope(*this);
        EmitStmt(IfStatement->getElse());
      }
    }
    return;
  }

  // P0963R3: Handle structured bindings in condition
  // Syntax: if (auto [x, y] = expr) { ... }
  if (IfStatement->hasStructuredBinding()) {
    auto Bindings = IfStatement->getBindingDecls();
    
    // Emit each binding declaration
    for (auto *BD : Bindings) {
      EmitBindingDecl(BD, nullptr, BD->getBindingIndex());
    }
  } else if (VarDecl *CondVar = IfStatement->getConditionVariable()) {
    // Handle condition variable (e.g., if (int x = expr))
    EmitCondVarDecl(CondVar);
  }

  // Generate condition
  llvm::Value *Condition = EmitExpr(IfStatement->getCond());
  if (!Condition) {
    return;
  }

  llvm::BasicBlock *ThenBB = createBasicBlock("if.then");
  llvm::BasicBlock *ElseBB = IfStatement->getElse() ? createBasicBlock("if.else") : nullptr;
  llvm::BasicBlock *MergeBB = createBasicBlock("if.end");

  Condition = EmitConversionToBool(Condition, IfStatement->getCond()->getType());

  // Detect [[likely]]/[[unlikely]] attributes on then branch, attach BranchWeights metadata
  llvm::MDNode *Weights = createIfBranchWeights(
      CGM.getLLVMContext(), IfStatement->getThen(),
      IfStatement->getElse() != nullptr);
  auto *CondBr = Builder.CreateCondBr(Condition, ThenBB,
                       ElseBB ? ElseBB : MergeBB);
  if (Weights) {
    CondBr->setMetadata(llvm::LLVMContext::MD_prof, Weights);
  }

  // Then
  EmitBlock(ThenBB);
  if (IfStatement->getThen()) {
    RunCleanupsScope ThenScope(*this);
    EmitStmt(IfStatement->getThen());
  }
  if (haveInsertPoint()) {
    Builder.CreateBr(MergeBB);
  }

  // Else
  if (ElseBB) {
    EmitBlock(ElseBB);
    // else if chain: recursively handle nested IfStmt
    {
      RunCleanupsScope ElseScope(*this);
      EmitStmt(IfStatement->getElse());
    }
    if (haveInsertPoint()) {
      Builder.CreateBr(MergeBB);
    }
  }

  // Merge
  EmitBlock(MergeBB);
}

//===----------------------------------------------------------------------===//
// SwitchStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitSwitchStmt(SwitchStmt *SwitchStatement) {
  if (!SwitchStatement) {
    return;
  }

  // 处理条件变量（如 switch (int x = expr)）
  if (VarDecl *CondVar = SwitchStatement->getConditionVariable()) {
    EmitCondVarDecl(CondVar);
  }

  llvm::Value *Condition = EmitExpr(SwitchStatement->getCond());
  if (!Condition) {
    return;
  }

  // Switch 条件 lifetime 扩展：
  // Clang 将 switch 条件值的 lifetime 扩展到整个 switch 块。
  // 将条件值保存到 alloca，确保在整个 switch 期间有效。
  llvm::AllocaInst *CondAlloca = nullptr;
  llvm::Value *CondValue = Condition;
  {
    llvm::Type *CondTy = Condition->getType();
    CondAlloca = CreateAlloca(
        SwitchStatement->getCond()->getType(), "switch.cond");
    Builder.CreateStore(Condition, CondAlloca);

    // 生成 llvm.lifetime.start intrinsic
    llvm::Type *CondSizeTy = llvm::Type::getInt64Ty(CGM.getLLVMContext());
    uint64_t CondSize = CGM.getTarget().getTypeSize(
        SwitchStatement->getCond()->getType());
    llvm::Constant *SizeVal = llvm::ConstantInt::get(CondSizeTy, CondSize);
    llvm::FunctionType *LifetimeStartTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(CGM.getLLVMContext()),
        {CondSizeTy, llvm::PointerType::get(CGM.getLLVMContext(), 0)}, false);
    llvm::FunctionCallee LifetimeStart =
        CGM.getModule()->getOrInsertFunction("llvm.lifetime.start.p0",
                                              LifetimeStartTy);
    Builder.CreateCall(LifetimeStart, {SizeVal, CondAlloca});

    // 后续通过 load alloca 获取条件值（确保值在 switch 期间一直有效）
    CondValue = CondAlloca;
  }

  llvm::BasicBlock *DefaultBB = createBasicBlock("switch.default");
  llvm::BasicBlock *EndBB = createBasicBlock("switch.end");

  // Push break target（switch 中 continue 无效，设为 nullptr）
  BreakContinueStack.push_back({EndBB, nullptr});

  // Case 信息
  struct CaseInfo {
    llvm::ConstantInt *Value;     // 下界值（或普通 case 的值）
    llvm::ConstantInt *UpperValue; // 上界值（仅 GNU case range 有效）
    llvm::BasicBlock *BB;
    bool IsRange;
  };
  llvm::SmallVector<CaseInfo, 8> Cases;
  llvm::SmallVector<CaseInfo, 4> RangeCases;
  llvm::DenseMap<llvm::ConstantInt *, llvm::BasicBlock *> CaseValueToBB;
  llvm::BasicBlock *FoundDefaultBB = nullptr;

  // 递归收集单个 stmt 中的 case/default 信息。
  // 处理 CaseStmt → SubStmt → CaseStmt 链式结构（非 CompoundStmt body）。
  auto collectCasesFromStmt = [&](Stmt *S, auto &Self) -> void {
    while (S) {
      if (auto *CaseS = llvm::dyn_cast<CaseStmt>(S)) {
        if (auto *CaseExpr = CaseS->getLHS()) {
          llvm::Constant *ConstVal = CGM.getConstants().EmitConstant(CaseExpr);
          if (auto *CI = llvm::dyn_cast_or_null<llvm::ConstantInt>(ConstVal)) {
            llvm::BasicBlock *CaseBB = createBasicBlock("switch.case");
            bool IsRange = (CaseS->getRHS() != nullptr);
            llvm::ConstantInt *UpperCI = nullptr;
            if (IsRange) {
              llvm::Constant *UpperVal =
                  CGM.getConstants().EmitConstant(CaseS->getRHS());
              UpperCI = llvm::dyn_cast_or_null<llvm::ConstantInt>(UpperVal);
              if (!UpperCI) {
                IsRange = false; // RHS 不是常量整数，降级为普通 case
              }
            }
            CaseInfo Info{CI, IsRange ? UpperCI : nullptr, CaseBB, IsRange};
            if (IsRange) {
              RangeCases.push_back(Info);
            }
            Cases.push_back(Info);
            CaseValueToBB[CI] = CaseBB;
          }
        }
        // 递归处理 SubStmt（可能是下一个 CaseStmt 或 DefaultStmt）
        S = CaseS->getSubStmt();
      } else if (auto *DefaultS = llvm::dyn_cast<DefaultStmt>(S)) {
        FoundDefaultBB = DefaultBB;
        // DefaultStmt 的 SubStmt 也可能包含更多 case
        S = DefaultS->getSubStmt();
      } else {
        break; // 普通语句，停止
      }
    }
  };

  // 统一收集：CompoundStmt 和非 CompoundStmt 共用递归逻辑
  Stmt *Body = SwitchStatement->getBody();
  if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    for (Stmt *S : CS->getBody()) {
      collectCasesFromStmt(S, collectCasesFromStmt);
    }
  } else if (Body) {
    collectCasesFromStmt(Body, collectCasesFromStmt);
  }

  // 如果条件值需要截断/扩展以匹配 case 常量的位宽，确保类型一致
  // SwitchInst 要求条件值和所有 case 值类型一致
  // 从 lifetime alloca 中加载条件值
  llvm::Value *SwitchCond = Builder.CreateLoad(
      SwitchStatement->getCond()->getType().isNull()
          ? Condition->getType()
          : CGM.getTypes().ConvertType(SwitchStatement->getCond()->getType()),
      CondAlloca, "switch.cond.val");
  if (!Cases.empty()) {
    llvm::Type *CaseTy = Cases[0].Value->getType();
    if (SwitchCond->getType() != CaseTy) {
      SwitchCond = Builder.CreateIntCast(
          SwitchCond, CaseTy,
          SwitchCond->getType()->isIntegerTy(1) ? false : true, "switch.cast");
    }
  }

  // 如果有 range case，需要在 default 路径中插入范围判断
  // 将 SwitchInst 的 default 目标改为 range check block
  llvm::BasicBlock *SwitchDefaultBB = DefaultBB;
  if (!RangeCases.empty()) {
    SwitchDefaultBB = createBasicBlock("switch.rangecheck");
  }

  // 创建 SwitchInst（只包含普通 case，range case 在 default 路径中处理）
  unsigned NormalCaseCount = Cases.size() - RangeCases.size();
  auto *SwitchInst =
      Builder.CreateSwitch(SwitchCond, SwitchDefaultBB, NormalCaseCount);
  for (auto &C : Cases) {
    if (!C.IsRange) {
      SwitchInst->addCase(C.Value, C.BB);
    }
  }

  // 生成 GNU case range 的范围判断代码（在 default 路径中）
  if (!RangeCases.empty()) {
    EmitBlock(SwitchDefaultBB);
    for (unsigned i = 0, e = RangeCases.size(); i < e; ++i) {
      auto &RC = RangeCases[i];
      llvm::BasicBlock *NextBB =
          (i + 1 < e) ? createBasicBlock("switch.rangecheck")
                      : DefaultBB;
      llvm::Value *LowerCmp =
          Builder.CreateICmpSGE(SwitchCond, RC.Value, "range.lb");
      llvm::Value *UpperCmp =
          Builder.CreateICmpSLE(SwitchCond, RC.UpperValue, "range.ub");
      llvm::Value *InRange =
          Builder.CreateAnd(LowerCmp, UpperCmp, "range.in");
      Builder.CreateCondBr(InRange, RC.BB, NextBB);
      if (i + 1 < e) {
        EmitBlock(NextBB);
      }
    }
  }

  // 递归生成 case/default 的代码
  auto emitSwitchBodyFromStmt = [&](Stmt *S, auto &Self) -> void {
    while (S) {
      if (auto *CaseS = llvm::dyn_cast<CaseStmt>(S)) {
        // 从缓存的映射中查找 BB
        llvm::BasicBlock *CaseBB = nullptr;
        if (auto *CaseExpr = CaseS->getLHS()) {
          llvm::Constant *ConstVal = CGM.getConstants().EmitConstant(CaseExpr);
          if (auto *CI = llvm::dyn_cast_or_null<llvm::ConstantInt>(ConstVal)) {
            auto It = CaseValueToBB.find(CI);
            if (It != CaseValueToBB.end()) {
              CaseBB = It->second;
            }
          }
        }
        if (CaseBB) {
          EmitBlock(CaseBB);
        }
        // 递归处理 SubStmt（可能是下一个 CaseStmt 或普通语句）
        S = CaseS->getSubStmt();
        // 注意：不 EmitStmt(SubStmt) 然后 break
        // 而是继续 while 循环处理链式结构
      } else if (auto *DefaultS = llvm::dyn_cast<DefaultStmt>(S)) {
        EmitBlock(DefaultBB);
        S = DefaultS->getSubStmt();
      } else {
        // 普通语句 — 发射并停止
        EmitStmt(S);
        break;
      }
    }
  };

  // 统一生成 body
  if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    for (Stmt *S : CS->getBody()) {
      emitSwitchBodyFromStmt(S, emitSwitchBodyFromStmt);
    }
  } else if (Body) {
    emitSwitchBodyFromStmt(Body, emitSwitchBodyFromStmt);
  }

  // Default case（如果没有在 body 中遇到 DefaultStmt）
  if (!FoundDefaultBB) {
    EmitBlock(DefaultBB);
  }

  // End
  if (haveInsertPoint()) {
    Builder.CreateBr(EndBB);
  }

  // 如果 EndBB 无前驱（所有分支都以 return/break 终止），
  // 仍需 EmitBlock 以放置 lifetime.end intrinsic
  EmitBlock(EndBB);

  // 生成 llvm.lifetime.end intrinsic（条件值 lifetime 结束）
  if (CondAlloca) {
    llvm::Type *CondSizeTy = llvm::Type::getInt64Ty(CGM.getLLVMContext());
    uint64_t CondSize = CGM.getTarget().getTypeSize(
        SwitchStatement->getCond()->getType());
    llvm::Constant *SizeVal = llvm::ConstantInt::get(CondSizeTy, CondSize);
    llvm::FunctionType *LifetimeEndTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(CGM.getLLVMContext()),
        {CondSizeTy, llvm::PointerType::get(CGM.getLLVMContext(), 0)}, false);
    llvm::FunctionCallee LifetimeEnd =
        CGM.getModule()->getOrInsertFunction("llvm.lifetime.end.p0",
                                              LifetimeEndTy);
    Builder.CreateCall(LifetimeEnd, {SizeVal, CondAlloca});
  }

  BreakContinueStack.pop_back();
}

//===----------------------------------------------------------------------===//
// ForStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitForStmt(ForStmt *ForStatement) {
  if (!ForStatement) {
    return;
  }

  // Init
  if (ForStatement->getInit()) {
    EmitStmt(ForStatement->getInit());
  }

  llvm::BasicBlock *CondBB = createBasicBlock("for.cond");
  llvm::BasicBlock *BodyBB = createBasicBlock("for.body");
  llvm::BasicBlock *IncBB = createBasicBlock("for.inc");
  llvm::BasicBlock *EndBB = createBasicBlock("for.end");

  BreakContinueStack.push_back({EndBB, IncBB});

  // Condition
  EmitBlock(CondBB);

  // 条件变量（如 for (;int x = expr;)）
  if (ForStatement->getConditionVariable()) {
    EmitCondVarDecl(ForStatement->getConditionVariable());
  }

  if (ForStatement->getCond()) {
    llvm::Value *Cond = EmitExpr(ForStatement->getCond());
    if (Cond) {
      Cond = EmitConversionToBool(Cond, ForStatement->getCond()->getType());
      auto *CondBr = Builder.CreateCondBr(Cond, BodyBB, EndBB);
      // [[likely]]/[[unlikely]] 分支权重
      if (auto *Weights = createLoopBranchWeights(CGM.getLLVMContext(),
                                                    ForStatement->getBody())) {
        CondBr->setMetadata(llvm::LLVMContext::MD_prof, Weights);
      }
    } else {
      Builder.CreateBr(BodyBB);
    }
  } else {
    Builder.CreateBr(BodyBB); // 无条件循环
  }

  // Body
  EmitBlock(BodyBB);
  if (ForStatement->getBody()) {
    RunCleanupsScope BodyScope(*this);
    EmitStmt(ForStatement->getBody());
  }
  if (haveInsertPoint()) {
    Builder.CreateBr(IncBB);
  }

  // Increment
  EmitBlock(IncBB);
  if (ForStatement->getInc()) {
    EmitExpr(ForStatement->getInc());
  }
  Builder.CreateBr(CondBB);

  // End
  EmitBlock(EndBB);

  BreakContinueStack.pop_back();
}

//===----------------------------------------------------------------------===//
// WhileStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitWhileStmt(WhileStmt *WhileStatement) {
  if (!WhileStatement) {
    return;
  }

  llvm::BasicBlock *CondBB = createBasicBlock("while.cond");
  llvm::BasicBlock *BodyBB = createBasicBlock("while.body");
  llvm::BasicBlock *EndBB = createBasicBlock("while.end");

  BreakContinueStack.push_back({EndBB, CondBB});

  // Condition
  EmitBlock(CondBB);

  // 条件变量（如 while (int x = expr)）
  if (WhileStatement->getConditionVariable()) {
    EmitCondVarDecl(WhileStatement->getConditionVariable());
  }

  if (WhileStatement->getCond()) {
    llvm::Value *Cond = EmitExpr(WhileStatement->getCond());
    if (Cond) {
      Cond = EmitConversionToBool(Cond, WhileStatement->getCond()->getType());
      auto *CondBr = Builder.CreateCondBr(Cond, BodyBB, EndBB);
      // [[likely]]/[[unlikely]] 分支权重
      if (auto *Weights = createLoopBranchWeights(CGM.getLLVMContext(),
                                                    WhileStatement->getBody())) {
        CondBr->setMetadata(llvm::LLVMContext::MD_prof, Weights);
      }
    } else {
      Builder.CreateBr(EndBB);
    }
  } else {
    Builder.CreateBr(EndBB);
  }

  // Body
  EmitBlock(BodyBB);
  if (WhileStatement->getBody()) {
    RunCleanupsScope BodyScope(*this);
    EmitStmt(WhileStatement->getBody());
  }
  if (haveInsertPoint()) {
    Builder.CreateBr(CondBB);
  }

  // End
  EmitBlock(EndBB);

  BreakContinueStack.pop_back();
}

//===----------------------------------------------------------------------===//
// DoStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitDoStmt(DoStmt *DoStatement) {
  if (!DoStatement) {
    return;
  }

  llvm::BasicBlock *BodyBB = createBasicBlock("do.body");
  llvm::BasicBlock *CondBB = createBasicBlock("do.cond");
  llvm::BasicBlock *EndBB = createBasicBlock("do.end");

  BreakContinueStack.push_back({EndBB, CondBB});

  // Body
  EmitBlock(BodyBB);
  if (DoStatement->getBody()) {
    RunCleanupsScope BodyScope(*this);
    EmitStmt(DoStatement->getBody());
  }
  if (haveInsertPoint()) {
    Builder.CreateBr(CondBB);
  }

  // Condition
  EmitBlock(CondBB);
  if (DoStatement->getCond()) {
    llvm::Value *Cond = EmitExpr(DoStatement->getCond());
    if (Cond) {
      Cond = EmitConversionToBool(Cond, DoStatement->getCond()->getType());
      auto *CondBr = Builder.CreateCondBr(Cond, BodyBB, EndBB);
      // [[likely]]/[[unlikely]] 分支权重
      if (auto *Weights = createLoopBranchWeights(CGM.getLLVMContext(),
                                                    DoStatement->getBody())) {
        CondBr->setMetadata(llvm::LLVMContext::MD_prof, Weights);
      }
    } else {
      Builder.CreateBr(EndBB);
    }
  } else {
    Builder.CreateBr(EndBB);
  }

  // End
  EmitBlock(EndBB);

  BreakContinueStack.pop_back();
}

//===----------------------------------------------------------------------===//
// ReturnStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitReturnStmt(ReturnStmt *ReturnStatement) {
  if (!ReturnStatement) {
    return;
  }

  // 在 return 前清理所有活跃的局部变量析构函数
  EmitCleanupsForScope(0);

  if (Expr *ReturnExpr = ReturnStatement->getRetValue()) {
    // NRVO 检测：如果返回表达式是 NRVO 候选变量（直接使用 ReturnValue alloca），
    // 则不需要 store — 值已经在 ReturnValue 中
    bool IsNRVO = false;
    if (this->ReturnValue && !IsSRetFn) {
      if (auto *DRE = llvm::dyn_cast<DeclRefExpr>(ReturnExpr)) {
        if (auto *VD = llvm::dyn_cast<VarDecl>(DRE->getDecl())) {
          if (isNRVOCandidate(VD)) {
            IsNRVO = true;
            // 值已经在 ReturnValue alloca 中，直接跳到 ReturnBlock
          }
        }
      }
    }

    if (!IsNRVO) {
      llvm::Value *RetVal = EmitExpr(ReturnExpr);
      if (RetVal && this->ReturnValue) {
        if (IsSRetFn) {
          // sret 模式：从 ReturnValue alloca 加载 sret 指针，写入返回值
          llvm::Value *SRetPtr = Builder.CreateLoad(
              llvm::PointerType::get(CGM.getLLVMContext(), 0),
              this->ReturnValue, "sret.ptr");
          Builder.CreateStore(RetVal, SRetPtr);
        } else {
          Builder.CreateStore(RetVal, this->ReturnValue);
        }
      } else if (RetVal) {
        Builder.CreateRet(ReturnValue);
        return;
      }
    }
  }

  // 跳到统一的 ReturnBlock
  if (ReturnBlock) {
    Builder.CreateBr(ReturnBlock);
  } else {
    if (haveInsertPoint()) {
      Builder.CreateRetVoid();
    }
  }
}

//===----------------------------------------------------------------------===//
// DeclStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitDeclStmt(DeclStmt *DeclarationStatement) {
  if (!DeclarationStatement) {
    return;
  }

  for (Decl *Declaration : DeclarationStatement->getDecls()) {
    // P7.4.3: Handle BindingDecl (structured binding)
    if (auto *BD = llvm::dyn_cast<BindingDecl>(Declaration)) {
      // Full implementation:
      // 1. The BindingExpr is already std::get<N>(tuple) from Sema
      // 2. EmitBindingDecl will create alloca and store the value
      EmitBindingDecl(BD, nullptr, BD->getBindingIndex());
      continue;
    }
    
    if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(Declaration)) {
      QualType VarType = VariableDecl->getType();

      // NRVO: 如果变量是 NRVO 候选，直接使用 ReturnValue alloca
      llvm::AllocaInst *Alloca = nullptr;
      if (isNRVOCandidate(VariableDecl) && ReturnValue && !IsSRetFn) {
        Alloca = ReturnValue;
      } else {
        Alloca = CreateAlloca(VarType, VariableDecl->getName());
      }
      if (!Alloca) {
        continue;
      }

      setLocalDecl(VariableDecl, Alloca);

      // 生成局部变量调试信息
      if (CGM.getDebugInfo().isInitialized()) {
        CGM.getDebugInfo().EmitLocalVarDI(VariableDecl, Alloca);
      }

      // 注册析构 cleanup（如果类型有非 trivial 析构函数）
      pushCleanup(VariableDecl);

      // 初始化
      if (Expr *Initializer = VariableDecl->getInit()) {
        // Copy elision: 如果初始化器是 CXXConstructExpr（prvalue 直接构造），
        // 直接在变量的 alloca 上构造，避免临时对象 + load/store
        if (auto *CCE = llvm::dyn_cast<CXXConstructExpr>(Initializer)) {
          if (CCE->isPRValue() || CCE->getType()->isRecordType()) {
            EmitCXXConstructExpr(CCE, Alloca);
            // 不需要额外 store，构造函数已直接写入 Alloca
          } else {
            llvm::Value *InitValue = EmitExpr(Initializer);
            if (InitValue) {
              Builder.CreateStore(InitValue, Alloca);
            }
          }
        } else {
          llvm::Value *InitValue = EmitExpr(Initializer);
          if (InitValue) {
            Builder.CreateStore(InitValue, Alloca);
          }
        }
      } else {
        // 零初始化
        llvm::Type *LLVMType = CGM.getTypes().ConvertType(VarType);
        if (LLVMType) {
          Builder.CreateStore(llvm::Constant::getNullValue(LLVMType), Alloca);
        }
      }
    }
  }
}

// P7.4.3: Structured binding code generation
void CodeGenFunction::EmitBindingDecl(BindingDecl *BD, llvm::Value *TupleAddr, unsigned Index) {
  // Full implementation for structured binding:
  // 1. The BindingExpr should already be std::get<Index>(tuple)
  // 2. Create alloca for the binding variable
  // 3. Store the extracted value
  
  QualType BindingType = BD->getType();
  llvm::AllocaInst *Alloca = CreateAlloca(BindingType, BD->getName());
  
  if (!Alloca) {
    return;
  }
  
  setLocalDecl(BD, Alloca);
  
  // Generate debug info for binding variable
  if (CGM.getDebugInfo().isInitialized()) {
    CGM.getDebugInfo().EmitLocalVarDI(BD, Alloca);
  }
  
  // Initialize with the binding expression (std::get<N>(tuple))
  Expr *BindingExpr = BD->getBindingExpr();
  if (BindingExpr) {
    // Emit the std::get<N>(tuple) call expression
    llvm::Value *InitValue = EmitExpr(BindingExpr);
    if (InitValue) {
      Builder.CreateStore(InitValue, Alloca);
    }
  } else {
    // Fallback: zero-initialize if no binding expression
    llvm::Type *LLVMType = CGM.getTypes().ConvertType(BindingType);
    if (LLVMType) {
      Builder.CreateStore(llvm::Constant::getNullValue(LLVMType), Alloca);
    }
  }
}

//===----------------------------------------------------------------------===//
// BreakStmt / ContinueStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitBreakStmt(BreakStmt *BreakStatement) {
  assert(!BreakContinueStack.empty() && "break outside loop/switch");
  Builder.CreateBr(BreakContinueStack.back().BreakBB);
}

void CodeGenFunction::EmitContinueStmt(ContinueStmt *ContinueStatement) {
  assert(!BreakContinueStack.empty() && "continue outside loop");
  Builder.CreateBr(BreakContinueStack.back().ContinueBB);
}

//===----------------------------------------------------------------------===//
// GotoStmt / LabelStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitGotoStmt(GotoStmt *GotoStatement) {
  if (!GotoStatement) {
    return;
  }

  LabelDecl *Label = GotoStatement->getLabel();
  if (!Label) {
    return;
  }

  // 查找或创建 label 对应的 BasicBlock
  llvm::BasicBlock *TargetBB = getOrCreateLabelBB(Label);
  if (haveInsertPoint()) {
    Builder.CreateBr(TargetBB);
  }
}

void CodeGenFunction::EmitLabelStmt(LabelStmt *LabelStatement) {
  if (!LabelStatement) {
    return;
  }

  LabelDecl *Label = LabelStatement->getLabel();
  llvm::BasicBlock *LabelBB = nullptr;

  if (Label) {
    // 查找是否已有 goto 前向引用创建的 BB
    auto It = LabelMap.find(Label);
    if (It != LabelMap.end()) {
      LabelBB = It->second;
    } else {
      LabelBB = createBasicBlock(Label->getName());
      LabelMap[Label] = LabelBB;
    }
  } else {
    LabelBB = createBasicBlock("label");
  }

  if (haveInsertPoint()) {
    Builder.CreateBr(LabelBB);
  }
  EmitBlock(LabelBB);

  if (LabelStatement->getSubStmt()) {
    EmitStmt(LabelStatement->getSubStmt());
  }
}

//===----------------------------------------------------------------------===//
// CXXTryStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitCXXTryStmt(CXXTryStmt *TryStatement) {
  if (!TryStatement) {
    return;
  }

  // try/catch 代码生成（Itanium C++ ABI）
  // 使用 invoke + landingpad 模式
  //
  // 结构：
  //   try block: invoke @func to normalBB unwind lpad
  //   normalBB: → try.cont
  //   lpad: catch dispatch using typeinfo → catch handlers → try.cont
  //   eh.resume: resume 未捕获的异常
  //
  // catch(T e) 的处理流程：
  //   1. landingpad clause 使用 RTTI typeinfo（而非 catch-all）
  //   2. __cxa_begin_catch(exception_ptr) 获取异常对象
  //   3. 将异常对象存入 catch 参数的 alloca
  //   4. 执行 handler body
  //   5. __cxa_end_catch()

  llvm::BasicBlock *TryContBB = createBasicBlock("try.cont");

  // 如果有 catch blocks，生成 invoke 上下文 + landingpad
  if (!TryStatement->getCatchBlocks().empty()) {
    llvm::BasicBlock *LandingPadBB = createBasicBlock("lpad");

    // 保存 landingpad 结果的 alloca（在 entry 块创建）
    llvm::StructType *LPadResultType = llvm::StructType::get(
        CGM.getLLVMContext(),
        {llvm::PointerType::get(CGM.getLLVMContext(), 0),
         llvm::Type::getInt32Ty(CGM.getLLVMContext())});
    llvm::AllocaInst *LPadAlloca =
        CreateAlloca(LPadResultType, "lpad.result");

    // 正常返回块（invoke 成功后跳转）
    llvm::BasicBlock *NormalBB = createBasicBlock("try.normal");

    // 设置 invoke 上下文：try 块内的函数调用将生成 invoke 而非 call
    pushInvokeTarget(NormalBB, LandingPadBB);

    // 生成 try block
    if (TryStatement->getTryBlock()) {
      EmitStmt(TryStatement->getTryBlock());
    }

    // try 块正常结束 → 跳到 try.cont
    if (haveInsertPoint()) {
      Builder.CreateBr(TryContBB);
    }

    // NormalBB: invoke 成功后跳转到这里
    EmitBlock(NormalBB);
    Builder.CreateBr(TryContBB);

    // 离开 try 块的 invoke 上下文
    popInvokeTarget();

    // 生成 landingpad
    EmitBlock(LandingPadBB);
    llvm::LandingPadInst *LPad = Builder.CreateLandingPad(
        LPadResultType,
        TryStatement->getCatchBlocks().size(), "lpad");
    LPad->setCleanup(true); // 允许 catch-all 和类型匹配共存

    // 为每个 catch 块添加 catch clause 到 landingpad（使用 RTTI typeinfo）
    for (Stmt *CatchBlock : TryStatement->getCatchBlocks()) {
      if (auto *Catch = llvm::dyn_cast<CXXCatchStmt>(CatchBlock)) {
        if (!Catch->getExceptionDecl()) {
          // catch(...) — catch-all clause（null clause）
          LPad->addClause(nullptr);
        } else {
          // catch(T e) — 使用 RTTI 类型信息作为 clause
          QualType CatchType = Catch->getExceptionDecl()->getType();
          llvm::Value *TypeInfoClause = nullptr;

          if (CatchType->isRecordType()) {
            if (auto *RT = llvm::dyn_cast<RecordType>(CatchType.getTypePtr())) {
              if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                if (auto *TI = CGM.getCXX().EmitTypeInfo(CXXRD)) {
                  TypeInfoClause = Builder.CreateBitCast(TI,
                      llvm::PointerType::get(CGM.getLLVMContext(), 0));
                }
              }
            }
          }

          if (TypeInfoClause) {
            LPad->addClause(llvm::cast<llvm::Constant>(TypeInfoClause));
          } else {
            // 无法生成 typeinfo，fallback 到 catch-all
            LPad->addClause(nullptr);
          }
        }
      } else {
        LPad->addClause(llvm::Constant::getNullValue(
            llvm::PointerType::get(CGM.getLLVMContext(), 0)));
      }
    }

    // 保存 landingpad 结果
    Builder.CreateStore(LPad, LPadAlloca);

    // 提取 selector value（landingpad 返回 { i8*, i32 } 中的 i32）
    llvm::Value *Selector = Builder.CreateExtractValue(LPad, {1}, "selector");

    // 声明 __cxa_begin_catch 和 __cxa_end_catch
    auto &Ctx = CGM.getLLVMContext();
    llvm::FunctionType *BeginCatchTy = llvm::FunctionType::get(
        llvm::PointerType::get(Ctx, 0),
        {llvm::PointerType::get(Ctx, 0)}, false);
    llvm::FunctionCallee BeginCatch =
        CGM.getModule()->getOrInsertFunction("__cxa_begin_catch", BeginCatchTy);

    llvm::FunctionType *EndCatchTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(Ctx), false);
    llvm::FunctionCallee EndCatch =
        CGM.getModule()->getOrInsertFunction("__cxa_end_catch", EndCatchTy);

    // 声明 llvm.eh.typeid.for intrinsic（获取 catch clause 的 type ID）
    llvm::FunctionType *TypeidForTy = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(Ctx),
        {llvm::PointerType::get(Ctx, 0)}, false);
    llvm::FunctionCallee TypeidFor =
        CGM.getModule()->getOrInsertFunction("llvm.eh.typeid.for", TypeidForTy);

    // 为每个 catch 块计算 type ID，并准备 dispatch 块
    llvm::SmallVector<llvm::BasicBlock *, 4> CatchBBs;
    llvm::SmallVector<llvm::Value *, 4> TypeIDs;
    bool HasCatchAll = false;

    // 预先为每个 catch 创建 BB 和 type ID
    for (Stmt *CatchBlock : TryStatement->getCatchBlocks()) {
      llvm::BasicBlock *CatchBB = createBasicBlock("catch");
      CatchBBs.push_back(CatchBB);

      if (auto *Catch = llvm::dyn_cast<CXXCatchStmt>(CatchBlock)) {
        if (!Catch->getExceptionDecl()) {
          // catch(...) — catch-all，无需 type ID
          TypeIDs.push_back(nullptr);
          HasCatchAll = true;
        } else {
          // catch(T e) — 获取 typeinfo 并计算 type ID
          QualType CatchType = Catch->getExceptionDecl()->getType();
          llvm::Value *TypeInfoClause =
              CGM.getCXX().EmitCatchTypeInfo(*this, CatchType);
          if (TypeInfoClause) {
            llvm::Value *TypeID = Builder.CreateCall(TypeidFor,
                {TypeInfoClause}, "typeid");
            TypeIDs.push_back(TypeID);
          } else {
            // 无法生成 typeinfo → fallback 到 catch-all
            TypeIDs.push_back(nullptr);
            HasCatchAll = true;
          }
        }
      } else {
        TypeIDs.push_back(nullptr);
        HasCatchAll = true;
      }
    }

    // 生成 dispatch: 比较 selector 与每个 type ID
    // 如果匹配则跳到对应的 catch BB，否则跳到下一个比较或 eh.resume
    auto *EHResumeBB = createBasicBlock("eh.resume");

    for (unsigned I = 0; I < CatchBBs.size(); ++I) {
      if (TypeIDs[I]) {
        // 有 type ID → 比较 selector == typeID
        llvm::Value *MatchCond = Builder.CreateICmpEQ(
            Selector, TypeIDs[I], "match");
        // 如果是最后一个 catch 或后面没有需要 dispatch 的 catch
        llvm::BasicBlock *NextBB;
        if (I + 1 < CatchBBs.size()) {
          NextBB = createBasicBlock("catch.dispatch");
        } else if (HasCatchAll) {
          // 已经有 catch-all 了，不需要 eh.resume
          NextBB = nullptr;
        } else {
          NextBB = EHResumeBB;
        }

        if (NextBB) {
          Builder.CreateCondBr(MatchCond, CatchBBs[I], NextBB);
          if (I + 1 < CatchBBs.size()) {
            EmitBlock(NextBB);
          }
        } else {
          // 最后一个 catch 且无 catch-all → 匹配则执行，否则必为 catch-all
          Builder.CreateCondBr(MatchCond, CatchBBs[I],
                               CatchBBs[CatchBBs.size() - 1]);
        }
      } else {
        // catch-all (null type ID) → 直接跳到这个 catch BB
        Builder.CreateBr(CatchBBs[I]);
      }
    }

    // 生成每个 catch handler
    for (unsigned I = 0; I < CatchBBs.size(); ++I) {
      EmitBlock(CatchBBs[I]);

      if (auto *Catch = llvm::dyn_cast<CXXCatchStmt>(
              TryStatement->getCatchBlocks()[I])) {
        // 从 landingpad 结果中提取异常对象指针
        llvm::Value *LPadResult = Builder.CreateLoad(LPadResultType,
                                                     LPadAlloca, "lpad");
        llvm::Value *ExceptionPtr = Builder.CreateExtractValue(
            LPadResult, {0}, "exception.ptr");

        // 调用 __cxa_begin_catch 获取异常对象
        llvm::Value *CatchObj = Builder.CreateCall(BeginCatch,
            {ExceptionPtr}, "catch.obj");

        // 为 catch 参数创建 alloca（如果有异常声明）
        if (Catch->getExceptionDecl()) {
          llvm::AllocaInst *CatchAlloca =
              CreateAlloca(Catch->getExceptionDecl()->getType(),
                           Catch->getExceptionDecl()->getName());
          setLocalDecl(Catch->getExceptionDecl(), CatchAlloca);

          // 将异常对象转换为 catch 参数类型并加载值
          llvm::Type *CatchTy =
              CGM.getTypes().ConvertType(Catch->getExceptionDecl()->getType());
          if (CatchTy && CatchObj) {
            llvm::Value *TypedPtr = Builder.CreateBitCast(
                CatchObj, llvm::PointerType::get(CatchTy, 0), "catch.typed");
            llvm::Value *CatchVal = Builder.CreateLoad(CatchTy, TypedPtr,
                                                       "catch.val");
            Builder.CreateStore(CatchVal, CatchAlloca);
          }
        }
        if (Catch->getHandlerBlock()) {
          EmitStmt(Catch->getHandlerBlock());
        }

        // 调用 __cxa_end_catch 通知运行时异常处理完成
        if (haveInsertPoint()) {
          Builder.CreateCall(EndCatch, {}, "end.catch");
        }
      } else {
        EmitStmt(TryStatement->getCatchBlocks()[I]);
      }
      if (haveInsertPoint()) {
        Builder.CreateBr(TryContBB);
      }
    }

    // 生成 eh.resume（未被 catch 捕获的异常继续传播）
    EmitBlock(EHResumeBB);
    llvm::Value *LPadResult = Builder.CreateLoad(LPadResultType, LPadAlloca,
                                                  "lpad");
    Builder.CreateResume(LPadResult);
  } else {
    // 无 catch blocks：直接生成 try block
    if (TryStatement->getTryBlock()) {
      EmitStmt(TryStatement->getTryBlock());
    }
  }

  EmitBlock(TryContBB);
}

//===----------------------------------------------------------------------===//
// CXXForRangeStmt (range-based for loop)
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitCXXForRangeStmt(CXXForRangeStmt *ForRangeStatement) {
  if (!ForRangeStatement) {
    return;
  }

  // Range-based for loop 结构：
  //   for (auto &&__range : range_expr) {
  //     auto __begin = __range.begin();
  //     auto __end = __range.end();
  //     for (; __begin != __end; ++__begin) {
  //       auto &&__ref = *__begin;
  //       loop_var = __ref;
  //       body;
  //     }
  //   }
  //
  // 简化实现：对数组类型直接生成指针迭代，对其他类型调用 begin/end

  VarDecl *LoopVar = ForRangeStatement->getLoopVariable();
  Expr *RangeExpr = ForRangeStatement->getRangeInit();
  Stmt *Body = ForRangeStatement->getBody();

  if (!RangeExpr || !LoopVar) {
    return;
  }

  // 获取 range 的地址
  llvm::Value *RangePtr = EmitLValue(RangeExpr);
  llvm::Value *RangeVal = RangePtr ? Builder.CreateLoad(
      CGM.getTypes().ConvertType(RangeExpr->getType()), RangePtr, "range")
      : EmitExpr(RangeExpr);

  if (!RangeVal) {
    return;
  }

  // 创建循环变量 alloca
  llvm::AllocaInst *LoopVarAlloca =
      CreateAlloca(LoopVar->getType(), LoopVar->getName());
  setLocalDecl(LoopVar, LoopVarAlloca);

  // 判断 range 类型：数组 vs 容器
  QualType RangeType = RangeExpr->getType();
  bool IsArrayType = false;
  uint64_t ArraySize = 0;

  if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(RangeType.getTypePtr())) {
    IsArrayType = true;
    ArraySize = CAT->getSize().getZExtValue();
  } else if (RangePtr) {
    // 检查是否是指向数组的指针
    if (auto *PT = llvm::dyn_cast<PointerType>(RangeType.getTypePtr())) {
      QualType Pointee = QualType(PT->getPointeeType(), Qualifier::None);
      if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(Pointee.getTypePtr())) {
        IsArrayType = true;
        ArraySize = CAT->getSize().getZExtValue();
        RangeVal = RangePtr; // 使用指针本身
      }
    }
  }

  if (IsArrayType) {
    // 数组类型：直接用指针迭代
    // 获取元素类型
    QualType ElemQualType = LoopVar->getType();
    if (RangeType->getTypeClass() == TypeClass::ConstantArray) {
      ElemQualType = QualType(
          llvm::cast<ConstantArrayType>(RangeType.getTypePtr())->getElementType(),
          Qualifier::None);
    } else if (RangeType->getTypeClass() == TypeClass::Pointer) {
      auto *PtrType = llvm::cast<PointerType>(RangeType.getTypePtr());
      QualType Pointee(PtrType->getPointeeType(), Qualifier::None);
      if (Pointee->getTypeClass() == TypeClass::ConstantArray) {
        ElemQualType = QualType(
            llvm::cast<ConstantArrayType>(Pointee.getTypePtr())->getElementType(),
            Qualifier::None);
      }
    }

    llvm::Type *ElemTy = CGM.getTypes().ConvertType(ElemQualType);

    // __begin = &range[0]
    llvm::Value *BeginPtr = RangeVal;
    if (!RangeVal->getType()->isPointerTy()) {
      BeginPtr = Builder.CreateBitCast(RangeVal,
          llvm::PointerType::get(ElemTy, 0), "range.begin");
    }

    // __end = __begin + array_size
    llvm::Value *EndPtr = Builder.CreateGEP(ElemTy, BeginPtr,
        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), ArraySize)},
        "range.end");

    // 创建迭代器变量（存储指向元素的指针）
    llvm::AllocaInst *IterPtrAlloca = CreateAlloca(
        llvm::PointerType::get(ElemTy, 0), "__iter.ptr");

    // 初始化迭代器 = begin
    Builder.CreateStore(BeginPtr, IterPtrAlloca);

    // 循环结构
    llvm::BasicBlock *CondBB = createBasicBlock("for.range.cond");
    llvm::BasicBlock *BodyBB = createBasicBlock("for.range.body");
    llvm::BasicBlock *IncBB = createBasicBlock("for.range.inc");
    llvm::BasicBlock *EndBB = createBasicBlock("for.range.end");

    BreakContinueStack.push_back({EndBB, IncBB});

    // Condition: __iter != __end
    EmitBlock(CondBB);
    llvm::Value *Iter = Builder.CreateLoad(
        llvm::PointerType::get(ElemTy, 0), IterPtrAlloca, "iter");
    llvm::Value *Cond = Builder.CreateICmpNE(Iter, EndPtr, "range.cmp");
    Builder.CreateCondBr(Cond, BodyBB, EndBB);

    // Body
    EmitBlock(BodyBB);
    // *LoopVar = *__iter
    llvm::Value *ElemPtr = Iter;
    llvm::Value *ElemVal = Builder.CreateLoad(ElemTy, ElemPtr, "range.elem");
    Builder.CreateStore(ElemVal, LoopVarAlloca);

    if (Body) {
      EmitStmt(Body);
    }
    if (haveInsertPoint()) {
      Builder.CreateBr(IncBB);
    }

    // Increment: ++__iter
    EmitBlock(IncBB);
    Iter = Builder.CreateLoad(
        llvm::PointerType::get(ElemTy, 0), IterPtrAlloca, "iter");
    llvm::Value *NextIter = Builder.CreateGEP(ElemTy, Iter,
        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), 1)},
        "iter.next");
    Builder.CreateStore(NextIter, IterPtrAlloca);
    Builder.CreateBr(CondBB);

    EmitBlock(EndBB);
    BreakContinueStack.pop_back();
  } else {
    // 容器类型：生成 begin()/end() 调用的循环
    // 模式：
    //   auto __begin = range.begin();  // 或 begin(range)
    //   auto __end = range.end();      // 或 end(range)
    //   for (; __begin != __end; ++__begin) {
    //     auto& __ref = *__begin;
    //     loop_var = __ref;
    //     body;
    //   }

    // 获取元素类型（从 LoopVar 推导）
    QualType ElemQualType = LoopVar->getType();
    llvm::Type *ElemTy = CGM.getTypes().ConvertType(ElemQualType);
    if (!ElemTy) {
      return;
    }

    // 尝试生成 range.begin() 调用
    // 简化实现：查找 CXXRecordDecl 中的 begin()/end() 方法
    llvm::Value *BeginVal = nullptr;
    llvm::Value *EndVal = nullptr;
    llvm::Type *IterTy = nullptr;

    if (RangePtr && RangeType->isRecordType()) {
      // 对象类型：调用 obj.begin() / obj.end()
      auto *RecType = llvm::dyn_cast<RecordType>(RangeType.getTypePtr());
      auto *RecordDecl = RecType ? RecType->getDecl() : nullptr;
      auto *CXXRecord = llvm::dyn_cast_or_null<CXXRecordDecl>(RecordDecl);
      if (CXXRecord) {
        FunctionDecl *BeginFn = nullptr;
        FunctionDecl *EndFn = nullptr;
        for (auto *MemberDecl : CXXRecord->decls()) {
          if (auto *Method = llvm::dyn_cast<CXXMethodDecl>(MemberDecl)) {
            if (Method->getName() == "begin" && Method->getNumParams() == 0) {
              BeginFn = Method;
            } else if (Method->getName() == "end" && Method->getNumParams() == 0) {
              EndFn = Method;
            }
          }
        }

        if (BeginFn && EndFn) {
          // 生成 begin() 调用
          llvm::Function *BeginFunc = CGM.GetOrCreateFunctionDecl(BeginFn);
          if (BeginFunc) {
            BeginVal = Builder.CreateCall(BeginFunc, {RangePtr}, "range.begin");
            IterTy = BeginVal->getType();
          }
          // 生成 end() 调用
          llvm::Function *EndFunc = CGM.GetOrCreateFunctionDecl(EndFn);
          if (EndFunc) {
            EndVal = Builder.CreateCall(EndFunc, {RangePtr}, "range.end");
          }
        }
      }
    }

    // 如果 begin()/end() 方法不存在，尝试自由函数 begin(range)/end(range)
    if (!BeginVal || !EndVal) {
      // 查找自由函数 begin/end（通过 ADL 或 std::begin/std::end）
      // 简化：在模块中查找 begin/end 函数
      // 如果找不到，使用 range 值本身作为迭代器（指针类型场景）
      if (RangeVal->getType()->isPointerTy()) {
        // 指针类型：直接作为迭代器
        BeginVal = RangeVal;
        IterTy = RangeVal->getType();
        // 对于指针，无法确定 end，跳过生成
        // 使用占位实现：只执行一次 body
        if (LoopVarAlloca && RangeVal) {
          llvm::Value *ElemPtr = RangeVal;
          llvm::Value *ElemVal = Builder.CreateLoad(ElemTy, ElemPtr, "range.elem");
          Builder.CreateStore(ElemVal, LoopVarAlloca);
        }
        if (Body) {
          EmitStmt(Body);
        }
        return;
      }

      // 无法生成 begin/end，执行占位实现
      if (LoopVarAlloca && RangeVal) {
        Builder.CreateStore(RangeVal, LoopVarAlloca);
      }
      if (Body) {
        EmitStmt(Body);
      }
      return;
    }

    // 成功获取 begin/end — 生成迭代器循环
    // 创建迭代器 alloca
    llvm::AllocaInst *IterAlloca = CreateAlloca(IterTy, "__iter");

    // 初始化迭代器 = begin
    Builder.CreateStore(BeginVal, IterAlloca);

    // 循环结构
    llvm::BasicBlock *CondBB = createBasicBlock("for.range.cond");
    llvm::BasicBlock *BodyBB = createBasicBlock("for.range.body");
    llvm::BasicBlock *IncBB = createBasicBlock("for.range.inc");
    llvm::BasicBlock *EndBB = createBasicBlock("for.range.end");

    BreakContinueStack.push_back({EndBB, IncBB});

    // Condition: __iter != __end
    EmitBlock(CondBB);
    llvm::Value *Iter = Builder.CreateLoad(IterTy, IterAlloca, "iter");
    llvm::Value *Cond = Builder.CreateICmpNE(Iter, EndVal, "range.cmp");
    Builder.CreateCondBr(Cond, BodyBB, EndBB);

    // Body
    EmitBlock(BodyBB);
    // *LoopVar = *__iter（解引用迭代器）
    llvm::Value *DerefVal = Builder.CreateLoad(ElemTy, Iter, "range.elem");
    Builder.CreateStore(DerefVal, LoopVarAlloca);

    if (Body) {
      EmitStmt(Body);
    }
    if (haveInsertPoint()) {
      Builder.CreateBr(IncBB);
    }

    // Increment: ++__iter
    EmitBlock(IncBB);
    Iter = Builder.CreateLoad(IterTy, IterAlloca, "iter");
    // 调用 operator++ 或 GEP
    // 简化：假设迭代器支持 +1（指针或随机访问迭代器）
    llvm::Value *NextIter = Builder.CreateGEP(ElemTy, Iter,
        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), 1)},
        "iter.next");
    Builder.CreateStore(NextIter, IterAlloca);
    Builder.CreateBr(CondBB);

    EmitBlock(EndBB);
    BreakContinueStack.pop_back();
  }
}

//===----------------------------------------------------------------------===//
// Coroutine Statements
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitCoreturnStmt(CoreturnStmt *Statement) {
  if (!Statement) {
    return;
  }

  // 协程 co_return — 基本框架
  // 完整实现需要 coroutine promise 类型分析和 coroutine frame 管理
  // 简化：生成 return 语义
  if (Expr *Operand = Statement->getOperand()) {
    llvm::Value *RetVal = EmitExpr(Operand);
    if (RetVal && ReturnValue) {
      Builder.CreateStore(RetVal, ReturnValue);
    }
  }

  // 跳转到 return block（或生成 unreachable 表示协程挂起）
  if (ReturnBlock) {
    Builder.CreateBr(ReturnBlock);
  } else if (haveInsertPoint()) {
    Builder.CreateRetVoid();
  }
}

void CodeGenFunction::EmitCoyieldStmt(CoyieldStmt *Statement) {
  if (!Statement) {
    return;
  }

  // 协程 co_yield — 基本框架
  // 完整实现需要：
  // 1. 将操作数传递给 promise.yield_value()
  // 2. 检查是否需要挂起
  // 3. 如果挂起，保存 coroutine state 并返回
  // 简化：只求值操作数（忽略挂起语义）
  if (Expr *Operand = Statement->getOperand()) {
    EmitExpr(Operand);
  }

  // TODO: 生成协程挂起点（await suspend check）
}

//===----------------------------------------------------------------------===//
// 控制流辅助函数
//===----------------------------------------------------------------------===//

llvm::Value *
CodeGenFunction::EmitConversionToBool(llvm::Value *SrcValue, QualType SrcType) {
  if (!SrcValue) {
    return nullptr;
  }

  llvm::Type *SrcTy = SrcValue->getType();

  // 已经是 i1（bool 类型）
  if (SrcTy->isIntegerTy(1)) {
    return SrcValue;
  }

  // 整数类型：与 0 比较
  // ICmpNE 是按位不等比较，不受 signedness 影响。
  // ConstantInt::get(SrcTy, 0, /*isSigned=*/false) 中 isSigned 对值 0 无意义
  // （0 的有符号和无符号表示相同），因此对所有整数类型均正确。
  if (SrcTy->isIntegerTy()) {
    return Builder.CreateICmpNE(
        SrcValue, llvm::ConstantInt::get(SrcTy, 0), "tobool");
  }

  // 浮点类型：与 0.0 比较
  if (SrcTy->isFloatingPointTy()) {
    return Builder.CreateFCmpUNE(
        SrcValue, llvm::ConstantFP::get(SrcTy, 0.0), "tobool");
  }

  // 指针类型：与 null 比较
  if (SrcTy->isPointerTy()) {
    return Builder.CreateICmpNE(
        SrcValue, llvm::ConstantPointerNull::get(
                      llvm::cast<llvm::PointerType>(SrcTy)),
        "tobool");
  }

  // 其他类型：通用 null 比较
  return Builder.CreateICmpNE(
      SrcValue, llvm::Constant::getNullValue(SrcTy), "tobool");
}

void CodeGenFunction::EmitCondVarDecl(VarDecl *CondVariable) {
  if (!CondVariable) {
    return;
  }

  // 生成条件变量的声明和初始化
  QualType VarType = CondVariable->getType();
  llvm::AllocaInst *Alloca = CreateAlloca(VarType, CondVariable->getName());
  if (!Alloca) {
    return;
  }

  setLocalDecl(CondVariable, Alloca);

  if (Expr *Initializer = CondVariable->getInit()) {
    llvm::Value *InitValue = EmitExpr(Initializer);
    if (InitValue) {
      Builder.CreateStore(InitValue, Alloca);
    }
  } else {
    // 零初始化
    llvm::Type *LLVMType = CGM.getTypes().ConvertType(VarType);
    if (LLVMType) {
      Builder.CreateStore(llvm::Constant::getNullValue(LLVMType), Alloca);
    }
  }
}

llvm::BasicBlock *
CodeGenFunction::getOrCreateLabelBB(LabelDecl *Label) {
  if (!Label) {
    return createBasicBlock("label");
  }

  auto It = LabelMap.find(Label);
  if (It != LabelMap.end()) {
    return It->second;
  }

  // 前向引用：提前创建 BB（goto 到尚未定义的 label）
  llvm::BasicBlock *BB = createBasicBlock(Label->getName());
  LabelMap[Label] = BB;
  return BB;
}

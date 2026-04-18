//===--- CodeGenStmt.cpp - Statement Code Generation ----------*- C++ -*-===//
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
  EmitStmts(CompoundStatement->getBody());
}

//===----------------------------------------------------------------------===//
// IfStmt
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitIfStmt(IfStmt *IfStatement) {
  if (!IfStatement) {
    return;
  }

  // 处理条件变量（如 if (int x = expr)）
  if (VarDecl *CondVar = IfStatement->getConditionVariable()) {
    EmitCondVarDecl(CondVar);
  }

  // consteval if — 编译时已确定分支，只生成对应分支
  if (IfStatement->isConsteval()) {
    // 简化：在 CodeGen 阶段无法真正评估 consteval，
    // 保守地生成两个分支（实际编译器会移除死分支）
    // TODO: 配合 Sema 传播 consteval 结果
  }

  // 生成条件
  llvm::Value *Condition = EmitExpr(IfStatement->getCond());
  if (!Condition) {
    return;
  }

  llvm::BasicBlock *ThenBB = createBasicBlock("if.then");
  llvm::BasicBlock *ElseBB = IfStatement->getElse() ? createBasicBlock("if.else") : nullptr;
  llvm::BasicBlock *MergeBB = createBasicBlock("if.end");

  Condition = EmitConversionToBool(Condition, IfStatement->getCond()->getType());
  Builder.CreateCondBr(Condition, ThenBB,
                       ElseBB ? ElseBB : MergeBB);

  // Then
  EmitBlock(ThenBB);
  if (IfStatement->getThen()) {
    EmitStmt(IfStatement->getThen());
  }
  if (haveInsertPoint()) {
    Builder.CreateBr(MergeBB);
  }

  // Else
  if (ElseBB) {
    EmitBlock(ElseBB);
    // else if 链：递归处理嵌套的 IfStmt
    EmitStmt(IfStatement->getElse());
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

  llvm::BasicBlock *DefaultBB = createBasicBlock("switch.default");
  llvm::BasicBlock *EndBB = createBasicBlock("switch.end");

  // Push break target（switch 中 continue 无效，设为 nullptr）
  BreakContinueStack.push_back({EndBB, nullptr});

  // 遍历 switch body，收集 case 分支并使用 SwitchInst
  struct CaseInfo {
    llvm::ConstantInt *Value;
    llvm::BasicBlock *BB;
  };
  llvm::SmallVector<CaseInfo, 8> Cases;
  llvm::DenseMap<llvm::ConstantInt *, llvm::BasicBlock *> CaseValueToBB;
  llvm::BasicBlock *FoundDefaultBB = nullptr;

  // 收集函数：从 body 中提取 case/default 分支
  auto CollectCases = [&](Stmt *Body) {
    if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
      for (Stmt *S : CS->getBody()) {
        if (auto *CaseS = llvm::dyn_cast<CaseStmt>(S)) {
          if (auto *CaseExpr = CaseS->getLHS()) {
            llvm::Constant *ConstVal = CGM.getConstants().EmitConstant(CaseExpr);
            if (auto *CI = llvm::dyn_cast_or_null<llvm::ConstantInt>(ConstVal)) {
              llvm::BasicBlock *CaseBB = createBasicBlock("switch.case");
              Cases.push_back({CI, CaseBB});
              CaseValueToBB[CI] = CaseBB;
            }
          }
          // GNU case range: case LHS ... RHS → 生成 LHS 到 RHS 之间的所有值
          // 对于大范围（如 case 'a'...'z'），逐值添加会消耗大量内存
          // 简化实现：只处理 LHS，RHS 作为 fallthrough 记录
          // 完整实现需要位检测范围或线性判断
          if (auto *CaseS2 = llvm::dyn_cast<CaseStmt>(S)) {
            if (CaseS2->getRHS()) {
              // GNU case range — 标记为需要范围判断（简化处理）
              // TODO: 实现范围判断代码生成
            }
          }
        } else if (auto *DefaultS = llvm::dyn_cast<DefaultStmt>(S)) {
          FoundDefaultBB = DefaultBB;
        }
      }
    } else if (Body) {
      // 非 CompoundStmt 的 switch body（如 switch(x) case 1: ...）
      // 尝试将 body 直接作为单个语句处理
      if (auto *CaseS = llvm::dyn_cast<CaseStmt>(Body)) {
        if (auto *CaseExpr = CaseS->getLHS()) {
          llvm::Constant *ConstVal = CGM.getConstants().EmitConstant(CaseExpr);
          if (auto *CI = llvm::dyn_cast_or_null<llvm::ConstantInt>(ConstVal)) {
            llvm::BasicBlock *CaseBB = createBasicBlock("switch.case");
            Cases.push_back({CI, CaseBB});
            CaseValueToBB[CI] = CaseBB;
          }
        }
      } else if (auto *DefaultS = llvm::dyn_cast<DefaultStmt>(Body)) {
        FoundDefaultBB = DefaultBB;
      }
    }
  };

  CollectCases(SwitchStatement->getBody());

  // 如果条件值需要截断/扩展以匹配 case 常量的位宽，确保类型一致
  // SwitchInst 要求条件值和所有 case 值类型一致
  if (!Cases.empty()) {
    llvm::Type *CaseTy = Cases[0].Value->getType();
    if (Condition->getType() != CaseTy) {
      Condition = Builder.CreateIntCast(Condition, CaseTy, Condition->getType()->isIntegerTy(1) ? false : true, "switch.cast");
    }
  }

  // 创建 SwitchInst
  auto *SwitchInst = Builder.CreateSwitch(Condition, DefaultBB, Cases.size());
  for (auto &C : Cases) {
    SwitchInst->addCase(C.Value, C.BB);
  }

  // 生成每个 case/default 的代码
  auto EmitSwitchBody = [&](Stmt *Body) {
    if (auto *CS = llvm::dyn_cast<CompoundStmt>(Body)) {
      for (Stmt *S : CS->getBody()) {
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
          if (CaseS->getSubStmt()) {
            EmitStmt(CaseS->getSubStmt());
          }
          // fallthrough: 不添加 br，允许继续到下一个 case
        } else if (auto *DefaultS = llvm::dyn_cast<DefaultStmt>(S)) {
          EmitBlock(DefaultBB);
          if (DefaultS->getSubStmt()) {
            EmitStmt(DefaultS->getSubStmt());
          }
        } else {
          // 普通 fallthrough 代码
          EmitStmt(S);
        }
      }
    } else if (Body) {
      // 非 CompoundStmt body — 直接处理
      if (auto *CaseS = llvm::dyn_cast<CaseStmt>(Body)) {
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
        if (CaseS->getSubStmt()) {
          EmitStmt(CaseS->getSubStmt());
        }
      } else if (auto *DefaultS = llvm::dyn_cast<DefaultStmt>(Body)) {
        EmitBlock(DefaultBB);
        if (DefaultS->getSubStmt()) {
          EmitStmt(DefaultS->getSubStmt());
        }
      } else {
        EmitStmt(Body);
      }
    }
  };

  EmitSwitchBody(SwitchStatement->getBody());

  // Default case（如果没有在 body 中遇到 DefaultStmt）
  if (!FoundDefaultBB) {
    EmitBlock(DefaultBB);
  }

  // End
  if (haveInsertPoint()) {
    Builder.CreateBr(EndBB);
  }
  EmitBlock(EndBB);

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
      Builder.CreateCondBr(Cond, BodyBB, EndBB);
    } else {
      Builder.CreateBr(BodyBB);
    }
  } else {
    Builder.CreateBr(BodyBB); // 无条件循环
  }

  // Body
  EmitBlock(BodyBB);
  if (ForStatement->getBody()) {
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
      Builder.CreateCondBr(Cond, BodyBB, EndBB);
    } else {
      Builder.CreateBr(EndBB);
    }
  } else {
    Builder.CreateBr(EndBB);
  }

  // Body
  EmitBlock(BodyBB);
  if (WhileStatement->getBody()) {
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
      Builder.CreateCondBr(Cond, BodyBB, EndBB);
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

  if (Expr *ReturnExpr = ReturnStatement->getRetValue()) {
    llvm::Value *ReturnValue = EmitExpr(ReturnExpr);
    if (ReturnValue && this->ReturnValue) {
      Builder.CreateStore(ReturnValue, this->ReturnValue);
    } else if (ReturnValue) {
      Builder.CreateRet(ReturnValue);
      return;
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
    if (auto *VariableDecl = llvm::dyn_cast<VarDecl>(Declaration)) {
      QualType VarType = VariableDecl->getType();
      llvm::AllocaInst *Alloca = CreateAlloca(VarType, VariableDecl->getName());
      if (!Alloca) {
        continue;
      }

      setLocalDecl(VariableDecl, Alloca);

      // 初始化
      if (Expr *Initializer = VariableDecl->getInit()) {
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

  // 基本 try/catch 代码生成
  // 使用 invoke + landingpad 模式（简化版本）
  //
  // 结构：
  //   try block: invoke @func to normal unwind landingpad
  //   normal: continue
  //   landingpad: catch dispatch → catch handlers → resume/unreachable
  //   catch.all: 处理 catch(...)

  llvm::BasicBlock *TryContBB = createBasicBlock("try.cont");

  // 如果有 catch blocks，生成 landingpad 和 catch dispatch
  if (!TryStatement->getCatchBlocks().empty()) {
    llvm::BasicBlock *LandingPadBB = createBasicBlock("lpad");

    // 生成 try block（使用 invoke 替代 call 来支持异常处理）
    // 简化：直接生成 try block 代码
    if (TryStatement->getTryBlock()) {
      EmitStmt(TryStatement->getTryBlock());
    }
    if (haveInsertPoint()) {
      Builder.CreateBr(TryContBB);
    }

    // 生成 landingpad
    EmitBlock(LandingPadBB);
    llvm::LandingPadInst *LPad = Builder.CreateLandingPad(
        llvm::StructType::get(CGM.getLLVMContext(),
                              {llvm::PointerType::get(CGM.getLLVMContext(), 0),
                               llvm::Type::getInt32Ty(CGM.getLLVMContext())}),
        TryStatement->getCatchBlocks().size(), "lpad");

    // 为每个 catch 块添加 catch clause 到 landingpad
    llvm::BasicBlock *CatchDispatchBB = createBasicBlock("catch.dispatch");
    llvm::BasicBlock *UnwindBB = createBasicBlock("eh.resume");

    for (Stmt *CatchBlock : TryStatement->getCatchBlocks()) {
      if (auto *Catch = llvm::dyn_cast<CXXCatchStmt>(CatchBlock)) {
        // catch(...) 匹配所有
        // catch(T e) 需要类型信息匹配（简化：catch(...) 的方式处理）
        LPad->addClause(nullptr); // catch-all clause
      } else {
        LPad->addClause(llvm::Constant::getNullValue(
            llvm::PointerType::get(CGM.getLLVMContext(), 0)));
      }
    }

    Builder.CreateBr(CatchDispatchBB);

    // 生成每个 catch handler
    for (Stmt *CatchBlock : TryStatement->getCatchBlocks()) {
      llvm::BasicBlock *CatchBB = createBasicBlock("catch");
      EmitBlock(CatchBB);

      if (auto *Catch = llvm::dyn_cast<CXXCatchStmt>(CatchBlock)) {
        // 为 catch 参数创建 alloca（如果有异常声明）
        if (Catch->getExceptionDecl()) {
          llvm::AllocaInst *CatchAlloca =
              CreateAlloca(Catch->getExceptionDecl()->getType(),
                           Catch->getExceptionDecl()->getName());
          setLocalDecl(Catch->getExceptionDecl(), CatchAlloca);
          // 简化：不真正 extractvalue 从 landingpad 中提取异常对象
        }
        if (Catch->getHandlerBlock()) {
          EmitStmt(Catch->getHandlerBlock());
        }
      } else {
        EmitStmt(CatchBlock);
      }
      if (haveInsertPoint()) {
        Builder.CreateBr(TryContBB);
      }
    }

    // 生成 eh.resume（未被 catch 捕获的异常继续传播）
    EmitBlock(UnwindBB);
    llvm::Value *LPadVal = Builder.CreateLoad(
        LPad->getType(),
        llvm::UndefValue::get(llvm::PointerType::get(LPad->getType(), 0)),
        "lpad.val");
    Builder.CreateResume(LPadVal);
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
    // 直接在 entry 块创建 alloca，类型为 element pointer
    llvm::IRBuilder<>::InsertPoint SavedIP = Builder.saveIP();
    llvm::BasicBlock *EntryBlock = &CurFn->getEntryBlock();
    llvm::Instruction *InsertPos = EntryBlock->getFirstNonPHIOrDbg();
    if (InsertPos) {
      Builder.SetInsertPoint(InsertPos);
    } else {
      Builder.SetInsertPoint(EntryBlock);
    }
    llvm::AllocaInst *IterPtrAlloca = Builder.CreateAlloca(
        llvm::PointerType::get(ElemTy, 0), nullptr, "__iter.ptr");
    Builder.restoreIP(SavedIP);

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
    // 容器类型：尝试调用 begin()/end() 方法
    // 简化实现：假设 range 是指针或可迭代对象，生成 begin/end 调用
    // 完整实现需要 ADL 查找 begin/end 函数

    // 简化：使用 range 值本身作为 begin，不生成循环体
    // 这是一个占位实现，真实实现需要 Sema 提供的 begin/end 表达式
    llvm::Value *InitVal = RangeVal;
    if (LoopVarAlloca && InitVal) {
      Builder.CreateStore(InitVal, LoopVarAlloca);
    }

    if (Body) {
      EmitStmt(Body);
    }
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
  if (SrcTy->isIntegerTy()) {
    return Builder.CreateICmpNE(
        SrcValue, llvm::ConstantInt::get(SrcTy, 0, false), "tobool");
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

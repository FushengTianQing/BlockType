//===--- IREmitStmt.cpp - IR Statement Emitter -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IREmitStmt.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitExpr.h"

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace frontend {

using NodeKind = ASTNode::NodeKind;

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

IREmitStmt::IREmitStmt(ASTToIRConverter& Converter)
  : Converter_(Converter) {}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

ir::IRBuilder& IREmitStmt::getBuilder() {
  return Converter_.getBuilder();
}

ir::IRFunction* IREmitStmt::getCurrentFunction() {
  ir::IRBasicBlock* CurBB = getBuilder().getInsertBlock();
  if (!CurBB) return nullptr;
  return CurBB->getParent();
}

ir::IRBasicBlock* IREmitStmt::createBasicBlock(ir::StringRef Name) {
  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return nullptr;
  return Fn->addBasicBlock(Name);
}

ir::IRValue* IREmitStmt::emitCondition(const Expr* Cond) {
  if (!Cond) return nullptr;

  ir::IRValue* CondVal = Converter_.getExprEmitter()->Emit(Cond);
  if (!CondVal) return nullptr;

  // If condition is not i1, compare with zero
  ir::IRType* CondTy = CondVal->getType();
  if (CondTy && CondTy->isInteger() &&
      static_cast<ir::IRIntegerType*>(CondTy)->getBitWidth() != 1) {
    ir::IRValue* Zero = getBuilder().getInt32(0);
    CondVal = getBuilder().createICmp(ir::ICmpPred::NE, CondVal, Zero, "tobool");
  }
  return CondVal;
}

void IREmitStmt::emitBranchIfNotTerminated(ir::IRBasicBlock* NextBB) {
  ir::IRBasicBlock* CurBB = getBuilder().getInsertBlock();
  if (CurBB && !CurBB->getTerminator()) {
    getBuilder().createBr(NextBB);
  }
}

//===----------------------------------------------------------------------===//
// Emit - General Dispatch
//===----------------------------------------------------------------------===//

void IREmitStmt::Emit(const Stmt* S) {
  if (!S) return;

  switch (S->getKind()) {
    case NodeKind::IfStmtKind:
      EmitIfStmt(static_cast<const IfStmt*>(S));
      break;
    case NodeKind::ForStmtKind:
      EmitForStmt(static_cast<const ForStmt*>(S));
      break;
    case NodeKind::WhileStmtKind:
      EmitWhileStmt(static_cast<const WhileStmt*>(S));
      break;
    case NodeKind::DoStmtKind:
      EmitDoStmt(static_cast<const DoStmt*>(S));
      break;
    case NodeKind::ReturnStmtKind:
      EmitReturnStmt(static_cast<const ReturnStmt*>(S));
      break;
    case NodeKind::SwitchStmtKind:
      EmitSwitchStmt(static_cast<const SwitchStmt*>(S));
      break;
    case NodeKind::CompoundStmtKind:
      EmitCompoundStmt(static_cast<const CompoundStmt*>(S));
      break;
    case NodeKind::DeclStmtKind:
      EmitDeclStmt(static_cast<const DeclStmt*>(S));
      break;
    case NodeKind::NullStmtKind:
      EmitNullStmt(static_cast<const NullStmt*>(S));
      break;
    case NodeKind::GotoStmtKind:
      EmitGotoStmt(static_cast<const GotoStmt*>(S));
      break;
    case NodeKind::LabelStmtKind:
      EmitLabelStmt(static_cast<const LabelStmt*>(S));
      break;
    case NodeKind::BreakStmtKind:
      EmitBreakStmt(static_cast<const BreakStmt*>(S));
      break;
    case NodeKind::ContinueStmtKind:
      EmitContinueStmt(static_cast<const ContinueStmt*>(S));
      break;
    case NodeKind::ExprStmtKind: {
      const auto* ES = static_cast<const ExprStmt*>(S);
      Converter_.getExprEmitter()->Emit(ES->getExpr());
      break;
    }
    default:
      break;
  }
}

//===----------------------------------------------------------------------===//
// EmitIfStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitIfStmt(const IfStmt* IS) {
  if (!IS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  ir::IRBasicBlock* ThenBB = createBasicBlock("if.then");
  ir::IRBasicBlock* ElseBB = IS->getElse() ? createBasicBlock("if.else") : nullptr;
  ir::IRBasicBlock* EndBB   = createBasicBlock("if.end");

  ir::IRValue* Cond = emitCondition(IS->getCond());
  if (!Cond) {
    getBuilder().createBr(EndBB);
    getBuilder().setInsertPoint(EndBB);
    return;
  }

  getBuilder().createCondBr(Cond, ThenBB, ElseBB ? ElseBB : EndBB);

  getBuilder().setInsertPoint(ThenBB);
  Emit(IS->getThen());
  emitBranchIfNotTerminated(EndBB);

  if (ElseBB) {
    getBuilder().setInsertPoint(ElseBB);
    Emit(IS->getElse());
    emitBranchIfNotTerminated(EndBB);
  }

  getBuilder().setInsertPoint(EndBB);
}

//===----------------------------------------------------------------------===//
// EmitForStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitForStmt(const ForStmt* FS) {
  if (!FS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  ir::IRBasicBlock* CondBB = createBasicBlock("for.cond");
  ir::IRBasicBlock* BodyBB = createBasicBlock("for.body");
  ir::IRBasicBlock* IncBB  = createBasicBlock("for.inc");
  ir::IRBasicBlock* EndBB  = createBasicBlock("for.end");

  // Emit init
  Emit(FS->getInit());

  getBuilder().createBr(CondBB);

  // Condition block
  getBuilder().setInsertPoint(CondBB);
  if (FS->getCond()) {
    ir::IRValue* Cond = emitCondition(FS->getCond());
    if (Cond) {
      getBuilder().createCondBr(Cond, BodyBB, EndBB);
    } else {
      getBuilder().createBr(BodyBB);
    }
  } else {
    getBuilder().createBr(BodyBB);
  }

  // Push loop targets
  BreakTargets_.push_back(EndBB);
  ContinueTargets_.push_back(IncBB);

  // Body block
  getBuilder().setInsertPoint(BodyBB);
  Emit(FS->getBody());
  emitBranchIfNotTerminated(IncBB);

  // Pop loop targets
  BreakTargets_.pop_back();
  ContinueTargets_.pop_back();

  // Increment block
  getBuilder().setInsertPoint(IncBB);
  if (FS->getInc()) {
    Converter_.getExprEmitter()->Emit(FS->getInc());
  }
  getBuilder().createBr(CondBB);

  getBuilder().setInsertPoint(EndBB);
}

//===----------------------------------------------------------------------===//
// EmitWhileStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitWhileStmt(const WhileStmt* WS) {
  if (!WS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  ir::IRBasicBlock* CondBB = createBasicBlock("while.cond");
  ir::IRBasicBlock* BodyBB = createBasicBlock("while.body");
  ir::IRBasicBlock* EndBB  = createBasicBlock("while.end");

  getBuilder().createBr(CondBB);

  getBuilder().setInsertPoint(CondBB);
  ir::IRValue* Cond = emitCondition(WS->getCond());
  if (Cond) {
    getBuilder().createCondBr(Cond, BodyBB, EndBB);
  } else {
    getBuilder().createBr(EndBB);
  }

  BreakTargets_.push_back(EndBB);
  ContinueTargets_.push_back(CondBB);

  getBuilder().setInsertPoint(BodyBB);
  Emit(WS->getBody());
  emitBranchIfNotTerminated(CondBB);

  BreakTargets_.pop_back();
  ContinueTargets_.pop_back();

  getBuilder().setInsertPoint(EndBB);
}

//===----------------------------------------------------------------------===//
// EmitDoStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitDoStmt(const DoStmt* DS) {
  if (!DS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  ir::IRBasicBlock* BodyBB = createBasicBlock("do.body");
  ir::IRBasicBlock* CondBB = createBasicBlock("do.cond");
  ir::IRBasicBlock* EndBB  = createBasicBlock("do.end");

  getBuilder().createBr(BodyBB);

  BreakTargets_.push_back(EndBB);
  ContinueTargets_.push_back(CondBB);

  getBuilder().setInsertPoint(BodyBB);
  Emit(DS->getBody());
  emitBranchIfNotTerminated(CondBB);

  BreakTargets_.pop_back();
  ContinueTargets_.pop_back();

  getBuilder().setInsertPoint(CondBB);
  ir::IRValue* Cond = emitCondition(DS->getCond());
  if (Cond) {
    getBuilder().createCondBr(Cond, BodyBB, EndBB);
  } else {
    getBuilder().createBr(EndBB);
  }

  getBuilder().setInsertPoint(EndBB);
}

//===----------------------------------------------------------------------===//
// EmitReturnStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitReturnStmt(const ReturnStmt* RS) {
  if (!RS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  if (Expr* RetVal = RS->getRetValue()) {
    ir::IRValue* Val = Converter_.getExprEmitter()->Emit(RetVal);
    if (Val) {
      getBuilder().createRet(Val);
    } else {
      ir::IRType* RetTy = Fn->getFunctionType()->getReturnType();
      getBuilder().createRet(Converter_.emitErrorPlaceholder(RetTy));
    }
  } else {
    getBuilder().createRetVoid();
  }
}

//===----------------------------------------------------------------------===//
// EmitSwitchStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitSwitchStmt(const SwitchStmt* SS) {
  if (!SS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  ir::IRBasicBlock* EndBB = createBasicBlock("switch.end");

  // Evaluate switch condition
  ir::IRValue* CondVal = Converter_.getExprEmitter()->Emit(SS->getCond());
  if (!CondVal) {
    getBuilder().createBr(EndBB);
    getBuilder().setInsertPoint(EndBB);
    return;
  }

  Stmt* Body = SS->getBody();
  if (!Body) {
    getBuilder().createBr(EndBB);
    getBuilder().setInsertPoint(EndBB);
    return;
  }

  // Collect cases from body
  llvm::SmallVector<std::pair<ir::IRValue*, ir::IRBasicBlock*>, 8> Cases;
  ir::IRBasicBlock* DefaultBB = EndBB;

  if (auto* CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    for (Stmt* Child : CS->getBody()) {
      if (auto* CaseS = llvm::dyn_cast<CaseStmt>(Child)) {
        ir::IRBasicBlock* CaseBB = createBasicBlock("switch.case");
        Cases.push_back({nullptr, CaseBB});
      } else if (auto* DefaultS = llvm::dyn_cast<DefaultStmt>(Child)) {
        DefaultBB = createBasicBlock("switch.default");
      }
    }
  }

  // Push break target
  BreakTargets_.push_back(EndBB);

  if (auto* CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    unsigned CaseIdx = 0;

    for (Stmt* Child : CS->getBody()) {
      if (auto* CaseS = llvm::dyn_cast<CaseStmt>(Child)) {
        ir::IRBasicBlock* CaseBB = Cases[CaseIdx].second;
        ir::IRBasicBlock* NextCheckBB;

        if (CaseIdx + 1 < Cases.size()) {
          NextCheckBB = createBasicBlock("case.check");
        } else {
          NextCheckBB = DefaultBB;
        }

        // Emit case value comparison
        ir::IRValue* CaseVal = Converter_.getExprEmitter()->Emit(CaseS->getLHS());
        if (CaseVal) {
          ir::IRValue* Cmp = getBuilder().createICmp(
              ir::ICmpPred::EQ, CondVal, CaseVal, "case.cmp");
          getBuilder().createCondBr(Cmp, CaseBB, NextCheckBB);
        } else {
          getBuilder().createBr(CaseBB);
        }

        // Emit case body
        getBuilder().setInsertPoint(CaseBB);
        Emit(CaseS->getSubStmt());
        emitBranchIfNotTerminated(EndBB);

        CaseIdx++;
      } else if (auto* DefaultS = llvm::dyn_cast<DefaultStmt>(Child)) {
        getBuilder().setInsertPoint(DefaultBB);
        Emit(DefaultS->getSubStmt());
        emitBranchIfNotTerminated(EndBB);
      } else {
        Emit(Child);
      }
    }
  }

  BreakTargets_.pop_back();

  getBuilder().setInsertPoint(EndBB);
}

//===----------------------------------------------------------------------===//
// EmitCompoundStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitCompoundStmt(const CompoundStmt* CS) {
  if (!CS) return;

  for (Stmt* S : CS->getBody()) {
    Emit(S);
  }
}

//===----------------------------------------------------------------------===//
// EmitDeclStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitDeclStmt(const DeclStmt* DS) {
  if (!DS) return;

  for (Decl* D : DS->getDecls()) {
    auto* VD = llvm::dyn_cast<VarDecl>(D);
    if (!VD) continue;

    ir::IRType* VarTy = Converter_.getTypeMapper().mapType(VD->getType());
    if (!VarTy || !VarTy->isInteger()) {
      // Fallback: mapType may return error opaque type, try emitErrorType
      if (!VarTy) VarTy = Converter_.emitErrorType();
    }

    auto VName = VD->getName();
    ir::IRValue* Alloca = getBuilder().createAlloca(VarTy,
        ir::StringRef(VName.data(), VName.size()));

    Converter_.setDeclValue(VD, Alloca);

    if (Expr* Init = VD->getInit()) {
      ir::IRValue* InitVal = Converter_.getExprEmitter()->Emit(Init);
      if (InitVal) {
        getBuilder().createStore(InitVal, Alloca);
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// EmitNullStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitNullStmt(const NullStmt* NS) {
  (void)NS;
}

//===----------------------------------------------------------------------===//
// EmitGotoStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitGotoStmt(const GotoStmt* GS) {
  if (!GS) return;
  (void)GS->getLabel();
  getBuilder().createUnreachable();
}

//===----------------------------------------------------------------------===//
// EmitLabelStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitLabelStmt(const LabelStmt* LS) {
  if (!LS) return;

  std::string LabelName = "label.";
  auto LName = LS->getLabel()->getName();
  LabelName.append(LName.data(), LName.size());

  ir::IRBasicBlock* LabelBB = createBasicBlock(LabelName);

  emitBranchIfNotTerminated(LabelBB);

  getBuilder().setInsertPoint(LabelBB);
  Emit(LS->getSubStmt());
}

//===----------------------------------------------------------------------===//
// EmitBreakStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitBreakStmt(const BreakStmt* BS) {
  if (!BS) return;

  if (BreakTargets_.empty()) {
    return;
  }

  ir::IRBasicBlock* Target = BreakTargets_.back();
  getBuilder().createBr(Target);
}

//===----------------------------------------------------------------------===//
// EmitContinueStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitContinueStmt(const ContinueStmt* CS) {
  if (!CS) return;

  if (ContinueTargets_.empty()) {
    return;
  }

  ir::IRBasicBlock* Target = ContinueTargets_.back();
  getBuilder().createBr(Target);
}

} // namespace frontend
} // namespace blocktype

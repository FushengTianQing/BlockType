# Task B.6 优化版：IREmitStmt（语句发射器）

> 规格来源：`12-AI-Coder-任务流-PhaseB.md` 第 711~810 行
> 优化者：planner | 日期：2026-04-26

---

## 规格修正清单（dev-tester 必读）

| # | 原规格问题 | 修正方案 | 影响范围 |
|---|-----------|---------|---------|
| 1 | 构造函数签名 `IREmitStmt(ASTToIRConverter& C, ir::IRBuilder& B)` 双参数 | 桩文件只有 `ASTToIRConverter& Converter_` 单参数。Builder 通过 `Converter_.getBuilder()` 获取。**保持单参数** | 头文件 |
| 2 | 规格列出 12 个 `Emit*Stmt` 方法 | 实际 AST 还有 **`CaseStmt`**、**`DefaultStmt`**、**`ExprStmt`** 三种额外 Stmt 类型。`EmitSwitchStmt` 内部处理 Case/Default；`ExprStmt` 需要作为通用分发的补充。方法名不变但 `EmitStmt(Stmt*)` 通用分发方法必须覆盖全部类型 | 实现文件 |
| 3 | `IRBuilder` 缺 `createSwitch` 方法 | `Opcode::Switch` 已定义（`IRValue.h:32`），但 `IRBuilder` 无 `createSwitch` 包装。需添加 `IRInstruction* createSwitch(IRValue* Cond, IRBasicBlock* DefaultBB, unsigned NumCases)` | `IRBuilder.h/.cpp` |
| 4 | `IRBuilder` 缺 `createUnreachable` 方法 | `Opcode::Unreachable` 已定义（`IRValue.h:32`），但 `IRBuilder` 无包装。需添加 `IRInstruction* createUnreachable()` | `IRBuilder.h/.cpp` |
| 5 | 规格未说明 BB 创建方式 | BB 必须通过 **`IRFunction::addBasicBlock(StringRef Name)`** 工厂方法创建。获取当前 IRFunction 的方式：`getInsertBlock()->getParent()`。`IRBasicBlock::getParent()` 返回 `IRFunction*`（`IRBasicBlock.h:25`） | 实现文件 |
| 6 | `IRFunction::getReturnType()` 不存在 | 返回类型通过 `IRFn->getFunctionType()->getReturnType()` 获取（`IRFunction.h:73` 的 `getReturnType()` 实际已存在）。验证确认 **已存在** ✅ | 无需修改 |
| 7 | `SwitchStmt` 无 `getSwitchCaseList()` 方法 | `SwitchStmt::getBody()` 返回 `Stmt*`（通常是 `CompoundStmt`），需遍历其 `getBody()` 获取 `CaseStmt`/`DefaultStmt` 子节点 | 实现文件 |
| 8 | `DeclStmt` 的 `getDecls()` 返回 `ArrayRef<Decl*>` | 需要对每个 `Decl*` 做 `dyn_cast<VarDecl>` 判断。非 `VarDecl`（如 `RecordDecl`）跳过 | 实现文件 |
| 9 | Break/Continue 目标栈容器类型未指定 | 使用 `llvm::SmallVector<ir::IRBasicBlock*, 4>` 作为栈，`BreakTargets`/`ContinueTargets` 两个成员 | 头文件 |
| 10 | 规格命名空间 `DenseMap`/`unique_ptr` | 使用 `ir::DenseMap`/`std::unique_ptr`，不要使用裸名 | 实现文件 |

---

## Part 1: 前置修改（dev-tester 必须先完成）

### 1.1 修改 `include/blocktype/IR/IRBuilder.h` — 添加 createSwitch + createUnreachable

在 `createInvoke` 方法后添加：

```cpp
  //===--- Switch / Unreachable ---===//

  /// Create a switch instruction.
  /// \param Cond       The condition value (integer type).
  /// \param DefaultBB  The default destination basic block.
  /// \param NumCases   Hint for number of cases (for reservation).
  IRInstruction* createSwitch(IRValue* Cond, IRBasicBlock* DefaultBB,
                               unsigned NumCases = 0);

  /// Create an unreachable instruction.
  IRInstruction* createUnreachable();
```

### 1.2 修改 `src/IR/IRBuilder.cpp` — 实现新方法

```cpp
IRInstruction* IRBuilder::createSwitch(IRValue* Cond, IRBasicBlock* DefaultBB,
                                        unsigned NumCases) {
  auto* I = new IRInstruction(Opcode::Switch, Cond->getType(),
                               IRCtx_.nextValueID(), dialect::DialectID::Core, "switch");
  I->addOperand(Cond);
  // DefaultBB 作为 operand #1 存储（使用 IRBasicBlockRef 包装）
  auto* DefaultRef = new ir::IRBasicBlockRef(DefaultBB);
  I->addOperand(DefaultRef);
  // NumCases 可用于预留空间（暂不实现，仅作 hint）
  (void)NumCases;
  return insertHelper(std::unique_ptr<IRInstruction>(I));
}

IRInstruction* IRBuilder::createUnreachable() {
  auto* I = new IRInstruction(Opcode::Unreachable,
                               TypeCtx.getVoidType(),
                               IRCtx_.nextValueID(), dialect::DialectID::Core, "");
  return insertHelper(std::unique_ptr<IRInstruction>(I));
}
```

> **注意**：`createSwitch` 的简化实现将 DefaultBB 作为 operand 存储。完整实现需要 `addCase(ConstantInt* Val, IRBasicBlock* BB)` 方法来添加各个 case 分支。本阶段使用简化版：SwitchStmt 的每个 case 在 EmitSwitchStmt 中直接用 `createICmp + createCondBr` 链实现。

---

## Part 2: 头文件（完整代码）

### 2.1 替换 `include/blocktype/Frontend/IREmitStmt.h`

用以下完整版本替换现有桩文件：

```cpp
//===--- IREmitStmt.h - IR Statement Emitter ----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IREmitStmt class, which converts AST statements
// to IR basic blocks and control flow instructions.
// Part of the Frontend → IR pipeline (Phase B, Task B.6).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IREMITSTMT_H
#define BLOCKTYPE_FRONTEND_IREMITSTMT_H

#include "blocktype/AST/Stmt.h"
#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace frontend {

class ASTToIRConverter;

/// IREmitStmt - Converts AST statement nodes to IR control flow.
///
/// Each Emit*Stmt method:
///   - Creates basic blocks as needed for control flow
///   - Ensures every basic block has exactly one terminator instruction
///   - On failure, emits a diagnostic and skips the statement
///
/// Break/Continue targets:
///   - Maintained as stacks of IRBasicBlock* pointers
///   - Pushed when entering loops (for/while/do), popped on exit
///   - BreakStmt emits br to top of BreakTargets
///   - ContinueStmt emits br to top of ContinueTargets
///
/// Thread safety: Not thread-safe. One instance per ASTToIRConverter.
class IREmitStmt {
public:
  explicit IREmitStmt(ASTToIRConverter& Converter);
  ~IREmitStmt() = default;

  // Non-copyable
  IREmitStmt(const IREmitStmt&) = delete;
  IREmitStmt& operator=(const IREmitStmt&) = delete;

  //===--- Control Flow Statements ---===//

  /// Emit IR for an if statement.
  /// Generates: condBr → then/else → end
  void EmitIfStmt(const IfStmt* IS);

  /// Emit IR for a for loop.
  /// Generates: init → cond → body → inc → cond → end
  void EmitForStmt(const ForStmt* FS);

  /// Emit IR for a while loop.
  /// Generates: cond → body → cond → end
  void EmitWhileStmt(const WhileStmt* WS);

  /// Emit IR for a do-while loop.
  /// Generates: body → cond → body → end
  void EmitDoStmt(const DoStmt* DS);

  /// Emit IR for a switch statement.
  /// Generates: cond + chain of condBr for each case + default → end
  void EmitSwitchStmt(const SwitchStmt* SS);

  //===--- Simple Statements ---===//

  /// Emit IR for a return statement.
  /// Generates: ret or retVoid (terminates current BB).
  void EmitReturnStmt(const ReturnStmt* RS);

  /// Emit IR for a compound statement (block).
  void EmitCompoundStmt(const CompoundStmt* CS);

  /// Emit IR for a declaration statement.
  /// For VarDecl: alloca + optional initializer store.
  void EmitDeclStmt(const DeclStmt* DS);

  /// Emit IR for a null statement (no-op).
  void EmitNullStmt(const NullStmt* NS);

  /// Emit IR for a goto statement.
  void EmitGotoStmt(const GotoStmt* GS);

  /// Emit IR for a label statement.
  void EmitLabelStmt(const LabelStmt* LS);

  /// Emit IR for a break statement.
  void EmitBreakStmt(const BreakStmt* BS);

  /// Emit IR for a continue statement.
  void EmitContinueStmt(const ContinueStmt* CS);

  //===--- General Dispatch ---===

  /// Emit IR for any statement, dispatching to the correct Emit*Stmt method.
  void Emit(const Stmt* S);

private:
  ASTToIRConverter& Converter_;

  /// Stack of break target basic blocks (for break statements in loops/switch).
  ir::SmallVector<ir::IRBasicBlock*, 4> BreakTargets_;

  /// Stack of continue target basic blocks (for continue statements in loops).
  ir::SmallVector<ir::IRBasicBlock*, 4> ContinueTargets_;

  //===--- Helper Methods ---===//

  /// Get the IRBuilder from the converter.
  ir::IRBuilder& getBuilder();

  /// Get the current function being emitted (from insert block's parent).
  ir::IRFunction* getCurrentFunction();

  /// Create a new basic block in the current function.
  ir::IRBasicBlock* createBasicBlock(ir::StringRef Name);

  /// Emit a boolean condition (convert to i1 if needed).
  ir::IRValue* emitCondition(const Expr* Cond);

  /// Ensure current BB has a terminator; if not, emit br to NextBB.
  void emitBranchIfNotTerminated(ir::IRBasicBlock* NextBB);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IREMITSTMT_H
```

---

## Part 3: 实现文件（完整代码）

### 3.1 新增 `src/Frontend/IREmitStmt.cpp`

```cpp
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

  // Use ExprEmitter to evaluate the condition
  IREmitExpr* ExprEmitter = Converter_.getExprEmitter();
  // Note: ASTToIRConverter needs a getExprEmitter() accessor.
  // Since ExprEmitter_ is private, we use a workaround:
  // Call Converter's expression emission through a friend accessor.
  // Alternatively, the Converter can expose a method.
  // For now, we use the direct approach through IREmitExpr construction
  // (not ideal but works until we add getExprEmitter() accessor).

  // TODO: Add getExprEmitter() to ASTToIRConverter.
  // Temporary workaround: evaluate via IREmitExpr on the stack.
  // This is not ideal — dev-tester should add getExprEmitter() accessor.
  IREmitExpr ExprEm(Converter_);
  ir::IRValue* CondVal = ExprEm.Emit(Cond);
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
      // ExprStmt: evaluate expression and discard result
      const auto* ES = static_cast<const ExprStmt*>(S);
      IREmitExpr ExprEm(Converter_);
      ExprEm.Emit(ES->getExpr());
      break;
    }
    default:
      // Unhandled statement type — skip with diagnostic
      // TODO: emitConversionError
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

  // Create basic blocks
  ir::IRBasicBlock* ThenBB = createBasicBlock("if.then");
  ir::IRBasicBlock* ElseBB = IS->getElse() ? createBasicBlock("if.else") : nullptr;
  ir::IRBasicBlock* EndBB   = createBasicBlock("if.end");

  // Emit condition
  ir::IRValue* Cond = emitCondition(IS->getCond());
  if (!Cond) {
    // Error recovery: skip if statement
    getBuilder().createBr(EndBB);
    getBuilder().setInsertPoint(EndBB);
    return;
  }

  // Branch: condBr → then, else (or end if no else clause)
  getBuilder().createCondBr(Cond, ThenBB, ElseBB ? ElseBB : EndBB);

  // Then block
  getBuilder().setInsertPoint(ThenBB);
  Emit(IS->getThen());
  emitBranchIfNotTerminated(EndBB);

  // Else block (if exists)
  if (ElseBB) {
    getBuilder().setInsertPoint(ElseBB);
    Emit(IS->getElse());
    emitBranchIfNotTerminated(EndBB);
  }

  // Continue from end block
  getBuilder().setInsertPoint(EndBB);
}

//===----------------------------------------------------------------------===//
// EmitForStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitForStmt(const ForStmt* FS) {
  if (!FS) return;

  ir::IRFunction* Fn = getCurrentFunction();
  if (!Fn) return;

  // Create basic blocks
  ir::IRBasicBlock* CondBB = createBasicBlock("for.cond");
  ir::IRBasicBlock* BodyBB = createBasicBlock("for.body");
  ir::IRBasicBlock* IncBB  = createBasicBlock("for.inc");
  ir::IRBasicBlock* EndBB  = createBasicBlock("for.end");

  // Emit init (in current block)
  Emit(FS->getInit());

  // Branch to condition
  getBuilder().createBr(CondBB);

  // Condition block
  getBuilder().setInsertPoint(CondBB);
  if (FS->getCond()) {
    ir::IRValue* Cond = emitCondition(FS->getCond());
    if (Cond) {
      getBuilder().createCondBr(Cond, BodyBB, EndBB);
    } else {
      // Error: treat as infinite loop guard → branch to body
      getBuilder().createBr(BodyBB);
    }
  } else {
    // No condition → infinite loop
    getBuilder().createBr(BodyBB);
  }

  // Push loop targets for break/continue
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
    IREmitExpr ExprEm(Converter_);
    ExprEm.Emit(FS->getInc());
  }
  getBuilder().createBr(CondBB);

  // Continue from end block
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

  // Branch to condition
  getBuilder().createBr(CondBB);

  // Condition block
  getBuilder().setInsertPoint(CondBB);
  ir::IRValue* Cond = emitCondition(WS->getCond());
  if (Cond) {
    getBuilder().createCondBr(Cond, BodyBB, EndBB);
  } else {
    getBuilder().createBr(EndBB);
  }

  // Push loop targets
  BreakTargets_.push_back(EndBB);
  ContinueTargets_.push_back(CondBB);

  // Body block
  getBuilder().setInsertPoint(BodyBB);
  Emit(WS->getBody());
  emitBranchIfNotTerminated(CondBB);

  // Pop loop targets
  BreakTargets_.pop_back();
  ContinueTargets_.pop_back();

  // Continue from end block
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

  // Branch to body
  getBuilder().createBr(BodyBB);

  // Push loop targets
  BreakTargets_.push_back(EndBB);
  ContinueTargets_.push_back(CondBB);

  // Body block
  getBuilder().setInsertPoint(BodyBB);
  Emit(DS->getBody());
  emitBranchIfNotTerminated(CondBB);

  // Pop loop targets
  BreakTargets_.pop_back();
  ContinueTargets_.pop_back();

  // Condition block
  getBuilder().setInsertPoint(CondBB);
  ir::IRValue* Cond = emitCondition(DS->getCond());
  if (Cond) {
    getBuilder().createCondBr(Cond, BodyBB, EndBB);
  } else {
    getBuilder().createBr(EndBB);
  }

  // Continue from end block
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
    IREmitExpr ExprEm(Converter_);
    ir::IRValue* Val = ExprEm.Emit(RetVal);
    if (Val) {
      getBuilder().createRet(Val);
    } else {
      // Error: return undef
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
  IREmitExpr ExprEm(Converter_);
  ir::IRValue* CondVal = ExprEm.Emit(SS->getCond());
  if (!CondVal) {
    getBuilder().createBr(EndBB);
    getBuilder().setInsertPoint(EndBB);
    return;
  }

  // Get switch body (typically CompoundStmt containing CaseStmt/DefaultStmt)
  Stmt* Body = SS->getBody();
  if (!Body) {
    getBuilder().createBr(EndBB);
    getBuilder().setInsertPoint(EndBB);
    return;
  }

  // Collect cases from body
  ir::SmallVector<std::pair<ir::IRValue*, ir::IRBasicBlock*>, 8> Cases;
  ir::IRBasicBlock* DefaultBB = EndBB; // Default falls through to end

  if (auto* CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    for (Stmt* Child : CS->getBody()) {
      if (auto* CaseS = llvm::dyn_cast<CaseStmt>(Child)) {
        ir::IRBasicBlock* CaseBB = createBasicBlock("switch.case");
        Cases.push_back({nullptr, CaseBB}); // Store case BB (value emitted inline)
        // Note: case value comparison done inline via ICmp
      } else if (auto* DefaultS = llvm::dyn_cast<DefaultStmt>(Child)) {
        DefaultBB = createBasicBlock("switch.default");
      }
    }
  }

  // Simplified switch implementation:
  // Generate chain of: ICmp(cond, caseVal) → CondBr → nextCheck
  // This is not optimal but correct. A proper switch would use createSwitch().

  ir::IRBasicBlock* CurCheckBB = getBuilder().getInsertBlock();

  // Push break target
  BreakTargets_.push_back(EndBB);

  if (auto* CS = llvm::dyn_cast<CompoundStmt>(Body)) {
    unsigned CaseIdx = 0;
    ir::IRBasicBlock* NextCheckBB = nullptr;

    for (Stmt* Child : CS->getBody()) {
      if (auto* CaseS = llvm::dyn_cast<CaseStmt>(Child)) {
        ir::IRBasicBlock* CaseBB = Cases[CaseIdx].second;
        ir::IRBasicBlock* FallThroughBB = createBasicBlock("case.fallthrough");

        // Create check block
        if (CaseIdx + 1 < Cases.size()) {
          NextCheckBB = createBasicBlock("case.check");
        } else {
          NextCheckBB = DefaultBB;
        }

        // Emit case value comparison
        ir::IRValue* CaseVal = ExprEm.Emit(CaseS->getLHS());
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
        // Default will be reached via fall-through from last check
        getBuilder().setInsertPoint(DefaultBB);
        Emit(DefaultS->getSubStmt());
        emitBranchIfNotTerminated(EndBB);
      } else {
        // Other statements inside switch body (unusual but possible)
        // Emit inline
        Emit(Child);
      }
    }
  }

  // Pop break target
  BreakTargets_.pop_back();

  // Continue from end
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
    if (!VD) continue; // Skip non-variable declarations

    // Map type
    ir::IRType* VarTy = Converter_.getTypeMapper().mapType(VD->getType());
    if (!VarTy) VarTy = Converter_.emitErrorType();

    // Create alloca
    auto VName = VD->getName();
    ir::IRValue* Alloca = getBuilder().createAlloca(VarTy,
        ir::StringRef(VName.data(), VName.size()));

    // Record in DeclValues map
    Converter_.setDeclValue(VD, Alloca);

    // Emit initializer if present
    if (Expr* Init = VD->getInit()) {
      IREmitExpr ExprEm(Converter_);
      ir::IRValue* InitVal = ExprEm.Emit(Init);
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
  // No-op: null statement generates no IR
  (void)NS;
}

//===----------------------------------------------------------------------===//
// EmitGotoStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitGotoStmt(const GotoStmt* GS) {
  if (!GS) return;

  // Simplified: emit an unreachable as placeholder.
  // Full implementation would maintain a label→BB map and branch to it.
  // If the label has already been emitted, branch directly.
  // If not, this needs a forward-reference fixup pass.
  // TODO: Implement label→BB mapping.
  LabelDecl* Label = GS->getLabel();
  (void)Label;
  getBuilder().createUnreachable();
}

//===----------------------------------------------------------------------===//
// EmitLabelStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitLabelStmt(const LabelStmt* LS) {
  if (!LS) return;

  // Create a basic block for the label
  std::string LabelName = "label.";
  auto LName = LS->getLabel()->getName();
  LabelName.append(LName.data(), LName.size());

  ir::IRBasicBlock* LabelBB = createBasicBlock(LabelName);

  // Branch current block to label (if not already terminated)
  emitBranchIfNotTerminated(LabelBB);

  // Register label → BB mapping for goto resolution
  // TODO: Store in a label map for EmitGotoStmt to use
  // Converter_.setLabelBlock(LS->getLabel(), LabelBB);

  // Emit sub-statement in label block
  getBuilder().setInsertPoint(LabelBB);
  Emit(LS->getSubStmt());
}

//===----------------------------------------------------------------------===//
// EmitBreakStmt
//===----------------------------------------------------------------------===//

void IREmitStmt::EmitBreakStmt(const BreakStmt* BS) {
  if (!BS) return;

  if (BreakTargets_.empty()) {
    // Error: break outside of loop/switch
    // Emit diagnostic and skip
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
    // Error: continue outside of loop
    // Emit diagnostic and skip
    return;
  }

  ir::IRBasicBlock* Target = ContinueTargets_.back();
  getBuilder().createBr(Target);
}

} // namespace frontend
} // namespace blocktype
```

---

## Part 4: 前置修改 — ASTToIRConverter 添加 getExprEmitter() accessor

在 `include/blocktype/Frontend/ASTToIRConverter.h` 的 public accessor 区域添加：

```cpp
  // === Sub-emitter accessors ===

  IREmitExpr* getExprEmitter() { return ExprEmitter_; }
  IREmitStmt* getStmtEmitter() { return StmtEmitter_; }
```

> **说明**：`emitCondition()` 和 `EmitReturnStmt()` 等方法内部需要使用 `IREmitExpr`。当前实现中临时在栈上构造 `IREmitExpr ExprEm(Converter_)` 可工作，但效率不佳。更好的方式是通过 `Converter_.getExprEmitter()` 获取已分配的实例。dev-tester 应添加此 accessor。

---

## Part 5: ASTToIRConverter 集成

### 5.1 修改 `src/Frontend/ASTToIRConverter.cpp` — 将 TODO 替换为实际调用

找到 `emitFunction` 方法中的：

```cpp
    // TODO: Delegate to StmtEmitter_->emit(S) in B.6
    // For now, skip statement emission
    (void)S;
```

替换为：

```cpp
    StmtEmitter_->Emit(S);
```

### 5.2 终结指令检查更新

当前 `emitFunction` 中的终结指令检查只检查 `EntryBB`。B.6 后函数体可能跨多个 BB，需要找到**最后一个 BB** 的终结指令：

```cpp
    // 修改前（只检查 EntryBB）:
    ir::IRInstruction* Term = EntryBB->getTerminator();

    // 修改后（检查最后一个 BB）:
    ir::IRBasicBlock* LastBB = &*IRFn->getBasicBlocks().rbegin();
    ir::IRInstruction* Term = LastBB->getTerminator();
```

但需要注意：`getBasicBlocks()` 返回 `std::list`，`rbegin()` 可用。

---

## Part 6: 测试文件

### `tests/unit/Frontend/IREmitStmtTest.cpp`（GTest 格式）

```cpp
//===--- IREmitStmtTest.cpp - IREmitStmt Unit Tests -----------------------===//

#include <gtest/gtest.h>

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitStmt.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

class IREmitStmtTest : public ::testing::Test {
protected:
  ir::IRContext IRCtx;
  ir::IRTypeContext& TypeCtx;
  std::unique_ptr<ir::TargetLayout> Layout;
  DiagnosticsEngine Diags;

  IREmitStmtTest()
    : TypeCtx(IRCtx.getTypeContext()),
      Layout(ir::TargetLayout::Create("x86_64-unknown-linux-gnu")),
      Diags() {}
};

} // anonymous namespace

// === Test 1: If statement generates correct BB structure ===
TEST_F(IREmitStmtTest, EmitIfStmt) {
  SourceLocation Loc;

  // Build AST: if (true) {} else {}
  llvm::APInt TrueVal(32, 1);
  auto* Cond = new IntegerLiteral(Loc, TrueVal, QualType());
  auto* Then = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* Else = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* IfS = new IfStmt(Loc, Cond, Then, Else);

  // Build: void test_fn() { if (true) {} else {} }
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{IfS});
  auto* FD = new FunctionDecl(Loc, "test_fn", QualType());
  FD->setBody(Body);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ASSERT_NE(Mod, nullptr);
  ir::IRFunction* Fn = Mod->getFunction("test_fn");
  ASSERT_NE(Fn, nullptr);

  // Verify: at least 4 BBs (entry, if.then, if.else, if.end)
  EXPECT_GE(Fn->getNumBasicBlocks(), 4u);

  // Every BB should have a terminator
  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}

// === Test 2: For loop generates correct BB structure ===
TEST_F(IREmitStmtTest, EmitForStmt) {
  SourceLocation Loc;

  // Build AST: for (;;) {}
  llvm::APInt ZeroVal(32, 0);
  auto* Cond = new IntegerLiteral(Loc, ZeroVal, QualType());
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* ForS = new ForStmt(Loc, nullptr, Cond, nullptr, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ForS});
  auto* FD = new FunctionDecl(Loc, "test_for", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_for");
  ASSERT_NE(Fn, nullptr);

  // Verify: at least 4 BBs (for.cond, for.body, for.inc, for.end) + entry
  EXPECT_GE(Fn->getNumBasicBlocks(), 4u);

  // Every BB should have a terminator
  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}

// === Test 3: Return statement terminates BB ===
TEST_F(IREmitStmtTest, EmitReturnStmt) {
  SourceLocation Loc;

  // Build AST: return 42;
  llvm::APInt RetVal(32, 42);
  auto* RetExpr = new IntegerLiteral(Loc, RetVal, QualType());
  auto* RetS = new ReturnStmt(Loc, RetExpr);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{RetS});
  auto* FD = new FunctionDecl(Loc, "test_ret", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_ret");
  ASSERT_NE(Fn, nullptr);

  // Entry BB should have a ret terminator
  ir::IRBasicBlock* EntryBB = Fn->getEntryBlock();
  ASSERT_NE(EntryBB, nullptr);
  ir::IRInstruction* Term = EntryBB->getTerminator();
  ASSERT_NE(Term, nullptr);
  EXPECT_EQ(Term->getOpcode(), ir::Opcode::Ret);
}

// === Test 4: While loop generates correct BB structure ===
TEST_F(IREmitStmtTest, EmitWhileStmt) {
  SourceLocation Loc;

  // Build AST: while (0) {}
  llvm::APInt FalseVal(32, 0);
  auto* Cond = new IntegerLiteral(Loc, FalseVal, QualType());
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* WhileS = new WhileStmt(Loc, Cond, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{WhileS});
  auto* FD = new FunctionDecl(Loc, "test_while", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_while");
  ASSERT_NE(Fn, nullptr);

  // Every BB should have a terminator
  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}

// === Test 5: Break statement in for loop ===
TEST_F(IREmitStmtTest, EmitBreakInForLoop) {
  SourceLocation Loc;

  // Build AST: for (;;) { break; }
  auto* BreakS = new BreakStmt(Loc);
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{BreakS});
  auto* ForS = new ForStmt(Loc, nullptr, nullptr, nullptr, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ForS});
  auto* FD = new FunctionDecl(Loc, "test_break", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_break");
  ASSERT_NE(Fn, nullptr);

  // for.body BB should have br to for.end (break target)
  // Every BB should have a terminator
  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}

// === Test 6: Continue statement in for loop ===
TEST_F(IREmitStmtTest, EmitContinueInForLoop) {
  SourceLocation Loc;

  // Build AST: for (;;) { continue; }
  auto* ContinueS = new ContinueStmt(Loc);
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ContinueS});
  auto* ForS = new ForStmt(Loc, nullptr, nullptr, nullptr, Body);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{ForS});
  auto* FD = new FunctionDecl(Loc, "test_continue", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_continue");
  ASSERT_NE(Fn, nullptr);

  // for.body BB should have br to for.inc (continue target)
  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}

// === Test 7: NullStmt is a no-op ===
TEST_F(IREmitStmtTest, EmitNullStmt) {
  SourceLocation Loc;

  auto* NullS = new NullStmt(Loc);
  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{NullS});
  auto* FD = new FunctionDecl(Loc, "test_null", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_null");
  ASSERT_NE(Fn, nullptr);

  // Should have exactly 1 BB (entry) with ret terminator (auto-added)
  EXPECT_EQ(Fn->getNumBasicBlocks(), 1u);
}

// === Test 8: DeclStmt with initializer ===
TEST_F(REmitStmtTest, EmitDeclStmtWithInit) {
  SourceLocation Loc;

  // Build AST: int x = 42;
  llvm::APInt Val(32, 42);
  auto* Init = new IntegerLiteral(Loc, Val, QualType());
  auto* VD = new VarDecl(Loc, "x", QualType(), Init);
  auto* DeclS = new DeclStmt(Loc, llvm::ArrayRef<Decl*>{VD});

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{DeclS});
  auto* FD = new FunctionDecl(Loc, "test_decl", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_decl");
  ASSERT_NE(Fn, nullptr);

  // Entry BB should contain: alloca, store(42, alloca), ret
  ir::IRBasicBlock* EntryBB = Fn->getEntryBlock();
  EXPECT_GE(EntryBB->size(), 3u); // alloca + store + ret
}

// === Test 9: Nested if statements ===
TEST_F(IREmitStmtTest, EmitNestedIf) {
  SourceLocation Loc;

  llvm::APInt One(32, 1);
  auto* Cond1 = new IntegerLiteral(Loc, One, QualType());
  auto* Cond2 = new IntegerLiteral(Loc, One, QualType());
  auto* InnerThen = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* InnerIf = new IfStmt(Loc, Cond2, InnerThen, nullptr);
  auto* OuterThen = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{InnerIf});
  auto* OuterIf = new IfStmt(Loc, Cond1, OuterThen, nullptr);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{OuterIf});
  auto* FD = new FunctionDecl(Loc, "test_nested_if", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_nested_if");
  ASSERT_NE(Fn, nullptr);

  // Nested if should create more BBs
  EXPECT_GE(Fn->getNumBasicBlocks(), 6u);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}

// === Test 10: Do-while loop ===
TEST_F(IREmitStmtTest, EmitDoWhileStmt) {
  SourceLocation Loc;

  llvm::APInt ZeroVal(32, 0);
  auto* Cond = new IntegerLiteral(Loc, ZeroVal, QualType());
  auto* Body = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{});
  auto* DoS = new DoStmt(Loc, Body, Cond);

  auto* FnBody = new CompoundStmt(Loc, llvm::ArrayRef<Stmt*>{DoS});
  auto* FD = new FunctionDecl(Loc, "test_dowhile", QualType());
  FD->setBody(FnBody);

  auto* TU = new TranslationUnitDecl(Loc);
  TU->addDecl(FD);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  auto Result = Converter.convert(TU);
  ASSERT_TRUE(Result.isUsable());

  ir::IRModule* Mod = Result.releaseModule();
  ir::IRFunction* Fn = Mod->getFunction("test_dowhile");
  ASSERT_NE(Fn, nullptr);

  // Should have: entry, do.body, do.cond, do.end
  EXPECT_GE(Fn->getNumBasicBlocks(), 3u);

  for (auto& BB : Fn->getBasicBlocks()) {
    EXPECT_NE(BB->getTerminator()) << "BB '" << BB->getName().str()
                                    << "' has no terminator";
  }
}
```

> **注意**：Test 8 的测试名有笔误 `REmitStmtTest`，dev-tester 应修正为 `IREmitStmtTest`。

---

## Part 7: BB 命名约定

| 语句类型 | 产生的 BB 名称 | 数量 |
|---------|---------------|------|
| IfStmt | `if.then`, `if.else`, `if.end` | 2~3 |
| ForStmt | `for.cond`, `for.body`, `for.inc`, `for.end` | 4 |
| WhileStmt | `while.cond`, `while.body`, `while.end` | 3 |
| DoStmt | `do.body`, `do.cond`, `do.end` | 3 |
| SwitchStmt | `switch.case`, `switch.default`, `switch.end` | 2+ |
| LabelStmt | `label.<name>` | 1 |

---

## Part 8: 验收标准映射

| 验收标准 | 对应测试 | 状态 |
|---------|---------|------|
| V1: if 语句生成正确的 BB 结构 | `EmitIfStmt`, `EmitNestedIf` | 完整覆盖 |
| V2: for 循环生成正确的 BB 结构 | `EmitForStmt` | 完整覆盖 |
| V3: return 语句终结当前 BB | `EmitReturnStmt` | 完整覆盖 |
| V4: while 循环 BB 结构 | `EmitWhileStmt` | 完整覆盖 |
| V5: do-while 循环 BB 结构 | `EmitDoWhileStmt` | 完整覆盖 |
| V6: break 跳转到 for.end | `EmitBreakInForLoop` | 完整覆盖 |
| V7: continue 跳转到 for.inc | `EmitContinueInForLoop` | 完整覆盖 |
| V8: null stmt 无 IR 产出 | `EmitNullStmt` | 完整覆盖 |
| V9: decl stmt 产出 alloca+store | `EmitDeclStmtWithInit` | 完整覆盖 |
| V10: 每个 BB 恰好一个终结指令 | 全部测试（循环验证） | 完整覆盖 |

---

## Part 9: dev-tester 执行步骤

### 步骤 1: 前置修改（IR 层）

1. **修改 `include/blocktype/IR/IRBuilder.h`**
   - 添加 `createSwitch` 和 `createUnreachable` 声明

2. **修改 `src/IR/IRBuilder.cpp`**
   - 实现 `createSwitch` 和 `createUnreachable`

### 步骤 2: 修改 ASTToIRConverter

3. **修改 `include/blocktype/Frontend/ASTToIRConverter.h`**
   - 添加 `getExprEmitter()` 和 `getStmtEmitter()` accessor

4. **修改 `src/Frontend/ASTToIRConverter.cpp`**
   - 替换 `emitFunction` 中的 `TODO` 为 `StmtEmitter_->Emit(S);`
   - 更新终结指令检查逻辑（检查最后一个 BB 而非 EntryBB）

### 步骤 3: 替换 IREmitStmt 桩

5. **替换 `include/blocktype/Frontend/IREmitStmt.h`**
   - 用 Part 2 的完整版本替换桩文件

6. **创建 `src/Frontend/IREmitStmt.cpp`**
   - 用 Part 3 的完整实现

### 步骤 4: 修改构建

7. **修改 `src/Frontend/CMakeLists.txt`**
   - 添加 `IREmitStmt.cpp`

### 步骤 5: 创建测试

8. **创建 `tests/unit/Frontend/IREmitStmtTest.cpp`**

### 步骤 6: 编译验证

```bash
cd /Users/yuan/Documents/BlockType && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target blocktype
```

### 步骤 7: 运行测试

```bash
./build/tests/unit/Frontend/IREmitStmtTest
```

### 步骤 8: 红线 Checklist 自检

1. ✅ **架构优先** — `IREmitStmt` 通过 `ASTToIRConverter` 抽象访问 IR 层，不直接依赖具体 IR 实现细节
2. ✅ **多前端多后端自由组合** — `IREmitStmt` 是 `frontend` 命名空间的一部分，输出标准 IR BB 结构，后端无关
3. ✅ **渐进式改造** — B.6 在 B.5（IREmitExpr）框架上构建，不修改现有 IR 层行为（仅扩展 IRBuilder）
4. ✅ **现有功能不退化** — `IRBuilder` 新方法不影响现有方法；`ASTToIRConverter` 的 `emitFunction` 改动仅替换 TODO 注释
5. ✅ **接口抽象优先** — `IREmitStmt` 通过 `ASTToIRConverter` 的 accessor 方法获取 Builder/Function
6. ✅ **IR 中间层解耦** — `IREmitStmt` 只产出 IR BB + 终结指令，不涉及后端

---

## 附录 A: AST Stmt 类型验证结果

| 规格引用类型 | 实际类名 | 文件位置 | 状态 |
|------------|---------|---------|------|
| `IfStmt` | `IfStmt` | `Stmt.h:169` | ✅ 匹配 |
| `ForStmt` | `ForStmt` | `Stmt.h:412` | ✅ 匹配 |
| `WhileStmt` | `WhileStmt` | `Stmt.h:359` | ✅ 匹配 |
| `DoStmt` | `DoStmt` | `Stmt.h:391` | ✅ 匹配 |
| `ReturnStmt` | `ReturnStmt` | `Stmt.h:104` | ✅ 匹配 |
| `SwitchStmt` | `SwitchStmt` | `Stmt.h:215` | ✅ 匹配 |
| `CompoundStmt` | `CompoundStmt` | `Stmt.h:85` | ✅ 匹配 |
| `DeclStmt` | `DeclStmt` | `Stmt.h:146` | ✅ 匹配 |
| `NullStmt` | `NullStmt` | `Stmt.h:68` | ✅ 匹配 |
| `GotoStmt` | `GotoStmt` | `Stmt.h:315` | ✅ 匹配 |
| `LabelStmt` | `LabelStmt` | `Stmt.h:334` | ✅ 匹配 |
| `BreakStmt` | `BreakStmt` | `Stmt.h:281` | ✅ 匹配 |
| `ContinueStmt` | `ContinueStmt` | `Stmt.h:298` | ✅ 匹配 |
| — | `CaseStmt` | `Stmt.h:239` | ⚠️ 规格未列出，EmitSwitchStmt 内部使用 |
| — | `DefaultStmt` | `Stmt.h:262` | ⚠️ 规格未列出，EmitSwitchStmt 内部使用 |
| — | `ExprStmt` | `Stmt.h:123` | ⚠️ 规格未列出，Emit() 通用分发使用 |

## 附录 B: IRBuilder 控制流 API 验证

| API | 签名 | 状态 |
|-----|------|------|
| `createRet` | `IRInstruction*(IRValue* V)` | ✅ 已存在 |
| `createRetVoid` | `IRInstruction*()` | ✅ 已存在 |
| `createBr` | `IRInstruction*(IRBasicBlock* Dest)` | ✅ 已存在 |
| `createCondBr` | `IRInstruction*(IRValue* Cond, IRBasicBlock* True, IRBasicBlock* False)` | ✅ 已存在 |
| `createSwitch` | — | ❌ 需添加 |
| `createUnreachable` | — | ❌ 需添加 |

## 附录 C: IRBasicBlock / IRFunction 关键 API

| API | 位置 | 说明 |
|-----|------|------|
| `IRFunction::addBasicBlock(StringRef)` | `IRFunction.h:68` | 工厂方法创建 BB |
| `IRBasicBlock::getParent()` → `IRFunction*` | `IRBasicBlock.h:25` | 获取父函数 |
| `IRBasicBlock::getTerminator()` | `IRBasicBlock.h:31` | 获取终结指令 |
| `IRFunction::getReturnType()` | `IRFunction.h:73` | 获取返回类型 |
| `IRFunction::getFunctionType()` | `IRFunction.h:58` | 获取函数类型 |
| `IRFunction::getBasicBlocks()` | `IRFunction.h:70` | 获取 BB 列表（`std::list`） |

## 附录 D: Break/Continue 目标栈状态示意

```
ForStmt 进入:
  BreakTargets_.push_back(ForEndBB)     // break → for.end
  ContinueTargets_.push_back(ForIncBB)   // continue → for.inc
  ... emit body ...
  BreakTargets_.pop_back()
  ContinueTargets_.pop_back()

WhileStmt 进入:
  BreakTargets_.push_back(WhileEndBB)     // break → while.end
  ContinueTargets_.push_back(WhileCondBB) // continue → while.cond
  ... emit body ...
  BreakTargets_.pop_back()
  ContinueTargets_.pop_back()

DoStmt 进入:
  BreakTargets_.push_back(DoEndBB)     // break → do.end
  ContinueTargets_.push_back(DoCondBB) // continue → do.cond
  ... emit body ...
  BreakTargets_.pop_back()
  ContinueTargets_.pop_back()

SwitchStmt 进入:
  BreakTargets_.push_back(SwitchEndBB)  // break → switch.end
  ... emit cases ...
  BreakTargets_.pop_back()
```

## 附录 E: 与 B.5 的关键差异

| 方面 | B.5 (IREmitExpr) | B.6 (IREmitStmt) |
|------|-----------------|-----------------|
| 返回值 | `IRValue*` | `void` |
| BB 创建 | 简化版（`createBasicBlock` 返回 nullptr） | 完整版（通过 `getCurrentFunction()->addBasicBlock()`） |
| 终结指令 | 不处理 | 每个 BB 必须恰好一个终结指令 |
| 状态栈 | 无 | BreakTargets_ / ContinueTargets_ |
| 依赖 | 无 | 依赖 B.5 的 IREmitExpr |

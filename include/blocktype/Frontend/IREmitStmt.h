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
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRFunction.h"

#include "llvm/ADT/SmallVector.h"

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

  void EmitIfStmt(const IfStmt* IS);
  void EmitForStmt(const ForStmt* FS);
  void EmitWhileStmt(const WhileStmt* WS);
  void EmitDoStmt(const DoStmt* DS);
  void EmitSwitchStmt(const SwitchStmt* SS);

  //===--- Simple Statements ---===//

  void EmitReturnStmt(const ReturnStmt* RS);
  void EmitCompoundStmt(const CompoundStmt* CS);
  void EmitDeclStmt(const DeclStmt* DS);
  void EmitNullStmt(const NullStmt* NS);
  void EmitGotoStmt(const GotoStmt* GS);
  void EmitLabelStmt(const LabelStmt* LS);
  void EmitBreakStmt(const BreakStmt* BS);
  void EmitContinueStmt(const ContinueStmt* CS);

  //===--- General Dispatch ---===//

  void Emit(const Stmt* S);

private:
  ASTToIRConverter& Converter_;

  llvm::SmallVector<ir::IRBasicBlock*, 4> BreakTargets_;
  llvm::SmallVector<ir::IRBasicBlock*, 4> ContinueTargets_;

  //===--- Helper Methods ---===//

  ir::IRBuilder& getBuilder();
  ir::IRFunction* getCurrentFunction();
  ir::IRBasicBlock* createBasicBlock(ir::StringRef Name);
  ir::IRValue* emitCondition(const Expr* Cond);
  void emitBranchIfNotTerminated(ir::IRBasicBlock* NextBB);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IREMITSTMT_H

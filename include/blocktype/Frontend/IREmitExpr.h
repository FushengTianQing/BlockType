//===--- IREmitExpr.h - IR Expression Emitter ----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IREmitExpr class, which converts AST expressions
// to IR instructions. Part of the Frontend → IR pipeline (Phase B, Task B.5).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IREMITEEXPR_H
#define BLOCKTYPE_FRONTEND_IREMITEEXPR_H

#include "blocktype/AST/Expr.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace frontend {

class ASTToIRConverter;

/// IREmitExpr - Converts AST expression nodes to IR instructions.
///
/// Each Emit*Expr method:
///   - Accepts a specific AST expression type
///   - Returns an IRValue* representing the expression result
///   - On failure, returns emitErrorPlaceholder(T) via the Converter
///
/// Value categories:
///   - LValue expressions return a pointer IRValue* (address of the value)
///   - RValue expressions return a non-pointer IRValue* (the value itself)
///   - Callers needing rvalues from lvalues must emit an additional Load
///
/// Thread safety: Not thread-safe. One instance per ASTToIRConverter.
class IREmitExpr {
public:
  explicit IREmitExpr(ASTToIRConverter& Converter);
  ~IREmitExpr() = default;

  // Non-copyable
  IREmitExpr(const IREmitExpr&) = delete;
  IREmitExpr& operator=(const IREmitExpr&) = delete;

  //===--- Binary/Unary Operators ---===//

  ir::IRValue* EmitBinaryExpr(const BinaryOperator* BO);
  ir::IRValue* EmitUnaryExpr(const UnaryOperator* UO);

  //===--- Call Expressions ---===//

  ir::IRValue* EmitCallExpr(const CallExpr* CE);
  ir::IRValue* EmitCXXMemberCallExpr(const CXXMemberCallExpr* MCE);

  //===--- Access Expressions ---===//

  ir::IRValue* EmitMemberExpr(const MemberExpr* ME);
  ir::IRValue* EmitDeclRefExpr(const DeclRefExpr* DRE);

  //===--- Cast Expressions ---===//

  ir::IRValue* EmitCastExpr(const CastExpr* CE);

  //===--- C++ Specific Expressions ---===//

  ir::IRValue* EmitCXXConstructExpr(const CXXConstructExpr* CCE);
  ir::IRValue* EmitCXXNewExpr(const CXXNewExpr* NE);
  ir::IRValue* EmitCXXDeleteExpr(const CXXDeleteExpr* DE);
  ir::IRValue* EmitCXXThisExpr(const CXXThisExpr* TE);

  //===--- Conditional / Init ---===//

  ir::IRValue* EmitConditionalOperator(const ConditionalOperator* CO);
  ir::IRValue* EmitInitListExpr(const InitListExpr* ILE);

  //===--- Literals ---===//

  ir::IRValue* EmitStringLiteral(const StringLiteral* SL);
  ir::IRValue* EmitIntegerLiteral(const IntegerLiteral* IL);
  ir::IRValue* EmitFloatingLiteral(const FloatingLiteral* FL);
  ir::IRValue* EmitCharacterLiteral(const CharacterLiteral* CL);
  ir::IRValue* EmitBoolLiteral(const CXXBoolLiteral* BLE);

  //===--- General Dispatch ---===//

  ir::IRValue* Emit(const Expr* E);

private:
  ASTToIRConverter& Converter_;

  //===--- Helper Methods ---===//

  ir::IRBuilder& getBuilder();
  ir::IRValue* emitErrorPlaceholder(ir::IRType* T);
  ir::IRType* emitErrorType();
  ir::IRValue* emitShortCircuitEval(const BinaryOperator* BO);
  ir::IRValue* emitAssignment(const BinaryOperator* BO);
  ir::IRValue* emitCompoundAssignment(const BinaryOperator* BO);
  ir::IRType* mapType(QualType T);
  bool isSignedType(QualType T);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IREMITEEXPR_H

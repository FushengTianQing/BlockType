//===--- Stmt.cpp - Statement AST Node Implementation -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// CompoundStmt
//===----------------------------------------------------------------------===//

void CompoundStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CompoundStmt: {\n";
  for (Stmt *S : Body) {
    if (S)
      S->dump(OS, Indent + 1);
  }
  printIndent(OS, Indent);
  OS << "}\n";
}

//===----------------------------------------------------------------------===//
// ReturnStmt
//===----------------------------------------------------------------------===//

void ReturnStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ReturnStmt: return\n";
  if (RetValue)
    RetValue->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// ExprStmt
//===----------------------------------------------------------------------===//

void ExprStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ExprStmt\n";
  if (Expression)
    Expression->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// DeclStmt
//===----------------------------------------------------------------------===//

void DeclStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DeclStmt\n";
  for (Decl *D : Decls) {
    if (D)
      D->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// IfStmt
//===----------------------------------------------------------------------===//

void IfStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "IfStmt: if";
  if (IsConsteval) {
    OS << (IsNegated ? " !consteval" : " consteval");
  }
  OS << "\n";
  if (Cond)
    Cond->dump(OS, Indent + 1);
  if (Then)
    Then->dump(OS, Indent + 1);
  if (Else) {
    printIndent(OS, Indent);
    OS << "else\n";
    Else->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// SwitchStmt
//===----------------------------------------------------------------------===//

void SwitchStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "SwitchStmt: switch\n";
  if (Cond)
    Cond->dump(OS, Indent + 1);
  if (Body)
    Body->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CaseStmt
//===----------------------------------------------------------------------===//

void CaseStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CaseStmt: case\n";
  if (LHS)
    LHS->dump(OS, Indent + 1);
  if (RHS) {
    printIndent(OS, Indent + 1);
    OS << "...\n";
    RHS->dump(OS, Indent + 1);
  }
  if (SubStmt)
    SubStmt->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// DefaultStmt
//===----------------------------------------------------------------------===//

void DefaultStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DefaultStmt: default\n";
  if (SubStmt)
    SubStmt->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// LabelStmt
//===----------------------------------------------------------------------===//

void LabelStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "LabelStmt: " << (Label ? Label->getName() : "") << "\n";
  if (SubStmt)
    SubStmt->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// GotoStmt
//===----------------------------------------------------------------------===//

void GotoStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "GotoStmt: " << (Label ? Label->getName() : "") << "\n";
}

//===----------------------------------------------------------------------===//
// WhileStmt
//===----------------------------------------------------------------------===//

void WhileStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "WhileStmt: while\n";
  if (Cond)
    Cond->dump(OS, Indent + 1);
  if (Body)
    Body->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// DoStmt
//===----------------------------------------------------------------------===//

void DoStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DoStmt: do\n";
  if (Body)
    Body->dump(OS, Indent + 1);
  if (Cond) {
    printIndent(OS, Indent);
    OS << "while\n";
    Cond->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// ForStmt
//===----------------------------------------------------------------------===//

void ForStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ForStmt: for\n";
  if (Init)
    Init->dump(OS, Indent + 1);
  if (Cond)
    Cond->dump(OS, Indent + 1);
  if (Inc)
    Inc->dump(OS, Indent + 1);
  if (Body)
    Body->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CXXForRangeStmt
//===----------------------------------------------------------------------===//

void CXXForRangeStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXForRangeStmt: for-range\n";
  if (LoopVar)
    LoopVar->dump(OS, Indent + 1);
  if (Range)
    Range->dump(OS, Indent + 1);
  if (Body)
    Body->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CXXTryStmt
//===----------------------------------------------------------------------===//

void CXXTryStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXTryStmt: try\n";
  if (TryBlock)
    TryBlock->dump(OS, Indent + 1);
  for (Stmt *Catch : CatchBlocks) {
    if (Catch)
      Catch->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// CXXCatchStmt
//===----------------------------------------------------------------------===//

void CXXCatchStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXCatchStmt: catch\n";
  if (ExceptionDecl)
    ExceptionDecl->dump(OS, Indent + 1);
  if (HandlerBlock)
    HandlerBlock->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CoreturnStmt
//===----------------------------------------------------------------------===//

void CoreturnStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CoreturnStmt: co_return\n";
  if (Operand)
    Operand->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CoyieldStmt
//===----------------------------------------------------------------------===//

void CoyieldStmt::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CoyieldStmt: co_yield\n";
  if (Operand)
    Operand->dump(OS, Indent + 1);
}

} // namespace blocktype
